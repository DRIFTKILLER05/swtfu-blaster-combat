// DK_Loader: shared dinput8.dll proxy for Star Wars: The Force Unleashed (SWTFU.exe).
// Forwards DINPUT8 to the system DLL and, inside SWTFU.exe only, loads every DK_*.dll plugin
// next to the exe. Plugin interface (exports, loader services): src\DK_Common\dk_plugin.h.
//
// Timing: DllMain runs before the SteamStub has decrypted the exe's .text, so plugins are loaded
// later, on the first call the game's own code (.text) makes to one of a few early KERNEL32
// imports (IAT hooks; the CRT startup calls GetSystemTimeAsFileTime first). DirectInput8Create is
// the fallback trigger. Nothing is done while the loader lock is held.
//
// Frame service (1.1): the WinMain loop's Game::Tick calls (and its Sleep(50) while the window is inactive)
// are redirected through RunFrame, which runs on the game thread between frames: pending plugin reloads,
// then each plugin's DK_PluginFrame.
// Hot reload (1.1): DK_Loader.ini DevReload=1 loads plugins from copies in DK_Shadow\, so a rebuilt DK_*.dll
// can be copied over the original while the game runs; the loader notices the new file (checked every
// 500 ms), calls the old instance's DK_PluginShutdown, unloads it and loads the new one.
#include <windows.h>
#include <unknwn.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <intrin.h>
#include "dk_util.h"
#include "dk_plugin.h"
#include "dk_services.h"
#pragma intrinsic(_ReturnAddress)

#define DK_LOADER_VERSION "1.3.0"   // 1.3.0: API v3 patch registry (VirtualProtect import hook, ListPatches)
static const char kMarker[] = "[DK_Loader]";   // install scripts recognise our dinput8.dll by this string

using dk::Log;

typedef HRESULT(WINAPI* DI8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
typedef void(__cdecl* PluginInit_t)(HMODULE);
typedef void(__cdecl* PluginAttach_t)(const DK_LoaderApi*);
typedef void(__cdecl* PluginFrame_t)();
typedef int(__cdecl* PluginShutdown_t)();
typedef void(__thiscall* Tick_t)(void* game);

static HMODULE g_real;
static HMODULE g_exe;
static BYTE* g_textLo;
static BYTE* g_textHi;
static bool g_isGame;
static volatile LONG g_loaded;
static char g_dir[MAX_PATH];
static int g_devReload;
static volatile LONG g_frame;
static volatile LONG g_lastFrameTick;   // GetTickCount of the last RunFrame (dkgame health: stuck test game)
static DK_LoaderApi g_api;

// ---------------------------------------------------------------- forwarding
static FARPROC Real(const char* n) {
    if (!g_real) {
        char p[MAX_PATH];
        GetSystemDirectoryA(p, MAX_PATH);   // SysWOW64 for this 32-bit process
        strcat_s(p, "\\dinput8.dll");
        g_real = LoadLibraryA(p);
    }
    return g_real ? GetProcAddress(g_real, n) : nullptr;
}

// ---------------------------------------------------------------- plugin table
struct Plugin {
    char name[64];             // DK_X.dll
    char loadPath[MAX_PATH];   // the file actually loaded (a DK_Shadow copy when DevReload=1)
    HMODULE mod;
    FILETIME ft;               // write time of the source file when it was loaded
    int gen;                   // load count
    PluginFrame_t frame;
    PluginShutdown_t shutdown;
    volatile LONG reloadReq;   // set by RequestReload (any thread)
    DWORD unloadAt;            // after a successful shutdown: tick count at which FreeLibrary is due
    bool waitingUnload;
    bool blocked;              // load failed or shutdown refused: not retried until the file changes again
    FILETIME pendingFt;        // candidate new file time, accepted once it is stable for one check
};
static Plugin g_pl[32];
static int g_plN;
static SRWLOCK g_plLock = SRWLOCK_INIT;   // guards the table against ListPlugins/RequestReload from other threads

static void SrcPath(const Plugin& p, char* out) { sprintf_s(out, MAX_PATH, "%s%s", g_dir, p.name); }

static bool FileTime(const char* path, FILETIME* ft) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &a)) return false;
    *ft = a.ftLastWriteTime;
    return true;
}

