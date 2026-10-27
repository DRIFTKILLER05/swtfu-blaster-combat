// DK_Loader services (API version 2): flight-recorder event ring, ini overlay for plugins, code-cave registry,
// crash filter with minidump, on-demand reports. See dk_plugin.h for the plugin side.
//
// Reports go to <game>\DK_Reports\<kind>_<yyyymmdd_hhmmss>\:
//   report.txt     what happened (crash: exception, faulting code and its owner, registers, return addresses)
//   events.txt     the loader's event ring (every DK log line of the last minutes, any plugin, survives reloads)
//   game.dmp       minidump (crash / hang: all threads)
//   logs\          tails of every DK_*.log, DK_Engine.log and the game's Log.txt
//   + whatever registered dumpers add (DK_Bridge: frames.csv flight recorder, engine log, screenshot)
#include <windows.h>
#include <intrin.h>
#include <dbghelp.h>
#include <psapi.h>
#include <stdio.h>
#include <share.h>
#include <string.h>
#include <stdlib.h>
#include "dk_util.h"
#include "dk_plugin.h"
#include "dk_services.h"

using dk::Log;

static char g_gameDir[MAX_PATH];
static volatile LONG* g_frameCounter;

// ---------------------------------------------------------------------------------------------- event ring
struct EvEntry { DWORD tick; SYSTEMTIME t; char tag[20]; char text[200]; };
static const int kEvCap = 16384;                 // a few minutes of DK log lines
static EvEntry* g_ev;
static LONG g_evHead;                            // total entries ever written
static CRITICAL_SECTION g_evCs;

static void __cdecl Svc_Event(const char* tag, const char* text) {
    if (!g_ev || !text) return;
    EnterCriticalSection(&g_evCs);
    EvEntry& e = g_ev[g_evHead % kEvCap];
    e.tick = GetTickCount(); GetLocalTime(&e.t);
    strncpy_s(e.tag, tag ? tag : "?", _TRUNCATE);
    strncpy_s(e.text, text, _TRUNCATE);
    for (char* p = e.text; *p; p++) if (*p == '\n' || *p == '\r') *p = ' ';
    g_evHead++;
    LeaveCriticalSection(&g_evCs);
}

static int __cdecl Svc_CopyEvents(char* buf, int size, DWORD lastMs) {
    if (!buf || size <= 0) return 0;
    buf[0] = 0;
    if (!g_ev) return 0;
    int n = 0;
    DWORD now = GetTickCount();
    EnterCriticalSection(&g_evCs);
    LONG first = g_evHead > kEvCap ? g_evHead - kEvCap : 0;
    for (LONG i = first; i < g_evHead; i++) {
        const EvEntry& e = g_ev[i % kEvCap];
        if (lastMs && now - e.tick > lastMs) continue;
        int k = _snprintf_s(buf + n, size - n, _TRUNCATE, "%02d:%02d:%02d.%03d [%s] %s\n", e.t.wHour, e.t.wMinute, e.t.wSecond,
                            e.t.wMilliseconds, e.tag, e.text);
        if (k < 0) break;
        n += k;
    }
    LeaveCriticalSection(&g_evCs);
    return n;
}

// ---------------------------------------------------------------------------------------------- code registry
// (8192: plugins built before 2026-10-20 register every cave separately and never free them; 1024 filled after ~200
// hot reloads. Newer dk_util registers one 64 KB slab per plugin instance.)
struct CodeRange { const uint8_t* lo; DWORD size; char owner[32]; };
static const LONG kCodeCap = 8192;
static CodeRange g_code[kCodeCap];
static LONG g_codeN;
static SRWLOCK g_codeLock = SRWLOCK_INIT;

static void __cdecl Svc_RegisterCode(const void* start, DWORD size, const char* owner) {
    bool full = false;
    AcquireSRWLockExclusive(&g_codeLock);
    if (g_codeN < kCodeCap) {
        CodeRange& c = g_code[g_codeN++];
        c.lo = (const uint8_t*)start; c.size = size; strncpy_s(c.owner, owner ? owner : "?", _TRUNCATE);
    } else full = true;
    ReleaseSRWLockExclusive(&g_codeLock);
    static LONG warned;
    if (full && !InterlockedExchange(&warned, 1)) Log("code registry full (%ld ranges): later code caves are not named in crash reports - restart the game", kCodeCap);
}

static const char* __cdecl Svc_CodeOwner(const void* addr) {
    const uint8_t* a = (const uint8_t*)addr;
    const char* r = nullptr;
    AcquireSRWLockShared(&g_codeLock);
    for (LONG i = 0; i < g_codeN && !r; i++)
        if (a >= g_code[i].lo && a < g_code[i].lo + g_code[i].size) r = g_code[i].owner;
    ReleaseSRWLockShared(&g_codeLock);
    return r;
}

