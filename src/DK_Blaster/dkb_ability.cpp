// DK_Blaster: abilities. Thermal detonator (G / pad RB): hold to cook - a live throw arc with the landing point and the
// blast radius on the ground - release to throw. Cooking shortens the fuse; the detonator leaves the hand by itself
// just before it would go off. Class special (F / pad LB), one per class: Rapid Fire, Scan Pulse, Bacta Stim, Jump
// Pack, Overcharge. Recharges and effects run on the game clock (a pause stops them; the same at every frame rate).
// The HUD part (arc, ring, pips, scan marks) is drawn by AbilityDraw from the HUD layer.
#include "dkb.h"
#include "dkb_render.h"
#include "dk_pack.h"
#include <stdio.h>

using namespace dk;

namespace dkb {

Boost g_boost;

// The engine launches a grenade only if its prop actor has pool instances: at level load it builds 20 per grenade the
// level's own abilities use (ProjectileMgr BuildPool), and Launch returns nil for any other. On blaster draw we build
// the thermal detonator's pool the same way; the engine's level unload destroys it with the others.
typedef void(__thiscall* PoolBuild_t)(void* mgr, uint32_t assetKey, char grenade);
typedef DWORD(__thiscall* PoolLookup_t)(void* mgr, uint32_t assetKey, int slot);
static PoolBuild_t s_poolBuild;
static PoolLookup_t s_poolLookup;
static BYTE** s_projMgr;
static const char* kGrenadeActorPath = "Scum/handheld/actor/oneHandSphere/thermalDetonator.actor.xml";
static int s_poolBuilds;
static bool s_poolOk;

// The grenade launch registers the grenade with the thrower's AI component (AIComp::AddOwnGrenade, a 10-slot list the
// AI uses to stay clear of its own throws) without a null check: the player has no AI component, so a player-thrown
// grenade read address 0xC. The call goes through this stub: AI throwers are unchanged, a null component is skipped.
static void* s_aiNadeAdd;
static __declspec(naked) void HkAiNadeAdd() {
    __asm {
        test ecx, ecx
        jz none
        jmp dword ptr [s_aiNadeAdd]
    none:
        ret 4
    }
}

void AbilityInstall(const Range& text) {
    s_poolBuild = (PoolBuild_t)FindSym(text, "Blaster_GrenadePoolBuild");
    s_poolLookup = (PoolLookup_t)FindSym(text, "Blaster_ProjActorLookup");
    s_projMgr = (BYTE**)FindSym(text, "gProjectileMgr");
    uint8_t* site = FindSym(text, "Blaster_GrenadeAiAddCall");
    uint8_t* add = FindSym(text, "Blaster_AiAddOwnGrenade");
    if (site && add && site[0] == 0xE8 && site + 5 + *(int32_t*)(site + 1) == add) s_aiNadeAdd = RedirectCall(site, (void*)&HkAiNadeAdd);
    if (!s_aiNadeAdd) { Log("grenade launch AI call not found - no thermal detonators (a player throw would fault)"); s_poolBuild = nullptr; }
    else if (!s_poolBuild || !s_poolLookup || !s_projMgr) Log("grenade pool calls not found - thermal detonators only where the level has them");
}

static bool PoolLookup(BYTE* mgr, uint32_t key) {
    __try { return s_poolLookup(mgr, key, 0) != 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// game thread, in a level, our pack chained: the detonator's pool exists (built once per level)
bool AbilityEnsurePool() {
    s_poolOk = false;
    if (!s_aiNadeAdd || !s_poolBuild || !s_poolLookup || !s_projMgr || !PackGrenadeOk() || !PackChained()) return false;
    BYTE* mgr = *s_projMgr;
    if (!mgr) return false;
    const uint32_t key = pack::StriCrc32(kGrenadeActorPath);
    if (PoolLookup(mgr, key)) { s_poolOk = true; return true; }
    const uint64_t t0 = GetTickCount64();
    __try { s_poolBuild(mgr, key, 1); }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("grenade pool build failed (exception 0x%08X)", GetExceptionCode()); return false; }
    s_poolBuilds++;
    s_poolOk = PoolLookup(mgr, key);
    Log("thermal detonator pool %s (%llu ms)", s_poolOk ? "built" : "NOT built", GetTickCount64() - t0);
    return s_poolOk;
}

static const uint32_t kWhite = 0xFFFFFF, kIce = 0x7CE0FF, kAmber = 0xFF9A3C, kRed = 0xFF3B30, kGold = 0xFFC83C,
                      kDark = 0x0A0F14;

static float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// ---------------------------------------------------------------------------------------------- thermal detonator
static const float kFuse = 3.0f;            // seconds from the arming click
static const float kNadeCooldown = 8.0f;    // recharge after a throw
static const float kNadeRange = 22.0f;      // farthest throw (m from the hand)
static int s_speedIdx;                      // the ability entry (throw speed) of the last solved throw
static const float kNadeRadius = 4.5f;      // blast radius (the ability's effect range)
static const float kAutoThrow = 0.35f;      // fuse left when an over-cooked detonator leaves the hand by itself
float g_nadeGravity = 9.81f;                // the engine grenade's fall (measured 9.8 m/s2 in flight, blaster_abilities.py)

static bool s_cooking;
static float s_cookT0;                      // game time of the arming click
static bool s_cookFast;                     // the fast beep (< 1 s left) played
static float s_nadeReady;                   // game time the next detonator is ready
static float s_nadeReadyFlash;              // game time it became ready (HUD pulse)
static int s_throws, s_throwFails, s_autoThrows;
static float s_lastFuse;
// throw if released now: origin, velocity, where it lands (15 checks per second; the drawn arc is exact every frame)
static float s_o[3], s_v[3];
static float s_landT = -1.0f, s_land[3];
static bool s_landHit;
static float s_nextLand;

// The throw lands on the crosshair: the point the camera ray hits, or (sky, or past the throw range) the point at the
// throw range along it. The flight time grows with the distance (a gentle lob close by, a flatter fast throw far away)
// and the launch velocity is the one that reaches that point under gravity in that time - the arc preview, the landing
// marker and the real grenade all follow it.
static bool ThrowFrame(float o[3], float v[3]) {
    float pp[3];
    if (!g_cam.valid || !RenPos(PlayerRen(), pp)) return false;
    const float* f = g_cam.fwd; const float* r = g_cam.right;
    float fl[3] = { f[0], 0.0f, f[2] };
    const float fn = sqrtf(fl[0] * fl[0] + fl[2] * fl[2]);
    if (fn > 0.01f) { fl[0] /= fn; fl[2] /= fn; }
    // the right hand, a little ahead of the chest
    o[0] = pp[0] + r[0] * 0.30f + fl[0] * 0.45f;
    o[1] = pp[1] + 1.45f;
    o[2] = pp[2] + r[2] * 0.30f + fl[2] * 0.45f;
    float t[3];
    float d2 = -1.0f;
    if (g_aimHitOk) {
        const float dx = g_aimHit[0] - o[0], dy = g_aimHit[1] - o[1], dz = g_aimHit[2] - o[2];
        d2 = dx * dx + dy * dy + dz * dz;
    }
    if (d2 >= 0.0f && d2 <= kNadeRange * kNadeRange) memcpy(t, g_aimHit, sizeof t);
    else for (int i = 0; i < 3; i++) t[i] = o[i] + f[i] * kNadeRange;
    const float dx = t[0] - o[0], dy = t[1] - o[1], dz = t[2] - o[2];
    const float h = sqrtf(dx * dx + dz * dz), g = g_nadeGravity;
    // the ideal lob: its flight time grows with the distance; the speed it needs picks the ability entry
    float T = 0.45f + h * 0.04f;                                   // 6 m: 0.69 s, 22 m: 1.33 s
    if (dy > 0.0f) T += dy * 0.05f;                                 // up a ledge: a higher lob
    const float ix = dx / T, iz = dz / T, iy = (dy + 0.5f * g * T * T) / T;
    const float want = sqrtf(ix * ix + iy * iy + iz * iz);
    // the slowest entry that reaches the point at all (45 degrees), then the one nearest the ideal speed
    const float reach2 = g * (dy + sqrtf(dy * dy + h * h));        // speed^2 needed at the best angle
    int best = kDkGrenadeSpeedCount - 1;
    float bestErr = 1e9f;
    for (int i = 0; i < kDkGrenadeSpeedCount; i++) {
        const float s = (float)kDkGrenadeSpeeds[i];
        if (s * s < reach2 * 1.02f) continue;
        const float e = fabsf(s - want);
        if (e < bestErr) { bestErr = e; best = i; }
    }
    s_speedIdx = best;
    const float s = (float)kDkGrenadeSpeeds[best], s2 = s * s;
    // launch angle for that speed: the lower of the two arcs; out of reach (sky, too far): 45 degrees
    float ang = 0.785398f;
    if (h > 0.01f) {
        const float disc = s2 * s2 - g * (g * h * h + 2.0f * dy * s2);
        if (disc >= 0.0f) ang = atanf((s2 - sqrtf(disc)) / (g * h));
    } else ang = dy >= 0.0f ? 1.5f : -1.5f;
    const float c = cosf(ang), hs = h > 0.01f ? 1.0f / h : 0.0f;
    v[0] = dx * hs * s * c; v[2] = dz * hs * s * c; v[1] = s * sinf(ang);
    return true;
}

static float FuseLeft(float now) { return kFuse - (now - s_cookT0); }
static const char* NadeEntry() {
    static char e[48];
    sprintf_s(e, "%s %d", kDkGrenadeEntry, kDkGrenadeSpeeds[s_speedIdx]);
    return e;
}

static void Throw(float now, bool automatic) {
    s_cooking = false;
    float o[3], v[3];
    if (!ThrowFrame(o, v)) { s_throwFails++; return; }
    float fuse = FuseLeft(now);
    if (fuse < 0.15f) fuse = 0.15f;
    const float sp = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    const char* id = LuaCall("return DKB.throw(%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.2f,%.2f,'%s','%s')", o[0], o[1], o[2],
                             v[0] / sp, v[1] / sp, v[2] / sp, sp, fuse, kDkAbilityGroup, NadeEntry());
    if (!strcmp(id, "0") || !strncmp(id, "error", 5)) {
        s_throwFails++;
        Log("thermal detonator: launch failed (%s)", id);
        Refuse("The detonator jammed");
        s_nadeReady = now + 1.0f;
        return;
    }
    s_throws++;
    if (automatic) s_autoThrows++;
    s_lastFuse = fuse;
    s_nadeReady = now + kNadeCooldown;
    Snd("nade_throw");
    CamKick(-0.6f, 0.0f, 0.0f, 0.0f);
    Log("thermal detonator %s: fuse %.2f s, %.1f m/s (%s)", id, fuse, sp, automatic ? "over-cooked" : "thrown");
}

static void CancelCook(const char* why) {
    if (!s_cooking) return;
    s_cooking = false;
    Log("thermal detonator: cook cancelled (%s)", why);
}

static void NadeTick(float now) {
    const bool ok = PackGrenadeOk() && PackChained() && s_poolOk;
    if (g_in.pressed[A_GRENADE] && !s_cooking) {
        if (!ok) Refuse("No thermal detonators here");
        else if (now < s_nadeReady) { HudToast("Detonator recharging", 0xFF9A3C, 0.8f); Snd("deny"); }
        else {
            s_cooking = true; s_cookT0 = now; s_cookFast = false; s_nextLand = 0.0f; s_landT = -1.0f;
            Snd("nade_beep");
        }
    }
    if (!s_cooking) return;
    const float left = FuseLeft(now);
    if (left < 1.0f && !s_cookFast) { s_cookFast = true; Snd("nade_cook"); }
    if (left <= kAutoThrow) { Throw(now, true); return; }
    if (!g_in.held[A_GRENADE]) { Throw(now, false); return; }
    // preview: where it lands (the drawn arc itself is computed every frame from s_o / s_v)
    if (ThrowFrame(s_o, s_v) && now >= s_nextLand) {
        s_nextLand = now + 1.0f / 15.0f;
        float n[5];
        const int got = LuaNums(n, 5, "return DKB.land(%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,0.05,%.2f)", s_o[0], s_o[1], s_o[2],
                                s_v[0], s_v[1], s_v[2], g_nadeGravity, kFuse + 0.5f);
        if (got >= 5) { s_landHit = n[0] == 1.0f; s_landT = n[1]; s_land[0] = n[2]; s_land[1] = n[3]; s_land[2] = n[4]; }
    }
}

// ---------------------------------------------------------------------------------------------- class specials
struct SpecialDef { const char* id; const char* name; float cooldown, duration; const char* cue; };
static const SpecialDef kSpecials[] = {
    { "rapid", "RAPID FIRE", 24.0f, 6.0f, "ab_rapid" },             // heat x0.3, rate x1.3
    { "scan", "SCAN PULSE", 18.0f, 8.0f, "ab_scan" },               // enemies within 45 m marked through walls
    { "stim", "BACTA STIM", 25.0f, 1.5f, "ab_stim" },               // +40 % health over 1.5 s
    { "jump", "JUMP PACK", 9.0f, 0.45f, "ab_jump" },                // thrust up and along the stick; held: hover
    { "overcharge", "OVERCHARGE", 20.0f, 12.0f, "ab_overcharge" },  // the next 3 shots: double damage, no heat
};
static const int kSpecialCount = sizeof kSpecials / sizeof kSpecials[0];
static const SpecialDef* SpecialById(const char* id) {
    for (auto& s : kSpecials) if (id && !_stricmp(s.id, id)) return &s;
    return nullptr;
}
// the class's special: set by the outfit / class code; without one, the gun's own
static const SpecialDef* s_classSpecial;
static const SpecialDef* WeaponSpecial(const WeaponDef* w) {
    static const struct { const char* w; const char* s; } k[] = {
        { "e11", "rapid" }, { "e11s", "rapid" }, { "dlt20a", "scan" }, { "dh17", "stim" }, { "z6", "jump" },
        { "fc11", "stim" }, { "bowcaster", "overcharge" }, { "dl44", "overcharge" }, { "a295", "rapid" }, { "dl18", "stim" },
        { "cycler", "scan" },
    };
    for (auto& e : k) if (w && !strcmp(e.w, w->id)) return SpecialById(e.s);
    return &kSpecials[0];
}
static const SpecialDef* Current() { return s_classSpecial ? s_classSpecial : WeaponSpecial(g_cb.w); }
void AbilitySetSpecial(const char* id) { s_classSpecial = SpecialById(id); }
const char* AbilitySpecialName() { return Current()->name; }
int AbilitySpecialCount() { return kSpecialCount; }
const char* AbilitySpecialId(int i) { return i >= 0 && i < kSpecialCount ? kSpecials[i].id : nullptr; }
const char* AbilitySpecialTitle(const char* id) { const SpecialDef* s = SpecialById(id); return s ? s->name : "-"; }
float AbilitySpecialCooldown(const char* id) { const SpecialDef* s = SpecialById(id); return s ? s->cooldown : 0.0f; }

static const SpecialDef* s_active;          // running special (duration > 0)
static float s_spT0, s_spUntil;             // game time
static float s_spReady;                     // game time the next special is ready
static float s_spReadyFlash;
static int s_uses[8];
static float s_nextStep;                    // stim heal steps / jump thrust steps / scan refreshes
static float s_healed;                      // fraction healed by this stim
static float s_jumpDir[2];

// jump pack: 0.45 s of thrust on top of the game's jump, then, while the key is held, a hover that holds the fall to
// kJetSink m/s for up to kJetHoverMax s of fuel. The thrusters (dkb_pack.cpp: the jump trooper's flame + smoke, the
// thrust loop sound) burn on two nozzles on the back of the worn body.
static const float kJetHoverMax = 1.6f, kJetSink = 1.2f;
static float s_jetHover;                    // hover seconds used this jump
static float s_jetGlow;                     // next refresh of the flames' light
static bool s_jetOn;                        // the thruster effects are lit
static void JetNozzle(float o[4]) {         // spine[3] offset of the left nozzle (x = left, mirrored), pitch
    const char* w = OutfitWornId();
    if (!_stricmp(w, "jango")) { o[0] = 0.184f; o[1] = -0.374f; o[2] = -0.18f; }
    else if (!_stricmp(w, "impcomndo")) { o[0] = 0.113f; o[1] = -0.17f; o[2] = -0.246f; }
    else { o[0] = 0.164f; o[1] = -0.233f; o[2] = -0.24f; }
    o[3] = 0.0f;
}
// the move stick / keys as a ground direction along the camera (standing still = 0)
static void StickDir(float out[2]) {
    float f[2] = { g_cam.fwd[0], g_cam.fwd[2] };
    const float fn = sqrtf(f[0] * f[0] + f[1] * f[1]);
    if (fn > 0.01f) { f[0] /= fn; f[1] /= fn; }
    float mx = 0, my = 0;
    InputMoveStick(&mx, &my);
    float rx = g_cam.right[0], rz = g_cam.right[2];
    const float rn = sqrtf(rx * rx + rz * rz);
    if (rn > 0.01f) { rx /= rn; rz /= rn; }
    out[0] = f[0] * my + rx * mx;
    out[1] = f[1] * my + rz * mx;
    const float m = sqrtf(out[0] * out[0] + out[1] * out[1]);
    if (m > 1.0f) { out[0] /= m; out[1] /= m; }
}

// scan pulse marks
struct Mark { float p[3]; };
static Mark s_marks[48];
static int s_markN;
static float s_scanMax;                     // most marks seen by this pulse (tests)

static void ScanRefresh() {
    const char* s = LuaCall("return DKB.scan(45, 48)");
    int n = 0;
    for (const char* p = s; *p && n < 48;) {
        float x, y, z;
        if (sscanf_s(p, "%f,%f,%f", &x, &y, &z) == 3) { s_marks[n].p[0] = x; s_marks[n].p[1] = y; s_marks[n].p[2] = z; n++; }
        const char* q = strchr(p, ';');
        if (!q) break;
        p = q + 1;
    }
    s_markN = n;
    if (n > s_scanMax) s_scanMax = (float)n;
}

static void EndSpecial() {
    if (!s_active) return;
    if (!strcmp(s_active->id, "rapid")) { g_boost.heatMul = 1.0f; g_boost.rpmMul = 1.0f; }
    if (!strcmp(s_active->id, "overcharge")) g_boost.charged = 0;
    if (!strcmp(s_active->id, "scan")) s_markN = 0;
    if (!strcmp(s_active->id, "jump")) {
        if (s_jetOn) LuaCall("return DKB.jetfx(0)");
        s_jetOn = false;
        Log("%s ended (hover %.2f s)", s_active->name, s_jetHover);
    } else Log("%s ended", s_active->name);
    s_active = nullptr;
}

static void UseSpecial(float now) {
    const SpecialDef* sp = Current();
    if (now < s_spReady) { HudToast("Ability recharging", 0xFF9A3C, 0.8f); Snd("deny"); return; }
    if (!strcmp(sp->id, "stim")) {
        float n[1];
        if (LuaNums(n, 1, "return DKB.heal(0)") >= 1 && n[0] >= 0.995f) { Refuse("Already at full health"); return; }
    }
    EndSpecial();
    s_active = sp; s_spT0 = now; s_spUntil = now + sp->duration; s_spReady = now + sp->cooldown;
    s_nextStep = now;
    int idx = (int)(sp - kSpecials);
    if (idx >= 0 && idx < 8) s_uses[idx]++;
    if (!strcmp(sp->id, "rapid")) { g_boost.heatMul = 0.3f; g_boost.rpmMul = 1.3f; }
    else if (!strcmp(sp->id, "overcharge")) g_boost.charged = 3;
    else if (!strcmp(sp->id, "scan")) { s_scanMax = 0; ScanRefresh(); }
    else if (!strcmp(sp->id, "stim")) s_healed = 0.0f;
    else if (!strcmp(sp->id, "jump")) {
        StickDir(s_jumpDir);   // along the camera's ground direction, scaled by the stick / keys (still = straight up)
        LuaCall("return DKB.jet(1, 0, 0, 0, 0)");
        s_jetHover = 0.0f; s_jetGlow = now + 0.25f; s_jetOn = false;
        if (PackJetFxOk() && PackChained()) {
            float o[4]; JetNozzle(o);
            s_jetOn = atoi(LuaCall("return DKB.jetfx(1, %.3f, %.3f, %.3f, %.1f)", o[0], o[1], o[2], o[3])) > 0;
            if (!s_jetOn) Log("jump pack: thruster effects did not start");
        }
    }
    Snd(sp->cue);
    HudToast(sp->name, kGold, 1.1f);
    Log("%s (recharge %.0f s)", sp->name, sp->cooldown);
}

static void SpecialTick(float now) {
    // the jump pack's key held past the thrust: the thrusters stay on (hover) while there is fuel
    if (s_active && !strcmp(s_active->id, "jump") && g_in.held[A_ABILITY] && s_jetHover < kJetHoverMax && s_spUntil < now + 0.05f)
        s_spUntil = now + 0.05f;
    if (s_active && now >= s_spUntil) {
        if (!strcmp(s_active->id, "stim") && s_healed < 0.4f) LuaCall("return DKB.heal(%.4f)", 0.4f - s_healed);
        EndSpecial();
    }
    if (!s_active) return;
    if (!strcmp(s_active->id, "scan") && now >= s_nextStep) { s_nextStep = now + 0.25f; ScanRefresh(); }
    else if (!strcmp(s_active->id, "stim") && now >= s_nextStep) {   // 15 steps of 0.1 s
        s_nextStep += 0.1f;
        const float step = 0.4f / 15.0f;
        if (s_healed + step <= 0.4f + 1e-4f) { LuaCall("return DKB.heal(%.4f)", step); s_healed += step; }
    } else if (!strcmp(s_active->id, "jump")) {
        const float t = now - s_spT0, dt = FrameDt();
        if (s_jetOn && now >= s_jetGlow) { s_jetGlow = now + 0.25f; LuaCall("return DKB.jetfx(2)"); }   // the flames' light
        if (dt > 0.0f && t > 0.03f) {
            if (t < s_active->duration) {
                // the game's own jump is under way: lift on top of it (fading out) and drift along the stick, per game second
                const float up = 9.0f * (1.0f - t / s_active->duration) * dt;
                LuaCall("return DKB.jet(0, %.3f, %.3f, %.4f, %.4f)", s_jumpDir[0], s_jumpDir[1], up > 0.0f ? up : 0.0f, 5.0f * dt);
            } else {
                // hover: the fall held to kJetSink, steered with the stick (slower than the thrust's drift)
                s_jetHover += dt;
                float d[2]; StickDir(d);
                const char* r = LuaCall("return DKB.jet(2, %.3f, %.3f, %.3f, %.4f, %.4f)", d[0], d[1], kJetSink, 3.0f * dt, dt);
                if (!strcmp(r, "l")) EndSpecial();   // landed
            }
        }
    } else if (!strcmp(s_active->id, "overcharge") && g_boost.charged <= 0) EndSpecial();
}

// ---------------------------------------------------------------------------------------------- tick / reset
void AbilityTick(bool armed) {
    const float now = GameNow();
    static float s_prev;
    if (now + 0.5f < s_prev) {   // the game clock restarted (level load): nothing carries over
        s_nadeReady = s_spReady = 0.0f; s_cooking = false; s_active = nullptr; g_boost = Boost(); s_markN = 0;
    }
    s_prev = now;
    if (s_nadeReady > 0.0f && now >= s_nadeReady && s_nadeReadyFlash < s_nadeReady) { s_nadeReadyFlash = now; if (armed) Snd("ab_ready"); }
    if (s_spReady > 0.0f && now >= s_spReady && s_spReadyFlash < s_spReady) { s_spReadyFlash = now; if (armed) Snd("ab_ready"); }
    if (!armed) {
        CancelCook("holstered");
        if (s_active) EndSpecial();
        return;
    }
    NadeTick(now);
    if (g_in.pressed[A_ABILITY]) UseSpecial(now);
    SpecialTick(now);
}

void AbilityReset() {
    CancelCook("mode off");
    EndSpecial();
    g_boost = Boost();
}

const char* AbilityStatus() {
    static char b[400];
    const float now = GameNow();
    sprintf_s(b, "pool %d builds %d | special %s active %s nadeIn %.2f spIn %.2f cooking %d fuse %.2f throws %d fails %d auto %d lastFuse %.2f "
              "land %d %.2f (%.1f,%.1f,%.1f) g %.2f marks %d max %.0f healed %.3f boost heat %.2f rpm %.2f charged %d uses %d/%d/%d/%d/%d",
              (int)s_poolOk, s_poolBuilds, Current()->id, s_active ? s_active->id : "-", s_nadeReady > now ? s_nadeReady - now : 0.0f,
              s_spReady > now ? s_spReady - now : 0.0f, (int)s_cooking, s_cooking ? FuseLeft(now) : 0.0f, s_throws, s_throwFails,
              s_autoThrows, s_lastFuse, (int)s_landHit, s_landT, s_land[0], s_land[1], s_land[2], g_nadeGravity, s_markN,
              s_scanMax, s_healed, g_boost.heatMul, g_boost.rpmMul, g_boost.charged, s_uses[0], s_uses[1], s_uses[2],
              s_uses[3], s_uses[4]);
    return b;
}

// dev / tests: recharges done now
void AbilityDevReady() { s_nadeReady = s_spReady = 0.0f; }

// ---------------------------------------------------------------------------------------------- HUD
static void RingOnGround(const rnd::Ctx& x, const float c[3], float radius, float thick, uint32_t color) {
    float px[33], py[33]; bool vis[33];
    for (int i = 0; i <= 32; i++) {
        const float a = i / 32.0f * 6.2831853f;
        const float p[3] = { c[0] + cosf(a) * radius, c[1] + 0.08f, c[2] + sinf(a) * radius };
        vis[i] = rnd::Project(x, p, &px[i], &py[i]);
    }
    for (int i = 0; i < 32; i++)
        if (vis[i] && vis[i + 1]) rnd::Line(px[i], py[i], px[i + 1], py[i + 1], thick, color);
}

static void Arc(const rnd::Ctx& x, float S, float a, float now) {
    if (!s_cooking || s_landT <= 0.0f) return;
    const float left = FuseLeft(now);
    const float tEnd = s_landT < left ? s_landT : left;   // it goes off in the air when the fuse runs out first
    const uint32_t col = left < 1.0f ? kAmber : kIce;
    const int n = 28;
    float lx = 0, ly = 0; bool lv = false;
    for (int i = 0; i <= n; i++) {
        const float t = tEnd * i / n;
        const float p[3] = { s_o[0] + s_v[0] * t, s_o[1] + s_v[1] * t - 0.5f * g_nadeGravity * t * t, s_o[2] + s_v[2] * t };
        float sx, sy;
        const bool v = rnd::Project(x, p, &sx, &sy);
        const float fade = 0.35f + 0.65f * (1.0f - (float)i / n);
        if (v && lv) rnd::Line(lx, ly, sx, sy, 2.0f * S, rnd::Argb(a * 0.55f * fade, col));
        if (v && (i % 3) == 0) rnd::Disc(sx, sy, 2.6f * S, rnd::Argb(a * fade, col));
        lx = sx; ly = sy; lv = v;
    }
    if (s_landHit && s_landT <= left) {
        // the blast radius on the ground: a faint hairline with the beeps' pulse, bold red only when you stand in it
        const float pulse = 0.75f + 0.25f * sinf(now * (left < 1.0f ? 20.0f : 8.0f));
        float pp[3];
        bool inside = false;
        if (RenPos(PlayerRen(), pp)) {
            const float dx = pp[0] - s_land[0], dy = pp[1] - s_land[1], dz = pp[2] - s_land[2];
            inside = dx * dx + dy * dy + dz * dz < kNadeRadius * kNadeRadius;
        }
        if (inside) RingOnGround(x, s_land, kNadeRadius, 2.5f * S, rnd::Argb(a * 0.85f * pulse, kRed));
        else RingOnGround(x, s_land, kNadeRadius, 1.2f * S, rnd::Argb(a * 0.28f * pulse, left < 1.0f ? kRed : kAmber));
        RingOnGround(x, s_land, 0.35f, 3.0f * S, rnd::Argb(a, kWhite));
    } else if (lv) {   // air burst: mark the point
        rnd::Line(lx - 7 * S, ly - 7 * S, lx + 7 * S, ly + 7 * S, 2.5f * S, rnd::Argb(a, kRed));
        rnd::Line(lx - 7 * S, ly + 7 * S, lx + 7 * S, ly - 7 * S, 2.5f * S, rnd::Argb(a, kRed));
    }
    // fuse countdown under the crosshair
    char b[16]; sprintf_s(b, "%.1f", left > 0 ? left : 0.0f);
    const float cx = x.vx + x.vw * 0.5f, cy = x.vy + x.vh * 0.5f;
    rnd::Text(rnd::F_BODY, cx, cy + 46.0f * S, 18.0f * S, rnd::Argb(a, left < 1.0f ? kRed : kAmber), b, rnd::A_CENTER);
}

static void Pip(float cx, float cy, float S, float a, float frac, bool ready, float flashAge, bool activeRun, float activeFrac,
                bool grenade, const char* key) {
    const float r = 19.0f * S;
    rnd::Disc(cx, cy, r + 3.0f * S, rnd::Argb(a * 0.45f, kDark));
    rnd::Arc(cx, cy, r, 3.0f * S, 0.0f, 6.2832f, rnd::Argb(a * 0.25f, kWhite));
    if (activeRun) rnd::Arc(cx, cy, r, 3.5f * S, 0.0f, 6.2832f * activeFrac, rnd::Argb(a, kGold));
    else if (!ready) rnd::Arc(cx, cy, r, 3.0f * S, 0.0f, 6.2832f * frac, rnd::Argb(a * 0.9f, kIce));
    else rnd::Arc(cx, cy, r, 3.0f * S, 0.0f, 6.2832f, rnd::Argb(a * 0.9f, kWhite));
    if (ready && flashAge >= 0.0f && flashAge < 0.6f) rnd::Glow(cx, cy, r * (1.4f + flashAge), rnd::Argb(a * (1.0f - flashAge / 0.6f) * 0.6f, kIce));
    const uint32_t ic = rnd::Argb(a * (ready || activeRun ? 1.0f : 0.45f), activeRun ? kGold : kWhite);
    if (grenade) {   // detonator: a sphere with its band
        rnd::Disc(cx, cy, 7.0f * S, ic);
        rnd::Line(cx - 7.5f * S, cy, cx + 7.5f * S, cy, 1.6f * S, rnd::Argb(a, kDark));
        rnd::Disc(cx, cy - 3.2f * S, 1.6f * S, rnd::Argb(a, ready ? kRed : kDark));
    } else {         // special: a diamond
        const float d = 8.0f * S;
        const float pts[8] = { cx, cy - d, cx + d, cy, cx, cy + d, cx - d, cy };
        rnd::Poly(pts, 4, ic);
    }
    const size_t kl = strlen(key);   // a rebound key's name: long ones (CAPSLOCK, MOUSE4) shrink to the pip's width
    rnd::Text(rnd::F_SMALL, cx, cy + r + 5.0f * S, (kl > 5 ? 65.0f / kl : 13.0f) * S, rnd::Argb(a * 0.8f, kWhite), key, rnd::A_CENTER);
}

static void ScanMarks(const rnd::Ctx& x, float S, float a, float now) {
    if (!s_active || strcmp(s_active->id, "scan")) return;
    const float age = now - s_spT0, left = s_spUntil - now;
    const float al = a * Clamp01(left / 1.0f);
    float pp[3];
    const bool havePlayer = RenPos(PlayerRen(), pp);
    if (havePlayer && age < 0.9f) RingOnGround(x, pp, 45.0f * (age / 0.9f), 3.0f * S, rnd::Argb(a * (1.0f - age / 0.9f), kIce));
    for (int i = 0; i < s_markN; i++) {
        const float p[3] = { s_marks[i].p[0], s_marks[i].p[1] + 1.1f, s_marks[i].p[2] };
        float sx, sy;
        if (!rnd::Project(x, p, &sx, &sy)) continue;
        if (sx < x.vx || sx > x.vx + x.vw || sy < x.vy || sy > x.vy + x.vh) continue;
        const float d = 9.0f * S;
        const uint32_t c = rnd::Argb(al, kAmber);
        rnd::Line(sx, sy - d, sx + d, sy, 2.0f * S, c); rnd::Line(sx + d, sy, sx, sy + d, 2.0f * S, c);
        rnd::Line(sx, sy + d, sx - d, sy, 2.0f * S, c); rnd::Line(sx - d, sy, sx, sy - d, 2.0f * S, c);
        rnd::Disc(sx, sy, 2.0f * S, rnd::Argb(al, kRed));
        if (havePlayer) {
            const float dx = p[0] - pp[0], dy = p[1] - pp[1], dz = p[2] - pp[2];
            char b[16]; sprintf_s(b, "%dm", (int)(sqrtf(dx * dx + dy * dy + dz * dz) + 0.5f));
            rnd::Text(rnd::F_SMALL, sx, sy + d + 2.0f * S, 12.0f * S, rnd::Argb(al * 0.85f, kWhite), b, rnd::A_CENTER);
        }
    }
}

void AbilityDraw(const rnd::Ctx& x, float S, float a) {
    const float now = GameNow();
    ScanMarks(x, S, a, now);
    Arc(x, S, a, now);
    // pips left of the weapon plate
    const float right = x.vx + x.vw - 48.0f * S, base = x.vy + x.vh - 64.0f * S;
    const float py = base - 22.0f * S;
    const bool pad = g_in.pad;
    const bool nadeOk = PackGrenadeOk() && PackChained() && s_poolOk;
    const float nf = s_nadeReady > now ? 1.0f - (s_nadeReady - now) / kNadeCooldown : 1.0f;
    Pip(right - 340.0f * S, py, S, a * (nadeOk ? 1.0f : 0.4f), Clamp01(nf), nadeOk && now >= s_nadeReady, now - s_nadeReadyFlash,
        s_cooking, s_cooking ? Clamp01(FuseLeft(now) / kFuse) : 0.0f, true, pad ? "RB" : VkName(cfg.kGrenade));
    const SpecialDef* sp = Current();
    const float sf = s_spReady > now ? 1.0f - (s_spReady - now) / sp->cooldown : 1.0f;
    const bool run = s_active != nullptr;
    const float af = !run ? 0.0f
                   : !strcmp(s_active->id, "overcharge") ? g_boost.charged / 3.0f
                   : !strcmp(s_active->id, "jump") ? 1.0f - s_jetHover / kJetHoverMax   // the hover fuel
                   : Clamp01((s_spUntil - now) / s_active->duration);
    Pip(right - 392.0f * S, py, S, a, Clamp01(sf), now >= s_spReady, now - s_spReadyFlash, run, af, false, pad ? "LB" : VkName(cfg.kAbility));
}

}  // namespace dkb
