#include "dk_util.h"
#include "dk_plugin.h"
#include <psapi.h>
#include <stdio.h>
#include <share.h>
#include <stdarg.h>
#include <string.h>
#include <vector>

namespace dk {

static char g_logPath[MAX_PATH];
static char g_logTag[32] = "DK";
static CRITICAL_SECTION g_logCs;
static int g_logLines, g_logMax = 400;

static LogSink_t g_sink;
static const DK_LoaderApi* g_loaderApi;
static bool g_loaderLooked;

void SetLogSink(LogSink_t sink) { g_sink = sink; }

const DK_LoaderApi* LoaderApi() {
    if (g_loaderLooked) return g_loaderApi;
    g_loaderLooked = true;
    HMODULE mods[256]; DWORD need = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &need)) return nullptr;
    for (DWORD i = 0; i < need / sizeof(HMODULE) && i < 256; i++) {
        auto get = (DK_GetLoaderApi_t)GetProcAddress(mods[i], "DK_GetLoaderApi");
        if (!get) continue;
        const DK_LoaderApi* api = get();
        if (api && api->version >= 2) g_loaderApi = api;
        break;
    }
    return g_loaderApi;
}

void LogOpen(const char* path, const char* tag, int maxLines, bool append) {
    static bool cs;
    if (!cs) { InitializeCriticalSection(&g_logCs); cs = true; }
    strcpy_s(g_logPath, path);
    strcpy_s(g_logTag, tag ? tag : "DK");
    g_logMax = maxLines > 0 ? maxLines : 400;
    g_logLines = 0;
    if (!append) DeleteFileA(g_logPath);
    else if (FILE* f = _fsopen(g_logPath, "rb", _SH_DENYNO)) {   // a reloaded instance continues the file: its lines count
        char buf[4096]; size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) for (size_t i = 0; i < n; i++) g_logLines += buf[i] == '\n';
        fclose(f);
    }
    if (!g_sink) { const DK_LoaderApi* api = LoaderApi(); if (api) g_sink = api->Event; }   // flight-recorder ring
}

// The line cap used to drop every later line silently (2026-10-21: DK_VaderSwap.log at its cap lost the "swap done"
// lines the test tools wait for, in a warm test game up for hours). At the cap the file now becomes <name>.old.log and
// a new one starts. A rename that fails (a reader holding the file without delete sharing) is retried 50 lines later;
// the line is written either way.
static void RotateLog() {
    char old[MAX_PATH];
    strcpy_s(old, g_logPath);
    if (char* dot = strrchr(old, '.')) if (!_stricmp(dot, ".log")) *dot = 0;
    strcat_s(old, ".old.log");
    if (!MoveFileExA(g_logPath, old, MOVEFILE_REPLACE_EXISTING)) { g_logLines = g_logMax - 50; return; }
    g_logLines = 1;
    if (FILE* f = _fsopen(g_logPath, "a", _SH_DENYWR)) {
        SYSTEMTIME t; GetLocalTime(&t);
        const char* base = strrchr(old, '\\');
        fprintf(f, "%02d:%02d:%02d.%03d [%s] log rotated: the previous %d lines are in %s\n", t.wHour, t.wMinute, t.wSecond,
                t.wMilliseconds, g_logTag, g_logMax, base ? base + 1 : old);
        fclose(f);
    }
}