// ---------------------------------------------------------------------------------------------- ini overlay
struct CfgEntry { char file[64], section[64], key[64], value[256]; };
static CfgEntry g_cfg[256];
static int g_cfgN;
static SRWLOCK g_cfgLock = SRWLOCK_INIT;

static const char* BaseName(const char* p) {
    const char* b = p;
    for (; *p; p++) if (*p == '\\' || *p == '/') b = p + 1;
    return b;
}

static void __cdecl Svc_SetConfig(const char* file, const char* section, const char* key, const char* value) {
    AcquireSRWLockExclusive(&g_cfgLock);
    if (!file) g_cfgN = 0;
    else {
        int i = 0;
        for (; i < g_cfgN; i++)
            if (!_stricmp(g_cfg[i].file, BaseName(file)) && !_stricmp(g_cfg[i].section, section ? section : "") &&
                !_stricmp(g_cfg[i].key, key ? key : "")) break;
        if (!value) {
            if (i < g_cfgN) { g_cfg[i] = g_cfg[--g_cfgN]; }
        } else if (i < g_cfgN || g_cfgN < 256) {
            if (i == g_cfgN) g_cfgN++;
            strncpy_s(g_cfg[i].file, BaseName(file), _TRUNCATE); strncpy_s(g_cfg[i].section, section ? section : "", _TRUNCATE);
            strncpy_s(g_cfg[i].key, key ? key : "", _TRUNCATE); strncpy_s(g_cfg[i].value, value, _TRUNCATE);
        }
    }
    ReleaseSRWLockExclusive(&g_cfgLock);
    char line[512];
    if (!file) strcpy_s(line, "ini overlay cleared");
    else _snprintf_s(line, _TRUNCATE, "ini overlay %s [%s] %s = %s", BaseName(file), section ? section : "", key ? key : "", value ? value : "(removed)");
    Log("%s", line);
}

static int __cdecl Svc_ListConfig(char* buf, int size) {
    if (!buf || size <= 0) return 0;
    int n = 0; buf[0] = 0;
    AcquireSRWLockShared(&g_cfgLock);
    for (int i = 0; i < g_cfgN; i++) {
        int k = _snprintf_s(buf + n, size - n, _TRUNCATE, "%s [%s] %s=%s\n", g_cfg[i].file, g_cfg[i].section, g_cfg[i].key, g_cfg[i].value);
        if (k < 0) break;
        n += k;
    }
    ReleaseSRWLockShared(&g_cfgLock);
    return n;
}

static bool FindCfg(const char* file, const char* section, const char* key, char* out, size_t size) {
    if (!file || !section || !key || !g_cfgN) return false;
    bool hit = false;
    AcquireSRWLockShared(&g_cfgLock);
    for (int i = 0; i < g_cfgN && !hit; i++)
        if (!_stricmp(g_cfg[i].file, BaseName(file)) && !_stricmp(g_cfg[i].section, section) && !_stricmp(g_cfg[i].key, key)) {
            strncpy_s(out, size, g_cfg[i].value, _TRUNCATE); hit = true;
        }
    ReleaseSRWLockShared(&g_cfgLock);
    return hit;
}

static UINT WINAPI Hk_GetPrivateProfileIntA(LPCSTR sec, LPCSTR key, INT def, LPCSTR file) {
    char v[256];
    if (FindCfg(file, sec, key, v, sizeof(v))) return (UINT)strtol(v, nullptr, 0);
    return GetPrivateProfileIntA(sec, key, def, file);
}

static DWORD WINAPI Hk_GetPrivateProfileStringA(LPCSTR sec, LPCSTR key, LPCSTR def, LPSTR ret, DWORD size, LPCSTR file) {
    char v[256];
    if (ret && size && FindCfg(file, sec, key, v, sizeof(v))) {
        strncpy_s(ret, size, v, _TRUNCATE);
        return (DWORD)strlen(ret);
    }
    return GetPrivateProfileStringA(sec, key, def, ret, size, file);
}