static bool FileComplete(const char* path) {   // not still being written by a build or a copy
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

// Load (or reload) one plugin from its source file. Game thread or the first-load trigger.
static bool LoadOne(Plugin& p) {
    char src[MAX_PATH];
    SrcPath(p, src);
    FileTime(src, &p.ft);
    p.gen++;
    if (g_devReload) {
        char dir[MAX_PATH], base[64];
        sprintf_s(dir, "%sDK_Shadow", g_dir);
        CreateDirectoryA(dir, nullptr);
        strcpy_s(base, p.name);
        if (char* dot = strrchr(base, '.')) *dot = 0;
        sprintf_s(p.loadPath, "%s\\%s.%d.dll", dir, base, p.gen);
        if (!CopyFileA(src, p.loadPath, FALSE)) {
            Log("%s: copy to %s failed (error %lu), skipped", p.name, p.loadPath, GetLastError());
            p.blocked = true;
            return false;
        }
    } else {
        strcpy_s(p.loadPath, src);
    }
    HMODULE m = LoadLibraryA(p.loadPath);
    if (!m) { Log("%s: load failed (error %lu), skipped", p.name, GetLastError()); p.blocked = true; return false; }
    auto init = (PluginInit_t)GetProcAddress(m, "DK_PluginInit");
    if (!init) { Log("%s: no DK_PluginInit export, skipped", p.name); FreeLibrary(m); p.blocked = true; return false; }
    Svc_HookPluginConfig(m);   // ini overlay (dkmatrix) before the plugin reads its ini
    auto attach = (PluginAttach_t)GetProcAddress(m, "DK_PluginAttach");
    p.mod = m;
    p.frame = (PluginFrame_t)GetProcAddress(m, "DK_PluginFrame");
    p.shutdown = (PluginShutdown_t)GetProcAddress(m, "DK_PluginShutdown");
    p.blocked = false;
    Log("%s: loaded (generation %d%s%s%s), calling DK_PluginInit", p.name, p.gen, g_devReload ? ", shadow copy" : "",
        p.frame ? ", frame" : "", p.shutdown ? ", reloadable" : "");
    __try {
        if (attach) attach(&g_api);
        init(g_exe);
        Log("%s: init done", p.name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("%s: init raised exception 0x%08X, plugin left inactive", p.name, GetExceptionCode());
        p.frame = nullptr;
    }
    return true;
}

static int CmpStr(const void* a, const void* b) { return _stricmp((const char*)a, (const char*)b); }

// Names of DK_*.dll next to the exe, sorted.
static int ScanPlugins(char (*names)[64], int max) {
    int n = 0;
    char pat[MAX_PATH];
    sprintf_s(pat, "%sDK_*.dll", g_dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && n < max && strlen(fd.cFileName) < 64) strcpy_s(names[n++], 64, fd.cFileName);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    qsort(names, n, 64, CmpStr);
    return n;
}

static Plugin* FindPlugin(const char* name) {
    for (int i = 0; i < g_plN; i++) if (!_stricmp(g_pl[i].name, name)) return &g_pl[i];
    return nullptr;
}

// ---------------------------------------------------------------- reload (game thread, between frames)
static bool CallShutdown(Plugin& p, int* result) {
    __try { *result = p.shutdown(); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("%s: DK_PluginShutdown raised exception 0x%08X", p.name, GetExceptionCode()); return false; }
}

static void BeginUnload(Plugin& p, const char* why) {
    if (!p.mod || p.waitingUnload) return;
    if (!p.shutdown) { Log("%s: %s, but it has no DK_PluginShutdown - not reloaded", p.name, why); p.blocked = true; return; }
    int r = 0;
    if (!CallShutdown(p, &r) || r != 1) {
        Log("%s: %s, but DK_PluginShutdown refused (%d) - the old instance stays", p.name, why, r);
        p.blocked = true;
        return;
    }
    Log("%s: %s - shut down, unloading", p.name, why);
    p.frame = nullptr;           // no more calls into it
    p.waitingUnload = true;
    p.unloadAt = GetTickCount() + 150;   // let other threads leave its code (callbacks, trampolines)
}

static void FinishUnload(Plugin& p) {
    FreeLibrary(p.mod);
    // someone else holds a reference (a LoadLibrary of its own, a hooked import): the old instance stays mapped
    HMODULE still = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, p.loadPath, &still) && still)
        Log("%s: generation %d is still mapped after FreeLibrary (a reference is held) - its memory stays in use", p.name, p.gen);
    p.mod = nullptr;
    p.waitingUnload = false;
    p.shutdown = nullptr;
    if (g_devReload && p.loadPath[0]) DeleteFileA(p.loadPath);
}

