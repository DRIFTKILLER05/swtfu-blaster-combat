// DK_Blaster: engine access (player, positions, game state, clocks) and the facing / strafe hooks.
// Every address comes from the symbol DB (dk_syms.h) and is verified by its signature; a missing one turns that
// feature off with a log line.
#include "dkb.h"

using namespace dk;

namespace dkb {

// ---------------------------------------------------------------------------------------------- clocks
static LARGE_INTEGER s_qpf, s_qp0;
double RealNow() {
    if (!s_qpf.QuadPart) { QueryPerformanceFrequency(&s_qpf); QueryPerformanceCounter(&s_qp0); }
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    return (double)(q.QuadPart - s_qp0.QuadPart) / (double)s_qpf.QuadPart;
}
float GameNow() {
    float t = 0.0f;
    SafeRead((const void*)sym::gGameTime, &t, 4);
    return t;
}

// ---------------------------------------------------------------------------------------------- engine
typedef DWORD(__thiscall* GetPlayerRen_t)(void* mgr, int idx);
typedef BYTE*(__cdecl* GetPlayerComp_t)(DWORD ren);
typedef float*(__cdecl* RenGetPos_t)(void* entry);
typedef float*(__cdecl* RenFwd_t)(DWORD ren);
typedef void(__thiscall* SetStrafe_t)(BYTE* pc, bool on);
typedef void(__thiscall* Facing2D_t)(BYTE* self, float* out);

typedef void*(__thiscall* GetComp_t)(void* mgr, DWORD ren);
typedef float(__thiscall* HealthGet_t)(void* h);

static GetPlayerRen_t s_getPlayerRen;
static void** s_healthMgr; static int s_healthAdd; static GetComp_t s_healthComp; static DWORD s_healthOff;
static HealthGet_t s_healthGet, s_healthGetMax;
static GetPlayerComp_t s_getPlayerComp;
static RenGetPos_t s_renGetPos;
static RenFwd_t s_renFwd;                // Ren_Forward: the body's own forward (optional, see HkFacing2D)
static SetStrafe_t s_setStrafe;
static Facing2D_t s_facingOrig;
static DWORD* s_facingSlot;
static bool s_strafeOn;
static BYTE* s_stickJcc;                 // Blaster_StickEyeSub: jbe rel32 -> jmp rel32 while armed
static BYTE s_stickOrig[6], s_stickJmp[6];
static bool s_stickOn;

DWORD PlayerRen() {
    if (!s_getPlayerRen) return 0;
    __try {
        void* mgr = *(void**)sym::gPlayerMgr;
        return mgr ? s_getPlayerRen(mgr, 0) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
BYTE* PlayerComp(DWORD ren) {
    if (!ren || !s_getPlayerComp) return nullptr;
    __try { return s_getPlayerComp(ren); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
static BYTE* RenEntry(DWORD ren) {
    BYTE* base = *(BYTE**)sym::gRenTable;
    if (!base) return nullptr;
    BYTE* e = base + (ren & 0xFFFF) * 0x40;
    return (*(DWORD*)(e + 8) == ren) ? e : nullptr;
}
bool RenPos(DWORD ren, float out[3]) {
    if (!ren || !s_renGetPos) return false;
    __try {
        BYTE* e = RenEntry(ren);
        if (!e) return false;
        float* p = s_renGetPos(e);
        if (!p) return false;
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// health of any ren (-1 = no health component), the way game.GetHealth reads it
float RenHealth(DWORD ren) {
    if (!ren || !s_healthGet) return -1.0f;
    __try {
        void* m = *s_healthMgr;
        void* c = m ? s_healthComp((BYTE*)m + s_healthAdd, ren) : nullptr;
        return c ? s_healthGet((BYTE*)c + s_healthOff) : -1.0f;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1.0f; }
}
bool RenHealthOk() { return s_healthGet != nullptr; }
float RenMaxHealth(DWORD ren) {
    if (!ren || !s_healthGetMax) return -1.0f;
    __try {
        void* m = *s_healthMgr;
        void* c = m ? s_healthComp((BYTE*)m + s_healthAdd, ren) : nullptr;
        return c ? s_healthGetMax((BYTE*)c + s_healthOff) : -1.0f;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1.0f; }
}
static uint8_t* Rel(uint8_t* call) { return call + 5 + *(int32_t*)(call + 1); }

BYTE* CamSys() {
    __try {
        BYTE* game = *(BYTE**)sym::g_Game;
        return game ? *(BYTE**)(game + 0x90) : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
int GameState() { int s = 0; SafeRead((const void*)sym::gGameState, &s, 4); return s; }
bool CinematicNow() {
    BYTE c = 0, iye = 0;
    SafeRead((const void*)sym::gCineFlag, &c, 1);
    SafeRead((const void*)sym::Cine_InYourEarFlag, &iye, 1);
    return c && !iye;   // In-Your-Ear radio dialogue sets the same flag during play
}
bool InGameplay() {
    int sim = 1; SafeRead((const void*)sym::gSimState, &sim, 4);
    return GameState() == 2 && sim == 0 && !CinematicNow();
}

// ---------------------------------------------------------------------------------------------- strafe + facing
static int s_retakes;
void SetStrafe(bool on) {
    if (!s_setStrafe) return;
    BYTE* pc = PlayerComp(PlayerRen());
    if (!pc) { s_strafeOn = false; return; }
    if (on && s_strafeOn) {
        // the game resets strafe by itself (knock-downs, lock-on, respawn code write the flag / count directly):
        // take it again when our reference is gone, never stack a second one
        __try {
            const int refs = *(int*)(pc + 0x5458);
            if (!pc[0x5454]) {
                if (refs <= 0) s_strafeOn = false;
                else if (refs == 1) { s_setStrafe(pc, false); s_strafeOn = false; }
                if (!s_strafeOn) { s_retakes++; if (s_retakes <= 20) Log("strafe was reset by the game (refs %d) - taken again", refs); }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { s_strafeOn = false; }
    }
    if (on == s_strafeOn) return;
    __try {
        if (!on && *(int*)(pc + 0x5458) <= 0) { s_strafeOn = false; return; }   // a new player ren: nothing of ours
        s_setStrafe(pc, on);
        s_strafeOn = on;
    } __except (EXCEPTION_EXECUTE_HANDLER) { s_strafeOn = false; Log("strafe call faulted"); }
}
void StrafeForget() { s_strafeOn = false; }   // the player ren changed: its refcount died with it
int StrafeRetakes() { return s_retakes; }
void StrafeRetake() {
    if (!s_setStrafe || !s_strafeOn) return;
    BYTE* pc = PlayerComp(PlayerRen());
    if (!pc) { s_strafeOn = false; return; }
    __try { s_setStrafe(pc, false); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    s_strafeOn = false;
    SetStrafe(true);
}
bool StrafeFlagSet() {
    BYTE* pc = PlayerComp(PlayerRen());
    BYTE f = 0;
    return pc && SafeRead(pc + 0x5454, &f, 1) && f;
}
const char* StrafeStatus() {
    static char b[64];
    BYTE* pc = PlayerComp(PlayerRen());
    BYTE f = 0; int refs = -1;
    if (pc) { SafeRead(pc + 0x5454, &f, 1); SafeRead(pc + 0x5458, &refs, 4); }
    sprintf_s(b, "strafe flag %d refs %d ours %d", f, refs, (int)s_strafeOn);
    return b;
}

// The player's ActionDirectLocomotion reads its facing from ActionResourceFacing2DSW (0x87F7C0: the lock-on target in
// strafe, else the body's own forward) and its strafe state snaps the body to it every frame. While armed it returns the
// flattened aim instead, so strafing / backpedalling keep the gun on the crosshair. Moving, the turn is rate-limited
// from the body's own forward (mouse flicks swing the body over ~0.2 s instead of one frame); standing, the full aim
// goes through and the game's own idle-turn animation plays for turns over 5 degrees.
// The rate limit used to start from the game's answer (1.2.0, g_faceFrom 0). After a 180 degree mouse turn while
// crouch-walking, or while reversing out of a sprint, that answer stays on the old heading, so every call started the turn
// from there again and the body kept facing the camera for seconds (user video 2026-10-27; tests\blaster_flick.py:
// 2.2 s of 2.2 s under 1.2.0, ~0.1 s now).
int g_faceFrom = 1;
static float s_face[3];
static volatile LONG s_faceOn;
static DWORD s_faceRen;
static double s_faceLast;
static float s_bodyYaw, s_gameYaw, s_outYaw;
static double s_bodyYawAt = -1.0;
static int s_lockPulls;                  // calls where the game's answer was over 30 degrees off the body (a lock target)
static int s_turnDir;                    // the way a big turn goes (+1 / -1): near 180 degrees the shortest way flips with every wobble
static volatile LONG s_faceSnap;         // the next call returns the full aim (FacingSnap: the off-aim watchdog)
static const float kFaceRate = 900.0f * 0.0174532925f;   // rad/s while moving
static float WrapPi(float d) {
    if (d > 3.14159265f) d -= 6.2831853f;
    if (d < -3.14159265f) d += 6.2831853f;
    return d;
}
static void __fastcall HkFacing2D(BYTE* self, void*, float* out) {
    s_facingOrig(self, out);
    if (!s_faceOn || !out) return;
    __try {
        const DWORD ren = *(DWORD*)(self + 8);
        if (ren != s_faceRen) return;
        const double now = RealNow();
        float dt = (float)(now - s_faceLast);
        s_faceLast = now;
        const bool fresh = dt < 0.0f || dt > 0.1f;   // the first call after a pause / a facing gap
        if (fresh) dt = 0.1f;
        const bool gameDir = out[0] != 0.0f || out[2] != 0.0f;
        const float game = atan2f(out[0], out[2]), want = atan2f(s_face[0], s_face[2]);
        // the body itself: Ren_Forward, else what we returned last (the strafe snaps the body to it)
        float body = game;
        bool bodyKnown = false;
        const float* bf = s_renFwd ? s_renFwd(ren) : nullptr;
        if (bf && (bf[0] * bf[0] + bf[2] * bf[2]) > 1e-4f) { body = atan2f(bf[0], bf[2]); bodyKnown = true; }
        else if (!fresh) { body = s_outYaw; bodyKnown = true; }
        if (gameDir && bodyKnown && fabsf(WrapPi(game - body)) > 0.5236f) s_lockPulls++;
        const float cur = g_faceFrom && bodyKnown ? body : game;
        const bool known = g_faceFrom ? (bodyKnown || gameDir) : gameDir;
        s_gameYaw = game; s_bodyYaw = cur; s_bodyYawAt = now;
        const float d = WrapPi(want - cur);
        BYTE* pc = PlayerComp(s_faceRen);
        const bool moving = pc && *(float*)(pc + 0x20) > 0.1f;   // move stick magnitude (the stick conversion's output)
        const float lim = kFaceRate * dt;
        float a = want;
        if (InterlockedExchange(&s_faceSnap, 0)) s_turnDir = 0;
        else if (moving && known && fabsf(d) > lim) {
            int dir = d > 0.0f ? 1 : -1;
            if (g_faceFrom && s_turnDir && fabsf(d) > 2.618f) dir = s_turnDir;   // over 150 degrees: keep going the same way
            s_turnDir = dir;
            a = cur + (float)dir * lim;
        } else s_turnDir = 0;
        s_outYaw = a;
        out[0] = sinf(a); out[1] = 0.0f; out[2] = cosf(a);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
void FacingSnap() { InterlockedExchange(&s_faceSnap, 1); }
const char* FacingStatus() {
    static char b[128];
    const float k = 57.29578f;
    sprintf_s(b, "facing: from %d game %.0f body %.0f out %.0f lock pulls %d ren fwd %d", g_faceFrom, s_gameYaw * k,
              s_bodyYaw * k, s_outYaw * k, s_lockPulls, s_renFwd ? 1 : 0);
    return b;
}
void SetFacing(const float* fwd, DWORD ren) {
    if (!fwd) { s_faceOn = 0; return; }
    float l = sqrtf(fwd[0] * fwd[0] + fwd[2] * fwd[2]);
    if (l < 1e-3f) return;
    s_face[0] = fwd[0] / l; s_face[2] = fwd[2] / l; s_faceRen = ren;
    s_faceOn = 1;
}
bool FacingOk() { return s_facingSlot != nullptr; }
bool RenYaw(DWORD ren, float* yaw) {
    if (!s_renFwd || !ren) return false;
    __try {
        const float* f = s_renFwd(ren);
        if (!f || f[0] * f[0] + f[2] * f[2] < 1e-4f) return false;
        *yaw = atan2f(f[0], f[2]);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool BodyYaw(float* yaw) {
    if (!s_faceOn || RealNow() - s_bodyYawAt > 0.25) return false;
    *yaw = s_bodyYaw;
    return true;
}
// snapshot of the engine state the mod changes: body-facing override on/off, the stick branch's first two bytes
// (0F 86 = vanilla jbe, E9 xx = our jmp)
const char* EngineSnap() {
    static char b[64];
    BYTE s[2] = {};
    if (s_stickJcc) SafeRead(s_stickJcc, s, 2);
    sprintf_s(b, "face=%d stick=%02X%02X", (int)s_faceOn, s[0], s[1]);
    return b;
}

// The move stick becomes a world direction in 0xC35340: left / right along the camera's right axis, forward / back
// along the eye -> player line. Over the shoulder that line is off the view by atan(shoulder / distance) (~11 degrees),
// so W walked off the crosshair. While armed the branch that swaps in the eye line is always skipped: the camera's own
// forward axis, exact.
void SetStickFix(bool on) {
    if (!s_stickJcc || on == s_stickOn) return;
    if (WriteMem(s_stickJcc, on ? s_stickJmp : s_stickOrig, 6)) s_stickOn = on;
}

// One pointer to fn in .rdata (a vtable slot); nullptr when absent or not unique.
DWORD* UniqueSlot(HMODULE exe, uint32_t fn, int* count) {
    Range rd = SectionRange(exe, ".rdata");
    DWORD* hit = nullptr; int n = 0;
    for (BYTE* p = rd.lo; p + 4 <= rd.hi; p += 4)
        if (*(DWORD*)p == fn) { if (!hit) hit = (DWORD*)p; n++; }
    if (count) *count = n;
    return n ? hit : nullptr;
}

bool CamInstall(const Range& text);
bool InputInstall(HMODULE exe, const Range& text);
bool CombatInstall(const Range& text);

bool EngineInit(HMODULE exe) {
    Range t = TextRange(exe);
    s_getPlayerRen = (GetPlayerRen_t)FindSym(t, "PlayerMgr_GetPlayerRen");
    s_getPlayerComp = (GetPlayerComp_t)FindSym(t, "PlayerMgr_Find");
    s_renGetPos = (RenGetPos_t)FindSym(t, "Ren_GetPos");
    s_renFwd = (RenFwd_t)FindSym(t, "Ren_Forward");
    if (!s_renFwd) Log("Ren_Forward not found - body turns start from the last facing returned");
    if (!s_getPlayerRen || !s_getPlayerComp || !s_renGetPos) {
        Log("core engine symbols missing (GetPlayerRen %p, GetPlayerComp %p, Ren_GetPos %p) - mod off",
            s_getPlayerRen, s_getPlayerComp, s_renGetPos);
        return false;
    }
    if (uint8_t* g = FindSym(t, "Blaster_GetHealthLua")) {
        if (g[0xD] == 0x8B && g[0xE] == 0x0D && g[0x17] == 0x83 && g[0x18] == 0xC1 && g[0x1A] == 0xE8 && g[0x23] == 0x8D && g[0x29] == 0xE8) {
            s_healthMgr = *(void***)(g + 0xF); s_healthAdd = (int8_t)g[0x19]; s_healthComp = (GetComp_t)Rel(g + 0x1A);
            s_healthOff = *(DWORD*)(g + 0x25); s_healthGet = (HealthGet_t)Rel(g + 0x29);
        }
    }
    if (!s_healthGet) Log("health read not resolved - kill markers off");
    // game.GetMaxHealth: the same layout as GetHealth (manager, component lookup, field) with the max getter
    // (fld [ecx+imm8]; ret); anything else leaves heavy-enemy damage off
    if (uint8_t* g = s_healthGet ? FindSym(t, "Blaster_GetMaxHealthLua") : nullptr) {
        if (g[0xD] == 0x8B && g[0xE] == 0x0D && *(void***)(g + 0xF) == s_healthMgr && g[0x1A] == 0xE8 &&
            Rel(g + 0x1A) == (uint8_t*)s_healthComp && g[0x23] == 0x8D && *(DWORD*)(g + 0x25) == s_healthOff && g[0x29] == 0xE8) {
            uint8_t* f = Rel(g + 0x29);
            if (f[0] == 0xD9 && f[1] == 0x41 && f[3] == 0xC3 && f != (uint8_t*)s_healthGet) s_healthGetMax = (HealthGet_t)f;
        }
    }
    if (!s_healthGetMax) Log("max health read not resolved - heavy enemies take plain damage");
    s_setStrafe = (SetStrafe_t)FindSym(t, "Blaster_SetStrafe");
    if (!s_setStrafe) Log("strafe call not found - the body will not turn with the camera");
    if (uint8_t* f = FindSym(t, "Blaster_Facing2DSW")) {
        int n = 0;
        DWORD* slot = UniqueSlot(exe, (uint32_t)f, &n);
        if (slot && n == 1) {
            s_facingOrig = (Facing2D_t)f;
            void* hk = (void*)&HkFacing2D;
            if (WriteMem(slot, &hk, 4)) s_facingSlot = slot;
        } else Log("player Facing2D vtable slot not unique (%d) - body facing off", n);
    } else Log("player Facing2D not found - body facing off");
    if (uint8_t* j = FindSym(t, "Blaster_StickEyeSub")) {
        if (j[0] == 0x0F && j[1] == 0x86) {        // jbe rel32 -> jmp rel32 to the same target + nop
            const int32_t rel = *(int32_t*)(j + 2);
            const int32_t rel5 = rel + 1;          // target = j + 6 + rel = j + 5 + (rel + 1)
            memcpy(s_stickOrig, j, 6);
            s_stickJmp[0] = 0xE9; memcpy(s_stickJmp + 1, &rel5, 4); s_stickJmp[5] = 0x90;
            s_stickJcc = j;
        }
    }
    if (!s_stickJcc) Log("stick conversion branch not found - forward / back may drift a few degrees off the crosshair");
    bool cam = CamInstall(t);
    bool in = InputInstall(exe, t);
    bool cb = CombatInstall(t);
    SoundsInstall(t);
    AbilityInstall(t);
    OutfitInstall(t);
    HoldInstall(exe, t);
    const bool cr = CrouchInstall(t);
    AimInstall(t);
    Log("engine: crouch %s", cr ? "ok" : "off");
    Log("engine: strafe %s, facing %s, stick fix %s, camera %s, input %s, damage hook %s", s_setStrafe ? "ok" : "off",
        s_facingSlot ? "ok" : "off", s_stickJcc ? "ok" : "off", cam ? "ok" : "off", in ? "ok" : "off", cb ? "ok" : "off");
    return true;
}

void EngineShutdown() {
    SetStrafe(false);
    SetStickFix(false);
    s_faceOn = 0;
}

}  // namespace dkb