static int PatchImport(HMODULE m, const char* fn, void* hook) {
    uint8_t* base = (uint8_t*)m;
    int n = 0;
    __try {
        auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
        auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!dir.VirtualAddress) return 0;
        for (auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; imp++) {
            if (!imp->OriginalFirstThunk) continue;
            auto names = (IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk);
            auto iat = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
            for (; names->u1.AddressOfData; names++, iat++) {
                if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
                if (strcmp((const char*)((IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData))->Name, fn)) continue;
                DWORD old;
                if (VirtualProtect(&iat->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) {
                    iat->u1.Function = (DWORD)(uintptr_t)hook;
                    VirtualProtect(&iat->u1.Function, sizeof(void*), old, &old);
                    n++;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}

// ---------------------------------------------------------------------------------------------- patch registry
// Every plugin's VirtualProtect on SWTFU.exe's image goes through Hk_VirtualProtect (import hook, like the ini overlay):
// the first unprotect of a range by an owner keeps the bytes it had then, so ListPatches can tell who changed which game
// bytes and whether the change is still in place - for every plugin, old builds included. One entry per (address,
// owner): a hot-reloaded instance updates its entry (generation), a plugin stacked on another one gets its own entry
// whose "orig" holds the lower plugin's bytes. Not seen: writes into memory that is writable anyway (.data).
struct PatchEntry { uint32_t addr, n; char owner[32]; int gen; uint8_t pre, orig[16]; DWORD tick; };
static const LONG kPatchCap = 4096;
static PatchEntry* g_pt;
static LONG g_ptN;
static SRWLOCK g_ptLock = SRWLOCK_INIT;
static uint8_t *g_exeLo, *g_exeHi;

static void OwnerOf(const void* ret, char* owner, int size, int* gen) {
    *gen = 0; strcpy_s(owner, size, "?");
    HMODULE m = nullptr; char p[MAX_PATH];
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)ret, &m) ||
        !GetModuleFileNameA(m, p, MAX_PATH)) return;
    char b[64]; strncpy_s(b, BaseName(p), _TRUNCATE);
    if (char* dot = strrchr(b, '.')) *dot = 0;                       // .dll
    if (char* g = strrchr(b, '.')) { *gen = atoi(g + 1); *g = 0; }    // shadow copy DK_X.<generation>
    strncpy_s(owner, size, b, _TRUNCATE);
}

static void RecordPatch(uint8_t* a, SIZE_T size, const void* ret) {
    char owner[32]; int gen;
    OwnerOf(ret, owner, sizeof owner, &gen);
    AcquireSRWLockExclusive(&g_ptLock);
    LONG i = 0;
    for (; i < g_ptN; i++) if (g_pt[i].addr == (uint32_t)(uintptr_t)a && !strcmp(g_pt[i].owner, owner)) break;
    if (i < g_ptN) { g_pt[i].gen = gen; if (size > g_pt[i].n) g_pt[i].n = (uint32_t)size; }
    else if (g_ptN < kPatchCap) {
        PatchEntry& e = g_pt[g_ptN++];
        e.addr = (uint32_t)(uintptr_t)a; e.n = (uint32_t)size; e.gen = gen; e.tick = GetTickCount();
        strncpy_s(e.owner, owner, _TRUNCATE);
        dk::SafeRead(a - 1, &e.pre, 1);
        dk::SafeRead(a, e.orig, size < 16 ? size : 16);
    }
    ReleaseSRWLockExclusive(&g_ptLock);
}

static BOOL WINAPI Hk_VirtualProtect(LPVOID addr, SIZE_T size, DWORD prot, PDWORD old) {
    uint8_t* a = (uint8_t*)addr;
    const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (g_pt && a > g_exeLo && a < g_exeHi && size && (prot & writable)) RecordPatch(a, size, _ReturnAddress());
    return VirtualProtect(addr, size, prot, old);
}

static void Hex(char* o, const uint8_t* b, int n) { for (int i = 0; i < n; i++) sprintf_s(o + 2 * i, 3, "%02X", b[i]); o[2 * n] = 0; }

static int __cdecl Svc_ListPatches(char* buf, int size) {
    if (!buf || size <= 0) return 0;
    int n = 0; buf[0] = 0;
    AcquireSRWLockShared(&g_ptLock);
    for (LONG i = 0; i < g_ptN; i++) {
        const PatchEntry& e = g_pt[i];
        const int k = e.n < 16 ? (int)e.n : 16;
        uint8_t now[16] = {};
        bool same = false;
        __try { memcpy(now, (void*)(uintptr_t)e.addr, k); same = !memcmp(now, e.orig, k); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        char o[40], w[40]; Hex(o, e.orig, k); Hex(w, now, k);
        int r = _snprintf_s(buf + n, size - n, _TRUNCATE, "%08X %u %s %d %s %02X %s %s\n", e.addr, e.n, e.owner, e.gen,
                            same ? "restored" : "active", e.pre, o, w);
        if (r < 0) break;
        n += r;
    }
    ReleaseSRWLockShared(&g_ptLock);
    return n;
}

void Svc_HookPluginConfig(HMODULE plugin) {
    PatchImport(plugin, "GetPrivateProfileIntA", (void*)Hk_GetPrivateProfileIntA);
    PatchImport(plugin, "GetPrivateProfileStringA", (void*)Hk_GetPrivateProfileStringA);
    PatchImport(plugin, "VirtualProtect", (void*)Hk_VirtualProtect);   // patch registry
}

// ---------------------------------------------------------------------------------------------- reports + crashes
struct Dumper { char name[48]; void (__cdecl* fn)(const char*); };
static Dumper g_dump[16];
static SRWLOCK g_dumpLock = SRWLOCK_INIT;
typedef BOOL(WINAPI* MiniDumpWriteDump_t)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                          PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
static MiniDumpWriteDump_t g_mdw;
static volatile LONG g_inCrash;
static LPTOP_LEVEL_EXCEPTION_FILTER g_nextFilter;   // the filter the game/AWL installed (Log.txt), chained after ours

static void __cdecl Svc_RegisterDumper(const char* name, void (__cdecl* fn)(const char*)) {
    AcquireSRWLockExclusive(&g_dumpLock);
    Dumper* free = nullptr;
    for (auto& d : g_dump) {
        if (d.fn && !strcmp(d.name, name)) { d.fn = fn; free = nullptr; ReleaseSRWLockExclusive(&g_dumpLock); return; }
        if (!d.fn && !free) free = &d;
    }
    if (free) { strncpy_s(free->name, name, _TRUNCATE); free->fn = fn; }
    ReleaseSRWLockExclusive(&g_dumpLock);
}

static void __cdecl Svc_UnregisterDumper(const char* name) {
    AcquireSRWLockExclusive(&g_dumpLock);
    for (auto& d : g_dump) if (d.fn && !strcmp(d.name, name)) d.fn = nullptr;
    ReleaseSRWLockExclusive(&g_dumpLock);
}

static bool CallDumper(void (__cdecl* fn)(const char*), const char* dir) {
    __try { fn(dir); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void CopyTail(const char* src, const char* dst, long maxBytes) {
    FILE* f = _fsopen(src, "rb", _SH_DENYNO);
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f), from = sz > maxBytes ? sz - maxBytes : 0;
    fseek(f, from, SEEK_SET);
    FILE* o = nullptr;
    if (!fopen_s(&o, dst, "wb") && o) {
        static char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) fwrite(buf, 1, n, o);
        fclose(o);
    }
    fclose(f);
}

static void CopyLogs(const char* dir) {
    char logs[MAX_PATH], pat[MAX_PATH], src[MAX_PATH], dst[MAX_PATH];
    sprintf_s(logs, "%s\\logs", dir);
    CreateDirectoryA(logs, nullptr);
    sprintf_s(pat, "%sDK_*.log", g_gameDir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            sprintf_s(src, "%s%s", g_gameDir, fd.cFileName); sprintf_s(dst, "%s\\%s", logs, fd.cFileName);
            CopyTail(src, dst, 256 * 1024);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    char la[MAX_PATH];
    if (GetEnvironmentVariableA("LOCALAPPDATA", la, MAX_PATH)) {
        sprintf_s(src, "%s\\Aspyr\\Star Wars The Force Unleashed\\Log.txt", la); sprintf_s(dst, "%s\\GameLog.txt", logs);
        CopyTail(src, dst, 32 * 1024);
    }
}

static void WriteEvents(const char* dir) {
    static char buf[kEvCap * 240];
    int n = Svc_CopyEvents(buf, sizeof(buf), 0);
    char p[MAX_PATH]; sprintf_s(p, "%s\\events.txt", dir);
    FILE* f = nullptr;
    if (!fopen_s(&f, p, "wb") && f) { fwrite(buf, 1, n, f); fclose(f); }
}

static DWORD g_crashTid;   // the crashed thread (the report is written by the crash helper thread)
static void* g_reserve;    // address space set aside at start, released for the dump (a 32-bit game that ran out of
static const SIZE_T kReserve = 16 * 1024 * 1024;   // address space left dbghelp no room: a 0-byte game.dmp, 2026-10-20)

// Tries the full dump, then smaller ones; each attempt's result goes into report.txt ("minidump:" lines). A failed
// attempt leaves a partial file, which is truncated before the next; when all fail the empty file is removed.
static bool WriteMiniDump(const char* dir, EXCEPTION_POINTERS* ep) {
    char rp[MAX_PATH]; sprintf_s(rp, "%s\\report.txt", dir);
    FILE* r = nullptr; fopen_s(&r, rp, "a");
    if (!g_mdw) { if (r) { fprintf(r, "\nminidump: dbghelp MiniDumpWriteDump not available\n"); fclose(r); } return false; }
    if (g_reserve && VirtualFree(g_reserve, 0, MEM_RELEASE)) {
        g_reserve = nullptr;
        if (r) fprintf(r, "\nminidump: released the %u MB address-space reserve for it\n", (unsigned)(kReserve >> 20));
    }
    char p[MAX_PATH]; sprintf_s(p, "%s\\game.dmp", dir);
    HANDLE f = CreateFileA(p, GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) { if (r) { fprintf(r, "minidump: cannot create game.dmp (error %lu)\n", GetLastError()); fclose(r); } return false; }
    MINIDUMP_EXCEPTION_INFORMATION mei = { ep && g_crashTid ? g_crashTid : GetCurrentThreadId(), ep, FALSE };
    static const struct { MINIDUMP_TYPE type; const char* name; } kTry[] = {
        { (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory | MiniDumpWithThreadInfo |
                          MiniDumpWithUnloadedModules | MiniDumpWithHandleData), "full (stacks + referenced memory)" },
        { (MINIDUMP_TYPE)(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules), "normal + thread info" },
        { MiniDumpNormal, "normal (stacks only)" },
    };
    bool ok = false;
    for (const auto& t : kTry) {
        SetFilePointer(f, 0, nullptr, FILE_BEGIN); SetEndOfFile(f);
        DWORD t0 = GetTickCount();
        if (g_mdw(GetCurrentProcess(), GetCurrentProcessId(), f, t.type, ep ? &mei : nullptr, nullptr, nullptr)) {
            LARGE_INTEGER sz = {}; GetFileSizeEx(f, &sz);
            if (r) fprintf(r, "minidump: %s written, %lld KB, %lu ms\n", t.name, sz.QuadPart >> 10, GetTickCount() - t0);
            ok = true;
            break;
        }
        DWORD e = GetLastError();   // MiniDumpWriteDump sets an HRESULT: 0x8007000E = out of memory, 0x8007012B = partial copy
        if (r) fprintf(r, "minidump: %s FAILED, error 0x%08lX%s\n", t.name, e,
                       e == 0x8007000E || e == ERROR_NOT_ENOUGH_MEMORY || e == ERROR_OUTOFMEMORY ? " (out of memory)" :
                       e == 0x8007012B || e == ERROR_PARTIAL_COPY ? " (partial copy: unreadable memory)" : "");
    }
    CloseHandle(f);
    if (!ok) { DeleteFileA(p); if (r) fprintf(r, "minidump: none written (game.dmp removed)\n"); }
    if (r) fclose(r);
    return ok;
}

// The process's memory at the report moment: a 32-bit game fails allocations (C++ bad_alloc 0xE06D7363 in the asset
// loader, failed dumps) when its address space is used up or too fragmented - look at "largest free block".
static void WriteMemState(FILE* f) {
    PROCESS_MEMORY_COUNTERS_EX pm = { sizeof(pm) };
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pm, sizeof(pm)))
        fprintf(f, "\nmemory: private %lu MB, working set %lu MB (peak %lu MB), commit %lu MB\n", (unsigned long)(pm.PrivateUsage >> 20),
                (unsigned long)(pm.WorkingSetSize >> 20), (unsigned long)(pm.PeakWorkingSetSize >> 20), (unsigned long)(pm.PagefileUsage >> 20));
    SYSTEM_INFO si; GetSystemInfo(&si);
    uintptr_t a = (uintptr_t)si.lpMinimumApplicationAddress, hi = (uintptr_t)si.lpMaximumApplicationAddress;
    unsigned long long freeAll = 0, largest = 0, reserved = 0, committed = 0; unsigned blocks = 0;
    MEMORY_BASIC_INFORMATION mbi;
    while (a < hi && VirtualQuery((void*)a, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_FREE) { freeAll += mbi.RegionSize; blocks++; if (mbi.RegionSize > largest) largest = mbi.RegionSize; }
        else if (mbi.State == MEM_RESERVE) reserved += mbi.RegionSize;
        else committed += mbi.RegionSize;
        a = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (!mbi.RegionSize) break;
    }
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)GetModuleHandleA(nullptr);
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)((const uint8_t*)dos + dos->e_lfanew);
    bool laa = (nt->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) != 0;
    fprintf(f, "address space: %llu MB usable (%s), %llu MB committed, %llu MB reserved, %llu MB free in %u blocks, largest free block %llu MB%s\n",
            (unsigned long long)(hi - (uintptr_t)si.lpMinimumApplicationAddress) >> 20, laa ? "large-address-aware" : "not large-address-aware",
            committed >> 20, reserved >> 20, freeAll >> 20, blocks, largest >> 20,
            largest < (64ull << 20) ? "  <- LOW: allocations of tens of MB fail (out of address space / fragmented)" : "");
}

