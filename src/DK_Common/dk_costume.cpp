// DK plugins for SWTFU: the player costume swap engine (see dk_costume.h): DK_Blaster's own, and DK_VaderSwap's under
// its Vader kit (Hooks).
//
// The engine has a live costume-swap path: PlayerMgr::UpdateCostumeChange (called once per frame from the frame
// function) consumes a request {pending, playerIdx, name} filed by PlayerMgr::RequestCostumeChange (no callers in
// retail): it chains "<name>.actor.xml", destroys the player ren and spawns the new actor at the same transform. The
// frame function's call is redirected through OnUpdate, which runs a waiting request at a safe moment and does what the
// dev path leaves out: the player bind (persistence, powers, HUD, input), the HUD caches of the old ren, the render /
// sector placement and the boss field. The player spawner's actor spawn is wrapped to learn the level's own outfit
// (DK_COSTUME_HOME). The persistent (saved) costume is never written: an outfit lasts until the level spawns the player.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "dk_costume.h"
#include "dk_plugin.h"
#include "dk_util.h"
#include "dk_syms.h"

using namespace dk;

typedef void(__thiscall* UpdateCostume_t)(void* mgr);
typedef void(__thiscall* RequestCostume_t)(void* mgr, int playerIdx, const char* name);
typedef DWORD(__thiscall* GetPlayerRen_t)(void* mgr, int playerIdx);
typedef DWORD(__thiscall* SpawnActor_t)(void* spawner, void* a1, DWORD actorKey);
typedef bool(__thiscall* ConsoleActive_t)(void* app);
typedef bool(__thiscall* MapOnStack_t)(void* input, const char* map, int controller);
typedef void*(__thiscall* GetComp_t)(void* mgr, DWORD ren);
typedef float(__thiscall* GetHealth_t)(void* healthField);
typedef DWORD(__cdecl* PathToKey_t)(const char* path, bool);
typedef bool(__thiscall* ChainPack_t)(void* catalog, const char* pack, int flags);
typedef void*(__cdecl* GetPlayerComp_t)(DWORD ren);
typedef void(__thiscall* BindPlayer_t)(void* comp, int playerIdx);
typedef BYTE*(__thiscall* GetPersist_t)(void* persistMgr, int playerIdx);
typedef void(__thiscall* NotifyTeleport_t)(void* renObj, int resetPlayerState);
typedef void(__thiscall* SectorCopy_t)(void* set, DWORD ren, const DWORD* srcMask);
typedef void(__thiscall* AttachSectors_t)(void* comp, DWORD srcRen);

static struct Game {
    UpdateCostume_t update; RequestCostume_t request; GetPlayerRen_t getPlayerRen; void** mgrVar;
    SpawnActor_t spawnActor; uint8_t* tickSite; uint8_t* spawnSite[2];
    int* stateVar; int* simVar; BYTE* cineFlag; BYTE* iyeFlag;
    void** appVar; ConsoleActive_t consoleActive; MapOnStack_t mapOnStack;
    void** healthMgrVar; GetComp_t getHealthComp; GetHealth_t getHealth;
    void** syncMgrVar; GetComp_t getSyncComp;
    GetPlayerComp_t getPlayerComp; BindPlayer_t bindPlayer;
    void** persistMgrVar; GetPersist_t getPersist;
    BYTE** renTable; NotifyTeleport_t notifyTeleport;
    SectorCopy_t sectorCopy; void** attachMgrVar; GetComp_t getAttach; AttachSectors_t attachSectors;
    PathToKey_t pathToKey; void** catalogVar; ChainPack_t chainPack;
    DWORD markerOff, bossOff;
} G;

namespace F = dk::sym::field;
static char s_dir[MAX_PATH];
static bool s_resolved, s_on;
static volatile LONG s_faulted;
static char s_problem[128] = "not resolved";
static dk::costume::Hooks s_hooks;

// ---------------------------------------------------------------------------------------------- HUD caches
// Some Anark HUD contracts (UIUpdaterMiniGameSW = QTE button prompts, ForcePoints, Player, Position) cache the player
// ren in their init slot (this+off = GetPlayerRen). The game re-runs that init only at level start, so after a swap they
// would keep the destroyed ren (QTE prompts never appear). The init slots are hooked to learn the instances; after a swap
// every cache that still holds the old ren is re-pointed. (File scope: the naked trampolines address s_hud directly.)
struct HudHook { const char* name; DWORD* vtSlot; DWORD vtable; DWORD orig; BYTE off; void* inst[4]; int n; };
static HudHook s_hud[6];   // orig at +12: the trampolines jump through it
static int s_hudN;
static void __cdecl HudRecord(int i, void* self) {
    HudHook& h = s_hud[i];
    for (int k = 0; k < h.n; k++) if (h.inst[k] == self) return;
    if (h.n < 4) h.inst[h.n++] = self;
}
#define DK_HUD_TRAMP(i) static __declspec(naked) void HudTramp##i() { __asm { push ecx } __asm { push ecx } \
    __asm { push i } __asm { call HudRecord } __asm { add esp, 8 } __asm { pop ecx } __asm { jmp dword ptr [s_hud + i * TYPE HudHook + 12] } }