static void CheckFiles() {
    static DWORD next;
    DWORD now = GetTickCount();
    if ((int)(now - next) < 0) return;
    next = now + 500;
    char names[32][64];
    int n = ScanPlugins(names, 32);
    AcquireSRWLockExclusive(&g_plLock);
    for (int i = 0; i < n; i++) {   // new plugin files
        if (FindPlugin(names[i]) || g_plN >= 32) continue;
        Plugin& p = g_pl[g_plN++];
        memset(&p, 0, sizeof(p));
        strcpy_s(p.name, names[i]);
        char src[MAX_PATH]; SrcPath(p, src);
        FileTime(src, &p.pendingFt);   // loaded by a later check once the file time is stable
    }
    for (int i = 0; i < g_plN; i++) {
        Plugin& p = g_pl[i];
        if (p.waitingUnload) continue;
        char src[MAX_PATH]; SrcPath(p, src);
        FILETIME ft;
        if (!FileTime(src, &ft)) continue;   // removed: keep the loaded instance
        if (CompareFileTime(&ft, &p.ft) == 0 && p.mod) { p.pendingFt = ft; continue; }
        if (CompareFileTime(&ft, &p.pendingFt) != 0) { p.pendingFt = ft; p.blocked = false; continue; }  // wait until stable
        if (p.blocked || !FileComplete(src)) continue;
        InterlockedExchange(&p.reloadReq, 1);
    }
    ReleaseSRWLockExclusive(&g_plLock);
}

static void ProcessReloads() {
    if (g_devReload) CheckFiles();
    AcquireSRWLockExclusive(&g_plLock);
    for (int i = 0; i < g_plN; i++) {
        Plugin& p = g_pl[i];
        if (p.waitingUnload) {
            if ((int)(GetTickCount() - p.unloadAt) < 0) continue;
            FinishUnload(p);
            char src[MAX_PATH]; SrcPath(p, src);
            if (GetFileAttributesA(src) != INVALID_FILE_ATTRIBUTES) LoadOne(p);
            else Log("%s: file removed, not reloaded", p.name);
            continue;
        }
        if (!InterlockedExchange(&p.reloadReq, 0)) continue;
        if (!p.mod) LoadOne(p);
        else BeginUnload(p, "reload requested");
    }
    ReleaseSRWLockExclusive(&g_plLock);
}