static void ModuleOf(const void* a, char* name, size_t size, uintptr_t* base) {
    HMODULE m = nullptr;
    *base = 0;
    strcpy_s(name, size, "");
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)a, &m) && m) {
        char full[MAX_PATH]; GetModuleFileNameA(m, full, MAX_PATH);
        strcpy_s(name, size, BaseName(full)); *base = (uintptr_t)m;
    }
}

// "SWTFU.exe+21B152 (nearest DK symbol luaD_throw+52)" / "DK_FPSFix.dll+1A20" / "DK code cave of DK_X" / private code
static void Describe(const void* a, char* out, size_t size) {
    char mod[64]; uintptr_t base;
    ModuleOf(a, mod, sizeof(mod), &base);
    uintptr_t va = (uintptr_t)a;
    if (base) {
        int n = _snprintf_s(out, size, _TRUNCATE, "%s+%X", mod, (unsigned)(va - base));
        if (!_stricmp(mod, "SWTFU.exe")) {   // nearest preceding function symbol from the database
            const char* best = nullptr; uint32_t bva = 0;
            for (const auto& s : dk::sym::kAll)
                if (s.func && s.va <= va && s.va > bva) { bva = s.va; best = s.name; }
            if (best && va - bva < 0x4000 && n > 0) _snprintf_s(out + n, size - n, _TRUNCATE, " (%s+%X)", best, (unsigned)(va - bva));
        }
        return;
    }
    if (const char* o = Svc_CodeOwner(a)) { _snprintf_s(out, size, _TRUNCATE, "%08X DK code cave (%s)", (unsigned)va, o); return; }
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(a, &mbi, sizeof(mbi)) && mbi.Type == MEM_PRIVATE && (mbi.Protect & 0xF0))
        _snprintf_s(out, size, _TRUNCATE, "%08X private executable memory (a DK patch cave / trampoline)", (unsigned)va);
    else _snprintf_s(out, size, _TRUNCATE, "%08X (no module)", (unsigned)va);
}