void Log(const char* fmt, ...) {
    if (!g_logPath[0]) return;
    char line[1024];
    va_list a; va_start(a, fmt); _vsnprintf_s(line, _TRUNCATE, fmt, a); va_end(a);
    EnterCriticalSection(&g_logCs);
    if (g_logLines >= g_logMax) RotateLog();   // keep the files short even if something misbehaves
    g_logLines++;
    {
        FILE* f = nullptr;
        // shareable for readers (test tools read the logs while the game runs); fopen_s denies all sharing
        if ((f = _fsopen(g_logPath, "a", _SH_DENYWR)) != nullptr) {
            SYSTEMTIME t; GetLocalTime(&t);
            fprintf(f, "%02d:%02d:%02d.%03d [%s] %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, g_logTag, line);
            fclose(f);
        }
    }
    LeaveCriticalSection(&g_logCs);
    if (g_sink) g_sink(g_logTag, line);   // the ring keeps every line, also past the file's line cap
}

// Code caves / trampolines come from one 64 KB slab per plugin instance (2026-10-20: one VirtualAlloc per cave reserved
// 64 KB of address space each and took a loader code-registry slot each, which filled after ~200 hot reloads - crash
// reports then stopped naming the cave's owner). Caves are never freed (a hook may still jump through one), so the slab
// stays too; a cave larger than 4 KB gets its own allocation.
static SRWLOCK g_slabLock = SRWLOCK_INIT;
static uint8_t* g_slab;
static size_t g_slabUsed;
static const size_t kSlab = 64 * 1024;

uint8_t* AllocCode(size_t size) {
    const DK_LoaderApi* api = LoaderApi();
    if (size > 4096) {
        uint8_t* p = (uint8_t*)VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p && api) api->RegisterCode(p, (DWORD)size, g_logTag);
        return p;
    }
    size = (size + 15) & ~(size_t)15;
    AcquireSRWLockExclusive(&g_slabLock);
    if (!g_slab || g_slabUsed + size > kSlab) {
        uint8_t* s = (uint8_t*)VirtualAlloc(nullptr, kSlab, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!s) { ReleaseSRWLockExclusive(&g_slabLock); return nullptr; }
        memset(s, 0xCC, kSlab);   // int3 between caves
        g_slab = s; g_slabUsed = 0;
        if (api) api->RegisterCode(s, (DWORD)kSlab, g_logTag);
    }
    uint8_t* p = g_slab + g_slabUsed;
    g_slabUsed += size;
    ReleaseSRWLockExclusive(&g_slabLock);
    return p;
}

Range TextRange(HMODULE m) { return SectionRange(m, ".text"); }

static bool Parse(const char* p, std::vector<int>& out) {
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (p[0] == '?') { out.push_back(-1); p += (p[1] == '?') ? 2 : 1; continue; }
        char* e; long v = strtol(p, &e, 16);
        if (e == p || v < 0 || v > 255) return false;
        out.push_back((int)v); p = e;
    }
    return !out.empty();
}

uint8_t* FindUnique(const Range& r, const char* pattern, int* count) {
    std::vector<int> pat;
    if (count) *count = 0;
    if (!r.lo || !Parse(pattern, pat)) return nullptr;
    // anchor on the first non-wildcard byte
    size_t a = 0;
    while (a < pat.size() && pat[a] < 0) a++;
    if (a == pat.size()) return nullptr;
    uint8_t first = (uint8_t)pat[a];
    uint8_t* found = nullptr; int n = 0;
    uint8_t* end = r.hi - pat.size();
    for (uint8_t* p = r.lo; p <= end; p++) {
        p = (uint8_t*)memchr(p + a, first, (end + a) - (p + a) + 1);
        if (!p) break;
        p -= a;
        size_t i = 0;
        for (; i < pat.size(); i++)
            if (pat[i] >= 0 && p[i] != (uint8_t)pat[i]) break;
        if (i == pat.size()) { if (!found) found = p; n++; }
    }
    if (count) *count = n;
    return n == 1 ? found : nullptr;
}

uint8_t* Sig(const Range& r, const char* name, const char* pattern, int offset) {
    int n = 0;
    uint8_t* p = FindUnique(r, pattern, &n);
    if (p) { Log("sig %-22s found at %08X", name, (unsigned)(uintptr_t)(p + offset)); return p + offset; }
    Log("sig %-22s %s (%d matches) - feature left vanilla", name, n ? "AMBIGUOUS" : "MISSING", n);
    return nullptr;
}

bool SigAll(const Range& r, const char* name, const char* pattern, uint8_t** out, int expected) {
    std::vector<int> pat;
    int n = 0;
    if (r.lo && Parse(pattern, pat)) {
        uint8_t* end = r.hi - pat.size();
        for (uint8_t* p = r.lo; p <= end; p++) {
            size_t i = 0;
            for (; i < pat.size(); i++)
                if (pat[i] >= 0 && p[i] != (uint8_t)pat[i]) break;
            if (i == pat.size()) { if (n < expected) out[n] = p; n++; }
        }
    }
    if (n == expected) { Log("sig %-22s found %d sites (first %08X)", name, n, (unsigned)(uintptr_t)out[0]); return true; }
    Log("sig %-22s %d matches, expected %d - feature left vanilla", name, n, expected);
    return false;
}

Range SectionRange(HMODULE m, const char* name) {
    uint8_t* base = (uint8_t*)m;
    auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    auto s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++)
        if (!strncmp((const char*)s->Name, name, 8)) return { base + s->VirtualAddress, base + s->VirtualAddress + s->Misc.VirtualSize };
    return { nullptr, nullptr };
}