DK_HUD_TRAMP(0) DK_HUD_TRAMP(1) DK_HUD_TRAMP(2) DK_HUD_TRAMP(3) DK_HUD_TRAMP(4) DK_HUD_TRAMP(5)
static void (*const s_hudTramps[6])() = {HudTramp0, HudTramp1, HudTramp2, HudTramp3, HudTramp4, HudTramp5};

namespace dk { namespace costume {

static ULONGLONG Now() { return GetTickCount64(); }
static DWORD U32(const void* p) { DWORD v; memcpy(&v, p, 4); return v; }
static DWORD RelTarget(const uint8_t* call) { return (DWORD)(call + 5) + (int)U32(call + 1); }
static bool In(const Range& r, DWORD va) { return va >= (DWORD)r.lo && va < (DWORD)r.hi; }
// first match of a short pattern in [b, b+n) ("8B 86 ?? ?? 00 00"), nullptr if none
static uint8_t* FindIn(uint8_t* b, DWORD n, const char* pat) {
    int bytes[32], len = 0;
    for (const char* p = pat; *p && len < 32;) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (*p == '?') { bytes[len++] = -1; p += p[1] == '?' ? 2 : 1; }
        else { bytes[len++] = (int)strtoul(p, nullptr, 16); p += 2; }
    }
    for (uint8_t* q = b; q + len <= b + n; q++) {
        int i = 0;
        for (; i < len; i++) if (bytes[i] >= 0 && q[i] != (uint8_t)bytes[i]) break;
        if (i == len) return q;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------- SEH-guarded game calls
static void Fault(const char* where, DWORD code) {
    if (InterlockedExchange(&s_faulted, 1) != 0) return;
    if (s_hooks.fault) s_hooks.fault(where, code);
    else Log("costume: exception 0x%08X in %s - costume swaps off for this session", code, where);
}
static DWORD SafePlayerRen() {
    __try { void* m = *G.mgrVar; return m ? G.getPlayerRen(m, 0) : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { Fault("GetPlayerRen", GetExceptionCode()); return 0; }
}
static bool SafeRequest(const char* name) {
    __try { void* m = *G.mgrVar; if (!m) return false; G.request(m, 0, name); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { Fault("RequestCostumeChange", GetExceptionCode()); return false; }
}
static bool SafeUpdate(void* mgr) {
    __try { G.update(mgr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { Fault("UpdateCostumeChange", GetExceptionCode()); return false; }
}
// The dev path spawns the new player but never runs the player fix-up the level spawner and Mp.FixupPlayerSpawn do;
// without it the new PlayerComp has no persistence and the next update crashes. The fix-up also runs the checkpoint
// respawn branch (a teleport to the persistence's checkpoint marker): the marker is blanked for the call.
static bool SafeFixup(DWORD ren) {
    __try {
        void* c = G.getPlayerComp(ren); if (!c) return false;
        void* pm = *G.persistMgrVar;
        BYTE* per = pm ? G.getPersist(pm, 0) : nullptr;
        DWORD saved = per ? *(DWORD*)(per + G.markerOff) : 0;
        if (per) *(DWORD*)(per + G.markerOff) = 0;
        G.bindPlayer(c, 0);
        if (per) *(DWORD*)(per + G.markerOff) = saved;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { Fault("player fix-up", GetExceptionCode()); return false; }
}
static BYTE* RenObjOf(DWORD ren) {   // ren handle -> ren object, nullptr if stale
    BYTE* e = *G.renTable + (ren & 0xFFFF) * 0x40;
    return *(DWORD*)(e + F::RenEntry::ren) == ren ? *(BYTE**)(e + F::RenEntry::renObj) : nullptr;
}
// The new actor is created in place, but its render / sector placement is only refreshed when it moves (it stayed
// invisible until the player walked). The bind's teleport branch would do it; it is skipped above, so do it here.
static void SafeNotifyTeleport(DWORD ren) {
    __try { if (BYTE* o = RenObjOf(ren)) G.notifyTeleport(o, 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { Fault("notify teleport", GetExceptionCode()); }
}
// Rendering sectors: a ren's sector set is a 64-bit mask in its ren object. A freshly spawned ren gets its set from a
// lookup by position, which where sectors overlap can pick another sector (the level drawn dark and smeared until the
// player walks through a portal). The new player gets the old one's set, as location.MoveToSameSectorAs does it.
static bool SafeSectorMask(DWORD ren, DWORD out[2]) {
    __try { BYTE* o = RenObjOf(ren); if (!o) return false; out[0] = *(DWORD*)(o + F::RenObj::sectorSet); out[1] = *(DWORD*)(o + F::RenObj::sectorSet + 4); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool SafeSetSectorMask(DWORD ren, const DWORD mask[2]) {
    __try {
        BYTE* o = RenObjOf(ren); if (!o) return false;
        G.sectorCopy(o + F::RenObj::sectorSet, ren, mask);
        void* m = *G.attachMgrVar;
        void* c = m ? G.getAttach(m, ren) : nullptr;
        if (c) G.attachSectors(c, ren);   // weapon and other attachments: the player's sectors
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { Fault("sector copy", GetExceptionCode()); return false; }
}
// A cutscene: the cinematic flag set and not for an In-Your-Ear radio dialogue (that one plays during gameplay).
static bool CutsceneNow() { return *G.cineFlag != 0 && !(G.iyeFlag && *G.iyeFlag); }
static bool SafeCine() {
    __try { return CutsceneNow(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static DWORD SafeBossOf(DWORD ren) {
    __try { BYTE* c = (BYTE*)G.getPlayerComp(ren); return c ? *(DWORD*)(c + G.bossOff) : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static void SafeSetBoss(DWORD ren, DWORD boss) {
    __try { BYTE* c = (BYTE*)G.getPlayerComp(ren); if (c && !*(DWORD*)(c + G.bossOff)) *(DWORD*)(c + G.bossOff) = boss; }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static float SafeHealthOf(DWORD ren) {   // -1 = no health component
    __try {
        void* hm = *G.healthMgrVar;
        void* hc = hm ? G.getHealthComp((BYTE*)hm + F::HealthMgr::compMgr, ren) : nullptr;
        return hc ? G.getHealth((BYTE*)hc + F::HealthComp::health) : -1.0f;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1.0f; }
}
static DWORD SafeKey(const char* path) {
    __try { return G.pathToKey(path, false); }
    __except (EXCEPTION_EXECUTE_HANDLER) { Fault("FilePathToAssetKey", GetExceptionCode()); return 0; }
}
static bool SafeChainActorPack(const char* actor) {   // LevelPacks\<actor>.actor.xml_pc.lp into the level's catalog
    __try {
        void* cat = *G.catalogVar; if (!cat) return false;
        char pack[80]; sprintf_s(pack, "%s.actor.xml", actor);
        return G.chainPack(cat, pack, 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { Fault("ChainAssetPack", GetExceptionCode()); return false; }
}
// The fight script sets the boss on the player component and nothing clears it when the boss dies, so a boss only
// counts while its ren still has health left.
DWORD ActiveBoss(DWORD ren) {
    DWORD b = ren ? SafeBossOf(ren) : 0;
    return b && SafeHealthOf(b) > 0.0f ? b : 0;
}

// Why it is not safe to swap now (nullptr = safe). 'ren' = the current player.
const char* UnsafeReason(DWORD ren) {
    __try {
        if (*G.stateVar != 2) return "not in gameplay (menu/pause/loading)";
        if (*G.simVar != 0) return "game paused";
        if (CutsceneNow()) return "cinematic playing";
        void* cat = *G.catalogVar;
        if (!cat) return "asset catalog missing";
        if (*((BYTE*)cat + F::AssetCatalog::dynLoading)) return "level data still streaming";
        void* app = *G.appVar;
        if (!app) return "no app";
        if (G.consoleActive(app)) return "Lua console open";
        if (!ren) return "no player";
        const float hp = SafeHealthOf(ren);
        if (hp < 0.0f) return "player has no health component";
        if (hp <= 0.0f) return "player is dead";
        void* input = *(void**)app;
        if (input && G.mapOnStack(input, "playerMiniGame", 0)) return "QTE / minigame active";
        void* sm = *G.syncMgrVar;
        void* sc = sm ? G.getSyncComp(sm, ren) : nullptr;
        if (sc && *(DWORD*)((BYTE*)sc + F::SyncObj::partner)) return "finishing move / grapple in progress";
        return nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { Fault("state check", GetExceptionCode()); return "exception"; }
}

static void RefreshHud(DWORD oldRen, DWORD newRen) {
    for (int i = 0; i < s_hudN; i++) {
        HudHook& h = s_hud[i];
        for (int k = 0; k < h.n; k++) {
            __try {
                BYTE* o = (BYTE*)h.inst[k];
                if (*(DWORD*)o == h.vtable && *(DWORD*)(o + h.off) == oldRen) *(DWORD*)(o + h.off) = newRen;
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
}

// ---------------------------------------------------------------------------------------------- costume names
// Actor names from LevelPacks\*.actor.xml_pc.lp and their asset keys, to name the level's own outfit. Hashing needs
// ScumLib's asset catalog up: read at Install, or with Hooks::lateNames at the first frame / spawn.
struct ActorName { char name[48]; DWORD key; };
static const int kNameCap = 512;
static ActorName* s_names; static volatile LONG s_nameN;
static bool s_namesRead;
static void BuildNameTable() {
    if (s_namesRead) return;
    s_namesRead = true;
    s_names = (ActorName*)calloc(kNameCap, sizeof(ActorName));
    if (!s_names) return;
    char pat[MAX_PATH]; sprintf_s(pat, "%sLevelPacks\\*.actor.xml_pc.lp", s_dir);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) { Log("costume: no costume packs in LevelPacks"); return; }
    do {
        const char* e = strstr(fd.cFileName, ".actor.xml_pc.lp");
        if (!e || e - fd.cFileName >= 47 || s_nameN >= kNameCap) continue;
        ActorName& a = s_names[s_nameN];
        memcpy(a.name, fd.cFileName, e - fd.cFileName); a.name[e - fd.cFileName] = 0;
        char full[64]; sprintf_s(full, "%s.actor.xml", a.name);
        a.key = SafeKey(full);
        if (a.key) s_nameN++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
const char* NameForKey(DWORD key) {
    for (int i = 0; i < s_nameN; i++) if (s_names[i].key == key) return s_names[i].name;
    return nullptr;
}
DWORD KeyOf(const char* name) { char full[80]; sprintf_s(full, "%s.actor.xml", name); return SafeKey(full); }
void AddName(const char* actor) {
    BuildNameTable();
    const size_t len = actor ? strlen(actor) : 0;
    const DWORD key = len && len < sizeof s_names[0].name ? KeyOf(actor) : 0;
    if (!s_names || !key || NameForKey(key) || s_nameN >= kNameCap) return;
    ActorName& a = s_names[s_nameN];
    strcpy_s(a.name, actor); a.key = key;
    InterlockedIncrement(&s_nameN);   // (readers on other threads see the entry complete)
}
int NameCount() { return s_nameN; }
static bool PackThere(const char* actor) {
    char p[MAX_PATH]; sprintf_s(p, "%sLevelPacks\\%s.actor.xml_pc.lp", s_dir, actor);
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}

// ---------------------------------------------------------------------------------------------- swap state
static DWORD s_naturalKey;      // the actor the level spawned last (HOME)
static DWORD s_curKey, s_curRen;
static ULONGLONG s_renSince, s_lastSwap, s_lastCine;
static bool s_spawnSeen;        // the level spawner produced the player since the last frame
static unsigned s_lastSwapMs;
static int s_swapCount;
static char s_swapBase[64];     // a request's base pack, chained by the next DoSwap
// the request queue (dk_costume_api.h): one waits at a time, the newest replaces it
static SRWLOCK s_lock = SRWLOCK_INIT;
static char s_req[64], s_reqBase[64]; static int s_reqFlags;
static LONG s_seq, s_done; static bool s_ok;
static ULONGLONG s_since;
static char s_result[96] = "-";
static bool s_extActive;        // the player wears a caller's EXTERNAL outfit
static char s_extName[64]; static DWORD s_extKey;
static ULONGLONG s_extBeat;     // the outfit owner's last state read (it is alive)
static const ULONGLONG kSettleMs = 300, kSpawnSettleMs = 1500, kCineSettleMs = 3000, kOwnerAliveMs = 3000;

const char* HomeName() {
    if (s_hooks.home) return s_hooks.home();
    if (s_naturalKey) if (const char* n = NameForKey(s_naturalKey)) return n;
    return "player";
}
const char* CurName() {
    if (s_extKey && s_curKey == s_extKey) return s_extName;
    const char* n = NameForKey(s_curKey);
    return n ? n : "";
}

static bool DoSwap(void* mgr, const char* name, DWORD ren) {
    Log("costume: swapping player to %s.actor.xml", name);
    s_extActive = false;
    const DWORD boss = SafeBossOf(ren);
    DWORD sec0[2] = {};
    const bool hs = SafeSectorMask(ren, sec0) && (sec0[0] | sec0[1]);
    if (s_hooks.preSwap && !s_hooks.preSwap(name, ren)) { s_swapBase[0] = 0; return false; }
    if (s_swapBase[0]) {
        const bool chained = SafeChainActorPack(s_swapBase);
        Log("costume: base pack %s for %s: %s", s_swapBase, name, chained ? "chained" : "already chained / not chained");
        s_swapBase[0] = 0;
    }
    if (!SafeRequest(name)) return false;
    const ULONGLONG t0 = Now();
    if (!SafeUpdate(mgr)) return true;   // chains the costume pack, removes the old player ren, spawns the new one
    const DWORD nr = SafePlayerRen();
    if (nr && nr != ren) {
        if (!SafeFixup(nr)) Log("costume: player fix-up failed for the new ren %08X", nr);
        RefreshHud(ren, nr);
        SafeNotifyTeleport(nr);
        DWORD sec1[2] = {};
        if (hs && SafeSectorMask(nr, sec1) && (sec1[0] != sec0[0] || sec1[1] != sec0[1])) {
            const DWORD sp[2] = { sec1[0], sec1[1] };
            if (SafeSetSectorMask(nr, sec0) && SafeSectorMask(nr, sec1))
                Log("costume: sector: the spawn put the new player in %08X%08X - kept the old player's %08X%08X", sp[1], sp[0], sec1[1], sec1[0]);
        }
        if (boss) SafeSetBoss(nr, boss);   // (a finished fight's boss: the field is never cleared)
        s_curKey = KeyOf(name); s_curRen = nr; s_renSince = s_lastSwap = Now();
        s_lastSwapMs = (unsigned)(Now() - t0);
        s_swapCount++;
        Log("costume: swap done: now %s (%u ms)", name, s_lastSwapMs);
        if (s_hooks.postSwap) s_hooks.postSwap(name, ren, nr, boss);
    } else Log("costume: swap FAILED for %s (costume pack missing or spawn refused); player unchanged", name);
    return true;
}
bool Swap(void* mgr, const char* name, DWORD ren) { return DoSwap(mgr, name, ren); }

static void Finish(LONG seq, bool ok, const char* result) {
    AcquireSRWLockExclusive(&s_lock);
    if (s_seq == seq) { s_done = seq; s_ok = ok; strncpy_s(s_result, result, _TRUNCATE); }
    ReleaseSRWLockExclusive(&s_lock);
    if (!ok) Log("costume: request %ld refused: %s", seq, result);
}
// waits (not refusals): a swap just ran, a level spawn or a cinematic just ended
static const char* WaitReason(ULONGLONG t) {
    const ULONGLONG spawnSettle = s_hooks.spawnSettleMs ? s_hooks.spawnSettleMs : kSpawnSettleMs;
    if (t - s_lastSwap < kSettleMs) return "swap cooling down";
    if (s_renSince > s_lastSwap && t - s_renSince < spawnSettle) return "player just spawned";
    if (t - s_lastCine < kCineSettleMs) return "camera settling after a cinematic";
    return nullptr;
}
// game thread: run the waiting request if it is safe. Returns true when the game's update already ran.
static bool Process(void* mgr, DWORD ren) {
    char name[64], base[64]; int flags; LONG seq; ULONGLONG since;
    AcquireSRWLockShared(&s_lock);
    const bool waiting = s_done != s_seq;
    strcpy_s(name, s_req); strcpy_s(base, s_reqBase); flags = s_reqFlags; seq = s_seq; since = s_since;
    ReleaseSRWLockShared(&s_lock);
    if (!waiting) return false;
    const bool home = !strcmp(name, DK_COSTUME_HOME);
    char target[64]; strcpy_s(target, home ? HomeName() : name);
    const char* why = UnsafeReason(ren);
    if (!why && ActiveBoss(ren)) why = "boss fight in progress";
    if (why) { Finish(seq, false, why); return false; }
    const ULONGLONG t = Now();
    if (const char* w = WaitReason(t)) {
        if (t - since > (ULONGLONG)kDkCostumeWaitMs) Finish(seq, false, w);
        return false;
    }
    const DWORD key = KeyOf(target);
    if (!key) { Finish(seq, false, "unknown costume"); return false; }
    const bool ext = !home && (flags & DK_COSTUME_EXTERNAL);
    if (key == s_curKey) {
        s_extActive = ext; if (ext) { strcpy_s(s_extName, target); s_extKey = key; }
        Finish(seq, true, "already"); return false;
    }
    Log("costume: request %ld: %s%s%s%s", seq, target, home ? " (home)" : "", base[0] ? " on base " : "", base);
    strcpy_s(s_swapBase, home ? "" : base);
    const bool ran = DoSwap(mgr, target, ren);
    s_swapBase[0] = 0;
    if (s_curKey == key) {
        s_extActive = ext; if (ext) { strcpy_s(s_extName, target); s_extKey = key; }
        Finish(seq, true, "ok");
    } else Finish(seq, false, "swap failed (costume pack missing or spawn refused)");
    return ran;
}

// ---------------------------------------------------------------------------------------------- hooks
// the frame function's call to PlayerMgr::UpdateCostumeChange (game thread, once per frame)
static void __fastcall OnUpdate(void* mgr, void*) {
    if (s_on && !s_faulted) {
        BuildNameTable();   // (Hooks::lateNames: the first frame)
        const DWORD ren = SafePlayerRen();
        if (SafeCine()) s_lastCine = Now();
        if (ren != s_curRen) {
            const DWORD was = s_curRen;
            // replaced outside the level spawner (a scripted swap): actor unknown
            const bool outside = ren && !s_spawnSeen && (was || s_hooks.unknownAfterNone);
            if (outside) {
                Log("costume: player ren changed outside the spawner (%08X -> %08X)", was, ren);
                s_curKey = 0;
            }
            s_curRen = ren; s_renSince = Now();
            if (ren) s_spawnSeen = false;
            if (s_hooks.renChanged) s_hooks.renChanged(was, ren, outside);
        }
        if (s_hooks.frame && s_hooks.frame(mgr, ren, false)) return;
        if (s_done != s_seq && ren && Process(mgr, ren)) return;
        if (s_hooks.frame && s_hooks.frame(mgr, ren, true)) return;
    }
    G.update(mgr);
}
// PlayerSpawnerComp's actor spawn (level start, death / checkpoint respawn): the level's own outfit
static DWORD __fastcall OnSpawnActor(void* spawner, void*, void* a1, DWORD key) {
    if (s_hooks.preSpawn && s_on && !s_faulted) s_hooks.preSpawn(key);
    const DWORD r = G.spawnActor(spawner, a1, key);
    if (!s_on || s_faulted) return r;
    BuildNameTable();
    bool extOwner = false;
    if (r) {
        s_naturalKey = s_curKey = key; s_spawnSeen = true;
        extOwner = s_extActive && Now() - s_extBeat < kOwnerAliveMs;
        if (!s_hooks.postSpawn) {
            const char* n = NameForKey(key);
            Log("costume: player spawned by the level as %s (key %08X)%s", n ? n : "?", key,
                s_extActive ? " - the outfit's owner puts its outfit back or asks for home" : "");
        }
        s_extActive = false;
    }
    if (s_hooks.postSpawn) s_hooks.postSpawn(key, r, extOwner);
    return r;
}

// ---------------------------------------------------------------------------------------------- interface
static int __cdecl ApiRequest(const char* name, const char* base, int flags) {
    auto refuse = [](const char* why) {
        AcquireSRWLockExclusive(&s_lock); strncpy_s(s_result, why, _TRUNCATE); ReleaseSRWLockExclusive(&s_lock);
        return 0;
    };
    static const char kOk[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
    if (!name || !*name) return refuse("no costume name");
    const bool home = !strcmp(name, DK_COSTUME_HOME);
    const size_t len = strlen(name);
    if (!home && (len >= sizeof s_req || strspn(name, kOk) != len)) return refuse("costume name: letters, digits and _");
    if (!s_on || s_faulted || !s_namesRead) return refuse(s_faulted ? "costume swaps are off (a game call failed)" : "costume swaps are not active yet");
    if (!base) base = "";
    const size_t blen = strlen(base);
    if (blen >= sizeof s_reqBase || strspn(base, kOk) != blen) return refuse("base costume name: letters, digits and _");
    if (!home && !PackThere(name)) return refuse("costume pack not installed");
    if (!home && blen && !PackThere(base)) return refuse("base costume pack not installed");
    AcquireSRWLockExclusive(&s_lock);
    const LONG id = ++s_seq;
    strcpy_s(s_req, name); strcpy_s(s_reqBase, base); s_reqFlags = flags; s_since = Now();
    strcpy_s(s_result, "waiting");
    ReleaseSRWLockExclusive(&s_lock);
    return id;
}
static int State(DK_CostumeState* st) {
    if (!st || st->size < (int)sizeof(DK_CostumeState)) return 0;
    AcquireSRWLockShared(&s_lock);
    st->api = DK_COSTUME_API; st->seq = s_seq; st->done = s_done; st->ok = s_ok;
    strcpy_s(st->result, s_result);
    ReleaseSRWLockShared(&s_lock);
    st->external = s_extActive; st->swapMs = s_lastSwapMs; st->curKey = s_curKey; st->swaps = s_swapCount;
    strcpy_s(st->cur, CurName());
    strcpy_s(st->home, HomeName());
    return 1;
}
static int __cdecl ApiState(DK_CostumeState* st) {   // the caller's read: its EXTERNAL outfit's owner is alive
    s_extBeat = Now();
    return State(st);
}
static const DK_CostumeApi s_api = { DK_COSTUME_API, &ApiRequest, &ApiState };
const DK_CostumeApi* Api() { return &s_api; }
void Peek(DK_CostumeState* st) { State(st); }

void SetHooks(const Hooks& h) { s_hooks = h; }
void Off() { InterlockedExchange(&s_faulted, 1); }
DWORD PlayerRen() { return SafePlayerRen(); }
DWORD CurKey() { return s_curKey; }
DWORD CurRen() { return s_curRen; }
DWORD NaturalKey() { return s_naturalKey; }
ULONGLONG RenSince() { return s_renSince; }
ULONGLONG LastSwap() { return s_lastSwap; }
ULONGLONG LastCine() { return s_lastCine; }

#define NEED(x, what) do { if (!(x)) { sprintf_s(s_problem, "%s not found", what); Log("costume: %s - costume swaps off", s_problem); return false; } } while (0)

bool Resolve(HMODULE exe, const char* gameDir) {
    strcpy_s(s_dir, gameDir);
    const Range t = TextRange(exe), rd = SectionRange(exe, ".rdata");
    NEED(t.lo && rd.lo, "exe sections");
    HMODULE scum = GetModuleHandleA("ScumLibLEC_Retail.dll");
    NEED(scum, "ScumLibLEC_Retail.dll");
    G.pathToKey = (PathToKey_t)GetProcAddress(scum, "?FilePathToAssetKey@AssetCatalog@@SAIQBD_N@Z");
    G.catalogVar = (void**)GetProcAddress(scum, "?gpAssetCatalog@@3PAVAssetCatalog@@A");
    G.chainPack = (ChainPack_t)GetProcAddress(scum, "?ChainAssetPack@AssetCatalog@@QAE_NPBDW4AssetPackChainFlags@@@Z");
    NEED(G.pathToKey && G.catalogVar && G.chainPack, "ScumLib AssetCatalog exports");
    const struct { const char* sym; void** to; } kSyms[] = {
        {"PlayerMgr_UpdateCostumeChange", (void**)&G.update}, {"PlayerMgr_RequestCostumeChange", (void**)&G.request},
        {"PlayerMgr_GetPlayerRen", (void**)&G.getPlayerRen}, {"gPlayerMgr", (void**)&G.mgrVar},
        {"Tick_UpdateCostumeChangeCall", (void**)&G.tickSite},
        {"PlayerSpawnerComp_SpawnActorWithKey", (void**)&G.spawnActor},
        {"PlayerSpawner_SpawnActorCall1", (void**)&G.spawnSite[0]}, {"PlayerSpawner_SpawnActorCall2", (void**)&G.spawnSite[1]},
        {"gGameState", (void**)&G.stateVar}, {"gSimState", (void**)&G.simVar},
        {"Cine_PlayingFlag", (void**)&G.cineFlag}, {"Cine_InYourEarFlag", (void**)&G.iyeFlag},
        {"gApp", (void**)&G.appVar}, {"App_ConsoleActive", (void**)&G.consoleActive}, {"Input_IsMapOnStack", (void**)&G.mapOnStack},
        {"gHealthMgr", (void**)&G.healthMgrVar}, {"HealthMgr_GetComp", (void**)&G.getHealthComp}, {"Health_Get", (void**)&G.getHealth},
        {"gSyncMgr", (void**)&G.syncMgrVar}, {"SyncMgr_Get", (void**)&G.getSyncComp},
        {"PlayerMgr_Find", (void**)&G.getPlayerComp}, {"PlayerComp_Bind", (void**)&G.bindPlayer},
        {"gPersistMgr", (void**)&G.persistMgrVar}, {"Persistence_Lookup", (void**)&G.getPersist},
        {"gRenTable", (void**)&G.renTable}, {"Ren_NotifyTeleport", (void**)&G.notifyTeleport},
        {"SectorSet_CopyFrom", (void**)&G.sectorCopy}, {"gAttachMgr", (void**)&G.attachMgrVar},
        {"AttachMgr_GetComp", (void**)&G.getAttach}, {"AttachComp_MoveToSameSectorAs", (void**)&G.attachSectors},
    };
    for (const auto& s : kSyms) {
        *s.to = FindSym(t, s.sym);
        NEED(*s.to, s.sym);
    }
    // the call sites must still call the game's own functions (another plugin's redirect is checked again at Install)
    NEED(G.tickSite[0] == 0xE8 && G.tickSite[-6] == 0x8B && G.tickSite[-5] == 0x0D && U32(G.tickSite - 4) == (DWORD)G.mgrVar,
         "UpdateCostumeChange call site");
    for (uint8_t* s : G.spawnSite) NEED(s[0] == 0xE8, "player spawner call site");
    // offsets the swap writes, checked against the code that uses them (the database values are for 1.2.94564)
    uint8_t* mk = FindIn((uint8_t*)G.bindPlayer, 0x400, "8B 86 ?? ?? 00 00 8B 88 ?? ?? 00 00");
    NEED(mk && U32(mk + 8) == F::Persistence::checkpointMarker, "checkpoint marker field in the player bind");
    G.markerOff = U32(mk + 8);
    uint8_t* ub = FindSym(t, "UIUpdaterBoss_Update");
    uint8_t* bp = ub ? FindIn(ub, 0x40, "E8 ?? ?? ?? ?? 83 C4 04 85 C0 0F 84 ?? ?? ?? ?? 8B 80") : nullptr;
    NEED(bp && RelTarget(bp) == (DWORD)G.getPlayerComp && U32(bp + 18) == F::PlayerComp::boss, "boss field (UIUpdaterBoss_Update)");
    G.bossOff = U32(bp + 18);
    // HUD caches: each init function's vtable slot(s) in .rdata and the offset it stores the player ren at
    static const struct { const char* sym; int slot; } kHud[] = {
        {"UIUpdaterForcePoints_Init", 1}, {"UIUpdaterMiniGame_Init", 1}, {"UIUpdaterPlayer_Init", 3}, {"UIUpdaterPosition_Init", 1}};
    char mp[40]; const DWORD mv = (DWORD)G.mgrVar;
    sprintf_s(mp, "8B 0D %02X %02X %02X %02X", mv & 0xFF, (mv >> 8) & 0xFF, (mv >> 16) & 0xFF, mv >> 24);
    for (const auto& k : kHud) {
        uint8_t* fn = FindSym(t, k.sym);
        if (!fn) continue;
        uint8_t* end = fn;   // the body up to its ret / int3 padding
        while (end < fn + 0x60 && end[0] != 0xC3 && !(end[0] == 0xC2 && end[2] == 0x00) && !(end[0] == 0xCC && end[1] == 0xCC)) end++;
        uint8_t* m = FindIn(fn, (DWORD)(end - fn), mp);
        if (!m || m + 12 > end || m[7] != 0xE8 || RelTarget(m + 7) != (DWORD)G.getPlayerRen) { Log("costume: %s: no player ren cache", k.sym); continue; }
        uint8_t* st = FindIn(m + 12, (DWORD)(end - (m + 12)) < 16 ? (DWORD)(end - (m + 12)) : 16, "89 46");
        if (!st) { Log("costume: %s: no player ren store", k.sym); continue; }
        int found = 0;
        for (DWORD* q = (DWORD*)(((DWORD)rd.lo + 3) & ~3u); (uint8_t*)(q + 1) <= rd.hi && s_hudN < 6; q++) {
            if (*q != (DWORD)fn) continue;
            DWORD* vt = q - k.slot;
            if ((uint8_t*)vt <= rd.lo || !In(rd, vt[-1]) || !In(t, vt[0])) continue;   // vtable: RTTI locator before it
            HudHook& h = s_hud[s_hudN++];
            h.name = k.sym; h.vtSlot = q; h.vtable = (DWORD)vt; h.orig = (DWORD)fn; h.off = st[2];
            found++;
        }
        if (!found) Log("costume: %s: vtable slot not found", k.sym);
    }
    if (s_hudN < 4) Log("costume: %d HUD caches found (QTE prompts may not follow a swap)", s_hudN);
    s_problem[0] = 0;
    s_resolved = true;
    Log("costume: engine resolved (UpdateCostumeChange %08X call %08X, spawn calls %08X %08X, marker +%X, boss +%X, %d HUD caches)",
        (DWORD)G.update, (DWORD)G.tickSite, (DWORD)G.spawnSite[0], (DWORD)G.spawnSite[1], G.markerOff, G.bossOff, s_hudN);
    return true;
}

bool Install(const DK_LoaderApi* api, const char* handoffKey) {
    if (s_on) return true;
    if (!s_resolved) return false;
    // nobody else redirected these calls since Resolve (DK_VaderSwap and DK_Blaster's own engine patch the same ones)
    if (RelTarget(G.tickSite) != (DWORD)G.update || RelTarget(G.spawnSite[0]) != (DWORD)G.spawnActor ||
        RelTarget(G.spawnSite[1]) != (DWORD)G.spawnActor) {
        strcpy_s(s_problem, "another plugin redirected the costume calls");
        Log("costume: %s - costume swaps off", s_problem);
        s_resolved = false;
        return false;
    }
    if (!s_hooks.lateNames) BuildNameTable();
    if (!RedirectCall(G.tickSite, (void*)&OnUpdate)) { strcpy_s(s_problem, "patching the costume call failed"); Log("costume: %s", s_problem); s_resolved = false; return false; }
    if (!RedirectCall(G.spawnSite[0], (void*)&OnSpawnActor) || !RedirectCall(G.spawnSite[1], (void*)&OnSpawnActor))
        Log("costume: spawner call not patched - HOME is the default player outfit");
    for (int i = 0; i < s_hudN; i++) {
        const DWORD tr = (DWORD)s_hudTramps[i];
        if (*s_hud[i].vtSlot != s_hud[i].orig || !WriteMem(s_hud[i].vtSlot, &tr, 4)) { Log("costume: HUD slot %p not hooked", s_hud[i].vtSlot); s_hud[i].vtSlot = nullptr; }
    }
    if (Handoff* h = api && handoffKey ? (Handoff*)api->GetShared(handoffKey) : nullptr) {
        api->SetShared(handoffKey, nullptr);
        if (h->version == 1) {
            s_naturalKey = h->naturalKey; s_curKey = h->curKey; s_curRen = h->curRen; s_spawnSeen = true;
            s_extActive = h->extActive; s_extKey = h->extKey; strcpy_s(s_extName, h->extName); s_extBeat = Now();
            s_swapCount = h->swaps; s_lastSwapMs = h->lastSwapMs; s_renSince = Now();
            if (h->waiting) { strcpy_s(s_req, h->req); strcpy_s(s_reqBase, h->reqBase); s_reqFlags = h->reqFlags; s_seq = 1; s_since = Now(); strcpy_s(s_result, "waiting"); }
            for (auto& o : h->hud)
                for (int i = 0; i < s_hudN; i++)
                    if (o.vtSlot && s_hud[i].vtSlot == o.vtSlot) { memcpy(s_hud[i].inst, o.inst, sizeof o.inst); s_hud[i].n = o.n; }
            Log("costume: state taken over from the previous instance (wearing %s, %s%s)", CurName()[0] ? CurName() : "?",
                s_extActive ? "an external outfit" : "the player's own", h->waiting ? ", a request waiting" : "");
        }
        HeapFree(GetProcessHeap(), 0, h);
    }
    s_on = true;
    if (s_namesRead) Log("costume: engine installed (%d costume packs named, home %s)", NameCount(), HomeName());
    else Log("costume: engine installed (costume names at the first frame)");
    return true;
}

bool Resolved() { return s_resolved; }
bool Installed() { return s_on && !s_faulted; }
const char* Problem() { return s_faulted ? "a game call failed (see the log)" : s_problem; }

void Shutdown(const DK_LoaderApi* api, const char* handoffKey) {
    if (!s_on) return;
    s_on = false;   // requests stop before the hooks go (dk::RestorePatches)
    if (!api || !handoffKey) return;
    Handoff* h = (Handoff*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Handoff));
    if (!h) return;
    h->version = 1;
    h->naturalKey = s_naturalKey; h->curKey = s_curKey; h->curRen = s_curRen;
    h->extActive = s_extActive; h->extKey = s_extKey; strcpy_s(h->extName, s_extName);
    h->swaps = s_swapCount; h->lastSwapMs = s_lastSwapMs;
    AcquireSRWLockShared(&s_lock);
    h->waiting = s_done != s_seq;
    strcpy_s(h->req, s_req); strcpy_s(h->reqBase, s_reqBase); h->reqFlags = s_reqFlags;
    ReleaseSRWLockShared(&s_lock);
    for (int i = 0; i < s_hudN; i++) { h->hud[i].vtSlot = s_hud[i].vtSlot; memcpy(h->hud[i].inst, s_hud[i].inst, sizeof h->hud[i].inst); h->hud[i].n = s_hud[i].n; }
    if (void* prev = api->GetShared(handoffKey)) HeapFree(GetProcessHeap(), 0, prev);
    api->SetShared(handoffKey, h);
    Log("costume: state handed over (%s%s)", CurName()[0] ? CurName() : "?", h->waiting ? ", a request waiting" : "");
}

const char* Status() {
    static char b[256];
    sprintf_s(b, "engine %s natural %s cur %s ext %d req %ld/%ld ok %d '%s' swaps %d last %u ms hud %d names %d",
              s_on ? (s_faulted ? "faulted" : "on") : (s_resolved ? "resolved" : "off"), HomeName(), CurName()[0] ? CurName() : "?",
              (int)s_extActive, s_done, s_seq, (int)s_ok, s_result, s_swapCount, s_lastSwapMs, s_hudN, NameCount());
    return b;
}

}}  // namespace dk::costume