static bool AfterCall(uintptr_t ret) {
    uint8_t b[7];
    if (ret < 0x10000 || !dk::SafeRead((void*)(ret - 7), b, 7)) return false;
    return b[2] == 0xE8 || (b[1] == 0xFF && (b[2] & 0x38) == 0x10) || (b[4] == 0xFF && (b[5] & 0x38) == 0x10) ||
           (b[5] == 0xFF && (b[6] & 0x38) == 0x10);
}

static const char* ExName(DWORD c) {
    switch (c) {
    case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
    case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INVALID_OPERATION: return "EXCEPTION_FLT_INVALID_OPERATION";
    case 0xE06D7363: return "C++ exception";
    case 0xC0000409: return "STATUS_STACK_BUFFER_OVERRUN / fast fail";
    default: return "exception";
    }
}

static void WriteCrashText(const char* dir, const char* kind, const char* note, EXCEPTION_POINTERS* ep) {
    char p[MAX_PATH]; sprintf_s(p, "%s\\report.txt", dir);
    FILE* f = nullptr;
    if (fopen_s(&f, p, "w") || !f) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(f, "DK report: %s\ntime: %04d-%02d-%02d %02d:%02d:%02d.%03d\nnote: %s\nthread: %lu\nloader frame: %ld\n",
            kind, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, note ? note : "",
            ep && g_crashTid ? g_crashTid : GetCurrentThreadId(), g_frameCounter ? *g_frameCounter : -1);
    if (ep && ep->ExceptionRecord && ep->ContextRecord) {
        EXCEPTION_RECORD* er = ep->ExceptionRecord;
        CONTEXT* c = ep->ContextRecord;
        char where[200]; Describe(er->ExceptionAddress, where, sizeof(where));
        fprintf(f, "\nexception: 0x%08lX %s at %s\n", er->ExceptionCode, ExName(er->ExceptionCode), where);
        if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
            fprintf(f, "access: %s of 0x%08X\n", er->ExceptionInformation[0] == 0 ? "read" : er->ExceptionInformation[0] == 1 ? "write" : "execute",
                    (unsigned)er->ExceptionInformation[1]);
        fprintf(f, "registers: eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX ebp=%08lX esp=%08lX eip=%08lX\n",
                c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp, c->Eip);
        uint8_t code[16];
        if (dk::SafeRead((void*)(uintptr_t)c->Eip, code, sizeof(code))) {
            fprintf(f, "code at eip:");
            for (uint8_t b : code) fprintf(f, " %02X", b);
            fprintf(f, "\n");
        }
        fprintf(f, "\nreturn addresses on the stack (raw scan from esp; values after a CALL; stale ones are possible):\n");
        int shown = 0;
        for (uintptr_t a = c->Esp; a < (uintptr_t)c->Esp + 0x6000 && shown < 48; a += 4) {
            uintptr_t v;
            if (!dk::SafeRead((void*)a, &v, 4)) break;
            if (!AfterCall(v)) continue;
            char d[200]; Describe((void*)v, d, sizeof(d));
            if (strstr(d, "(no module)")) continue;
            fprintf(f, "  [esp+%04X] %08X %s\n", (unsigned)(a - c->Esp), (unsigned)v, d);
            shown++;
        }
    }
    WriteMemState(f);
    fprintf(f, "\nDK modules:\n");
    HMODULE mods[256]; DWORD need = 0;
    if (EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &need))
        for (DWORD i = 0; i < need / sizeof(HMODULE) && i < 256; i++) {
            char full[MAX_PATH]; GetModuleFileNameA(mods[i], full, MAX_PATH);
            const char* b = BaseName(full);
            MODULEINFO mi;
            if ((!_strnicmp(b, "DK_", 3) || !_stricmp(b, "dinput8.dll")) && GetModuleInformation(GetCurrentProcess(), mods[i], &mi, sizeof(mi)))
                fprintf(f, "  %08X-%08X %s\n", (unsigned)(uintptr_t)mi.lpBaseOfDll, (unsigned)((uintptr_t)mi.lpBaseOfDll + mi.SizeOfImage), full);
        }
    fclose(f);
}