// ---------------------------------------------------------------- frame service
static bool CallFrame(PluginFrame_t f) {
    __try { f(); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void RunFrame() {
    InterlockedIncrement(&g_frame);
    InterlockedExchange(&g_lastFrameTick, (LONG)GetTickCount());
    ProcessReloads();
    for (int i = 0; i < g_plN; i++) {
        Plugin& p = g_pl[i];
        if (p.frame && !CallFrame(p.frame)) {
            Log("%s: DK_PluginFrame raised an exception - its frame callback is disabled", p.name);
            p.frame = nullptr;
        }
    }
}

static Tick_t oTick;
static void __fastcall HkTick(void* game, void* /*edx*/) {
    RunFrame();
    oTick(game);
}
static void WINAPI HkIdleSleep(DWORD ms) {   // window inactive: the loop sleeps 50 ms instead of ticking
    RunFrame();
    Sleep(ms);
}

static void InstallFrameHooks() {
    dk::Range t = { g_textLo, g_textHi };
    uint8_t* tick = dk::FindSym(t, "Loop_TickCall");
    uint8_t* deact = dk::FindSym(t, "Loop_DeactivateTickCall");
    uint8_t* idle = dk::FindSym(t, "Loop_IdleSleepCall");
    if (!tick || tick[0] != 0xE8) { Log("frame service unavailable: Game::Tick call not found"); return; }
    oTick = (Tick_t)dk::RedirectCall(tick, (void*)HkTick);
    if (deact && deact[0] == 0xE8 && deact + 5 + dk::Rd<int32_t>(deact + 1) == (uint8_t*)oTick) dk::RedirectCall(deact, (void*)HkTick);
    else Log("deactivate Tick call not found - no frame calls on that path");
    if (idle && idle[0] == 0xFF && idle[1] == 0x15) {   // call [Sleep] (6 bytes) -> call HkIdleSleep; nop
        uint8_t patch[6] = { 0xE8, 0, 0, 0, 0, 0x90 };
        int32_t rel = (int32_t)((uint8_t*)HkIdleSleep - (idle + 5));
        memcpy(patch + 1, &rel, 4);
        dk::WriteMem(idle, patch, 6);
    } else Log("idle Sleep call not found - no frame calls while the window is inactive");
    Log("frame service: Game::Tick call %p, deactivate %p, idle %p", tick, deact, idle);
}

// ---------------------------------------------------------------- services for plugins
static void __cdecl Api_RequestReload(const char* name) {
    AcquireSRWLockShared(&g_plLock);
    for (int i = 0; i < g_plN; i++)
        if (!name || !_stricmp(g_pl[i].name, name)) InterlockedExchange(&g_pl[i].reloadReq, 1);
    ReleaseSRWLockShared(&g_plLock);
    Log("reload requested: %s", name ? name : "all");
}

struct Shared { char key[48]; void* value; };
static Shared g_shared[32];
static SRWLOCK g_sharedLock = SRWLOCK_INIT;
static void* __cdecl Api_GetShared(const char* key) {
    void* v = nullptr;
    AcquireSRWLockShared(&g_sharedLock);
    for (auto& s : g_shared) if (s.key[0] && !strcmp(s.key, key)) { v = s.value; break; }
    ReleaseSRWLockShared(&g_sharedLock);
    return v;
}
static void __cdecl Api_SetShared(const char* key, void* value) {
    AcquireSRWLockExclusive(&g_sharedLock);
    Shared* slot = nullptr;
    for (auto& s : g_shared) if (s.key[0] && !strcmp(s.key, key)) { slot = &s; break; }
    if (!slot) for (auto& s : g_shared) if (!s.key[0]) { slot = &s; strncpy_s(s.key, key, _TRUNCATE); break; }
    if (slot) slot->value = value;
    ReleaseSRWLockExclusive(&g_sharedLock);
}
static int __cdecl Api_ListPlugins(char* buf, int size) {
    int n = 0;
    if (!buf || size <= 0) return 0;
    buf[0] = 0;
    AcquireSRWLockShared(&g_plLock);
    for (int i = 0; i < g_plN && n < size; i++) {
        const Plugin& p = g_pl[i];
        int k = _snprintf_s(buf + n, size - n, _TRUNCATE, "%s %s gen=%d%s%s %s\n", p.name,
            p.mod ? (p.waitingUnload ? "unloading" : "loaded") : "not-loaded", p.gen,
            p.frame ? " frame" : "", p.shutdown ? " reloadable" : "", p.loadPath);
        if (k < 0) break;
        n += k;
    }
    ReleaseSRWLockShared(&g_plLock);
    return n;
}

// ---------------------------------------------------------------- first load
static void LoadConfig() {
    char ini[MAX_PATH];
    sprintf_s(ini, "%sDK_Loader.ini", g_dir);
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES) {
        FILE* f = nullptr;
        if (!fopen_s(&f, ini, "w") && f) {
            fputs("; DK_Loader (dinput8.dll) for SWTFU\n"
                  "[Loader]\n"
                  "; 1 = development mode: plugins are loaded from copies in DK_Shadow\\ and a DK_*.dll that changes on\n"
                  ";     disk is reloaded while the game runs (plugins with DK_PluginShutdown only). 0 = normal.\n"
                  "DevReload=0\n", f);
            fclose(f);
        }
    }
    g_devReload = GetPrivateProfileIntA("Loader", "DevReload", 0, ini);
}

static void CleanShadow() {
    char pat[MAX_PATH];
    sprintf_s(pat, "%sDK_Shadow\\*.dll", g_dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        char f[MAX_PATH];
        sprintf_s(f, "%sDK_Shadow\\%s", g_dir, fd.cFileName);
        DeleteFileA(f);   // copies of a previous session (an instance still running keeps its files locked)
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static void LoadPlugins(const char* trigger) {
    if (!g_isGame || InterlockedExchange(&g_loaded, 1)) return;
    Log("trigger: %s", trigger);
    LoadConfig();
    g_api.version = DK_LOADER_API_VERSION;
    g_api.gameDir = g_dir;
    g_api.frame = &g_frame;
    g_api.devReload = g_devReload;
    g_api.RequestReload = Api_RequestReload;
    g_api.GetShared = Api_GetShared;
    g_api.SetShared = Api_SetShared;
    g_api.ListPlugins = Api_ListPlugins;
    Svc_Init(&g_api, g_dir, &g_frame, &g_lastFrameTick);
    Svc_InstallCrashFilter();
    InstallFrameHooks();
    if (g_devReload) { CleanShadow(); Log("DevReload=1: plugins load from DK_Shadow copies and reload when their file changes"); }
    char names[32][64];
    int n = ScanPlugins(names, 32);
    if (!n) Log("no DK_*.dll plugins found");
    AcquireSRWLockExclusive(&g_plLock);
    for (int i = 0; i < n; i++) {
        Plugin& p = g_pl[g_plN++];
        memset(&p, 0, sizeof(p));
        strcpy_s(p.name, names[i]);
        LoadOne(p);
        p.pendingFt = p.ft;
    }
    ReleaseSRWLockExclusive(&g_plLock);
}

// ---------------------------------------------------------------- early IAT triggers
struct IatHook { const char* name; void** slot; void* orig; };
static void WINAPI Hk_GetSystemTimeAsFileTime(LPFILETIME);
static void WINAPI Hk_GetStartupInfoA(LPSTARTUPINFOA);
static LPSTR WINAPI Hk_GetCommandLineA();
static IatHook g_hooks[] = {
    { "GetSystemTimeAsFileTime", nullptr, nullptr },
    { "GetStartupInfoA", nullptr, nullptr },
    { "GetCommandLineA", nullptr, nullptr },
};
static void* const g_hookFns[] = { (void*)Hk_GetSystemTimeAsFileTime, (void*)Hk_GetStartupInfoA, (void*)Hk_GetCommandLineA };

static void PatchPtr(void** slot, void* v) {
    DWORD old;
    if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
        *slot = v;
        VirtualProtect(slot, sizeof(void*), old, &old);
    }
}

static void RemoveIatHooks() {
    for (auto& h : g_hooks)
        if (h.slot && *h.slot != h.orig) PatchPtr(h.slot, h.orig);
}

static bool FromGameText(void* ret) { return (BYTE*)ret >= g_textLo && (BYTE*)ret < g_textHi; }

static void OnEarlyCall(void* ret, const char* name) {
    if (g_loaded || !FromGameText(ret)) return;
    RemoveIatHooks();
    LoadPlugins(name);
}

static void WINAPI Hk_GetSystemTimeAsFileTime(LPFILETIME ft) {
    OnEarlyCall(_ReturnAddress(), "GetSystemTimeAsFileTime");
    ((void(WINAPI*)(LPFILETIME))g_hooks[0].orig)(ft);
}
static void WINAPI Hk_GetStartupInfoA(LPSTARTUPINFOA si) {
    OnEarlyCall(_ReturnAddress(), "GetStartupInfoA");
    ((void(WINAPI*)(LPSTARTUPINFOA))g_hooks[1].orig)(si);
}
static LPSTR WINAPI Hk_GetCommandLineA() {
    OnEarlyCall(_ReturnAddress(), "GetCommandLineA");
    return ((LPSTR(WINAPI*)())g_hooks[2].orig)();
}

static void InstallIatHooks() {
    BYTE* base = (BYTE*)g_exe;
    auto dos = (IMAGE_DOS_HEADER*)base;
    auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return;
    for (auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; imp++) {
        if (_stricmp((char*)(base + imp->Name), "KERNEL32.dll")) continue;
        if (!imp->OriginalFirstThunk) continue;
        auto names = (IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk);
        auto iat = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const char* fn = (const char*)((IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData))->Name;
            for (int i = 0; i < 3; i++) {
                if (strcmp(fn, g_hooks[i].name)) continue;
                g_hooks[i].slot = (void**)&iat->u1.Function;
                g_hooks[i].orig = *g_hooks[i].slot;
                PatchPtr(g_hooks[i].slot, g_hookFns[i]);
            }
        }
    }
    int k = 0;
    for (auto& h : g_hooks) k += h.slot != nullptr;
    Log("early triggers hooked: %d/3", k);
}

static void Init(HINSTANCE) {
    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    strcpy_s(g_dir, exe);
    char* slash = strrchr(g_dir, '\\');
    if (slash) slash[1] = 0;
    const char* nm = slash ? exe + (slash - g_dir) + 1 : exe;
    g_isGame = _stricmp(nm, "SWTFU.exe") == 0;
    if (!g_isGame) return;   // e.g. SWTFU Launcher.exe (SlimDX loads dinput8 too): forward only
    g_exe = GetModuleHandleA(nullptr);
    auto nt = (IMAGE_NT_HEADERS*)((BYTE*)g_exe + ((IMAGE_DOS_HEADER*)g_exe)->e_lfanew);
    auto s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++)
        if (!memcmp(s->Name, ".text", 6)) {
            g_textLo = (BYTE*)g_exe + s->VirtualAddress;
            g_textHi = g_textLo + s->Misc.VirtualSize;
        }
    char path[MAX_PATH];
    sprintf_s(path, "%sDK_Loader.log", g_dir);
    dk::LogOpen(path, "DK_Loader", 2000);   // keep the log to the current session
    Log("%s %s in %s", kMarker, DK_LOADER_VERSION, nm);
    InstallIatHooks();
}

