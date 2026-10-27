// DK_Blaster: the aimed gun pose. While the player aims down the sights or fires from the hip, his upper body plays
// the stormtrooper's own aim grid (aim_o RU / RD / FU / FD / LU / LD): the gun at the shoulder, pointed at the
// crosshair, held the way the trooper holds it; the legs keep walking underneath. It needs no new chore data: the
// player's LightningAtk / LightningC_Hold block already is a looping upper-body overlay with a 2D Aim grid, and its mask
// is bone for bone the trooper's. While the blaster is drawn (and only while that block is not playing) its six clips
// are re-pointed to the trooper's, its grid values set to the trooper's, and its aim setter told to aim at a stored
// point (StoredAimPos, the crosshair's world point, written every frame) with the trooper's aim geometry. Holstering
// puts every byte back. Force Lightning is muted while the blaster is out, so the block is ours meanwhile.
// Research: research\agent_blaster\aim_overlay\REPORT.md.
#include "dkb.h"
#include <stdio.h>
#include <math.h>

using namespace dk;

namespace dkb {

// dkb_hold.cpp helpers (asset keys, locks, chore sets)
bool HoldReady();
DWORD HoldCrc(const char* s);
DWORD HoldAnimKey(const char* anim);
void* HoldLockAnim(DWORD key);
void HoldUnlockAnim(DWORD key);
DWORD HoldSetDefOf(DWORD groupCrc);
int HoldBlockPlaying(void* ac, DWORD chore, DWORD block);

typedef bool(__fastcall* StopFade_t)(void* cm, void* edx, DWORD* id, float fade);   // PlaybackMgr_Stop, __thiscall ret 8
typedef bool(__fastcall* HandlePlaying_t)(void* cm, void* edx, DWORD id);            // ChoreMgr_IsHandlePlaying, ret 4
static StopFade_t s_stop;
static HandlePlaying_t s_handlePlaying;
static bool s_ok;

static const char* kFrom[6] = { "player_ma_com_force_lightning_charge_loop_RU", "player_ma_com_force_lightning_charge_loop_RD",
                                "player_ma_com_force_lightning_charge_loop_FU", "player_ma_com_force_lightning_charge_loop_FD",
                                "player_ma_com_force_lightning_charge_loop_LU", "player_ma_com_force_lightning_charge_loop_LD" };
static const char* kTo[6] = { "stormtrooper_ma_com_blaster_aim_o_RU", "stormtrooper_ma_com_blaster_aim_o_RD",
                              "stormtrooper_ma_com_blaster_aim_o_FU", "stormtrooper_ma_com_blaster_aim_o_FD",
                              "stormtrooper_ma_com_blaster_aim_o_LU", "stormtrooper_ma_com_blaster_aim_o_LD" };
// the trooper's grid (yaw, pitch per clip; + yaw = left, - pitch = up) and the lightning grid it replaces
static const float kGrid[12] = { -0.785398f, -0.610865f, -0.785398f, 0.610865f, 0.0f, -0.610865f,
                                 0.0f, 0.610865f, 0.785398f, -0.610865f, 0.785398f, 0.610865f };
static const float kLightning[12] = { -1.5708f, -1.309f, -1.5708f, 1.309f, 0.0f, -1.309f, 0.0f, 1.309f, 1.5708f, -1.309f, 1.5708f, 1.309f };
static const DWORD kTargetStored = 6;       // AimScumPlayer TargetType StoredAimPos (PlayerComp+0x5520 + the player)
static const DWORD kTargetRanged = 1;

static struct Plan {
    DWORD groupCrc, csDef, chore, block;
    BYTE* B; DWORD wrap; DWORD* en; float* grid; BYTE* S;
    DWORD fromKey[6], toKey[6];
    DWORD oldEn[6][3]; bool locked[6];
    float oldGrid[12]; float oldS10[4], oldS40[4]; DWORD oldS70, oldS74, oldS90;
    bool geometry;        // the setter's bone fields hold stricrc32 names: the trooper's aim root / joint go in
    bool valid, patched;
} s_p;
static char s_group[160];
static bool s_want;                 // the pose is wanted now (aim / fire)
static bool s_armed;                // the blaster is drawn (the block stays patched meanwhile)
static DWORD s_handle;              // our play of the block (0: none)
static double s_retry, s_lastStart;
static int s_starts, s_stops, s_lost, s_patches, s_unpatches, s_refused;
static const char* s_why = "-";
static bool s_stored;               // we set the stored-aim flag (cleared once when the pose ends; vanilla Force moves use it too)

static void Forget() { memset(&s_p, 0, sizeof s_p); s_handle = 0; }

static bool Resolved() { return s_p.wrap && *(BYTE*)(s_p.wrap + 0x18) != 0; }   // the block's clips are loaded

// finds LightningAtk / LightningC_Hold in the player's chore set and checks every field we will touch
static bool SafePlan(DWORD groupCrc) {
    Forget();
    s_p.groupCrc = groupCrc;
    __try {
        const DWORD csDef = HoldSetDefOf(groupCrc);
        if (!csDef) { s_why = "chore set not registered"; return false; }
        s_p.csDef = csDef;
        s_p.chore = HoldCrc("LightningAtk"); s_p.block = HoldCrc("LightningC_Hold");
        const DWORD nc = *(DWORD*)(csDef + 0x0C), ca = *(DWORD*)(csDef + 0x10);
        for (DWORD i = 0; i < nc && i < 4096 && !s_p.B; i++) {
            const DWORD ch = *(DWORD*)(ca + i * 8 + 4);
            if (!ch || *(DWORD*)(ch + 0x0C) != s_p.chore) continue;
            const DWORD nb = *(DWORD*)(ch + 0x10), ba = *(DWORD*)(ch + 0x14);
            for (DWORD j = 0; j < nb && j < 512; j++) {
                const DWORD bd = *(DWORD*)(ba + j * 8 + 4);
                if (bd && *(DWORD*)(bd + 0x0C) == s_p.block) { s_p.B = (BYTE*)bd; break; }
            }
        }
        BYTE* B = s_p.B;
        if (!B) { s_why = "no LightningC_Hold block in this chore set"; return false; }
        if (*(DWORD*)(B + 0x10) != 6 || !*(DWORD*)(B + 0x18) || *(int*)(B + 0x30) != -1 || *(DWORD*)(B + 0x60) < 1 || *(DWORD*)(B + 0x68) < 1) {
            s_why = "LightningC_Hold layout unexpected"; return false;
        }
        s_p.wrap = *(DWORD*)B;
        s_p.en = *(DWORD**)(B + 0x14);
        for (int k = 0; k < 6; k++) {
            s_p.fromKey[k] = HoldAnimKey(kFrom[k]); s_p.toKey[k] = HoldAnimKey(kTo[k]);
            if (s_p.en[k * 4] != s_p.fromKey[k]) { s_why = "LightningC_Hold clips unexpected"; return false; }
        }
        BYTE* C = *(BYTE**)(*(BYTE**)(B + 0x6C) + 4);
        s_p.S = *(BYTE**)(*(BYTE**)(B + 0x64) + 4);
        if (!C || !s_p.S || *(DWORD*)(C + 4) != 2) { s_why = "aim grid not found"; return false; }
        s_p.grid = *(float**)(C + 0x14);
        for (int k = 0; k < 12; k++) if (fabsf(s_p.grid[k] - kLightning[k]) > 0.002f) { s_why = "aim grid values unexpected"; return false; }
        if (*(DWORD*)(s_p.S + 0x90) != kTargetRanged) { s_why = "aim setter unexpected"; return false; }
        s_p.geometry = *(DWORD*)(s_p.S + 0x70) == HoldCrc("spine[1]") && *(DWORD*)(s_p.S + 0x74) == HoldCrc("lHand[0]");
        if (!s_p.geometry) Log("aim pose: the aim setter's bones are not stricrc32 names (%08X %08X) - the lightning aim geometry stays",
                               *(DWORD*)(s_p.S + 0x70), *(DWORD*)(s_p.S + 0x74));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { s_why = "exception while planning"; Forget(); return false; }
    s_p.valid = true;
    s_why = "ready";
    return true;
}

// the block over to the trooper (on) or back; it must not be playing
static bool SafePatch(bool on) {
    if (!s_p.valid || s_p.patched == on) return true;
    __try {
        DWORD* en = s_p.en;
        if (on) {
            const bool res = Resolved();
            int k = 0;
            for (; k < 6; k++) {
                DWORD* e = en + k * 4;
                s_p.oldEn[k][0] = e[0]; s_p.oldEn[k][1] = e[1]; s_p.oldEn[k][2] = e[2];
                s_p.locked[k] = false;
                if (res) {   // loaded: ours is locked and put in (the game unlocks what the set holds when it unloads)
                    void* data = HoldLockAnim(s_p.toKey[k]);
                    if (!data) break;
                    e[0] = s_p.toKey[k]; e[1] = (DWORD)data;
                    const float len = -1.0f; memcpy(&e[2], &len, 4);
                    s_p.locked[k] = true;
                } else e[0] = s_p.toKey[k];   // not loaded yet: the game locks our key itself when the block loads
            }
            if (k < 6) {   // a clip is missing: everything back, the pose stays off for this set
                for (int j = 0; j < k; j++) {
                    DWORD* e = en + j * 4;
                    if (s_p.locked[j]) HoldUnlockAnim(s_p.toKey[j]);
                    e[0] = s_p.oldEn[j][0]; e[1] = s_p.oldEn[j][1]; e[2] = s_p.oldEn[j][2];
                }
                Log("aim pose: trooper aim clip %s not loaded - off", kTo[k]);
                s_p.valid = false; s_why = "aim clips missing";
                return false;
            }
            memcpy(s_p.oldGrid, s_p.grid, sizeof s_p.oldGrid);
            memcpy(s_p.grid, kGrid, sizeof kGrid);
            BYTE* S = s_p.S;
            memcpy(s_p.oldS10, S + 0x10, 16); memcpy(s_p.oldS40, S + 0x40, 16);
            s_p.oldS70 = *(DWORD*)(S + 0x70); s_p.oldS74 = *(DWORD*)(S + 0x74); s_p.oldS90 = *(DWORD*)(S + 0x90);
            *(DWORD*)(S + 0x90) = kTargetStored;
            if (s_p.geometry) {   // the trooper's aim geometry: from the hips (outside the mask), the gun hand as the joint
                *(DWORD*)(S + 0x70) = HoldCrc("hips[0]"); *(DWORD*)(S + 0x74) = HoldCrc("rWeapon[0]");
                float* c = (float*)(S + 0x10); c[0] = 0.0f; c[1] = 0.374f; c[2] = 0.0f;
                float* r = (float*)(S + 0x40); r[0] = -0.158f; r[1] = 0.0f; r[2] = 0.0f;
            }
            s_patches++;
        } else {
            for (int k = 0; k < 6; k++) {
                DWORD* e = en + k * 4;
                if (e[0] != s_p.toKey[k]) continue;   // the set reloaded the entry itself
                if (s_p.locked[k]) {
                    e[0] = s_p.oldEn[k][0]; e[1] = s_p.oldEn[k][1]; e[2] = s_p.oldEn[k][2];
                    HoldUnlockAnim(s_p.toKey[k]);
                } else if (Resolved()) {   // loaded by the game since, with our key locked: its own clip goes back, locked the same way
                    void* data = HoldLockAnim(s_p.oldEn[k][0]);
                    if (!data) continue;
                    e[0] = s_p.oldEn[k][0]; e[1] = (DWORD)data;
                    const float len = -1.0f; memcpy(&e[2], &len, 4);
                    HoldUnlockAnim(s_p.toKey[k]);
                } else e[0] = s_p.oldEn[k][0];
                s_p.locked[k] = false;
            }
            memcpy(s_p.grid, s_p.oldGrid, sizeof s_p.oldGrid);
            BYTE* S = s_p.S;
            memcpy(S + 0x10, s_p.oldS10, 16); memcpy(S + 0x40, s_p.oldS40, 16);
            *(DWORD*)(S + 0x70) = s_p.oldS70; *(DWORD*)(S + 0x74) = s_p.oldS74; *(DWORD*)(S + 0x90) = s_p.oldS90;
            s_unpatches++;
        }
        s_p.patched = on;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("aim pose: exception 0x%08X while switching the block - off", GetExceptionCode()); s_p.valid = false; s_why = "exception"; return false; }
    return true;
}

static bool SafeHandlePlaying(void* cm, DWORD h) {
    __try { return h && s_handlePlaying(cm, nullptr, h); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void Stop(void* cm, float fade) {
    if (!s_handle) return;
    DWORD h = s_handle;
    s_handle = 0;
    __try { if (cm) s_stop(cm, nullptr, &h, fade); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    s_stops++;
}

// the crosshair's world point for the aim setter (StoredAimPos = PlayerComp+0x5520 + the player's position)
static bool StoreAim(DWORD p, bool on, float* rel = nullptr) {   // rel: the stored aim point - player position
    BYTE* pc = PlayerComp(p);
    if (!pc) return false;
    __try {
        if (!on) { if (s_stored) *(BYTE*)(pc + 0x5530) = 0; s_stored = false; return false; }
        float pos[3];
        if (!g_cam.valid || !RenPos(p, pos)) return false;
        static float s_dist = 40.0f;   // the last ray hit's distance (rays run 15 times a second; the view every frame)
        if (g_aimHitOk) {
            const float d[3] = { g_aimHit[0] - g_cam.eye[0], g_aimHit[1] - g_cam.eye[1], g_aimHit[2] - g_cam.eye[2] };
            const float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            s_dist = l < 4.0f ? 4.0f : (l > 200.0f ? 200.0f : l);
        }
        float* v = (float*)(pc + 0x5520);
        for (int i = 0; i < 3; i++) v[i] = g_cam.eye[i] + g_cam.fwd[i] * s_dist - pos[i];
        *(BYTE*)(pc + 0x5530) = 1;
        s_stored = true;
        if (rel) memcpy(rel, v, 3 * sizeof(float));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

bool AimInstall(const Range& t) {
    s_stop = (StopFade_t)FindSym(t, "PlaybackMgr_Stop");
    s_handlePlaying = (HandlePlaying_t)FindSym(t, "ChoreMgr_IsHandlePlaying");
    s_ok = s_stop && s_handlePlaying;
    if (!s_ok) Log("aim pose: stop %p / handle check %p not found - the gun keeps the carry pose", s_stop, s_handlePlaying);
    return s_ok;
}

// every frame. armed: the blaster is drawn; want: aiming / firing (the gun up)
void AimTick(bool armed, bool want) {
    const DWORD p = PlayerRen();
    void* cm = p ? AnimCompOfRen(p) : nullptr;
    const bool usable = s_ok && HoldReady() && cfg.aimPose && PackAimOk();
    s_armed = armed && usable;
    s_want = s_armed && want && cm;
    // the chore set: planned once per body; a rebuilt / unloaded set takes its plan (and our bytes) with it
    if (s_armed && cm && RealNow() >= s_retry) {
        if (!s_group[0] || !s_p.groupCrc || HoldSetDefOf(s_p.groupCrc) != s_p.csDef) {
            const char* g = LuaCall("local ok, s = pcall(animation.GetChoreSet, ren.GetPlayer(0)) return ok and s or ''");
            if (g[0]) {
                if (s_p.patched && HoldSetDefOf(s_p.groupCrc) == s_p.csDef && _stricmp(g, s_group)) SafePatch(false);   // another body
                strncpy_s(s_group, g, _TRUNCATE);
                if (!SafePlan(HoldCrc(s_group))) { s_retry = RealNow() + 2.0; if (s_refused++ < 3) Log("aim pose: off for %s (%s)", s_group, s_why); }
                else Log("aim pose: LightningC_Hold of %s planned (geometry %s)", s_group, s_p.geometry ? "trooper" : "kept");
            } else s_retry = RealNow() + 1.0;
        }
    }
    if (!s_p.valid) { s_handle = 0; CrouchAim(false, nullptr, 0.0f); return; }
    const int playing = cm ? HoldBlockPlaying(cm, s_p.chore, s_p.block) : 0;
    if (s_handle && cm && !SafeHandlePlaying(cm, s_handle)) { s_handle = 0; if (s_want) s_lost++; }   // ended by the game
    if (s_want) {
        if (!s_p.patched && playing == 0) SafePatch(true);
        float rel[3], yaw = 0.0f;
        bool fix = false;
        if (s_p.patched) {
            fix = StoreAim(p, true, rel);
            if (!s_handle && playing == 0 && RealNow() - s_lastStart > 0.25) {
                s_lastStart = RealNow();
                float n[2] = {};
                if (LuaNums(n, 2, "local ok, a, h = pcall(animation.StartBlock, ren.GetPlayer(0), 'LightningAtk', 'LightningC_Hold') "
                                  "return (ok and a) and 1 or 0, (ok and h) or 0") == 2 && n[0] == 1.0f && n[1] > 0.0f) {
                    s_handle = (DWORD)n[1];
                    s_starts++;
                }
            }
        }
        CrouchAim(fix && s_handle && BodyYaw(&yaw), rel, yaw);   // the chest finishes the aim (dkb_crouch.cpp)
    } else {
        CrouchAim(false, nullptr, 0.0f);
        if (s_handle) Stop(cm, 0.22f);
        if (p && s_stored) StoreAim(p, false);
        if (!s_armed && s_p.patched && playing == 0) SafePatch(false);   // holstered and out of the block: every byte back
    }
}

void AimShutdown() {   // plugin unload: our play stopped, the block restored (the anim worker never sees a half state)
    const DWORD p = PlayerRen();
    void* cm = p ? AnimCompOfRen(p) : nullptr;
    CrouchAim(false, nullptr, 0.0f);
    if (s_handle) Stop(cm, 0.0f);
    if (p) StoreAim(p, false);
    if (s_p.patched && s_p.valid && HoldSetDefOf(s_p.groupCrc) == s_p.csDef) {
        if (!cm || HoldBlockPlaying(cm, s_p.chore, s_p.block) == 0) SafePatch(false);
        else Log("aim pose: the block still plays at unload - its bytes stay (the chore set's next load restores them)");
    }
}

bool AimUp() { return s_handle != 0; }

const char* AimStatus() {
    static char b[256];
    _snprintf_s(b, sizeof b, _TRUNCATE, "pose %s (%s) want %d armed %d patched %d handle %lu starts %d stops %d lost %d patches %d/%d set %s",
                !s_ok ? "unavailable" : !cfg.aimPose ? "off (AimPose=0)" : !PackAimOk() ? "off (clips not in the pack)" : s_p.valid ? "ready" : "not planned",
                s_why, s_want, s_armed, s_p.patched, s_handle, s_starts, s_stops, s_lost, s_patches, s_unpatches, s_group[0] ? s_group : "-");
    return b;
}

}  // namespace dkb