static int MakeReportDir(const char* kind, char* dir, int size) {
    char root[MAX_PATH]; sprintf_s(root, "%sDK_Reports", g_gameDir);
    CreateDirectoryA(root, nullptr);
    SYSTEMTIME t; GetLocalTime(&t);
    _snprintf_s(dir, size, _TRUNCATE, "%s\\%s_%04d%02d%02d_%02d%02d%02d_%03d", root, kind, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                t.wSecond, t.wMilliseconds);
    return CreateDirectoryA(dir, nullptr) ? 1 : 0;
}

static void RunDumpers(const char* dir) {
    Dumper copy[16];
    AcquireSRWLockShared(&g_dumpLock);
    memcpy(copy, g_dump, sizeof(copy));
    ReleaseSRWLockShared(&g_dumpLock);
    for (auto& d : copy) if (d.fn && !CallDumper(d.fn, dir)) Log("dumper %s raised an exception", d.name);
}

// kind: "crash" (with ep), "hang", "report", "dump"; minidump for crash/hang/dump.
static int WriteReportImpl(const char* kind, const char* note, EXCEPTION_POINTERS* ep, char* dirOut, int size) {
    char dir[MAX_PATH];
    if (!MakeReportDir(kind, dir, MAX_PATH)) return 0;
    if (ep && ep->ExceptionRecord) {
        char line[160];
        _snprintf_s(line, _TRUNCATE, "CRASH 0x%08lX at %p (thread %lu)", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress, g_crashTid);
        Svc_Event("DK_Loader", line);
    }
    WriteCrashText(dir, kind, note, ep);
    if (strcmp(kind, "report")) WriteMiniDump(dir, ep);
    RunDumpers(dir);
    WriteEvents(dir);
    CopyLogs(dir);
    if (dirOut && size > 0) strncpy_s(dirOut, size, dir, _TRUNCATE);
    return 1;
}