// ---------------------------------------------------------------- exports
extern "C" const DK_LoaderApi* __cdecl DK_GetLoaderApi() { return g_loaded ? &g_api : nullptr; }

extern "C" HRESULT WINAPI DK_DirectInput8Create(HINSTANCE h, DWORD v, REFIID r, LPVOID* o, LPUNKNOWN u) {
    LoadPlugins("DirectInput8Create");   // fallback if no early trigger fired
    auto f = (DI8Create_t)Real("DirectInput8Create");
    return f ? f(h, v, r, o, u) : E_FAIL;
}
extern "C" HRESULT WINAPI DK_DllCanUnloadNow() { auto f = (HRESULT(WINAPI*)())Real("DllCanUnloadNow"); return f ? f() : S_FALSE; }
extern "C" HRESULT WINAPI DK_DllGetClassObject(REFCLSID a, REFIID b, LPVOID* c) { auto f = (HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*))Real("DllGetClassObject"); return f ? f(a, b, c) : CLASS_E_CLASSNOTAVAILABLE; }
extern "C" HRESULT WINAPI DK_DllRegisterServer() { auto f = (HRESULT(WINAPI*)())Real("DllRegisterServer"); return f ? f() : E_FAIL; }
extern "C" HRESULT WINAPI DK_DllUnregisterServer() { auto f = (HRESULT(WINAPI*)())Real("DllUnregisterServer"); return f ? f() : E_FAIL; }
extern "C" LPCVOID WINAPI DK_GetdfDIJoystick() { auto f = (LPCVOID(WINAPI*)())Real("GetdfDIJoystick"); return f ? f() : nullptr; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        Init(h);
    }
    return TRUE;
}