// ---- recorded patches (hot reload): original bytes of the first write to each address
struct PatchRec { uint8_t* addr; uint32_t n; uint8_t orig[32]; uint8_t now[32]; };
static PatchRec g_patches[512];
static int g_patchN;
static SRWLOCK g_patchLock = SRWLOCK_INIT;

static bool RawWrite(void* dst, const void* src, size_t n) {
    DWORD old;
    if (!VirtualProtect(dst, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(dst, src, n);
    VirtualProtect(dst, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), dst, n);
    return true;
}

bool WriteMem(void* dst, const void* src, size_t n) {
    AcquireSRWLockExclusive(&g_patchLock);
    PatchRec* r = nullptr;
    if (n <= sizeof(r->orig)) {
        for (int i = 0; i < g_patchN && !r; i++)
            if (g_patches[i].addr == dst && g_patches[i].n == n) r = &g_patches[i];
        if (!r && g_patchN < (int)(sizeof(g_patches) / sizeof(g_patches[0]))) {
            r = &g_patches[g_patchN];
            if (SafeRead(dst, r->orig, n)) { r->addr = (uint8_t*)dst; r->n = (uint32_t)n; g_patchN++; }
            else r = nullptr;
        }
    }
    if (!r) Log("patch at %08X (%u bytes) not recorded - it cannot be undone", (unsigned)(uintptr_t)dst, (unsigned)n);
    bool ok = RawWrite(dst, src, n);
    if (ok && r) memcpy(r->now, src, n);
    ReleaseSRWLockExclusive(&g_patchLock);
    return ok;
}

int RestorePatches() {
    AcquireSRWLockExclusive(&g_patchLock);
    int conflicts = 0, restored = 0, kept = 0;
    for (int i = g_patchN - 1; i >= 0; i--) {
        PatchRec& r = g_patches[i];
        uint8_t cur[32];
        if (!SafeRead(r.addr, cur, r.n) || memcmp(cur, r.now, r.n)) {
            conflicts++;
            Log("restore %08X: bytes changed since our patch (another mod?) - left as is", (unsigned)(uintptr_t)r.addr);
            continue;
        }
        if (RawWrite(r.addr, r.orig, r.n)) { restored++; r.n = 0; }
    }
    // Conflicting records stay: a plugin that refused its unload because of them (another plugin is chained onto its
    // hook) must refuse again at the next attempt. Clearing them let the second attempt unload it while the other
    // plugin still called into it; the new instance then loaded at the same base and the two hooks called each other
    // until the stack overflowed (smoke.py twice on a warm game, 2026-10-19).
    for (int i = 0; i < g_patchN; i++) if (g_patches[i].n) g_patches[kept++] = g_patches[i];
    Log("restored %d patches, %d conflicts", restored, conflicts);
    g_patchN = kept;
    ReleaseSRWLockExclusive(&g_patchLock);
    return conflicts;
}

int PatchCount() { return g_patchN; }

bool SafeRead(const void* src, void* dst, size_t n) {
    __try { memcpy(dst, src, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool SafeWrite(void* dst, const void* src, size_t n) {
    __try { memcpy(dst, src, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

uint8_t* FindSym(const Range& text, const char* name) {
    for (const auto& s : sym::kSigs) {
        if (strcmp(s.name, name)) continue;
        int n = 0;
        uint8_t* p = FindUnique(text, s.sig, &n);
        if (!p) { Log("sym %-22s signature %s (%d matches)", name, n ? "AMBIGUOUS" : "MISSING", n); return nullptr; }
        p += s.off;
        uint8_t* r = p;
        if (s.how == 'r') r = (*p == 0xE8 || *p == 0xE9) ? p + 5 + Rd<int32_t>(p + 1) : nullptr;
        else if (s.how == 'i') r = Rd<uint8_t*>(p);
        else if (s.how == 'b') r = (uint8_t*)(uintptr_t)*p;
        if (r && (uintptr_t)r != s.va) Log("sym %-22s at %08X, database says %08X (game updated?)", name, (unsigned)(uintptr_t)r, s.va);
        return r;
    }
    Log("sym %s has no signature in dk_syms.h", name);
    return nullptr;
}

void* Detour(uint8_t* target, void* hook, size_t stolen) {
    if (!target || stolen < 5 || stolen > 32) return nullptr;
    uint8_t* tr = AllocCode(64);
    if (!tr) return nullptr;
    memcpy(tr, target, stolen);
    if (target[0] == 0xE8 || target[0] == 0xE9) {   // a rel32 call / jump first (another plugin's detour): relocated
        int32_t rel = (int32_t)((target + 5 + Rd<int32_t>(target + 1)) - (tr + 5));
        memcpy(tr + 1, &rel, 4);
    }
    tr[stolen] = 0xE9;
    int32_t back = (int32_t)((target + stolen) - (tr + stolen + 5));
    memcpy(tr + stolen + 1, &back, 4);
    uint8_t patch[32];
    memset(patch, 0x90, sizeof(patch));
    patch[0] = 0xE9;
    int32_t rel = (int32_t)((uint8_t*)hook - (target + 5));
    memcpy(patch + 1, &rel, 4);
    if (!WriteMem(target, patch, stolen)) return nullptr;   // (the cave stays: AllocCode carves caves from a shared slab)
    FlushInstructionCache(GetCurrentProcess(), tr, 64);
    return tr;
}

void* RedirectCall(uint8_t* site, void* fn) {
    if (!site || site[0] != 0xE8) return nullptr;
    void* old = site + 5 + Rd<int32_t>(site + 1);
    int32_t rel = (int32_t)((uint8_t*)fn - (site + 5));
    return WriteMem(site + 1, &rel, 4) ? old : nullptr;
}

bool HookedBy(const uint8_t* site, char* name, int size) {
    if (size > 0) *name = 0;
    uint8_t op = 0; int32_t rel = 0;
    if (!site || !SafeRead(site, &op, 1) || (op != 0xE8 && op != 0xE9) || !SafeRead(site + 1, &rel, 4)) return false;
    const uint8_t* t = site + 5 + rel;
    HMODULE m = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)t, &m)) {
        if (m == GetModuleHandleA(nullptr)) return false;   // the game's own code: vanilla
        char p[MAX_PATH] = "";
        GetModuleFileNameA(m, p, MAX_PATH);
        const char* b = strrchr(p, '\\');
        strncpy_s(name, size, b ? b + 1 : p, _TRUNCATE);
        return true;
    }
    const DK_LoaderApi* api = LoaderApi();
    const char* o = api && api->version >= 2 ? api->CodeOwner(t) : nullptr;
    if (o) _snprintf_s(name, size, _TRUNCATE, "a code cave of %s", o);
    else _snprintf_s(name, size, _TRUNCATE, "code at %08X", (unsigned)(uintptr_t)t);
    return true;
}

int StackedOn(char* who, int size) {
    if (who && size > 0) *who = 0;
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&StackedOn, &self);
    const DK_LoaderApi* api = LoaderApi();
    int n = 0;
    AcquireSRWLockShared(&g_patchLock);
    for (int i = 0; i < g_patchN; i++) {
        const PatchRec& r = g_patches[i];
        // only our rel32 call / jump redirects: a whole E8 / E9 instruction (Detour) or just its rel32 (RedirectCall
        // writes the 4 bytes after the opcode)
        const uint8_t* site = nullptr; const uint8_t* ours = nullptr;
        uint8_t op = 0;
        if (r.n >= 5 && (r.now[0] == 0xE8 || r.now[0] == 0xE9)) { site = r.addr; ours = r.now + 1; }
        else if (r.n == 4 && SafeRead(r.addr - 1, &op, 1) && (op == 0xE8 || op == 0xE9)) { site = r.addr - 1; ours = r.now; }
        if (!site) continue;
        uint8_t cur[5];
        if (!SafeRead(site, cur, 5) || (cur[0] != 0xE8 && cur[0] != 0xE9) || !memcmp(cur + 1, ours, 4)) continue;
        int32_t rel; memcpy(&rel, cur + 1, 4);
        const uint8_t* t = site + 5 + rel;
        HMODULE m = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)t, &m) && m == self) continue;
        const char* o = !m && api && api->version >= 2 ? api->CodeOwner(t) : nullptr;
        if (o && !strcmp(o, g_logTag)) continue;   // one of our own caves
        char name[MAX_PATH];
        if (!HookedBy(site, name, sizeof(name))) continue;   // back to the game's code: nobody calls us from there
        if (!n && who && size > 0) _snprintf_s(who, size, _TRUNCATE, "%s (at %08X)", name, (unsigned)(uintptr_t)site);
        n++;
    }
    ReleaseSRWLockShared(&g_patchLock);
    return n;
}

}  // namespace dk