static int __cdecl Svc_WriteReport(const char* kind, const char* note, char* dirOut, int size) {
    char line[200]; _snprintf_s(line, _TRUNCATE, "%s requested: %s", kind ? kind : "report", note ? note : "");
    Svc_Event("DK_Loader", line);
    return WriteReportImpl(kind ? kind : "report", note, nullptr, dirOut, size);
}

// The crash report is written by a helper thread created at start-up: the crashed thread may hold locks (heap, CRT,
// loader) or have a damaged stack, and a second fault inside the filter ends the process with nothing written (seen
// 2026-10-19). The filter only hands over the exception and waits (30 s at most), then chains to the game's filter.
static HANDLE g_crashGo, g_crashDone;
static EXCEPTION_POINTERS* g_crashEp;
static char g_crashDir[MAX_PATH];

static DWORD WINAPI CrashHelper(LPVOID) {
    for (;;) {   // a crash on a side thread does not always end the game (AWL's filter): report every one
        WaitForSingleObject(g_crashGo, INFINITE);
        ResetEvent(g_crashGo);
        g_crashDir[0] = 0;
        __try { WriteReportImpl("crash", "unhandled exception", g_crashEp, g_crashDir, MAX_PATH); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        SetEvent(g_crashDone);
    }
#pragma warning(suppress: 4702)   // unreachable
    return 0;
}

static LONG WINAPI DK_Filter(EXCEPTION_POINTERS* ep) {
    if (!InterlockedExchange(&g_inCrash, 1)) {   // one report at a time (a second crash meanwhile goes straight on)
        ResetEvent(g_crashDone);
        g_crashEp = ep;
        g_crashTid = GetCurrentThreadId();
        if (g_crashGo && g_crashDone) {
            SetEvent(g_crashGo);
            DWORD w = WaitForSingleObject(g_crashDone, 30000);
            Log("unhandled exception 0x%08lX at %p - report %s", ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0,
                ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr,
                w == WAIT_OBJECT_0 && g_crashDir[0] ? g_crashDir : "NOT finished (helper thread blocked)");
        }
        InterlockedExchange(&g_inCrash, 0);
    }
    return g_nextFilter ? g_nextFilter(ep) : EXCEPTION_CONTINUE_SEARCH;
}

// Modules that install their own filter (AWL_Release.dll: Log.txt crash log; the CRT) keep ours in front: their call
// is recorded as the filter we chain to.
static LPTOP_LEVEL_EXCEPTION_FILTER WINAPI Hk_SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER f) {
    LPTOP_LEVEL_EXCEPTION_FILTER prev = g_nextFilter;
    if (f != DK_Filter) g_nextFilter = f;
    return prev;
}

