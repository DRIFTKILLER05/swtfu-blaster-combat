// DK_Blaster: weapons, firing, heat and the projectile damage hook.
// Bolts are the game's own projectiles (projectile.Launch with the player as owner), so hit reactions, ragdolls,
// impact effects and saber blocks are vanilla. The hook on the projectile hit's ApplyAbilityDamage call sets our
// per-weapon damage (headshots included) and names the player as the attacker, so kills give Force points and kill
// stats like a saber kill. Every rate is in game seconds: the same at any frame rate, paused while the game is.
#include "dkb.h"
#include <stdio.h>
#include "dk_pack.h"

using namespace dk;

namespace dkb {

static const char* kTrooperBolt = "scum/runtime/abilities/Stormtrooper/StormTrooper_BlasterDamage.abilityGroup.xml";

// rpm, burst, damage, headMul, speed, heat/shot, cool/s, coolDelay, lock, spread hip/ads/move/perShot/max/recover,
// recoil pitch/yaw/fov/back, adsZoom, pellets, charge, scope, range
const WeaponDef kWeapons[] = {
    { "e11", "E-11 Blaster Rifle", "Scum/handheld/actor/twoHandGun/E11BlasterRifle.actor.xml", kTrooperBolt, "Blaster Damage",
      "Scum/audio/cues/wep_stormtrooper_fire_01.cue.xml",
      450, 0, 70, 2.5f, 60, 0.062f, 0.55f, 0.32f, 2.2f, 1.4f, 0.35f, 1.6f, 0.45f, 3.6f, 6.0f,
      0.85f, 0.35f, 0.6f, 0.03f, 0.62f, 1, 0, false, 120, 0xFF5040, "Laser_Red_01.vfx", "Blaster_E11Blaster.vfx", 1.57f },
    { "e11s", "E-11 Blaster", "Scum/handheld/actor/oneHandGun/E11Blaster.actor.xml", kTrooperBolt, "Blaster Damage",
      "Scum/audio/cues/wep_stormtrooper_fire_01.cue.xml",
      400, 0, 64, 2.5f, 60, 0.055f, 0.6f, 0.3f, 2.0f, 1.6f, 0.45f, 1.7f, 0.5f, 4.0f, 6.0f,
      0.75f, 0.3f, 0.5f, 0.025f, 0.66f, 1, 0, false, 100, 0xFF5040, "Laser_Red_01.vfx", "Blaster_E11Blaster.vfx", 1.45f },
    { "dlt20a", "DLT-20A Longblaster", "Scum/handheld/actor/twoHandGun/DLT20SniperRifle.actor.xml", kTrooperBolt, "Blaster Damage",
      "Scum/audio/cues/wep_stormtrooper_fire_01.cue.xml",
      48, 1, 380, 3.0f, 140, 0.24f, 0.40f, 1.4f, 3.0f, 2.8f, 0.0f, 2.5f, 1.2f, 4.0f, 3.0f,
      3.2f, 0.6f, 2.0f, 0.10f, 0.24f, 1, 1.1f, true, 400, 0xFF6040, "Laser_Red_Large_01.vfx", "Blaster_ScoutSniper_MuzzleFlash.vfx", 2.6f },
    { "dh17", "DH-17 Blaster Pistol", "Scum/handheld/actor/oneHandGun/DH17Blaster.actor.xml", kTrooperBolt, "Blaster Damage",
      "Scum/audio/cues/wep_stormtrooper_fire_01.cue.xml",
      420, 1, 58, 2.5f, 65, 0.08f, 0.75f, 0.38f, 1.8f, 1.2f, 0.4f, 1.2f, 0.5f, 3.5f, 9.0f,
      0.9f, 0.4f, 0.5f, 0.02f, 0.70f, 1, 0, false, 80, 0xFF5040, "Laser_Red_01.vfx", "Blaster_protorebelBlaster.vfx", 1.3f },
    { "z6", "Z-6 Rotary Blaster", "Scum/handheld/actor/twoHandGun/merrSonnZ6.actor.xml", kTrooperBolt, "Blaster Damage",
      "Scum/audio/cues/wep_stormtrooper_fire_01.cue.xml",
      900, 0, 46, 2.0f, 55, 0.024f, 0.35f, 0.45f, 2.8f, 2.4f, 1.0f, 2.2f, 0.18f, 3.5f, 4.0f,
      0.35f, 0.4f, 0.25f, 0.015f, 0.72f, 1, 0, false, 80, 0x5AA8FF, "Laser_Blue_01.vfx", "Blaster_Z6Blaster.vfx", 1.2f },
    { "fc11", "FC-11 Flechette", "Scum/handheld/actor/twoHandGun/FC11Flechette.actor.xml", kTrooperBolt, "Blaster Damage",
      "Scum/audio/cues/wep_stormtrooper_fire_01.cue.xml",
      80, 1, 24, 1.5f, 50, 0.16f, 0.45f, 0.85f, 2.4f, 5.5f, 4.0f, 1.5f, 1.0f, 7.0f, 5.0f,
      2.6f, 0.8f, 1.5f, 0.10f, 0.80f, 8, 0, false, 25, 0xFF5040, "Laser_Red_Flechette.vfx", "Blaster_FlechetteLauncher.vfx", 0.7f },
    { "bowcaster", "Bowcaster", "Scum/handheld/actor/twoHandGun/wookieeBowcaster.actor.xml", kTrooperBolt, "Blaster Damage",
      "Scum/audio/cues/wep_stormtrooper_fire_01.cue.xml",
      70, 1, 120, 2.0f, 55, 0.17f, 0.45f, 0.95f, 2.6f, 1.2f, 0.3f, 1.4f, 1.0f, 4.0f, 4.0f,
      2.2f, 0.5f, 1.2f, 0.08f, 0.66f, 3, 0.7f, false, 90, 0x60B0FF, "Laser_Blue_Large_01.vfx", "Blaster_Bowcaster.vfx", 1.9f },
    // the DLC guns (Hoth / Tatooine packs; left out with a log line when those packs are not installed)
    { "dl44", "DL-44 Heavy Blaster Pistol", "Scum/DLC3Content/handheld/actor/oneHandGun/HOT_DL44Blaster/HOT_DL44Blaster.actor.xml", kTrooperBolt, "Blaster Damage",
      "Scum/audio/cues/wep_stormtrooper_fire_01.cue.xml",
      240, 1, 95, 2.5f, 70, 0.11f, 0.7f, 0.40f, 1.8f, 1.0f, 0.25f, 1.2f, 0.9f, 3.0f, 8.0f,
      1.6f, 0.5f, 0.8f, 0.04f, 0.70f, 1, 0, false, 70, 0xFF5040, "Laser_Red_01.vfx", "Blaster_protorebelBlaster.vfx", 1.4f },
    { "a295", "A295 Blaster Rifle", "Scum/DLC3Content/handheld/actor/a295blaster/DLC3_A295Blaster.actor.xml", kTrooperBolt, "Blaster Damage",
      "Scum/audio/cues/wep_stormtrooper_fire_01.cue.xml",
      380, 0, 66, 2.5f, 60, 0.06f, 0.55f, 0.3f, 2.2f, 1.3f, 0.3f, 1.5f, 0.4f, 3.8f, 6.0f,
      0.8f, 0.3f, 0.55f, 0.03f, 0.62f, 1, 0, false, 110, 0x50FF70, "Laser_Green_01.vfx", "Blaster_E11Blaster.vfx", 1.57f },
    { "dl18", "DL-18 Blaster Pistol", "Scum/DLC2Content/handheld/actor/oneHandGun/TAT_DL18Blaster.actor.xml", kTrooperBolt, "Blaster Damage",
      "Scum/audio/cues/wep_stormtrooper_fire_01.cue.xml",
      380, 1, 50, 2.5f, 65, 0.07f, 0.75f, 0.38f, 1.6f, 1.1f, 0.3f, 1.1f, 0.45f, 3.2f, 9.0f,
      0.8f, 0.35f, 0.45f, 0.02f, 0.72f, 1, 0, false, 70, 0x50FF70, "Laser_Green_01.vfx", "Blaster_E11Blaster.vfx", 1.3f },
    { "cycler", "Tusken Cycler Rifle", "Scum/DLC2Content/handheld/actor/twoHandGun/TAT_tuskenRifle.actor.xml", kTrooperBolt, "Blaster Damage",
      "Scum/audio/cues/wep_stormtrooper_fire_01.cue.xml",
      36, 1, 200, 3.0f, 220, 0.30f, 0.4f, 1.8f, 3.2f, 3.2f, 0.0f, 2.8f, 1.4f, 4.5f, 2.5f,
      3.6f, 0.7f, 2.2f, 0.12f, 0.20f, 1, 0, true, 500, 0xFFB060, "Laser_Red_Long_01.vfx", "Blaster_ScoutSniper_MuzzleFlash.vfx", 3.0f },
};
const int kWeaponCount = sizeof kWeapons / sizeof kWeapons[0];

const WeaponDef* WeaponById(const char* id) {
    for (int i = 0; i < kWeaponCount; i++) if (!_stricmp(kWeapons[i].id, id)) return &kWeapons[i];
    return nullptr;
}

Combat g_cb;

// ---------------------------------------------------------------------------------------------- bolt registry
struct Bolt { DWORD ren; const WeaponDef* w; float t; float charge; DWORD target; bool headAim; float mul; };
static float s_shotMul = 1.0f;   // damage multiplier of the shot being fired (class specials)
static Bolt s_bolts[128];
static int s_boltNext;
static CRITICAL_SECTION s_boltCs;
static bool s_boltCsInit;

void CombatOnBolt(DWORD bolt, const WeaponDef* w, DWORD target, bool headAim) {
    if (!bolt) return;
    EnterCriticalSection(&s_boltCs);
    s_bolts[s_boltNext] = { bolt, w, GameNow(), g_cb.chargeT, target, headAim, s_shotMul };
    s_boltNext = (s_boltNext + 1) % 128;
    LeaveCriticalSection(&s_boltCs);
}
// Projectiles come from a 100-slot pool shared with the AI: a handle comes back for the next bolt, often a trooper's
// within a second. A registry entry counts only while that bolt can still fly (launched with maxTime 3 s), newest
// first, and the projectile's owner must be the player.
static const float kBoltLife = 3.5f;
static bool FindBolt(DWORD ren, Bolt* out) {
    bool ok = false;
    const float now = GameNow();
    EnterCriticalSection(&s_boltCs);
    for (int k = 1; k <= 128; k++) {
        const Bolt& b = s_bolts[(s_boltNext - k + 128) % 128];
        if (b.ren == ren && b.w && now - b.t >= 0.0f && now - b.t < kBoltLife) { *out = b; ok = true; break; }
    }
    LeaveCriticalSection(&s_boltCs);
    return ok;
}

typedef BYTE*(__thiscall* ProjLookup_t)(void* table, DWORD ren);
static ProjLookup_t s_projLookup;
static BYTE** s_projMgr;
// owner ren of a live projectile (0 = unknown: lookup missing or the projectile is gone)
static DWORD ProjOwner(DWORD ren) {
    if (!s_projLookup || !s_projMgr) return 0;
    __try {
        BYTE* m = *s_projMgr;
        BYTE* o = m ? s_projLookup(m + 4, ren) : nullptr;
        return o ? *(DWORD*)(o + 0x114) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// ---------------------------------------------------------------------------------------------- damage hook
typedef float(__cdecl* ApplyDamage_t)(DWORD victim, DWORD abLo, DWORD abHi, BYTE* msg);
static ApplyDamage_t s_applyDamage;
static uint8_t* s_dmgSite;
static DWORD s_headCrc[5];   // head + neck bones (rigs have no "head" joint: neck[2] carries the head)
static DWORD s_pendingVictims[16];
static bool s_pendingHead[16];
static int s_pendingN;
static long s_hookHits, s_hookOurs, s_hookForeign;
static DWORD s_lastBone;
static float s_lastDmgIn, s_lastDmgOut, s_lastMaxHp, s_lastTakenIn, s_lastTakenOut;
static long s_takenHits;
static const float kHeavyHp = 1600.0f;   // max health above which a target counts as heavy (jump / incinerator troopers = 1600)
static bool s_pendingAlive[16];

// an enemy bolt hitting the armed player: DamageTaken scales it for this call only. The message is put back afterwards
// (unless the game changed the value meanwhile, e.g. a block zeroed it), so nothing compounds and later readers see
// the game's own value.
static float IncomingHit(DWORD victim, DWORD abLo, DWORD abHi, BYTE* msg) {
    float* dmg = (float*)(msg + 0x48);
    float orig = 0.0f, scaled = 0.0f; bool ok = false;
    __try { orig = *dmg; if (orig > 0.0f) { scaled = orig * cfg.damageTaken; *dmg = scaled; ok = true; } } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    const float r = s_applyDamage(victim, abLo, abHi, msg);
    if (ok) {
        __try { if (*dmg == scaled) *dmg = orig; } __except (EXCEPTION_EXECUTE_HANDLER) {}
        s_takenHits++; s_lastTakenIn = orig; s_lastTakenOut = scaled;
    }
    return r;
}

static float __cdecl HkApplyDamage(DWORD victim, DWORD abLo, DWORD abHi, BYTE* msg) {
    s_hookHits++;
    Bolt b;
    DWORD bolt = 0;
    __try { bolt = *(DWORD*)(msg + 0x40); } __except (EXCEPTION_EXECUTE_HANDLER) { bolt = 0; }
    const DWORD me = PlayerRen();
    const bool scaleIn = g_mode == M_BLASTER && cfg.damageTaken != 1.0f && victim && victim == me;
    if (!bolt || !FindBolt(bolt, &b)) {
        if (scaleIn && ProjOwner(bolt) != victim) return IncomingHit(victim, abLo, abHi, msg);
        return s_applyDamage(victim, abLo, abHi, msg);
    }
    const DWORD owner = ProjOwner(bolt);
    if ((owner && owner != me) || victim == me) {   // (the projectile pool hands our old handles to enemy bolts)
        s_hookForeign++;
        if (scaleIn && owner != me) return IncomingHit(victim, abLo, abHi, msg);
        return s_applyDamage(victim, abLo, abHi, msg);
    }
    s_hookOurs++;
    const float hpBefore = RenHealth(victim);   // a kill only when this hit ended it (not a bolt into a corpse)
    const bool alive = !(hpBefore >= -0.5f && hpBefore <= 0.0f);
    bool head = false;
    __try {
        DWORD bone = *(DWORD*)(msg + 0x30);
        s_lastBone = bone;
        if (msg[0x38]) for (DWORD c : s_headCrc) if (c && bone == c) head = true;
        if (b.headAim && b.target == victim) head = true;   // aimed at this target's head, clipped its body on the way
        float* dmg = (float*)(msg + 0x48);
        s_lastDmgIn = *dmg;
        float d = b.w->damage;
        if (b.w->charge > 0.0f) d *= 0.45f + 0.55f * (b.charge >= b.w->charge ? 1.0f : b.charge / b.w->charge);
        if (head) d *= b.w->headMul;
        d *= cfg.damageScale * b.mul;
        if (cfg.heavyDamage) {   // big enemies (shock troopers 5600 HP): up to 2x, so a body-on fight lasts seconds
            const float mx = RenMaxHealth(victim);
            if (mx > kHeavyHp) d *= (std::min)(2.0f, sqrtf(mx / kHeavyHp));
            s_lastMaxHp = mx;
        }
        *dmg = d;
        s_lastDmgOut = d;
        if (cfg.killCredit) {
            DWORD p = PlayerRen();
            if (p) *(DWORD*)(msg + 0x40) = p;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    float r = s_applyDamage(victim, abLo, abHi, msg);
    if (s_pendingN < 16) { s_pendingHead[s_pendingN] = head; s_pendingAlive[s_pendingN] = alive; s_pendingVictims[s_pendingN++] = victim; }
    return r;
}

bool CombatHookOk() { return s_applyDamage != nullptr; }

// ---------------------------------------------------------------------------------------------- no auto-deflect
// Standing Starkiller deflects every blockable bolt by himself (TakeDamage -> DeflectCheck zeroes the hit). With a gun
// in his hands that made him untouchable: while armed, the check answers "not deflected" for the player, so enemy
// bolts hit him (damage arcs, low health; DamageTaken scales them). Everyone else and saber mode keep the game's check.
typedef char(__thiscall* Deflect_t)(BYTE* comp, BYTE* msg);
static Deflect_t s_deflect;
static long s_deflectSkips;
static char __fastcall HkDeflect(BYTE* comp, void*, BYTE* msg) {
    if (g_mode == M_BLASTER && !cfg.autoDeflect) {
        DWORD victim = 0;
        __try { victim = *(DWORD*)(comp + 0x40); } __except (EXCEPTION_EXECUTE_HANDLER) { victim = 0; }
        if (victim && victim == PlayerRen()) { s_deflectSkips++; return 0; }
    }
    return s_deflect(comp, msg);
}

// The second path: a blockable bolt flying at a player is first offered to PlayerComp_ReflectBolt (projectile update
// -> FUN_008587e0 loops over the player components), which can bat it back at the shooter with the saber block
// effects before it ever hits. Armed, the player's component answers "not reflected"; saber mode and AutoDeflect=1
// keep the game's answer. comp+0x10 = the owner actor (the game compares it with the bolt's shooter, proj+0x114).
typedef char(__thiscall* Reflect_t)(BYTE* comp, BYTE* proj);
static Reflect_t s_reflect;
static long s_reflectSkips, s_reflectKept;
static char __fastcall HkReflect(BYTE* comp, void*, BYTE* proj) {
    if (g_mode == M_BLASTER && !cfg.autoDeflect) {
        DWORD owner = 0;
        __try { owner = *(DWORD*)(comp + 0x10); } __except (EXCEPTION_EXECUTE_HANDLER) { owner = 0; }
        if (owner && owner == PlayerRen()) { s_reflectSkips++; return 0; }
    }
    char r = s_reflect(comp, proj);
    if (r) s_reflectKept++;
    return r;
}

// The third path, the pose: every frame PlayerComp_Update runs the ranged-block anticipation (FUN_00884d20), which asks
// the spatial database for the projectiles around the player (FUN_0073bf30, thiscall, 5 args: position, radius, 0,
// buffer, max) and, for blockable bolts on their way in, sets the animation's block requests - the BlockRanged saber
// pose Starkiller struck with a gun in his hands even though the bolts hit him. Armed, the query answers "nothing
// near" for the player: the game's own empty-sky return, so no pose and no leftover block state. [esi] = owner actor,
// esi+0x110 = the position passed in. Saber mode and AutoDeflect=1 keep it.
typedef int(__thiscall* BlockScan_t)(void* db, BYTE* pos, float radius, int flags, void* buf, int max);
static BlockScan_t s_blockScan;
static long s_blockScanSkips;
static int __fastcall HkBlockScan(void* db, void*, BYTE* pos, float radius, int flags, void* buf, int max) {
    if (g_mode == M_BLASTER && !cfg.autoDeflect) {
        DWORD owner = 0;
        __try { owner = *(DWORD*)(pos - 0x110); } __except (EXCEPTION_EXECUTE_HANDLER) { owner = 0; }
        if (owner && owner == PlayerRen()) { s_blockScanSkips++; return 0; }
    }
    return s_blockScan(db, pos, radius, flags, buf, max);
}

bool CombatInstall(const Range& text) {
    if (!s_boltCsInit) { InitializeCriticalSection(&s_boltCs); s_boltCsInit = true; }
    // body-part CRCs of the head bones (stricrc32 of the bone name, as the hit message stores them)
    s_headCrc[0] = pack::StriCrc32("neck[2]"); s_headCrc[1] = pack::StriCrc32("neck[1]"); s_headCrc[2] = pack::StriCrc32("head[0]");
    s_headCrc[3] = pack::StriCrc32("head"); s_headCrc[4] = pack::StriCrc32("neck[0]");
    s_projLookup = (ProjLookup_t)FindSym(text, "Blaster_ProjLookup");
    s_projMgr = (BYTE**)FindSym(text, "gProjectileMgr");
    if (!s_projLookup || !s_projMgr) Log("projectile owner lookup not found - bolts are recognised by handle and age only");
    if (uint8_t* site = FindSym(text, "Blaster_DeflectCall")) {
        if (site[0] == 0xE8 && site[-2] == 0x8B && site[-1] == 0xCB) s_deflect = (Deflect_t)RedirectCall(site, (void*)&HkDeflect);
    }
    if (!s_deflect) Log("deflect call not found - Starkiller keeps deflecting bolts in blaster mode");
    if (uint8_t* site = FindSym(text, "Blaster_ReflectCall")) {
        if (site[0] == 0xE8 && site[-1] == 0x53) s_reflect = (Reflect_t)RedirectCall(site, (void*)&HkReflect);
    }
    if (!s_reflect) Log("reflect call not found - Starkiller can still bat bolts back in blaster mode");
    if (uint8_t* site = FindSym(text, "Blaster_BlockScanCall")) {
        if (site[0] == 0xE8 && site[-2] == 0x8B && site[-1] == 0xCF) s_blockScan = (BlockScan_t)RedirectCall(site, (void*)&HkBlockScan);
    }
    if (!s_blockScan) Log("block-pose scan call not found - Starkiller may still strike the saber block pose in blaster mode");
    s_dmgSite = FindSym(text, "Blaster_ProjDamageCall");
    if (!s_dmgSite || s_dmgSite[0] != 0xE8) { Log("projectile damage call not found - vanilla bolt damage, no kill credit"); return false; }
    s_applyDamage = (ApplyDamage_t)RedirectCall(s_dmgSite, (void*)&HkApplyDamage);
    return s_applyDamage != nullptr;
}

// ---------------------------------------------------------------------------------------------- firing
static uint32_t s_rng = 0x9E3779B9;
static float Rand01() { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return (s_rng & 0xFFFFFF) / 16777216.0f; }

void CombatReset(const WeaponDef* w) {
    float heat = g_cb.heat, od = g_cb.overdriveUntil;
    int shots = g_cb.shots, hits = g_cb.hits, kills = g_cb.kills, heads = g_cb.heads;
    g_cb = Combat();
    g_cb.w = w;
    g_cb.heat = heat > 0.0f && heat < 1.0f ? heat : 0.0f;
    g_cb.overdriveUntil = od;   // the perfect vent's reward carries over to the other gun
    g_cb.shots = shots; g_cb.hits = hits; g_cb.kills = kills; g_cb.heads = heads;
}

float CombatSpread();
bool CombatShoot(float chargeT);

// current cone half-angle (degrees)
float g_devSpread = 1.0f;   // dev / tests: spread multiplier (0 = every bolt on the crosshair)
static float s_moveSpeed;
static bool s_aiming, s_sprinting;
void CombatSetMotion(float speed, bool aiming, bool sprinting) { s_moveSpeed = speed; s_aiming = aiming; s_sprinting = sprinting; }
float CombatSpread() {
    const WeaponDef* w = g_cb.w;
    if (!w) return 0.0f;
    float base = s_aiming ? w->spreadAds : w->spreadHip;
    float mv = s_moveSpeed > 0.5f ? w->spreadMove * (s_moveSpeed > 6.0f ? 1.0f : s_moveSpeed / 6.0f) : 0.0f;
    if (s_aiming) mv *= 0.5f;
    float s = base + mv + g_cb.bloom;
    s = s > w->spreadMax + base ? w->spreadMax + base : s;
    const float cr = CrouchDepth() / 0.32f;   // crouched: a steadier gun (up to -25 %)
    return s * (1.0f - 0.25f * (cr > 1.0f ? 1.0f : cr));
}

static int s_camShots;   // shots that started on the camera line (the muzzle's line was blocked)

// one trigger pull (all pellets) through DKB.shoot; returns false when Lua refused
bool CombatShoot(float chargeT) {
    const WeaponDef* w = g_cb.w;
    if (!w || !g_cam.valid) return false;
    const float* e = g_cam.eye; const float* f = g_cam.fwd; const float* r = g_cam.right; const float* u = g_cam.up;
    float pp[3];
    float s0 = 0.5f;
    if (RenPos(PlayerRen(), pp)) {   // start the camera ray at the player's depth: nothing behind him counts
        float d = (pp[0] - e[0]) * f[0] + (pp[1] - e[1]) * f[1] + (pp[2] - e[2]) * f[2];
        if (d > s0) s0 = d;
    }
    char sp[512]; int len = 0;
    const float cone = tanf(CombatSpread() * g_devSpread * 0.0174533f);
    const bool ours = PackAbilityOk() && PackChained();   // our bolt (every level) or the stormtrooper's (where it exists)
    const int n = w->pellets > 1 ? w->pellets : 1;
    for (int i = 0; i < n && len < 480; i++) {
        // uniform in the disc: sqrt for the radius
        float a = Rand01() * 6.2831853f, rr = sqrtf(Rand01()) * cone;
        len += sprintf_s(sp + len, sizeof sp - len, "%s%.5f,%.5f", i ? "," : "", cosf(a) * rr, sinf(a) * rr);
    }
    const char* res = LuaCall("return DKB.shoot(%.4f,%.4f,%.4f,%.5f,%.5f,%.5f,%.3f,%.1f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.1f,%.1f,'%s','%s','%s',{%s})",
        e[0], e[1], e[2], f[0], f[1], f[2], s0, w->range * 3.0f, r[0], r[1], r[2], u[0], u[1], u[2],
        w->speed, 3.0f, ours ? kDkAbilityGroup : w->abilityGroup, ours ? w->id : w->abilityEntry, SoundFireCue(w), sp);
    if (!strncmp(res, "error", 5)) { Log("shot failed: %s", res); return false; }
    // "id1,id2,\thit\tlive\tdist\tcam\theadAim\ttarget"
    const char* fld[7] = { res };
    for (int i = 1; i < 7; i++) { const char* t = fld[i - 1] ? strchr(fld[i - 1], '\t') : nullptr; fld[i] = t ? t + 1 : nullptr; }
    if (fld[4] && *fld[4] == '1') s_camShots++;
    const bool headAim = fld[5] && *fld[5] == '1';
    const DWORD target = fld[6] ? strtoul(fld[6], nullptr, 16) : 0;
    int bolts = 0;
    for (const char* p = res; *p && *p != '\t';) {
        DWORD id = strtoul(p, (char**)&p, 16);
        if (id) { CombatOnBolt(id, w, target, headAim); bolts++; }
        if (*p == ',') p++; else break;
    }
    (void)chargeT;
    return bolts > 0;
}

static void Kick(const WeaponDef* w, float mul) {
    float yaw = (Rand01() * 2.0f - 1.0f) * w->recoilYaw;
    CamKick(w->recoilPitch * mul, yaw * mul, w->recoilFov * mul, w->recoilBack * mul);
}

// heat timeline in game time (dev `heat`: tests prove the rates are the same at every frame rate)
static int s_overheats, s_climbShots, s_ohShots, s_perfect, s_missed, s_manualVents;
static float s_climbT0, s_ohSpan, s_tOverheat, s_lockSpan = -1.0f;

// a cool-down sweep (overheat or manual vent): the heat falls from where it is to 0 over T seconds, the gun is locked,
// and somewhere in the middle sits the active-cooldown window - at least 0.22 s wide in time (15 % of a long sweep)
static void StartSweep(float now, float T) {
    g_cb.ventH0 = g_cb.heat > 0.01f ? g_cb.heat : 0.01f;
    g_cb.ventT = T;
    g_cb.lockUntil = now + T;
    const float half = 0.5f * fmaxf(0.22f, 0.15f * T) / T * g_cb.ventH0;
    const float c = g_cb.ventH0 * (0.35f + Rand01() * 0.3f);
    g_cb.ventWindow0 = c - half; g_cb.ventWindow1 = c + half;
    g_cb.ventTried = false;
}
bool CombatSweeping() { return g_cb.overheated || g_cb.venting; }
float CombatOverdrive() { const float l = g_cb.overdriveUntil - GameNow(); return l > 0.0f ? l : 0.0f; }

static void Fire(float now, float charge) {
    const WeaponDef* w = g_cb.w;
    g_cb.chargeT = charge;
    const bool overcharged = g_boost.charged > 0;
    s_shotMul = g_boost.dmgMul * (overcharged ? 2.0f : 1.0f);
    if (!CombatShoot(charge)) { g_cb.nextShot = now + 0.25f; return; }
    if (overcharged) g_boost.charged--;
    g_cb.shots++;
    if (g_cb.heat <= 0.0f) { s_climbT0 = now; s_climbShots = 0; }
    s_climbShots++;
    g_cb.lastShot = now;
    const bool overdrive = now < g_cb.overdriveUntil;   // a perfect vent's reward: no heat for a few seconds
    g_cb.heat += w->heatPerShot * (w->charge > 0.0f && charge > 0.0f ? 0.6f + 0.4f * fminf(1.0f, charge / w->charge) : 1.0f) *
                 (overcharged || overdrive ? 0.0f : g_boost.heatMul);
    g_cb.bloom += w->spreadPerShot;
    if (g_cb.bloom > w->spreadMax) g_cb.bloom = w->spreadMax;
    Kick(w, s_aiming ? 0.65f : 1.0f);
    if (g_cb.heat >= 1.0f) {
        g_cb.heat = 1.0f;
        g_cb.overheated = true;
        StartSweep(now, w->overheatLock);
        s_overheats++; s_ohShots = s_climbShots; s_ohSpan = now - s_climbT0; s_tOverheat = now; s_lockSpan = -1.0f;
        Snd("overheat");
        HudToast("OVERHEATED", 0xFF7A30, 0.9f);
    } else if (g_cb.heat >= 0.8f && !g_cb.heatWarned) {   // once per climb past 80 %
        g_cb.heatWarned = true;
        Snd("heat_warn");
    }
}

float g_lastTriggerT;
void CombatTick(float dt, bool armed) {
    const WeaponDef* w = g_cb.w;
    if (!w) return;
    const float now = GameNow();
    static float s_prevNow;
    if (now + 0.5f < s_prevNow) {   // the game clock restarted (level load): timers from the old clock would stall
        g_cb.lastShot = g_cb.nextShot = 0.0f;
        if (CombatSweeping()) g_cb.lockUntil = now + (g_cb.lockUntil - s_prevNow);
        g_cb.overdriveUntil = 0.0f;
    }
    s_prevNow = now;
    // overdrive (after a perfect vent) ends with a soft cue
    static bool s_odWas;
    const bool od = now < g_cb.overdriveUntil;
    if (s_odWas && !od && armed) Snd("ready");
    s_odWas = od;
    // cooling
    if (CombatSweeping()) {
        float left = g_cb.lockUntil - now;
        g_cb.heat = left > 0.0f && g_cb.ventT > 0.0f ? fminf(1.0f, g_cb.ventH0 * left / g_cb.ventT) : 0.0f;
        if (left <= 0.0f) {
            if (g_cb.overheated) s_lockSpan = now - s_tOverheat;
            g_cb.overheated = g_cb.venting = false; g_cb.heat = 0.0f;
            if (armed) Snd("ready");
        }
    } else if (od) {
        g_cb.heat = 0.0f;
    } else if (now - g_cb.lastShot > w->coolDelay && g_cb.chargeT <= 0.0f) {   // a charging gun holds its heat
        g_cb.heat -= w->coolRate * dt;
        if (g_cb.heat < 0.0f) g_cb.heat = 0.0f;
    }
    if (g_cb.heat < 0.6f) g_cb.heatWarned = false;
    // bloom builds under sustained fire and recovers once the trigger rests (0.15 s after the last shot)
    if (now - g_cb.lastShot > 0.15f) g_cb.bloom -= w->spreadRecover * dt;
    if (g_cb.bloom < 0.0f) g_cb.bloom = 0.0f;
    if (!armed) { g_cb.chargeT = 0.0f; g_cb.burstLeft = 0; return; }

    // manual vent (Battlefront): the vent key below overheat starts the cool-down sweep - twice as fast as resting
    // cooling, the gun locked meanwhile - with the same active-cooldown window; the next press is the try
    bool started = false;
    if (!CombatSweeping() && !od && g_in.pressed[A_VENT] && g_cb.heat >= 0.10f) {
        g_cb.venting = true; started = true;
        StartSweep(now, fmaxf(0.6f, fminf(w->overheatLock * 0.6f, g_cb.heat / (2.0f * w->coolRate))));
        g_cb.burstLeft = 0; g_cb.chargeT = 0.0f;
        s_manualVents++;
        Snd("vent");
        if (cfg.debug) Log("manual vent from %.2f: %.2f s, window %.2f-%.2f", g_cb.ventH0, g_cb.ventT, g_cb.ventWindow0, g_cb.ventWindow1);
    }
    // active cooldown: one try per sweep
    if (CombatSweeping() && g_in.pressed[A_VENT] && !g_cb.ventTried && !started) {
        g_cb.ventTried = true;
        if (g_cb.heat >= g_cb.ventWindow0 && g_cb.heat <= g_cb.ventWindow1) {
            if (g_cb.overheated) s_lockSpan = now - s_tOverheat;
            // overdrive scales with the heat dumped: a full overheat earns all of it, a small manual vent a sliver
            const float odT = cfg.overdriveTime * fminf(1.0f, g_cb.ventH0 / 0.6f);
            g_cb.overheated = g_cb.venting = false; g_cb.heat = 0.0f; g_cb.lockUntil = now;
            s_perfect++;
            Snd("vent_perfect"); Snd("vent");
            g_cb.ventPerfectAt = now;
            if (odT > 0.05f) {
                g_cb.overdriveUntil = now + odT;
                HudToast("PERFECT VENT - OVERDRIVE", 0xFFE600, 1.0f);
            } else HudToast("PERFECT VENT", 0xFFE600, 0.8f);
            Log("active cooldown: perfect (vented %.2f) - overdrive %.2f s", g_cb.ventH0, odT);
        } else {
            g_cb.lockUntil += 0.5f; g_cb.ventT += 0.5f;   // fumble: a little longer
            s_missed++;
            Snd("vent_miss");
            HudToast("VENT MISSED", 0xFF7A30, 0.7f);
            Log("active cooldown: missed (heat %.2f window %.2f-%.2f)", g_cb.heat, g_cb.ventWindow0, g_cb.ventWindow1);
        }
    }
    if (CombatSweeping() || s_sprinting) { g_cb.chargeT = 0.0f; g_cb.burstLeft = 0; return; }

    const float interval = 60.0f / (w->rpm * g_boost.rpmMul);
    if (w->charge > 0.0f) {   // hold to charge, release to fire (a tap fires a quick weak shot)
        if (g_in.held[A_FIRE]) {
            if (now >= g_cb.nextShot) g_cb.chargeT += dt;
        } else if (g_in.released[A_FIRE] && now >= g_cb.nextShot) {
            Fire(now, g_cb.chargeT);
            g_cb.nextShot = now + interval;
            g_cb.chargeT = 0.0f;
        } else g_cb.chargeT = 0.0f;
        return;
    }
    if (w->burst == 0) {   // automatic: exact rate, a short backlog is fired in the same frame
        if (g_in.held[A_FIRE]) {
            if (g_in.pressed[A_FIRE] && g_cb.nextShot < now) g_cb.nextShot = now;
            int n = 0;
            while (now >= g_cb.nextShot && n < 4 && !g_cb.overheated) { Fire(now, 0.0f); g_cb.nextShot += interval; n++; }
            if (now - g_cb.nextShot > interval) g_cb.nextShot = now;
        }
        return;
    }
    // semi / burst: a press starts a burst of `burst` shots
    if (g_in.pressed[A_FIRE] && now >= g_cb.nextShot && g_cb.burstLeft == 0) g_cb.burstLeft = w->burst;
    if (g_cb.burstLeft > 0 && now >= g_cb.nextShot) {
        Fire(now, 0.0f);
        g_cb.burstLeft--;
        g_cb.nextShot = now + (g_cb.burstLeft > 0 ? 0.07f : interval);
    }
}

// hits reported by the damage hook: hit / kill / headshot markers (the kill check runs on the next DK frame)
void CombatFrameHits() {
    int snd = 0;   // one hit sound per frame (the pellets of one shot land together): kill 3 > head 2 > hit 1
    while (s_pendingN > 0) {
        int i = --s_pendingN;
        DWORD v = s_pendingVictims[i];
        bool head = s_pendingHead[i];
        const float hp = RenHealth(v);
        const bool dead = s_pendingAlive[i] && hp >= -0.5f && hp <= 0.0f;
        g_cb.hits++;
        if (head) g_cb.heads++;
        if (dead) g_cb.kills++;
        HudHit(dead, head);
        const int s = dead ? 3 : head ? 2 : 1;
        if (s > snd) snd = s;
        if (cfg.debug) Log("hit %08X%s%s (bone %08X, damage %.0f -> %.0f)", v, head ? " HEAD" : "", dead ? " KILL" : "",
                           s_lastBone, s_lastDmgIn, s_lastDmgOut);
    }
    if (snd && cfg.hitSound) Snd(snd == 3 ? "kill" : snd == 2 ? "head" : "hit");
}

const char* CombatStatus() {
    static char b[460];
    const WeaponDef* w = g_cb.w;
    _snprintf_s(b, _TRUNCATE, "weapon %s heat %.2f%s bloom %.2f spread %.2f shots %d hits %d kills %d heads %d hook %ld/%ld foreign %ld lastBone %08X dmg %.0f->%.0f maxhp %.0f camline %d nodeflect %ld taken %ld %.0f->%.0f noreflect %ld reflected %ld noblockpose %ld",
              w ? w->id : "-", g_cb.heat, g_cb.overheated ? " OVERHEATED" : "", g_cb.bloom, CombatSpread(), g_cb.shots,
              g_cb.hits, g_cb.kills, g_cb.heads, s_hookOurs, s_hookHits, s_hookForeign, s_lastBone, s_lastDmgIn, s_lastDmgOut, s_lastMaxHp,
              s_camShots, s_deflectSkips, s_takenHits, s_lastTakenIn, s_lastTakenOut, s_reflectSkips, s_reflectKept, s_blockScanSkips);
    return b;
}

const char* CombatHeatStatus() {
    static char b[400];
    sprintf_s(b, "now %.3f heat %.3f oh %d window %.3f-%.3f tried %d | overheats %d ohShots %d ohSpan %.3f lockSpan %.3f "
              "perfect %d missed %d bloom %.3f lastShot %.3f shots %d venting %d manual %d sweep %.3f overdrive %.3f",
              GameNow(), g_cb.heat, (int)g_cb.overheated, g_cb.ventWindow0, g_cb.ventWindow1, (int)g_cb.ventTried, s_overheats,
              s_ohShots, s_ohSpan, s_lockSpan, s_perfect, s_missed, g_cb.bloom, g_cb.lastShot, g_cb.shots, (int)g_cb.venting,
              s_manualVents, g_cb.ventT, CombatOverdrive());
    return b;
}

}  // namespace dkb
