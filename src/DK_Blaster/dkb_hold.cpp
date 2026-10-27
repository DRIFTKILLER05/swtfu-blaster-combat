// dkb_hold.cpp - the rifle stance: while the blaster is drawn, the player's locomotion (idle, strafe, walk, run, stops,
// turns) plays the stormtrooper's combat animations - the gun held two-handed at the ready - instead of Starkiller's
// saber stance, which carried the gun on the saber bone pointing down and back. The animation entries of the player's
// live chore set are re-pointed in memory (DK_VaderSwap's choke technique) and restored exactly on holster, unload and
// reset; nothing on disk changes. Each chore block switches only while the player is not in it (HoldTick). The trooper
// animations come with our pack (dkb_pack.cpp: kHoldAnims), so every level has them. Jumps, rolls, dashes and hit
// reactions keep the player's own animations.
#include "dkb.h"

using namespace dk;

namespace dkb {

typedef DWORD(__cdecl* StrCrc_t)(const char*);
typedef DWORD(__cdecl* PathToKey_t)(const char*, bool);
typedef void*(__cdecl* LockAsset_t)(DWORD key, int type, void** item);
typedef int(__cdecl* UnlockAsset_t)(DWORD key, int type);
typedef DWORD(__fastcall* FindByCrc_t)(void* reg, void* edx, DWORD crc);   // __thiscall (ret 4)
typedef void*(__fastcall* GetRenObj_t)(void* mgr, void* edx, DWORD id);      // __thiscall (ret 4)
typedef bool(__fastcall* BlockPlaying_t)(void* animComp, void* edx, DWORD choreCrc, DWORD blockCrc);   // __thiscall (ret 8)
static const DWORD kAnimCompOff = 0xF8;   // GetRenObj(ren entry +0x0C) + 0xF8 (animation.IsBlockPlaying, GetChoreSet)
static const int kAnimAssetType = (int)0xF0E0D413;

static StrCrc_t s_strcrc;
static PathToKey_t s_pathToKey;
static LockAsset_t s_lock;
static UnlockAsset_t s_unlock;
static FindByCrc_t s_findByCrc;
static void** s_renObjMgr;
static GetRenObj_t s_getRenObj;
static BlockPlaying_t s_blockPlaying;
static DWORD* s_reg;              // chore registry: +4 begin, +8 end of the ChoreSet* vector
static bool s_ok;

// the player's animation (base name) -> the trooper's
#define TR "stormTrooper_ma_com_"
#define Tr "stormtrooper_ma_com_"
static struct HoldMap { const char* from; const char* to; DWORD fromKey, toKey; } kMap[] = {
    // strafe (walk and run rows) and its starts: the trooper's combat strafe run
    {"player_ma_com_strafe_walk_back", TR "strafe_run_B"}, {"player_ma_com_strafe_walk_right", TR "strafe_run_R"},
    {"player_ma_com_strafe_walk_forward", TR "strafe_run_F"}, {"player_ma_com_strafe_walk_left", TR "strafe_run_L"},
    {"player_ma_com_strafe_back", TR "strafe_run_B"}, {"player_ma_com_strafe_right", TR "strafe_run_R"},
    {"player_ma_com_strafe_forward", TR "strafe_run_F"}, {"player_ma_com_strafe_left", TR "strafe_run_L"},
    {"player_ma_com_strafe_idle_to_walk_back", TR "strafe_run_B"}, {"player_ma_com_strafe_idle_to_walk_right", TR "strafe_run_R"},
    {"player_ma_com_strafe_idle_to_walk_forward", TR "strafe_run_F"}, {"player_ma_com_strafe_idle_to_walk_left", TR "strafe_run_L"},
    {"player_ma_com_strafe_idle_to_back", TR "strafe_run_B"}, {"player_ma_com_strafe_idle_to_right", TR "strafe_run_R"},
    {"player_ma_com_strafe_idle_to_forward", TR "strafe_run_F"}, {"player_ma_com_strafe_idle_to_left", TR "strafe_run_L"},
    // strafe stops: straight to the trooper's idle (his stops are forward-only: a side or back stop would slide forward)
    {"player_ma_com_strafe_walk_back_to_idle_LF", Tr "idle"}, {"player_ma_com_strafe_walk_right_to_idle_LF", Tr "idle"},
    {"player_ma_com_strafe_walk_forward_to_idle_LF", Tr "idle"}, {"player_ma_com_strafe_walk_left_to_idle_LF", Tr "idle"},
    {"player_ma_com_strafe_back_to_idle_LF", Tr "idle"}, {"player_ma_com_strafe_right_to_idle_LF", Tr "idle"},
    {"player_ma_com_strafe_forward_to_idle_LF", Tr "idle"}, {"player_ma_com_strafe_left_to_idle_LF", Tr "idle"},
    {"player_ma_com_strafe_walk_back_to_idle_RF", Tr "idle"}, {"player_ma_com_strafe_walk_right_to_idle_RF", Tr "idle"},
    {"player_ma_com_strafe_walk_forward_to_idle_RF", Tr "idle"}, {"player_ma_com_strafe_walk_left_to_idle_RF", Tr "idle"},
    {"player_ma_com_strafe_back_to_idle_RF", Tr "idle"}, {"player_ma_com_strafe_right_to_idle_RF", Tr "idle"},
    {"player_ma_com_strafe_forward_to_idle_RF", Tr "idle"}, {"player_ma_com_strafe_left_to_idle_RF", Tr "idle"},
    // idles
    {"player_ma_com_strafe_idle", Tr "idle"}, {"player_ma_com_idle_act1", Tr "idle"}, {"player_ma_nav_idle_act1", Tr "idle"},
    {"player_ma_nav_idle_to_com", Tr "idle"}, {"player_ma_com_idle_to_nav", Tr "idle"},
    // forward locomotion (sprint: strafe off)
    {"player_ma_com_walk_act1", TR "walk"}, {"player_ma_nav_walk_act1", TR "walk"},
    {"player_ma_com_run_act1", Tr "run"}, {"player_ma_nav_run_act1", Tr "run"},
    {"player_ma_com_run_bank_R", Tr "run_bank_R"}, {"player_ma_com_run_bank_L", Tr "run_bank_L"},
    {"player_ma_nav_run_bank_R", Tr "run_bank_R"}, {"player_ma_nav_run_bank_L", Tr "run_bank_L"},
    {"player_ma_com_idle_to_walk_LF", TR "walk"}, {"player_ma_com_idle_to_run_LF", Tr "run"},
    {"player_ma_nav_idle_to_walk_LF", TR "walk"}, {"player_ma_nav_idle_to_run_LF", Tr "run"},
    {"player_ma_com_walk_to_idle_LF", TR "run_to_idle_lFoot"}, {"player_ma_com_walk_to_idle_RF", TR "run_to_idle_rFoot"},
    {"player_ma_com_run_to_idle_LF", TR "run_to_idle_lFoot"}, {"player_ma_com_run_to_idle_RF", TR "run_to_idle_rFoot"},
    {"player_ma_nav_walk_to_idle_LF", TR "run_to_idle_lFoot"}, {"player_ma_nav_walk_to_idle_RF", TR "run_to_idle_rFoot"},
    {"player_ma_nav_run_to_idle_LF", TR "run_to_idle_lFoot"}, {"player_ma_nav_run_to_idle_RF", TR "run_to_idle_rFoot"},
    // turns on the spot, small steps, sharp turns
    {"player_ma_com_stepOrient_180_r", TR "stepOrient_180_R"}, {"player_ma_com_stepOrient_90_r", TR "stepOrient_90_R"},
    {"player_ma_com_stepOrient_90_l", TR "stepOrient_90_L"}, {"player_ma_com_stepOrient_180_l", TR "stepOrient_180_L"},
    {"player_ma_nav_stepOrient_180_r", TR "stepOrient_180_R"}, {"player_ma_nav_stepOrient_90_r", TR "stepOrient_90_R"},
    {"player_ma_nav_stepOrient_90_l", TR "stepOrient_90_L"}, {"player_ma_nav_stepOrient_180_l", TR "stepOrient_180_L"},
    {"player_ma_com_stepOrient_180_r_cape", TR "stepOrient_180_R"}, {"player_ma_com_stepOrient_90_r_cape", TR "stepOrient_90_R"},
    {"player_ma_com_stepOrient_90_l_cape", TR "stepOrient_90_L"}, {"player_ma_com_stepOrient_180_l_cape", TR "stepOrient_180_L"},
    {"player_ma_nav_stepOrient_180_r_cape", TR "stepOrient_180_R"}, {"player_ma_nav_stepOrient_90_r_cape", TR "stepOrient_90_R"},
    {"player_ma_nav_stepOrient_90_l_cape", TR "stepOrient_90_L"}, {"player_ma_nav_stepOrient_180_l_cape", TR "stepOrient_180_L"},
    {"player_ma_com_smallStep_F", TR "stepOrient_0_F"}, {"player_ma_nav_smallStep_F", TR "stepOrient_0_F"},
    {"player_ma_com_smallStep_F_cape", TR "stepOrient_0_F"}, {"player_ma_nav_smallStep_F_cape", TR "stepOrient_0_F"},
    {"player_ma_com_run_turn_180_r", Tr "runTurn_180_R"}, {"player_ma_com_run_turn_180_l", Tr "runTurn_180_L"},
    {"player_ma_nav_run_turn_180_r", Tr "runTurn_180_R"}, {"player_ma_nav_run_turn_180_l", Tr "runTurn_180_L"},
};
#undef TR
#undef Tr
static const int kMapN = sizeof kMap / sizeof kMap[0];

static char s_group[160];

bool HoldInstall(HMODULE exe, const Range& t) {
    // the asset catalog lives in ScumLibLEC_Retail.dll (the exe only imports it)
    HMODULE scum = GetModuleHandleA("ScumLibLEC_Retail.dll");
    if (!scum) scum = exe;
    s_strcrc = (StrCrc_t)GetProcAddress(scum, "?stricrc32@@YAIPBD@Z");
    s_pathToKey = (PathToKey_t)GetProcAddress(scum, "?FilePathToAssetKey@AssetCatalog@@SAIQBD_N@Z");
    s_lock = (LockAsset_t)GetProcAddress(scum, "?LockAsset@AssetCatalog@@SAPAXIHPAPAVAssetCatalogItem@@@Z");
    s_unlock = (UnlockAsset_t)GetProcAddress(scum, "?UnlockAsset@AssetCatalog@@SAHIH@Z");
    s_findByCrc = (FindByCrc_t)FindSym(t, "ChoreReg_FindByCrc");
    s_renObjMgr = (void**)FindSym(t, "gRenObjMgr");
    s_getRenObj = (GetRenObj_t)FindSym(t, "RenObjMgr_GetRenObj");
    s_blockPlaying = (BlockPlaying_t)FindSym(t, "AnimComp_IsBlockPlaying");
    uint8_t* rg = FindSym(t, "ChoreRegistry_Get");   // ... mov eax, offset registry (+0x58)
    if (rg && rg[0x58] == 0xB8) s_reg = *(DWORD**)(rg + 0x59);
    s_ok = s_strcrc && s_pathToKey && s_lock && s_unlock && s_findByCrc && s_reg && s_renObjMgr && s_getRenObj && s_blockPlaying;
    if (!s_ok) Log("rifle stance off (strcrc %p, key %p, lock %p/%p, chore lookup %p, registry %p, ren objects %p/%p, "
                   "block playing %p)", s_strcrc, s_pathToKey,
                   s_lock, s_unlock, s_findByCrc, s_reg, s_renObjMgr, s_getRenObj, s_blockPlaying);
    return s_ok;
}

// An entry is only ever swapped while its chore block is NOT in the player's animation tree: the animation worker
// decodes a playing clip through its entry, so swapping key / data under a live cursor decodes one clip's keys with the
// other's cursor (int divide by zero at 0x718471, NOTES "1.2.10 crash fix"). HoldApply / HoldRestore only say what is
// wanted; HoldTick moves each block over as soon as the player is out of it (most at once, the one he stands in a
// moment later).
struct Patch { DWORD* entry; DWORD wrap; DWORD old[3]; DWORD newKey; bool on, locked; const char* from; };   // locked: by us
struct Unit { DWORD chore, block; int first, count; bool on; };   // a chore block; its entries: s_patch[first, +count)
// only the locomotion (all of it is the chore Move, which plays the whole time the gun is out) and the combat idle;
// grips, finishers and get-ups that reuse an idle clip keep the player's own
static const char* kChores[] = { "Move", "PlayerIdle_Base_Combat" };
static DWORD s_choreCrc[2];
static Patch s_patch[192];
static int s_patchN;
static Unit s_unit[96];
static int s_unitN;
static DWORD s_csDef;             // the chore set of the plan (0: none)
static bool s_want;               // the stance is wanted (blaster drawn, RifleStance=1)
static int s_waiting;             // blocks still waiting for the player to leave them
static double s_nextTick;
static int s_applied, s_restored, s_missing, s_deferred;   // totals (dev status)

static bool Resolved(const Patch& p) { return p.wrap && *(BYTE*)(p.wrap + 0x18) != 0; }   // the block holds clips

// builds the plan for the player's chore set: every mapped entry of kChores, grouped by block; nothing is written
static int SafePlan(DWORD crc) {
    __try {
        const DWORD cs = s_findByCrc(s_reg, nullptr, crc);
        if (!cs) return -1;
        const DWORD csDef = *(DWORD*)(cs + 8);
        if (!csDef) return -1;
        s_csDef = csDef;
        const DWORD nc = *(DWORD*)(csDef + 0x0C), ca = *(DWORD*)(csDef + 0x10);
        for (DWORD i = 0; i < nc && i < 4096; i++) {
            const DWORD ch = *(DWORD*)(ca + i * 8 + 4);
            if (!ch) continue;
            const DWORD cid = *(DWORD*)(ch + 0x0C);   // chore def +0x0C = stricrc32(name)
            if (cid != s_choreCrc[0] && cid != s_choreCrc[1]) continue;
            const DWORD nb = *(DWORD*)(ch + 0x10), ba = *(DWORD*)(ch + 0x14);
            for (DWORD j = 0; j < nb && j < 512; j++) {
                const DWORD bd = *(DWORD*)(ba + j * 8 + 4);
                if (!bd) continue;
                Unit c = { cid, *(DWORD*)(bd + 0x0C), s_patchN, 0, false };   // block def +0x0C = its crc
                const DWORD na = *(DWORD*)(bd + 0x10), aa = *(DWORD*)(bd + 0x14), wrap = *(DWORD*)bd;
                for (DWORD k = 0; k < na && k < 64; k++) {
                    DWORD* en = (DWORD*)(aa + k * 16);
                    for (auto& m : kMap) {
                        if (en[0] != m.fromKey || !m.toKey) continue;
                        if (s_patchN >= (int)(sizeof s_patch / sizeof s_patch[0])) break;
                        Patch& p = s_patch[s_patchN++];
                        p.entry = en; p.wrap = wrap; p.newKey = m.toKey; p.on = p.locked = false;
                        p.from = m.from;
                        c.count++;
                        break;
                    }
                }
                if (c.count) {
                    if (s_unitN >= (int)(sizeof s_unit / sizeof s_unit[0])) { s_patchN = c.first; break; }
                    s_unit[s_unitN++] = c;
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("rifle stance: exception 0x%08X while planning", GetExceptionCode()); s_patchN = s_unitN = 0; return -1; }
    return s_patchN;
}

// one block's entries over to the trooper (on) or back; the block must not be playing
static void SafeSwitch(Unit& c, bool on) {
    __try {
        for (int i = c.first; i < c.first + c.count; i++) {
            Patch& p = s_patch[i];
            DWORD* en = p.entry;
            if (on && !p.on) {
                p.old[0] = en[0]; p.old[1] = en[1]; p.old[2] = en[2];
                if (Resolved(p)) {   // the block's clips are loaded: ours is locked and put in
                    void* data = s_lock(p.newKey, kAnimAssetType, nullptr);
                    if (!data) { s_missing++; continue; }
                    en[0] = p.newKey; en[1] = (DWORD)data;
                    const float len = -1.0f; memcpy(&en[2], &len, 4);
                    p.locked = true;
                } else {
                    en[0] = p.newKey;   // not loaded yet: the game locks our key itself when the block loads
                    p.locked = false;
                }
                p.on = true; s_applied++;
            } else if (!on && p.on) {
                if (en[0] == p.newKey) {
                    if (p.locked) {
                        en[0] = p.old[0]; en[1] = p.old[1]; en[2] = p.old[2];
                        s_unlock(p.newKey, kAnimAssetType);
                    } else if (Resolved(p)) {
                        // loaded by the game since, with our key locked: its own clip goes back in, locked the same way
                        void* data = s_lock(p.old[0], kAnimAssetType, nullptr);
                        if (!data) { s_missing++; continue; }   // retried; the set's unload unlocks whatever it holds
                        en[0] = p.old[0]; en[1] = (DWORD)data;
                        const float len = -1.0f; memcpy(&en[2], &len, 4);
                        s_unlock(p.newKey, kAnimAssetType);
                    } else en[0] = p.old[0];
                }
                p.on = p.locked = false; s_restored++;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("rifle stance: exception 0x%08X while switching a block", GetExceptionCode()); }
    c.on = on;
    for (int i = c.first; i < c.first + c.count; i++) if (s_patch[i].on != on) c.on = !on;   // (a missing clip: retried)
}

void* AnimCompOfRen(DWORD ren);
static void* PlayerAnimComp() { return AnimCompOfRen(PlayerRen()); }
void* AnimCompOfRen(DWORD ren) {   // the ren's animation object (ChoreManager; also the crouch pose owner)
    if (!ren || !s_renObjMgr || !s_getRenObj) return nullptr;
    __try {
        BYTE* base = *(BYTE**)sym::gRenTable;
        if (!base) return nullptr;
        BYTE* e = base + (ren & 0xFFFF) * 0x40;
        if (*(DWORD*)(e + 8) != ren) return nullptr;
        void* mgr = *s_renObjMgr;
        BYTE* o = mgr ? (BYTE*)s_getRenObj(mgr, nullptr, *(DWORD*)(e + 0x0C)) : nullptr;
        return o ? *(void**)(o + kAnimCompOff) : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static int SafePlaying(void* ac, const Unit& u) {   // 1 / 0, -1 = unknown
    __try { return s_blockPlaying(ac, nullptr, u.chore, u.block) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static void Forget() { s_patchN = s_unitN = 0; s_csDef = 0; s_waiting = 0; }

static DWORD SafeDef(DWORD crc) {   // the registered ChoreSet def of a group (0: none)
    __try { const DWORD cs = s_findByCrc(s_reg, nullptr, crc); return cs ? *(DWORD*)(cs + 8) : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static DWORD s_groupCrc;          // stricrc32(s_group)
static int s_gone;                // ticks the plan's set has read as gone / rebuilt in a row
static double s_planRetry;        // a failed plan is tried again from then on

static bool Plan() {
    if (RealNow() < s_planRetry) return false;
    const int n = SafePlan(s_groupCrc);
    if (n < 0) {
        if (!s_planRetry) Log("rifle stance: chore set %s not in the registry (tried again every second)", s_group);
        s_planRetry = RealNow() + 1.0;
        return false;
    }
    Log("rifle stance: %d animation entries in %d blocks of %s", n, s_unitN, s_group);
    s_planRetry = s_unitN ? 0 : 1e300;   // a set without any of our animations: nothing to do for this body
    return s_unitN > 0;
}

void HoldTick() {
    if (!s_unitN && !(s_want && s_groupCrc)) return;
    const double now = RealNow();
    if (now < s_nextTick) return;
    s_nextTick = now + 0.05;
    if (s_unitN) {
        if (SafeDef(s_groupCrc) != s_csDef) {
            if (++s_gone < 3) return;   // read again before it counts; nothing is written meanwhile
            Log("rifle stance: the chore set of %s was rebuilt or unloaded (its entries went with it)%s", s_group,
                s_want ? " - planned again" : "");
            Forget();
            s_gone = 0;
            if (!s_want) return;
        } else s_gone = 0;
    }
    if (!s_unitN && !Plan()) return;
    int todo = 0;
    for (int i = 0; i < s_unitN; i++) todo += s_unit[i].on != s_want;
    if (!todo) { s_waiting = 0; return; }
    void* ac = PlayerAnimComp();
    if (!ac) return;   // no player right now (load, respawn): later
    int waiting = 0;
    for (int i = 0; i < s_unitN; i++) {
        Unit& c = s_unit[i];
        if (c.on == s_want) continue;
        if (SafePlaying(ac, c) != 0) { waiting++; continue; }
        SafeSwitch(c, s_want);
    }
    if (waiting && waiting != s_waiting) s_deferred++;
    if (cfg.debug && waiting != s_waiting) Log("rifle stance: %d block(s) %s, %d waiting for the player to leave them",
                                             todo - waiting, s_want ? "on" : "restored", waiting);
    s_waiting = waiting;
}

void HoldRestore() {
    s_want = false;
    s_nextTick = 0;
    HoldTick();
}

void HoldApply() {
    if (!s_ok || !cfg.rifleStance || !PackHoldOk() || !PackChained()) { HoldRestore(); return; }
    static bool keyed;
    if (!keyed) {
        for (auto& m : kMap) { m.fromKey = s_pathToKey(m.from, false); m.toKey = s_pathToKey(m.to, false); }
        for (int i = 0; i < 2; i++) s_choreCrc[i] = s_strcrc(kChores[i]);
        keyed = true;
    }
    char group[160];
    strncpy_s(group, LuaCall("local ok, s = pcall(animation.GetChoreSet, ren.GetPlayer(0)) return ok and s or ''"), _TRUNCATE);
    if (!group[0]) { Log("rifle stance: the player has no chore set"); return; }
    if (_stricmp(group, s_group)) {
        if (s_unitN && SafeDef(s_groupCrc) == s_csDef) {
            // a body with another chore set (class outfit): the body that played the old set is gone with its
            // animation tree, so the old set goes back at once
            int left = 0;
            for (int i = 0; i < s_unitN; i++) { if (s_unit[i].on) SafeSwitch(s_unit[i], false); left += s_unit[i].on; }
            if (left) Log("rifle stance: %d blocks of %s could not be restored", left, s_group);
        }
        Forget();
        strncpy_s(s_group, group, _TRUNCATE);
        s_groupCrc = s_strcrc(s_group);
        s_planRetry = 0;
    }
    s_want = true;
    s_nextTick = 0;
    HoldTick();
}

const char* HoldStatus() {
    static char b[360];
    int on = 0, chOn = 0;
    for (int i = 0; i < s_patchN; i++) on += s_patch[i].on;
    for (int i = 0; i < s_unitN; i++) chOn += s_unit[i].on;
    _snprintf_s(b, _TRUNCATE, "stance %s, %d entries patched now (%d of %d blocks, %d waiting; %d applied, %d restored in total, "
                "%d missing, %d deferred), set %s",
                !s_ok ? "unavailable" : !cfg.rifleStance ? "off (RifleStance=0)" : !PackHoldOk() ? "off (animations not in the pack)"
                : s_want ? "on" : "holstered",
                on, chOn, s_unitN, s_waiting, s_applied, s_restored, s_missing, s_deferred, s_group[0] ? s_group : "-");
    return b;
}

// helpers for the aimed gun pose (dkb_aim.cpp)
bool HoldReady() { return s_ok; }
DWORD HoldCrc(const char* s) { return s_strcrc ? s_strcrc(s) : 0; }
DWORD HoldAnimKey(const char* anim) { return s_pathToKey ? s_pathToKey(anim, false) : 0; }
void* HoldLockAnim(DWORD key) { return s_lock(key, kAnimAssetType, nullptr); }
void HoldUnlockAnim(DWORD key) { s_unlock(key, kAnimAssetType); }
DWORD HoldSetDefOf(DWORD groupCrc) { return groupCrc && s_ok ? SafeDef(groupCrc) : 0; }
int HoldBlockPlaying(void* ac, DWORD chore, DWORD block) { Unit u = { chore, block, 0, 0, false }; return SafePlaying(ac, u); }

// dev: the blocks of the plan with their state (stance chores): chore/block crc, entries, on/off, playing, first clip
const char* HoldChores() {
    static char b[4000];
    void* ac = PlayerAnimComp();
    int n = _snprintf_s(b, _TRUNCATE, "%d blocks:", s_unitN);
    for (int i = 0; i < s_unitN && n > 0 && n < (int)sizeof b - 120; i++) {
        const Unit& u = s_unit[i];
        n += _snprintf_s(b + n, sizeof b - n, _TRUNCATE, " %08X/%08X/%d/%s/%s/%s", u.chore, u.block, u.count, u.on ? "on" : "off",
                         !ac ? "?" : SafePlaying(ac, u) == 1 ? "PLAYING" : "idle", s_patch[u.first].from);
    }
    return b;
}

}  // namespace dkb