void Svc_InstallCrashFilter() {
    HMODULE dbg = LoadLibraryA("dbghelp.dll");
    g_mdw = dbg ? (MiniDumpWriteDump_t)GetProcAddress(dbg, "MiniDumpWriteDump") : nullptr;
    g_reserve = VirtualAlloc(nullptr, kReserve, MEM_RESERVE, PAGE_NOACCESS);   // address space only (no memory)
    g_crashGo = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    g_crashDone = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    HANDLE th = CreateThread(nullptr, 256 * 1024, CrashHelper, nullptr, 0, nullptr);
    if (th) CloseHandle(th);
    else { CloseHandle(g_crashGo); g_crashGo = nullptr; }
    g_nextFilter = SetUnhandledExceptionFilter(DK_Filter);
    HMODULE mods[256]; DWORD need = 0;
    int n = 0;
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)DK_Filter, &self);
    if (EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &need))
        for (DWORD i = 0; i < need / sizeof(HMODULE) && i < 256; i++) {
            char full[MAX_PATH]; GetModuleFileNameA(mods[i], full, MAX_PATH);
            if (mods[i] == self || !_strnicmp(full, "C:\\Windows", 10)) continue;   // the game's modules only
            n += PatchImport(mods[i], "SetUnhandledExceptionFilter", (void*)Hk_SetUnhandledExceptionFilter);
        }
    Log("crash filter installed (minidump %s; %d SetUnhandledExceptionFilter imports kept behind it; chained to %p)",
        g_mdw ? "on" : "unavailable", n, g_nextFilter);
}

// ---------------------------------------------------------------------------------------------- init
static void __cdecl Svc_LogSink(const char* tag, const char* line) { Svc_Event(tag, line); }

void Svc_Init(DK_LoaderApi* api, const char* gameDir, volatile LONG* frame, volatile LONG* lastFrameTick) {
    strcpy_s(g_gameDir, gameDir);
    g_frameCounter = frame;
    InitializeCriticalSection(&g_evCs);
    g_ev = (EvEntry*)VirtualAlloc(nullptr, sizeof(EvEntry) * kEvCap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    api->lastFrameTick = lastFrameTick;
    api->Event = Svc_Event;
    api->CopyEvents = Svc_CopyEvents;
    api->RegisterCode = Svc_RegisterCode;
    api->CodeOwner = Svc_CodeOwner;
    api->SetConfig = Svc_SetConfig;
    api->ListConfig = Svc_ListConfig;
    api->RegisterDumper = Svc_RegisterDumper;
    api->UnregisterDumper = Svc_UnregisterDumper;
    api->WriteReport = Svc_WriteReport;
    if (HMODULE exe = GetModuleHandleA(nullptr)) {   // patch registry: the exe's whole image
        uint8_t* base = (uint8_t*)exe;
        auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
        g_exeLo = base; g_exeHi = base + nt->OptionalHeader.SizeOfImage;
        g_pt = (PatchEntry*)VirtualAlloc(nullptr, sizeof(PatchEntry) * kPatchCap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    }
    api->ListPatches = Svc_ListPatches;
    dk::SetLogSink(Svc_LogSink);   // the loader's own log lines go into the ring too
}
