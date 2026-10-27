#include "dk_lua.h"
#include "dk_util.h"
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

namespace dk { namespace lua {

typedef int(__cdecl* CF_t)(void* L);
static struct {
    int(__cdecl* gettop)(void*);
    void(__cdecl* settop)(void*, int);
    const char*(__cdecl* tolstring)(void*, int, size_t*);
    int(__cdecl* type)(void*, int);
    float(__cdecl* tonumber)(void*, int);
    int(__cdecl* toboolean)(void*, int);
    int(__cdecl* pcall)(void*, int, int, int);
    int(__cdecl* loadbuffer)(void*, const char*, size_t, const char*);
    int(__cdecl* cpcall)(void*, CF_t, void*);
    void*(__cdecl* lockGet)();
    void(__fastcall* lock)(void*, void*);
    void(__fastcall* unlock)(void*, void*);
    void** state;
} A;
static int s_init;   // 0 not tried, 1 ok, -1 failed
static int s_errors;
static char s_lastErr[256];

// True when the bytes at va match a "8B 4C 24 ??" style pattern.
static bool BytesAt(uint32_t va, const char* pat) {
    uint8_t want[32], mask[32]; int n = 0;
    for (const char* p = pat; *p && n < 32;) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (p[0] == '?') { mask[n] = 0; want[n++] = 0; p += (p[1] == '?') ? 2 : 1; continue; }
        unsigned v; if (sscanf_s(p, "%2x", &v) != 1) return false;
        mask[n] = 1; want[n++] = (uint8_t)v; p += 2;
    }
    uint8_t have[32];
    if (!n || !SafeRead((const void*)va, have, n)) return false;
    for (int i = 0; i < n; i++) if (mask[i] && have[i] != want[i]) return false;
    return true;
}

bool Init() {
    if (s_init) return s_init > 0;
    namespace S = dk::sym;
    HMODULE sl = GetModuleHandleA("ScumLibLEC_Retail.dll");
    A.lock = sl ? (decltype(A.lock))GetProcAddress(sl, "?Lock@CriticalSection@lec@@QAEXXZ") : nullptr;
    A.unlock = sl ? (decltype(A.unlock))GetProcAddress(sl, "?Unlock@CriticalSection@lec@@QAEXXZ") : nullptr;
    A.state = (void**)S::gLuaState;
    A.gettop = (decltype(A.gettop))S::lua_gettop;
    A.settop = (decltype(A.settop))S::lua_settop;
    A.tolstring = (decltype(A.tolstring))S::lua_tolstring;
    A.type = (decltype(A.type))S::lua_type;
    A.tonumber = (decltype(A.tonumber))S::lua_tonumber;
    A.toboolean = (decltype(A.toboolean))S::lua_toboolean;
    A.pcall = (decltype(A.pcall))S::lua_pcall;
    A.loadbuffer = (decltype(A.loadbuffer))S::luaL_loadbuffer;
    A.cpcall = (decltype(A.cpcall))S::lua_cpcall;
    A.lockGet = (decltype(A.lockGet))S::LuaLock_Get;
    const char* bad = nullptr;
    if (!A.lock || !A.unlock) bad = "ScumLib CriticalSection Lock/Unlock exports";
    else if (!BytesAt(S::lua_gettop, S::lua_gettop_bytes)) bad = "lua_gettop";
    else if (!BytesAt(S::lua_settop, S::lua_settop_bytes)) bad = "lua_settop";
    else if (!BytesAt(S::lua_tolstring, S::lua_tolstring_bytes)) bad = "lua_tolstring";
    else if (!BytesAt(S::lua_type, S::lua_type_bytes)) bad = "lua_type";
    else if (!BytesAt(S::lua_tonumber, S::lua_tonumber_bytes)) bad = "lua_tonumber";
    else if (!BytesAt(S::lua_toboolean, S::lua_toboolean_bytes)) bad = "lua_toboolean";
    else if (!BytesAt(S::lua_pcall, S::lua_pcall_bytes)) bad = "lua_pcall";
    else if (!BytesAt(S::luaL_loadbuffer, S::luaL_loadbuffer_bytes)) bad = "luaL_loadbuffer";
    else if (!BytesAt(S::lua_cpcall, S::lua_cpcall_bytes)) bad = "lua_cpcall";
    else if (!BytesAt(S::LuaLock_Get, S::LuaLock_Get_bytes)) bad = "LuaLock_Get";
    s_init = bad ? -1 : 1;
    if (bad) Log("Lua API not verified (%s) - every Lua call is refused", bad);
    return s_init > 0;
}

bool Ready() {
    if (!Init()) return false;
    void* L = nullptr;
    return SafeRead(A.state, &L, sizeof(L)) && L;
}

// One call in flight at a time (the game thread is the only caller; the lock below orders us with the GC job).
static struct Call { const char* code; const char* chunk; char* out; int size; float* nums; int maxNums; int count; bool ok; } s_call;

static void Append(char* out, int size, int* len, const char* s, size_t n) {
    if (*len && *len < size - 1) out[(*len)++] = '\t';
    for (size_t i = 0; i < n && *len < size - 1; i++) out[(*len)++] = s[i];
    out[*len] = 0;
}

static int __cdecl Protected(void* L) {
    Call& c = s_call;
    int base = A.gettop(L);
    size_t n = 0;
    if (A.loadbuffer(L, c.code, strlen(c.code), c.chunk) || A.pcall(L, 0, -1 /*LUA_MULTRET*/, 0)) {
        const char* e = A.tolstring(L, -1, &n);
        if (c.out) _snprintf_s(c.out, c.size, _TRUNCATE, "error: %s", e ? e : "?");
        strncpy_s(s_lastErr, e ? e : "?", _TRUNCATE);
        c.ok = false;
        return 0;
    }
    int top = A.gettop(L);
    int len = 0;
    if (c.out && c.size) c.out[0] = 0;
    for (int i = base + 1; i <= top; i++) {
        if (c.nums && c.count < c.maxNums) {
            int t = A.type(L, i);
            c.nums[c.count] = (t == 3 /*LUA_TNUMBER*/) ? A.tonumber(L, i) : NAN;
        }
        c.count++;
        if (c.out) {
            int t = A.type(L, i);
            if (t == 0) Append(c.out, c.size, &len, "nil", 3);
            else if (t == 1) { bool b = A.toboolean(L, i) != 0; Append(c.out, c.size, &len, b ? "true" : "false", b ? 4 : 5); }
            else if (t == 3 || t == 4) {   // numbers and strings (tolstring converts a number in place: fine for a result)
                const char* s = A.tolstring(L, i, &n);
                Append(c.out, c.size, &len, s ? s : "", s ? n : 0);
            } else Append(c.out, c.size, &len, "<obj>", 5);
        }
    }
    c.ok = true;
    return 0;
}

// where a native fault inside a Lua call happened (the binding that faulted is otherwise invisible)
static DWORD s_excCode; static void* s_excAt; static ULONG_PTR s_excRw, s_excAddr;
static int ExcFilter(EXCEPTION_POINTERS* e) {
    s_excCode = e->ExceptionRecord->ExceptionCode;
    s_excAt = e->ExceptionRecord->ExceptionAddress;
    s_excRw = e->ExceptionRecord->NumberParameters >= 2 ? e->ExceptionRecord->ExceptionInformation[0] : 0;
    s_excAddr = e->ExceptionRecord->NumberParameters >= 2 ? e->ExceptionRecord->ExceptionInformation[1] : 0;
    return EXCEPTION_EXECUTE_HANDLER;
}

// A native fault inside a Lua call unwinds past lua_cpcall without Lua's own unwinding (C call depth, error jump):
// a few are survivable, a repeating one would exhaust the C stack depth and end the game. After kMaxFaults this
// plugin makes no more Lua calls (logged once).
static int s_faults;
static const int kMaxFaults = 4;

static bool Exec(Call c) {
    if (!Ready() || s_faults >= kMaxFaults) {
        if (c.out && c.size) strncpy_s(c.out, c.size, s_faults >= kMaxFaults ? "error: Lua calls stopped after native faults" : "error: Lua not available", _TRUNCATE);
        return false;
    }
    void* cs = nullptr;
    bool ok = false;
    __try {
        void* L = *A.state;
        cs = A.lockGet();
        A.lock(cs, nullptr);
        int top = A.gettop(L);
        s_call = c;
        if (A.cpcall(L, Protected, nullptr)) {
            if (c.out && c.size) strncpy_s(c.out, c.size, "error: cpcall", _TRUNCATE);
            s_call.ok = false;
        }
        ok = s_call.ok;
        A.settop(L, top);
    } __except (ExcFilter(GetExceptionInformation())) {
        if (c.out && c.size)
            _snprintf_s(c.out, c.size, _TRUNCATE, "error: exception %08X at %p (%s %08X)", s_excCode, s_excAt,
                        s_excRw == 1 ? "write" : s_excRw == 8 ? "exec" : "read", (unsigned)s_excAddr);
        ok = false;
        if (++s_faults == kMaxFaults)
            Log("Lua: %d native faults inside Lua calls (last %08X at %p) - this plugin makes no more Lua calls", s_faults, s_excCode, s_excAt);
    }
    if (cs) A.unlock(cs, nullptr);
    if (!ok) {
        s_errors++;
        if (c.out && c.size) strncpy_s(s_lastErr, c.out, _TRUNCATE);
    }
    return ok;
}

bool Run(const char* code, char* out, int size, const char* chunkName) {
    Call c = {}; c.code = code; c.chunk = chunkName ? chunkName : "=DK"; c.out = out; c.size = size;
    return Exec(c);
}

static char s_buf[1024];
const char* Run(const char* code) {
    Run(code, s_buf, sizeof(s_buf), "=DK");
    return s_buf;
}

const char* Runf(const char* fmt, ...) {
    static char code[4096];
    va_list a; va_start(a, fmt); _vsnprintf_s(code, _TRUNCATE, fmt, a); va_end(a);
    return Run(code);
}

int RunNums(const char* code, float* out, int max) {
    Call c = {}; c.code = code; c.chunk = "=DK"; c.nums = out; c.maxNums = max;
    char err[256]; c.out = err; c.size = sizeof(err);
    for (int i = 0; i < max; i++) out[i] = NAN;
    if (!Exec(c)) return -1;
    return s_call.count < max ? s_call.count : max;
}

int Errors() { return s_errors; }
const char* LastError() { return s_lastErr; }

}}  // namespace dk::lua
