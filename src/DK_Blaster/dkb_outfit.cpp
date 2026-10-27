// DK_Blaster: trooper classes - the outfit, two guns and a special of every blaster trooper in the game.
// Outfits are the game's own player costumes plus six NPC troopers converted on the first start (dkb_pack.cpp). The
// swap runs in the shared costume engine (DK_Common\dk_costume.cpp: the game's own costume change with the fix-ups a
// swap needs), installed at the first frame - or, when DK_VaderSwap is installed too, in DK_VaderSwap's engine (both
// hook the same game call: one owner of the player's costume). Both answer through dk_costume_api.h. Health is kept
// across a swap (the new body would otherwise start at full health). Without an engine the classes carry loadouts only.
//
// Picking: ] / [ (Back + D-pad right / left) next / previous class, a tap of T the last one, holding T (Back + D-pad
// up) the Holo-Requisition wheel (slow motion, pages Imperial / Rebel / Specialist, favourites), Tab / Y in the wheel
// the Requisition terminal (loadout editing, live outfit preview). At most one swap per 0.3 s: requests in between
// replace each other and the newest one runs. Every refusal says why (HUD line + sound).
// With OutfitFollows (default) the outfit belongs to blaster mode: drawing the blaster puts the class outfit on;
// holstering, death, the emergency reset and unloading the plugin put the player's own back. Only the player's choices
// (last class, favourites, loadouts) are kept, in DK_Blaster_Classes.ini; the mode itself is never saved.
#include "dkb.h"
#include "dkb_render.h"
#include "dk_costume.h"
#include <stdio.h>
#include <math.h>

using namespace dk;

namespace dkb {

bool OutfitConvReady(const char* actor);
bool PackSwapFxOk();
int OutfitConvState();
const char* OutfitConvStatus();

enum { P_IMPERIAL, P_REBEL, P_SPECIALIST, P_COUNT };
static const char* const kPageNames[P_COUNT] = { "IMPERIAL", "REBEL", "SPECIALIST" };

struct ClassDef {
    const char* id;            // ini / dev name
    const char* code;          // wheel glyph
    const char* name;
    int page;
    const char* actor;         // costume actor (LevelPacks\<actor>.actor.xml_pc.lp)
    const char* base;          // converted outfit: the costume pack it builds on; nullptr = one of the game's costumes
    const char* gun1; const char* gun2; const char* special;
    const char* line;
};
static const char* kConvBase = "playerStormTrooper";
static const ClassDef kClasses[] = {
    { "storm", "ST", "Stormtrooper", P_IMPERIAL, "playerStormTrooper", nullptr, "e11", "e11s", "rapid",
      "The Emperor's fist. Steady aim, endless numbers." },
    { "shadow", "SH", "Shadow Trooper", P_IMPERIAL, "playerShadowTrooper", nullptr, "e11", "dh17", "scan",
      "Black armour for the Emperor's quiet work." },
    { "commander", "TC", "Trooper Commander", P_IMPERIAL, "playerTrooperCommander", nullptr, "e11", "e11s", "overcharge",
      "Leads from the front. Makes every shot count." },
    { "scout", "SC", "Scout Trooper", P_IMPERIAL, "playerScoutTrooper", nullptr, "dlt20a", "e11s", "scan",
      "Eyes of the Empire. Long rifle, longer sight." },
    { "snow", "SN", "Snowtrooper", P_IMPERIAL, "playerSnowTrooper", nullptr, "e11", "e11s", "stim",
      "Cold-assault infantry. Patches up and pushes on." },
    { "kashyyyk", "KT", "Kashyyyk Trooper", P_IMPERIAL, "playerKashyyykTrooper", nullptr, "e11", "bowcaster", "rapid",
      "Jungle camouflage. Carries a Wookiee trophy." },
    { "jump", "JT", "Jumptrooper", P_IMPERIAL, "playerJumpTrooper", nullptr, "e11", "e11s", "jump",
      "Rocket-pack infantry. Strikes from above." },
    { "evo", "EV", "EVO Trooper", P_IMPERIAL, "playerEVOTrooper", nullptr, "fc11", "e11s", "stim",
      "Sealed hazard armour for the galaxy's worst worlds." },
    { "shadowevo", "SE", "Shadow EVO Trooper", P_IMPERIAL, "playerShadowEVOTrooper", nullptr, "fc11", "dh17", "overcharge",
      "Black-ops hazard trooper. Close and lethal." },
    { "officer", "IO", "Imperial Officer", P_IMPERIAL, "playerImperialOfficer", nullptr, "dh17", "e11s", "scan",
      "Commands the field. Marks every target." },
    { "gunner", "GN", "Imperial Gunner", P_IMPERIAL, "playerGunner", nullptr, "z6", "e11s", "rapid",
      "Heavy weapons specialist. Suppresses everything." },
    { "incinerator", "IN", "Incinerator Trooper", P_IMPERIAL, "playerIncinerator", nullptr, "fc11", "e11", "overcharge",
      "Clears bunkers, jungles and rebels alike." },
    { "protorebel", "RF", "Rebel Fighter", P_REBEL, "playerProtoRebel", nullptr, "dh17", "dl44", "stim",
      "One of the first to stand against the Empire." },
    { "protoelite", "RE", "Rebel Elite", P_REBEL, "playerProtoRebelElite", nullptr, "z6", "dh17", "rapid",
      "Veteran of the first strikes. Holds any line." },
    { "saboteur", "SB", "Rebel Saboteur", P_REBEL, "playerProtoRebelSaboteur", nullptr, "dh17", "a295", "overcharge",
      "In, out, and the target is rubble." },
    { "veteran", "VT", "Rebel Veteran", P_REBEL, "playerVeteran", nullptr, "a295", "dh17", "stim",
      "Seen every front. Still standing." },
    { "hothgear", "HC", "Hoth Commando", P_REBEL, "playerHoth", nullptr, "a295", "dl44", "scan",
      "Cold-weather gear of Echo Base." },
    { "hothsoldier", "HS", "Hoth Rebel Soldier", P_REBEL, "dk_hothRebelSoldier", kConvBase, "a295", "dh17", "rapid",
      "Echo Base infantry. Holds the trench." },
    { "hothelite", "HE", "Hoth Rebel Elite", P_REBEL, "dk_hothRebelElite", kConvBase, "z6", "a295", "overcharge",
      "Heavy gunner of the Hoth defence." },
    { "pilot", "PL", "Rebel Pilot", P_REBEL, "dk_hothRebelPilot", kConvBase, "dl44", "dh17", "stim",
      "Grounded, never beaten. Quick on the draw." },
    { "sniper", "SP", "Rebel Sniper", P_REBEL, "dk_hothRebelSniper", kConvBase, "dlt20a", "dl44", "scan",
      "One shot from the ridge, then gone." },
    { "impcomndo", "CM", "Imperial Commando", P_SPECIALIST, "playerImpComndo", nullptr, "e11", "e11s", "jump",
      "Special forces of the Imperial Navy." },
    { "clone", "CL", "Clone Trooper", P_SPECIALIST, "playerCloneTrooper", nullptr, "a295", "dh17", "overcharge",
      "A soldier of the old Republic, still fighting." },
    { "jango", "JF", "Jango Fett", P_SPECIALIST, "playerJangoFett", nullptr, "dl18", "dl44", "jump",
      "The template of an army. Jetpack ready." },
    { "weequay", "WQ", "Weequay Mercenary", P_SPECIALIST, "dk_tatWeequay", kConvBase, "dl18", "dl44", "stim",
      "Hired gun of the Tatooine underworld." },
    { "tusken", "TR", "Tusken Sniper", P_SPECIALIST, "dk_tatTuskenSniper", kConvBase, "cycler", "dl18", "scan",
      "Patient. Unseen. Deadly at any range." },
};
static const int kN = sizeof kClasses / sizeof kClasses[0];

struct Loadout { const WeaponDef* g[2]; const char* special; };
static Loadout s_load[kN];
static bool s_fav[kN], s_avail[kN];
static int s_cur = 0, s_prev = -1;         // the selected class (blaster mode uses it) and the one before
static int s_slot;                          // 0 primary, 1 secondary
static int s_worn = -1;                     // class whose outfit the player wears (-1 = his own)
static int s_want = -1;                     // outfit to reach (-1 = his own)
static int s_keep = -1;                     // OutfitFollows=0: the outfit the player chose to wear in any mode
static int s_reqId, s_reqFor = -2;          // the request in flight (0 = none) and its target
static double s_reqT, s_doneT = -10.0, s_okT = -10.0;
static int s_swaps, s_refusals, s_requests, s_rearms;
static unsigned s_lastSwapMs, s_maxSwapMs;
static char s_lastResult[96] = "-";
static bool s_noApiWarned, s_inited, s_dirty, s_apiLogged;
static float s_preHp = -1.0f, s_preMax = -1.0f;   // the player's health just before the swap in flight
static int s_hpKept;                               // swaps whose new body got the old body's health
static const char kHandoff[] = "DK_Blaster.costume.state";
static char s_ini[MAX_PATH];
static int s_convSeen;
static int s_seenSwaps = -1, s_foreign;   // DK_VaderSwap's swap count as last seen (a change we did not ask for = someone else's)
static const double kSwapCooldown = 0.3;

static float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static float EaseOut(float t) { t = Clamp01(t); return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }
static int ById(const char* id) {
    for (int i = 0; i < kN; i++) if (id && !_stricmp(kClasses[i].id, id)) return i;
    return -1;
}

// ---------------------------------------------------------------------------------------------- costume engine
static const DK_CostumeApi* VaderSwapApi() {
    if (!g_api) return nullptr;
    const DK_CostumeApi* a = (const DK_CostumeApi*)g_api->GetShared(DK_COSTUME_SHARED);
    return a && a->api == DK_COSTUME_API && a->request && a->state ? a : nullptr;
}
static const DK_CostumeApi* Api() { return costume::Installed() ? costume::Api() : VaderSwapApi(); }
static const char* EngineName() { return costume::Installed() ? "own" : VaderSwapApi() ? "vaderswap" : "none"; }
// DK_VaderSwap at the loader: 'L' loaded, 'R' in a reload (unloading / not loaded yet), 0 = not installed
static char VaderSwapAtLoader() {
    if (!g_api || !g_api->ListPlugins) return 0;
    char b[2048];
    if (g_api->ListPlugins(b, sizeof b) <= 0) return 0;
    for (const char* l = b; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : nullptr)
        if (!_strnicmp(l, "DK_VaderSwap.dll ", 17)) return !strncmp(l + 17, "loaded ", 7) ? 'L' : 'R';
    return 0;
}
// First frames: DK_VaderSwap (loaded after us, plugins load in name order) has published its engine by now if it is
// installed and active; otherwise ours goes in. Ours patched first switches DK_VaderSwap off (it needs the same call),
// so a hot reload of DK_VaderSwap is waited out: the loader runs frames between its unload and the new load.
static void EngineCheck() {
    static double waitFrom = -1.0;
    if (costume::Installed() || !costume::Resolved() || VaderSwapApi()) { waitFrom = -1.0; return; }
    const char vs = VaderSwapAtLoader();
    if (vs == 'R') {
        const double now = RealNow();
        if (waitFrom < 0.0) { waitFrom = now; Log("outfits: DK_VaderSwap is being reloaded - waiting for its costume engine"); }
        if (now - waitFrom < 10.0) return;
    }
    waitFrom = -1.0;
    if (costume::Install(g_api, kHandoff))
        Log("outfits: own costume engine installed (%s)", vs == 'L' ? "DK_VaderSwap is loaded with its costume engine off" :
            vs ? "DK_VaderSwap did not come back from its reload in 10 s" : "DK_VaderSwap is not installed");
}
static bool ApiState(DK_CostumeState* st) {
    memset(st, 0, sizeof *st);
    st->size = sizeof *st;
    const DK_CostumeApi* a = Api();
    if (!a) return false;
    __try { return a->state(st) == 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static int ApiRequest(const char* name, const char* base) {
    const DK_CostumeApi* a = Api();
    if (!a) return 0;
    __try { return a->request(name, base, DK_COSTUME_EXTERNAL); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static bool PackThere(const char* actor) {
    char p[MAX_PATH]; sprintf_s(p, "%sLevelPacks\\%s.actor.xml_pc.lp", g_dir, actor);
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}
static void CheckAvail() {
    int n = 0;
    for (int i = 0; i < kN; i++) {
        const ClassDef& c = kClasses[i];
        s_avail[i] = c.base ? OutfitConvReady(c.actor) && PackThere(c.actor) && PackThere(c.base) : PackThere(c.actor);
        n += s_avail[i];
    }
    Log("classes: %d/%d outfits available (converted: %s)", n, kN, OutfitConvStatus());
}

// ---------------------------------------------------------------------------------------------- choices (ini)
static void DefaultLoadout(int i) {
    s_load[i].g[0] = WeaponById(kClasses[i].gun1);
    s_load[i].g[1] = WeaponById(kClasses[i].gun2);
    s_load[i].special = kClasses[i].special;
}
static const char* SpecialIdOf(const char* s) {
    for (int k = 0; k < AbilitySpecialCount(); k++) if (s && !_stricmp(AbilitySpecialId(k), s)) return AbilitySpecialId(k);
    return nullptr;
}
static void LoadChoices() {
    sprintf_s(s_ini, "%sDK_Blaster_Classes.ini", g_dir);
    char v[512];
    for (int i = 0; i < kN; i++) DefaultLoadout(i);
    GetPrivateProfileStringA("Classes", "Last", "storm", v, sizeof v, s_ini);
    const int last = ById(v);
    s_cur = last >= 0 ? last : 0;
    GetPrivateProfileStringA("Classes", "Favourites", "", v, sizeof v, s_ini);
    char* ctx = nullptr;
    for (char* t = strtok_s(v, ", ", &ctx); t; t = strtok_s(nullptr, ", ", &ctx)) { int k = ById(t); if (k >= 0) s_fav[k] = true; }
    for (int i = 0; i < kN; i++) {   // Loadouts: <class>=<primary>,<secondary>,<special>
        GetPrivateProfileStringA("Loadouts", kClasses[i].id, "", v, sizeof v, s_ini);
        if (!v[0]) continue;
        char a[3][32] = {};
        char* c2 = nullptr; int k = 0;
        for (char* t = strtok_s(v, ", ", &c2); t && k < 3; t = strtok_s(nullptr, ", ", &c2)) strncpy_s(a[k++], t, _TRUNCATE);
        if (const WeaponDef* w = WeaponById(a[0])) s_load[i].g[0] = w;
        if (const WeaponDef* w = WeaponById(a[1])) s_load[i].g[1] = w;
        if (const char* s = SpecialIdOf(a[2])) s_load[i].special = s;
    }
}
static bool Custom(int i) {
    return s_load[i].g[0] != WeaponById(kClasses[i].gun1) || s_load[i].g[1] != WeaponById(kClasses[i].gun2) ||
           _stricmp(s_load[i].special, kClasses[i].special) != 0;
}
static void SaveChoices() {
    if (!s_ini[0]) return;
    s_dirty = false;
    WritePrivateProfileStringA("Classes", "Last", kClasses[s_cur].id, s_ini);
    char v[512] = {};
    for (int i = 0; i < kN; i++) if (s_fav[i]) { if (v[0]) strcat_s(v, ","); strcat_s(v, kClasses[i].id); }
    WritePrivateProfileStringA("Classes", "Favourites", v, s_ini);
    for (int i = 0; i < kN; i++) {
        if (!Custom(i)) { WritePrivateProfileStringA("Loadouts", kClasses[i].id, nullptr, s_ini); continue; }
        sprintf_s(v, "%s,%s,%s", s_load[i].g[0]->id, s_load[i].g[1]->id, s_load[i].special);
        WritePrivateProfileStringA("Loadouts", kClasses[i].id, v, s_ini);
    }
}

// ---------------------------------------------------------------------------------------------- loadout
static const WeaponDef* SlotGun(int i, int slot) {
    const WeaponDef* w = s_load[i].g[slot & 1];
    return w ? w : WeaponById("e11s");
}
static void ApplyLoadout(bool announce, bool gun = true) {
    if (!cfg.loadoutFollows) { AbilitySetSpecial(nullptr); return; }
    AbilitySetSpecial(s_load[s_cur].special);
    if (gun && g_mode == M_BLASTER) {
        const WeaponDef* w = SetWeapon(SlotGun(s_cur, s_slot));
        if (announce && w) Log("class %s: %s + %s", kClasses[s_cur].id, w->name, AbilitySpecialTitle(s_load[s_cur].special));
    }
}
const WeaponDef* OutfitWeapon() { return cfg.loadoutFollows ? SlotGun(s_cur, s_slot) : nullptr; }
const char* OutfitWornId() { return s_worn >= 0 ? kClasses[s_worn].id : ""; }
int OutfitClass() { return s_cur; }

// ---------------------------------------------------------------------------------------------- swap requests
static double s_fxT = -10.0;   // last swap's flash (real time)
static int s_fxClass = -1;
static int s_fxVfx, s_fxSnd;
static void SwapFx(int target) {   // the requisition flash on the new body: effect (with our swap cue), light, HUD beam
    s_fxT = RealNow(); s_fxClass = target;
    SoundsEnsure();
    const char* r = PackSwapFxOk() && PackChained()
        ? LuaCall("local p = ren.GetPlayer(0) if not p then return 'noplayer' end "
                  "vfx.TriggerVfxLight(p, 0.7, 0.04, 3.0, 1.0, math.Vector(0.55, 0.85, 1.0), 0.6, 5.0) "
                  "return tostring(vfx.Trigger('dk_swap_flash.vfx', p))")
        : "nofx";
    if (!strcmp(r, "true")) s_fxVfx++;
    else { Snd("swap"); s_fxSnd++; if (cfg.debug) Log("swap flash: %s (sound only)", r); }
}
static const char* Friendly(const char* r) {
    static char b[96];
    strncpy_s(b, r && *r ? r : "not possible right now", _TRUNCATE);
    if (b[0] >= 'a' && b[0] <= 'z') b[0] = (char)(b[0] - 32);
    return b;
}
static bool Swapping() { return s_reqId != 0 || RealNow() - s_okT < 1.5; }
bool OutfitOwnsRenChange() { return Swapping(); }

// The new body starts at full health: it gets the old body's health fraction (no free heal from a class change).
static void KeepHealth() {
    const DWORD p = PlayerRen();
    const float hp = RenHealth(p), mx = RenMaxHealth(p);
    if (s_preHp <= 0.0f || s_preMax <= 0.0f || hp < 0.0f || mx <= 0.0f) return;
    const float want = s_preHp / s_preMax * mx;
    if (fabsf(hp - want) < 0.5f) return;
    const char* r = LuaCall("local p = ren.GetPlayer(0) if not p then return 'noplayer' end game.OverrideHealth(p, %.2f) "
                            "return string.format('%%.0f', game.GetHealth(p))", want);
    s_hpKept++;
    Log("outfit: health kept %.0f/%.0f -> %s/%.0f (the new body had %.0f)", s_preHp, s_preMax, r, mx, hp);
}
static void Pump(double now) {
    DK_CostumeState st;
    const bool have = ApiState(&st);
    if (have && !s_apiLogged) {   // the engine is there: what the player wears now (a hot reload keeps the engine's state)
        s_apiLogged = true;
        s_seenSwaps = st.swaps;
        if (st.external && st.done == st.seq && !s_reqId)
            for (int i = 0; i < kN; i++) if (!_stricmp(st.cur, kClasses[i].actor)) { s_worn = s_want = i; s_keep = cfg.outfitFollows ? -1 : i; }
        Log("outfits: costume engine ready (%s, v%u), wearing %s", EngineName(), (unsigned)Api()->api, s_worn >= 0 ? kClasses[s_worn].id : "own outfit");
    }
    if (s_reqId) {
        if (!have) { s_reqId = 0; s_reqFor = -2; s_doneT = now; return; }   // DK_VaderSwap reloaded / left
        if (st.done < s_reqId) {
            const DWORD p = PlayerRen();   // the health the swap (in this frame's game update) has to keep
            s_preHp = RenHealth(p); s_preMax = RenMaxHealth(p);
            if (now - s_reqT > kDkCostumeWaitMs / 1000.0 + 3.0) {
                Log("outfit request %d: no answer in %.1f s - given up", s_reqId, now - s_reqT);
                s_reqId = 0; s_reqFor = -2; s_doneT = now; s_want = s_worn;
            }
            return;
        }
        const int target = s_reqFor;
        s_reqId = 0; s_reqFor = -2; s_doneT = now;
        s_seenSwaps = st.swaps;
        strncpy_s(s_lastResult, st.result, _TRUNCATE);
        if (st.ok) {
            s_worn = target;
            if (strcmp(st.result, "already")) {
                s_swaps++; s_okT = now; s_lastSwapMs = st.swapMs;
                if (st.swapMs > s_maxSwapMs) s_maxSwapMs = st.swapMs;
                KeepHealth();
                SwapFx(target);
                Log("outfit: now %s (%u ms)", target < 0 ? "the player's own" : kClasses[target].name, st.swapMs);
            }
        } else {
            s_refusals++;
            char b[128]; sprintf_s(b, "Outfit unchanged - %s", Friendly(st.result));
            Refuse(b);
            if (s_want == target) s_want = s_worn;   // no retry loop; a newer wish stays
        }
        return;
    }
    if (have && s_seenSwaps != st.swaps) {   // a swap we did not ask for (DK_VaderSwap's own toggle): it stands
        if (s_seenSwaps >= 0) {
            int w = -1;
            if (st.external) for (int i = 0; i < kN; i++) if (!_stricmp(st.cur, kClasses[i].actor)) w = i;
            s_foreign++;
            Log("outfit: the player's costume was changed elsewhere (now %s) - left as it is", st.cur[0] ? st.cur : "?");
            s_worn = s_want = w; s_keep = -1; s_doneT = now;
        }
        s_seenSwaps = st.swaps;
    }
    if (s_want == s_worn || now - s_doneT < kSwapCooldown) return;   // the newest wish waits out the cooldown
    if (!have) {
        if (!s_noApiWarned) {
            s_noApiWarned = true;
            HudToast("Trooper outfits are unavailable - classes change guns only", 0xFF9A3C, 2.6f);
            Log("outfits: no costume engine (%s) - loadouts only", costume::Problem()[0] ? costume::Problem() : "not installed");
        }
        s_want = s_worn;
        return;
    }
    const char* name = s_want < 0 ? DK_COSTUME_HOME : kClasses[s_want].actor;
    const char* base = s_want < 0 ? nullptr : kClasses[s_want].base;
    { const DWORD p = PlayerRen(); s_preHp = RenHealth(p); s_preMax = RenMaxHealth(p); }
    if (s_want < 0 && s_preHp <= 0.0f && s_preHp != -1.0f) {   // home on a fallen body: refused anyway, and the respawn
        s_want = s_worn;                                        // brings the own outfit back - no "Player is dead" toast
        return;
    }
    const int id = ApiRequest(name, base);
    s_requests++;
    if (!id) {
        ApiState(&st);
        s_refusals++; s_doneT = now;
        char b[128]; sprintf_s(b, "Outfit unchanged - %s", Friendly(st.result));
        Refuse(b);
        s_want = s_worn;
        return;
    }
    s_reqId = id; s_reqFor = s_want; s_reqT = now;
    if (cfg.debug) Log("outfit request %d: %s%s%s", id, name, base ? " on " : "", base ? base : "");
}

// ---------------------------------------------------------------------------------------------- selecting
static void Want(int i) {
    s_want = i;
    if (!cfg.outfitFollows) s_keep = i;
}
static bool OutfitAllowed(int i, bool quiet) {
    if (i < 0 || !Api()) return true;
    if (s_avail[i]) return true;
    if (!quiet) {
        char b[128];
        sprintf_s(b, "%s: outfit not installed%s", kClasses[i].name, kClasses[i].base ? " (built at the next game start)" : "");
        Refuse(b);
    }
    return false;
}
// pick class i: loadout now, the outfit through the request queue
static bool Select(int i, const char* why, bool toast) {
    if (i < 0 || i >= kN) return false;
    if (!OutfitAllowed(i, false)) return false;
    if (i != s_cur) { s_prev = s_cur; s_cur = i; s_slot = 0; s_dirty = true; }
    ApplyLoadout(true);
    if (!cfg.outfitFollows || g_mode == M_BLASTER) Want(i);
    if (toast) {
        char b[160];
        const WeaponDef* g0 = SlotGun(i, 0);
        if (g_mode == M_BLASTER || !cfg.outfitFollows) sprintf_s(b, "%s  -  %s / %s", kClasses[i].name, g0->name, AbilitySpecialTitle(s_load[i].special));
        else sprintf_s(b, "%s  -  ready for the next draw", kClasses[i].name);
        HudToast(b, 0x7CE0FF, 1.6f);
    }
    Log("class: %s (%s)", kClasses[i].id, why);
    return true;
}
static int Step(int dir) {
    bool favs = false;
    if (cfg.cycleFavourites) for (int i = 0; i < kN; i++) favs |= s_fav[i] && OutfitAllowed(i, true);
    for (int k = 1; k <= kN; k++) {
        const int i = ((s_cur + dir * k) % kN + kN) % kN;
        if (!OutfitAllowed(i, true) || (favs && !s_fav[i])) continue;
        return i;
    }
    return s_cur;
}

// mode hooks (dkb_main.cpp)
void OutfitModeEnter() {
    ApplyLoadout(false, false);   // the gun: ModeEnter arms the body with it (a re-arm used to load it twice)
    if (cfg.outfitFollows && OutfitAllowed(s_cur, true)) s_want = s_cur;
}
void OutfitModeExit() {   // holstered / died / emergency / unloading: the player's own outfit
    if (cfg.outfitFollows) s_want = -1;
}
void OutfitRespawned() {   // a level spawn (death, checkpoint, level change): the player is in his own outfit again
    s_worn = -1;
    s_reqId = 0; s_reqFor = -2;
    s_want = cfg.outfitFollows ? -1 : s_keep;
    if (s_want >= 0) Log("respawn: wearing %s again", kClasses[s_want].name);
}
void OutfitRearmed() { s_rearms++; }
void OutfitSwapWeapon() {
    s_slot ^= 1;
    const WeaponDef* w = SetWeapon(SlotGun(s_cur, s_slot));
    Snd("equip");
    if (w) { char b[96]; sprintf_s(b, "%s", w->name); HudToast(b, 0xFFFFFF, 0.9f); }
}

// ---------------------------------------------------------------------------------------------- time scale
static uint32_t s_tsGlobal, s_tsOff;
static float s_tsSaved = -1.0f, s_tsSet = 1.0f, s_tsTarget = 1.0f;
static bool s_tsOwned;
static float* TsPtr() {
    if (!s_tsGlobal) return nullptr;
    BYTE* app = nullptr;
    if (!SafeRead((const void*)s_tsGlobal, &app, 4) || !app) return nullptr;
    return (float*)(app + s_tsOff);
}
static float TsGet() { float v = 1.0f; float* p = TsPtr(); if (!p || !SafeRead(p, &v, 4) || !(v > 0.0f && v < 10.0f)) v = 1.0f; return v; }
static void TsWrite(float v) { if (float* p = TsPtr()) SafeWrite(p, &v, 4); }
static void TsTick(float realDt, bool menu, float target) {
    if (!s_tsGlobal) return;
    if (menu && cfg.wheelSlowmo) {
        if (!s_tsOwned) { s_tsSaved = TsGet(); s_tsSet = s_tsSaved; s_tsOwned = true; }
        else if (fabsf(TsGet() - s_tsSet) > 0.001f) { s_tsOwned = false; return; }   // the game took it over: leave it
        s_tsTarget = target * s_tsSaved;
    } else if (s_tsOwned) {
        if (fabsf(TsGet() - s_tsSet) > 0.001f) { s_tsOwned = false; return; }
        s_tsTarget = s_tsSaved;
    } else return;
    s_tsSet += (s_tsTarget - s_tsSet) * (1.0f - expf(-14.0f * realDt));
    if (!menu && fabsf(s_tsSet - s_tsSaved) < 0.01f) { s_tsSet = s_tsSaved; TsWrite(s_tsSet); s_tsOwned = false; return; }
    TsWrite(s_tsSet);
}
static void TsRestoreNow() {
    if (s_tsOwned) { if (fabsf(TsGet() - s_tsSet) < 0.001f) TsWrite(s_tsSaved); s_tsOwned = false; }
}

// ---------------------------------------------------------------------------------------------- menus
enum { MN_NONE, MN_WHEEL, MN_TERM };
static int s_menu;
static double s_menuT;                       // opened (real time)
static double s_closeT = -10.0; static int s_closedMenu;
static int s_page, s_hover = -1;             // wheel: hovered class (-1 = centre)
static float s_selX, s_selY;                 // wheel: virtual cursor (unit circle)
static bool s_sticky;                        // opened from the controller: stays open until A / B
static double s_wheelDownT = -1.0;           // T went down (kbm): tap = last class, hold = wheel
static int s_termSel, s_termField;           // terminal: row (class index), focused field (0 list, 1-3 loadout)
static int s_termOrigWant, s_termOrigCur;    // what to go back to on cancel
static double s_termRestT;                   // the selection changed (live preview after a rest)
static float s_termScroll;                   // mouse counts toward the next row
static int s_hoverMoves, s_wheelOpens, s_termOpens, s_deploys;
static bool s_tabWas;
static double s_keyRep[4];                   // terminal arrow-key repeat

static int PageCount(int page) { int n = 0; for (auto& c : kClasses) n += c.page == page; return n; }
static int PageIndex(int page, int k) {      // k-th class of a page
    for (int i = 0; i < kN; i++) if (kClasses[i].page == page && k-- == 0) return i;
    return -1;
}
static int SlotOf(int i) { int k = 0; for (int j = 0; j < i; j++) k += kClasses[j].page == kClasses[i].page; return k; }
static void AimAt(int i) {   // the wheel's virtual cursor onto a class's slot
    s_page = kClasses[i].page;
    const int n = PageCount(s_page);
    const float ang = n > 0 ? SlotOf(i) * 6.2831853f / n : 0.0f;
    s_selX = sinf(ang) * 0.8f; s_selY = -cosf(ang) * 0.8f; s_hover = i;
}

// the game's own HUD (health, Force, minimap) fades out under the terminal and back after it
static bool s_hudHidden, s_hudRestore;
static void GameHud(bool show) {
    if (!show) {
        if (!s_hudHidden) { s_hudHidden = true; s_hudRestore = false; LuaCall("ui.hudFadeTo(0, 0.12) return 'ok'"); }
        return;
    }
    if (!s_hudHidden) return;
    s_hudHidden = false;
    if (InGameplay()) LuaCall("ui.hudFadeTo(100, 0.2) return 'ok'");
    else s_hudRestore = true;   // closed by a cutscene / load: once gameplay is back
}

static const char* MenuRefusal() {
    if (!cfg.enabled) return "Blaster Combat is off (MOD OPTIONS)";
    if (!InGameplay()) return "Not available here";
    return nullptr;
}
static void Open(int menu, bool sticky) {
    if (const char* r = MenuRefusal()) { Refuse(r); return; }
    if (s_menu == MN_NONE) { s_termOrigWant = s_want; s_termOrigCur = s_cur; }
    s_menu = menu; s_menuT = RealNow(); s_sticky = sticky;
    InputCapture(true);
    if (menu == MN_WHEEL) {
        s_wheelOpens++;
        s_page = kClasses[s_cur].page; s_hover = s_cur; s_selX = s_selY = 0.0f;
    } else {
        s_termOpens++;
        s_termSel = s_hover >= 0 ? s_hover : s_cur; s_page = kClasses[s_termSel].page;
        s_termField = 0; s_termRestT = RealNow(); s_termScroll = 0.0f;
        GameHud(false);
    }
    Snd("open");
    Log("%s opened (%s)", menu == MN_WHEEL ? "class wheel" : "requisition terminal", sticky ? "controller" : "keyboard");
}
// leaving the terminal any way but a deploy puts the class / outfit back that was on when it opened (the preview)
static void Close(bool sound, bool deployed = false) {
    if (s_menu == MN_NONE) return;
    if (s_menu == MN_TERM && !deployed) {
        s_want = s_termOrigWant; s_cur = s_termOrigCur; ApplyLoadout(false);
    }
    s_closedMenu = s_menu; s_closeT = RealNow();
    s_menu = MN_NONE;
    InputCapture(false);
    GameHud(true);
    if (sound) Snd("close");
}
static void Deploy(int i, const char* why) {
    s_deploys++;
    const bool ok = Select(i, why, true);
    Close(false, ok);
    if (!ok) return;
    Snd("ready");
    if (cfg.outfitFollows && g_mode != M_BLASTER) BlasterOn(why);   // deploying from the saber draws the blaster
    SaveChoices();
}
static void Cancel() { Close(true); }
static void ToggleFav(int i) {
    if (i < 0) return;
    s_fav[i] = !s_fav[i]; s_dirty = true;
    Snd(s_fav[i] ? "ready" : "move");
    SaveChoices();
}
static void PageStep(int d) {
    s_page = (s_page + d + P_COUNT) % P_COUNT;
    Snd("move");
    if (s_menu == MN_WHEEL) { s_hover = -1; s_selX = s_selY = 0.0f; }
    else { s_termSel = PageIndex(s_page, 0); s_termRestT = RealNow(); s_termField = 0; }
}

static void WheelInput(float realDt) {
    const WORD pp = g_in.padPressed;
    if (g_in.pressed[A_NEXT] || (pp & 0x0200)) PageStep(1);
    if (g_in.pressed[A_PREV] || (pp & 0x0100)) PageStep(-1);
    // pointer: mouse counts move a virtual cursor; the right stick points straight at a slot
    float mx, my; InputMenuLook(&mx, &my);
    s_selX += mx / 140.0f; s_selY += my / 140.0f;
    float len = sqrtf(s_selX * s_selX + s_selY * s_selY);
    if (len > 1.0f) { s_selX /= len; s_selY /= len; len = 1.0f; }
    float ax = s_selX, ay = -s_selY, al = len;
    const float sl = sqrtf(g_in.padRX * g_in.padRX + g_in.padRY * g_in.padRY);
    if (sl > 0.5f) { ax = g_in.padRX; ay = g_in.padRY; al = 1.0f; s_selX = ax / sl; s_selY = -ay / sl; }
    const int n = PageCount(s_page);
    int h = -1;
    if (al > 0.35f && n > 0) {
        float ang = atan2f(ax, ay);   // 0 = up, clockwise
        if (ang < 0) ang += 6.2831853f;
        const float step = 6.2831853f / n;
        h = PageIndex(s_page, ((int)floorf(ang / step + 0.5f)) % n);
    } else h = s_hover >= 0 && kClasses[s_hover].page == s_page && al > 0.2f ? s_hover : -1;   // near the centre: keep it
    if (h != s_hover) { s_hover = h; s_hoverMoves++; if (h >= 0) Snd("move"); }
    // favourite / terminal / deploy / cancel
    if ((pp & 0x4000) || g_in.pressed[A_ABILITY]) ToggleFav(s_hover);
    const bool tab = InputKey(VK_TAB);
    const bool tabPressed = tab && !s_tabWas; s_tabWas = tab;
    if ((pp & 0x8000) || tabPressed) { Open(MN_TERM, s_sticky); return; }
    if ((pp & 0x2000) || g_in.pressed[A_AIM]) { Cancel(); return; }
    const bool confirm = (pp & 0x1000) || g_in.pressed[A_FIRE] || (!s_sticky && !g_in.held[A_WHEEL]);
    if (confirm) {
        if (s_hover >= 0) Deploy(s_hover, "class wheel");
        else if (!cfg.outfitFollows && s_keep >= 0 && al <= 0.2f) { s_keep = -1; s_want = -1; Close(true); }   // centre: own outfit
        else Close(true);
    }
    (void)realDt;
}

static bool KeyEdge(int vk, int slot) {   // terminal arrows: press, then repeat every 0.12 s after 0.35 s
    const double now = RealNow();
    if (!InputKey(vk)) { s_keyRep[slot] = 0; return false; }
    if (!s_keyRep[slot]) { s_keyRep[slot] = now + 0.35; return true; }
    if (now >= s_keyRep[slot]) { s_keyRep[slot] = now + 0.12; return true; }
    return false;
}
static void TermValue(int dir) {
    const int i = s_termSel;
    if (i < 0) return;
    if (s_termField == 1 || s_termField == 2) {
        const int slot = s_termField - 1;
        const WeaponDef* cur = SlotGun(i, slot);
        int k = (int)(cur - kWeapons);
        for (int t = 0; t < kWeaponCount; t++) {
            k = ((k + dir) % kWeaponCount + kWeaponCount) % kWeaponCount;
            if (&kWeapons[k] != SlotGun(i, slot ^ 1)) break;   // the two slots never hold the same gun
        }
        s_load[i].g[slot] = &kWeapons[k];
    } else if (s_termField == 3) {
        int k = 0;
        for (int t = 0; t < AbilitySpecialCount(); t++) if (!_stricmp(AbilitySpecialId(t), s_load[i].special)) k = t;
        k = ((k + dir) % AbilitySpecialCount() + AbilitySpecialCount()) % AbilitySpecialCount();
        s_load[i].special = AbilitySpecialId(k);
    }
    s_dirty = true;
    Snd("move");
    if (i == s_cur) ApplyLoadout(false);
}
static void TermMove(int d) {
    const int n = PageCount(s_page);
    if (n <= 0) return;
    if (s_termField == 0) {
        const int k = ((SlotOf(s_termSel) + d) % n + n) % n;
        s_termSel = PageIndex(s_page, k); s_termRestT = RealNow();
    } else s_termField = ((s_termField - 1 + d) % 3 + 3) % 3 + 1;
    Snd("move");
}
static void TermInput() {
    const WORD pp = g_in.padPressed;
    if (g_in.pressed[A_NEXT] || (pp & 0x0200)) PageStep(1);
    if (g_in.pressed[A_PREV] || (pp & 0x0100)) PageStep(-1);
    float mx, my; InputMenuLook(&mx, &my);
    s_termScroll += my;
    while (s_termScroll > 45.0f) { s_termScroll -= 45.0f; TermMove(1); }
    while (s_termScroll < -45.0f) { s_termScroll += 45.0f; TermMove(-1); }
    if (KeyEdge(VK_DOWN, 0) || (pp & 0x0002)) TermMove(1);
    if (KeyEdge(VK_UP, 1) || (pp & 0x0001)) TermMove(-1);
    const bool l = KeyEdge(VK_LEFT, 2) || (pp & 0x0004), r = KeyEdge(VK_RIGHT, 3) || (pp & 0x0008);
    if (s_termField > 0 && (l || r)) TermValue(r ? 1 : -1);
    else if (s_termField == 0 && r) { s_termField = 1; Snd("move"); }
    const bool tab = InputKey(VK_TAB);
    const bool tabPressed = tab && !s_tabWas; s_tabWas = tab;
    if ((pp & 0x8000) || tabPressed) { s_termField = (s_termField + 1) % 4; Snd("move"); }   // focus: list > primary > secondary > special
    if (InputKey(VK_BACK) && Custom(s_termSel)) { DefaultLoadout(s_termSel); s_dirty = true; Snd("vent"); if (s_termSel == s_cur) ApplyLoadout(false); }
    if ((pp & 0x4000) || g_in.pressed[A_ABILITY]) ToggleFav(s_termSel);
    if ((pp & 0x2000) || g_in.pressed[A_AIM] || InputKey(VK_ESCAPE)) { Cancel(); return; }
    if ((pp & 0x1000) || g_in.pressed[A_FIRE]) { Deploy(s_termSel, "requisition terminal"); return; }
    // live preview: the outfit of the row the cursor rests on
    if (cfg.terminalPreview && Api() && s_termSel >= 0 && RealNow() - s_termRestT > 0.6 && s_avail[s_termSel] && s_want != s_termSel)
        s_want = s_termSel;
}

static void MenuTick(float realDt) {
    // the kbm wheel key: a tap swaps to the last class, holding it opens the wheel; the pad combo opens it at once
    if (s_menu == MN_NONE) {
        if (g_in.pressed[A_WHEEL]) {
            if (g_in.pad) Open(MN_WHEEL, true);
            else s_wheelDownT = RealNow();
        }
        if (s_wheelDownT > 0 && g_in.held[A_WHEEL] && RealNow() - s_wheelDownT >= 0.22) { s_wheelDownT = -1.0; Open(MN_WHEEL, false); }
        if (s_wheelDownT > 0 && !g_in.held[A_WHEEL]) {
            s_wheelDownT = -1.0;
            if (s_prev >= 0 && s_prev != s_cur) { Select(s_prev, "quick switch", true); SaveChoices(); }
            else Refuse("No previous class yet - hold the key for the class wheel");
        }
    }
    if (s_menu != MN_NONE && (MenuRefusal() || CinematicNow())) { Log("menu closed: %s", MenuRefusal() ? MenuRefusal() : "cutscene"); Cancel(); }
    if (s_menu == MN_WHEEL) WheelInput(realDt);
    else if (s_menu == MN_TERM) TermInput();
    if (s_menu != MN_NONE) {   // the menu has the buttons: nothing fires, throws or rolls meanwhile
        for (int a = 0; a < A_COUNT; a++) if (a != A_WHEEL && a != A_TOGGLE) { g_in.pressed[a] = g_in.released[a] = false; g_in.held[a] = false; }
    }
    TsTick(realDt, s_menu != MN_NONE, s_menu == MN_TERM ? 0.08f : 0.25f);
}

// ---------------------------------------------------------------------------------------------- frame
void OutfitTick(float realDt) {
    if (!s_inited) return;
    EngineCheck();
    const double now = RealNow();
    if (OutfitConvState() == 2 && s_convSeen != 2) { s_convSeen = 2; CheckAvail(); }
    if (GameState() != 2) {   // front end / loading: menus closed, time scale back, nothing in flight survives
        if (s_menu != MN_NONE) Cancel();
        TsRestoreNow();
        return;
    }
    Pump(now);
    if (s_hudRestore && InGameplay()) { s_hudRestore = false; LuaCall("ui.hudFadeTo(100, 0.2) return 'ok'"); }
    MenuTick(realDt);
    if (s_menu == MN_NONE && InGameplay()) {
        if (g_in.pressed[A_NEXT]) { Select(Step(1), "next", true); s_dirty = true; }
        if (g_in.pressed[A_PREV]) { Select(Step(-1), "previous", true); s_dirty = true; }
        if (g_in.pressed[A_SWAP] && g_mode == M_BLASTER) OutfitSwapWeapon();
    }
    static double nextSave;
    if (s_dirty && now >= nextSave) { nextSave = now + 2.0; SaveChoices(); }
}

// ---------------------------------------------------------------------------------------------- drawing
static const uint32_t kIce = 0x7CE0FF, kWhite = 0xFFFFFF, kDark = 0x081018, kGold = 0xFFC83C, kAmber = 0xFF9A3C;

static void Chip(float x, float y, float w, float h, uint32_t col, float a) {   // angled holo plate
    const float c = h * 0.35f;
    const float pts[12] = { x + c, y, x + w, y, x + w, y + h - c, x + w - c, y + h, x, y + h, x, y + c };
    rnd::Poly(pts, 6, rnd::Argb(a, col));
}

static void DrawWheel(const rnd::Ctx& x, float S, double now) {
    const float open = EaseOut((float)(now - s_menuT) / 0.16f);
    const float a = open;
    const float cx = x.vx + x.vw * 0.5f, cy = x.vy + x.vh * 0.5f;
    // backdrop: darken the world, a cold glow behind the projector
    rnd::Rect(x.vx, x.vy, x.vw, x.vh, rnd::Argb(0.38f * a, 0x000000));
    rnd::Glow(cx, cy, 380.0f * S, rnd::Argb(0.16f * a, kIce));
    const float sc = 0.86f + 0.14f * open;
    const float R = 250.0f * S * sc, r0 = 128.0f * S * sc, mid = (R + r0) * 0.5f, band = R - r0;
    const int n = PageCount(s_page);
    const float step = n > 0 ? 6.2831853f / n : 6.2831853f, gap = 0.035f;
    for (int k = 0; k < n; k++) {
        const int i = PageIndex(s_page, k);
        const float ia = Clamp01((float)(now - s_menuT - k * 0.012f) / 0.14f);
        if (ia <= 0.0f) continue;
        const bool hov = i == s_hover, worn = i == s_worn, sel = i == s_cur, ok = OutfitAllowed(i, true);
        const float c0 = k * step - step * 0.5f + gap, c1 = k * step + step * 0.5f - gap;
        const float push = hov ? 12.0f * S : 0.0f;
        rnd::Arc(cx, cy, mid + push, band, c0, c1, rnd::Argb(a * ia * (hov ? 0.78f : 0.62f), hov ? 0x10324A : kDark), 1.5f);
        rnd::Arc(cx, cy, R + push - 2.0f * S, (hov ? 3.0f : 1.5f) * S, c0, c1, rnd::Argb(a * ia * (hov ? 1.0f : ok ? 0.45f : 0.2f), kIce), 1.0f);
        if (sel) rnd::Arc(cx, cy, r0 + push + 5.0f * S, 3.0f * S, c0 + 0.05f, c1 - 0.05f, rnd::Argb(a * ia * 0.9f, kWhite), 1.0f);
        const float m = (c0 + c1) * 0.5f, tx = cx + sinf(m) * (mid + push), ty = cy - cosf(m) * (mid + push);
        const uint32_t tc = !ok ? 0x5A6670 : hov ? kWhite : worn ? kIce : 0xB8C8D4;
        rnd::Text(rnd::F_KORATAKI, tx, ty - 15.0f * S, (hov ? 30.0f : 26.0f) * S, rnd::Argb(a * ia, tc), kClasses[i].code, rnd::A_CENTER);
        if (s_fav[i]) {   // favourite: a gold diamond at the outer rim (a shape, not only a colour)
            const float fx = cx + sinf(m) * (R + push - 14.0f * S), fy = cy - cosf(m) * (R + push - 14.0f * S), d = 5.0f * S;
            const float pts[8] = { fx, fy - d, fx + d, fy, fx, fy + d, fx - d, fy };
            rnd::Poly(pts, 4, rnd::Argb(a * ia, kGold));
        }
        if (worn) rnd::Disc(cx + sinf(m) * (r0 + push + 16.0f * S), cy - cosf(m) * (r0 + push + 16.0f * S), 3.5f * S, rnd::Argb(a * ia, kIce));
        if (!ok) rnd::Line(tx - 16.0f * S, ty + 12.0f * S, tx + 16.0f * S, ty + 12.0f * S, 2.0f * S, rnd::Argb(a * ia * 0.8f, kAmber));
    }
    // centre: the projector disc with slow dashes and scanlines
    rnd::Disc(cx, cy, r0 - 10.0f * S, rnd::Argb(0.82f * a, kDark), 2.0f);
    const float spin = (float)fmod(now * 0.6, 6.2831853);
    for (int q = 0; q < 6; q++) rnd::Arc(cx, cy, r0 - 14.0f * S, 1.5f * S, spin + q * 1.047f, spin + q * 1.047f + 0.6f, rnd::Argb(0.45f * a, kIce));
    for (float yy = -r0 + 18.0f * S; yy < r0 - 18.0f * S; yy += 5.0f * S) {
        const float hw = sqrtf(fmaxf(0.0f, (r0 - 16.0f * S) * (r0 - 16.0f * S) - yy * yy));
        rnd::Rect(cx - hw, cy + yy, hw * 2.0f, 1.0f, rnd::Argb(0.05f * a, kIce));
    }
    const int show = s_hover >= 0 ? s_hover : -1;
    if (show >= 0) {
        const ClassDef& c = kClasses[show];
        rnd::Text(rnd::F_SMALL, cx, cy - 66.0f * S, 16.0f * S, rnd::Argb(0.75f * a, kIce), kPageNames[c.page], rnd::A_CENTER);
        float fs = 26.0f * S;
        while (fs > 14.0f * S && rnd::TextWidth(rnd::F_TITLE, fs, c.name) > (r0 - 24.0f * S) * 2.0f) fs -= 1.0f * S;
        rnd::Text(rnd::F_TITLE, cx, cy - 44.0f * S, fs, rnd::Argb(a, kWhite), c.name, rnd::A_CENTER);
        char b[96];
        sprintf_s(b, "%s  /  %s", SlotGun(show, 0)->name, SlotGun(show, 1)->name);
        float bs = 13.0f * S;
        while (bs > 10.0f * S && rnd::TextWidth(rnd::F_SMALL, bs, b) > (r0 - 20.0f * S) * 2.0f) bs -= 0.5f * S;
        rnd::Text(rnd::F_SMALL, cx, cy - 6.0f * S, bs, rnd::Argb(0.9f * a, 0xD8E4EC), b, rnd::A_CENTER);
        rnd::Text(rnd::F_SMALL, cx, cy + 14.0f * S, 15.0f * S, rnd::Argb(0.95f * a, kGold), AbilitySpecialTitle(s_load[show].special), rnd::A_CENTER);
        if (!OutfitAllowed(show, true)) rnd::Text(rnd::F_SMALL, cx, cy + 36.0f * S, 14.0f * S, rnd::Argb(a, kAmber), "OUTFIT NOT INSTALLED", rnd::A_CENTER);
        else if (show == s_worn) rnd::Text(rnd::F_SMALL, cx, cy + 36.0f * S, 14.0f * S, rnd::Argb(0.8f * a, kIce), "WORN", rnd::A_CENTER);
    } else {
        rnd::Text(rnd::F_TITLE, cx, cy - 20.0f * S, 20.0f * S, rnd::Argb(0.85f * a, kIce), "REQUISITION", rnd::A_CENTER);
        rnd::Text(rnd::F_SMALL, cx, cy + 10.0f * S, 15.0f * S, rnd::Argb(0.7f * a, 0xB8C8D4),
                  !cfg.outfitFollows && s_keep >= 0 ? "centre: your own outfit" : "point at a class", rnd::A_CENTER);
    }
    // page tabs
    const float ty = cy - R - 58.0f * S;
    for (int p = 0; p < P_COUNT; p++) {
        const float px = cx + (p - 1) * 170.0f * S;
        const bool on = p == s_page;
        rnd::Text(rnd::F_BODY, px, ty, (on ? 20.0f : 17.0f) * S, rnd::Argb(a * (on ? 1.0f : 0.5f), on ? kWhite : 0xB8C8D4), kPageNames[p], rnd::A_CENTER);
        if (on) rnd::RectGrad(px - 60.0f * S, ty + 28.0f * S, 120.0f * S, 2.0f * S, rnd::Argb(a, kIce), rnd::Argb(a * 0.2f, kIce));
    }
    rnd::Text(rnd::F_SMALL, cx - 270.0f * S, ty + 4.0f * S, 15.0f * S, rnd::Argb(0.6f * a, kIce), g_in.pad ? "LB" : VkName(cfg.kPrev), rnd::A_CENTER);
    rnd::Text(rnd::F_SMALL, cx + 270.0f * S, ty + 4.0f * S, 15.0f * S, rnd::Argb(0.6f * a, kIce), g_in.pad ? "RB" : VkName(cfg.kNext), rnd::A_CENTER);
    // hints
    // (the keys as bound in MOD OPTIONS / DK_Blaster.ini)
    char kb[160];
    sprintf_s(kb, "MOUSE select    %s deploy    %s favourite    TAB terminal    %s cancel",
              s_sticky ? VkName(cfg.kFire) : "RELEASE", VkName(cfg.kAbility), VkName(cfg.kAim));
    const char* hint = g_in.pad ? "RIGHT STICK select    A deploy    X favourite    Y terminal    B cancel" : kb;
    rnd::Text(rnd::F_SMALL, cx, cy + R + 34.0f * S, 15.0f * S, rnd::Argb(0.7f * a, 0xC8D4DC), hint, rnd::A_CENTER);
}

static void Bar(float x, float y, float w, float S, float v, const char* label, float a) {
    rnd::Text(rnd::F_SMALL, x, y - 2.0f * S, 14.0f * S, rnd::Argb(0.7f * a, 0xB8C8D4), label);
    const float bx = x + 120.0f * S, bw = w - 120.0f * S;
    rnd::Rect(bx, y + 4.0f * S, bw, 6.0f * S, rnd::Argb(0.25f * a, kIce));
    rnd::Rect(bx, y + 4.0f * S, bw * Clamp01(v), 6.0f * S, rnd::Argb(0.9f * a, kIce));
    for (int t = 1; t < 10; t++) rnd::Rect(bx + bw * t / 10.0f, y + 4.0f * S, 1.0f * S, 6.0f * S, rnd::Argb(0.5f * a, kDark));
}
static const char* SpecialLine(const char* id) {
    if (!_stricmp(id, "rapid")) return "Less heat, faster fire for 6 s.";
    if (!_stricmp(id, "scan")) return "Marks every enemy within 45 m, through walls.";
    if (!_stricmp(id, "stim")) return "Restores 40 % health over 1.5 s.";
    if (!_stricmp(id, "jump")) return "Thruster lift along your move. Hold to hover.";
    if (!_stricmp(id, "overcharge")) return "Next 3 shots: double damage, no heat.";
    return "";
}

static void DrawTerminal(const rnd::Ctx& x, float S, double now) {
    const float a = EaseOut((float)(now - s_menuT) / 0.2f);
    rnd::Rect(x.vx, x.vy, x.vw, x.vh, rnd::Argb(0.62f * a, 0x000000));
    const float slide = (1.0f - a) * 40.0f * S;
    const float L = x.vx + 70.0f * S - slide, T = x.vy + 70.0f * S;
    const float listW = 440.0f * S;
    // header
    rnd::Text(rnd::F_KORATAKI, L, T, 30.0f * S, rnd::Argb(a, kIce), "REQUISITION TERMINAL");
    rnd::RectGrad(L, T + 42.0f * S, x.vw - 140.0f * S, 2.0f * S, rnd::Argb(a, kIce), rnd::Argb(0.0f, kIce));
    for (int p = 0; p < P_COUNT; p++) {
        const bool on = p == s_page;
        const float px = L + p * 150.0f * S;
        rnd::Text(rnd::F_BODY, px, T + 56.0f * S, 18.0f * S, rnd::Argb(a * (on ? 1.0f : 0.45f), on ? kWhite : 0xB8C8D4), kPageNames[p]);
        if (on) rnd::Rect(px, T + 84.0f * S, 110.0f * S, 2.0f * S, rnd::Argb(a, kIce));
    }
    // class list
    const int n = PageCount(s_page);
    const float rowH = 40.0f * S;
    float y = T + 104.0f * S;
    for (int k = 0; k < n; k++, y += rowH) {
        const int i = PageIndex(s_page, k);
        const bool on = i == s_termSel, ok = OutfitAllowed(i, true);
        if (on) {
            rnd::RectGrad(L, y, listW, rowH - 4.0f * S, rnd::Argb(a * (s_termField == 0 ? 0.55f : 0.3f), 0x1C4A66), rnd::Argb(a * 0.15f, 0x1C4A66));
            rnd::Rect(L, y, 3.0f * S, rowH - 4.0f * S, rnd::Argb(a, kIce));
        }
        Chip(L + 12.0f * S, y + 6.0f * S, 44.0f * S, rowH - 16.0f * S, ok ? 0x173040 : 0x202428, a * 0.9f);
        rnd::Text(rnd::F_KORATAKI, L + 34.0f * S, y + 6.0f * S, 18.0f * S, rnd::Argb(a, ok ? kIce : 0x5A6670), kClasses[i].code, rnd::A_CENTER);
        rnd::Text(rnd::F_BODY, L + 68.0f * S, y + 7.0f * S, 18.0f * S, rnd::Argb(a * (ok ? 1.0f : 0.45f), on ? kWhite : 0xC8D4DC), kClasses[i].name);
        float rx = L + listW - 14.0f * S;
        if (s_fav[i]) {
            const float d = 5.0f * S, fy = y + rowH * 0.5f - 2.0f * S;
            const float pts[8] = { rx, fy - d, rx + d, fy, rx, fy + d, rx - d, fy };
            rnd::Poly(pts, 4, rnd::Argb(a, kGold));
            rx -= 18.0f * S;
        }
        if (i == s_worn) { rnd::Text(rnd::F_SMALL, rx, y + 11.0f * S, 13.0f * S, rnd::Argb(a, kIce), "WORN", rnd::A_RIGHT); rx -= 46.0f * S; }
        if (Custom(i)) rnd::Text(rnd::F_SMALL, rx, y + 11.0f * S, 13.0f * S, rnd::Argb(a * 0.8f, kGold), "CUSTOM", rnd::A_RIGHT);
        if (!ok) rnd::Text(rnd::F_SMALL, rx, y + 11.0f * S, 13.0f * S, rnd::Argb(a, kAmber), "NOT INSTALLED", rnd::A_RIGHT);
    }
    // details
    const int i = s_termSel >= 0 ? s_termSel : s_cur;
    const ClassDef& c = kClasses[i];
    const float D = L + listW + 60.0f * S, W = fminf(620.0f * S, x.vx + x.vw - 70.0f * S - D);
    float dy = T + 104.0f * S;
    rnd::Text(rnd::F_TITLE, D, dy, 40.0f * S, rnd::Argb(a, kWhite), c.name);
    const bool previewing = s_want == i && s_worn != i && cfg.terminalPreview;
    if (s_worn == i && i != s_termOrigWant) rnd::Text(rnd::F_SMALL, D + W, dy + 14.0f * S, 15.0f * S, rnd::Argb(a * (0.6f + 0.4f * sinf((float)now * 5.0f)), kIce), "PREVIEW", rnd::A_RIGHT);
    else if (previewing) rnd::Text(rnd::F_SMALL, D + W, dy + 14.0f * S, 15.0f * S, rnd::Argb(a * 0.6f, kIce), "REQUISITIONING...", rnd::A_RIGHT);
    dy += 52.0f * S;
    rnd::Text(rnd::F_BODY, D, dy, 17.0f * S, rnd::Argb(0.85f * a, 0xC8D4DC), c.line);
    dy += 44.0f * S;
    const char* labels[3] = { "PRIMARY", "SECONDARY", "SPECIAL" };
    for (int f = 0; f < 3; f++, dy += 44.0f * S) {
        const bool foc = s_termField == f + 1;
        rnd::RectGrad(D, dy - 4.0f * S, W, 36.0f * S, rnd::Argb(a * (foc ? 0.5f : 0.18f), 0x1C4A66), rnd::Argb(a * 0.05f, 0x1C4A66));
        if (foc) rnd::Rect(D, dy - 4.0f * S, 3.0f * S, 36.0f * S, rnd::Argb(a, kIce));
        rnd::Text(rnd::F_SMALL, D + 14.0f * S, dy + 6.0f * S, 14.0f * S, rnd::Argb(0.7f * a, kIce), labels[f]);
        const char* val = f < 2 ? SlotGun(i, f)->name : AbilitySpecialTitle(s_load[i].special);
        rnd::Text(rnd::F_BODY, D + 140.0f * S, dy + 3.0f * S, 18.0f * S, rnd::Argb(a, f == 2 ? kGold : kWhite), val);
        if (foc) {
            rnd::Text(rnd::F_BODY, D + W - 40.0f * S, dy + 3.0f * S, 18.0f * S, rnd::Argb(a, kIce), "<", rnd::A_CENTER);
            rnd::Text(rnd::F_BODY, D + W - 16.0f * S, dy + 3.0f * S, 18.0f * S, rnd::Argb(a, kIce), ">", rnd::A_CENTER);
        }
    }
    // the focused gun's numbers (relative to the arsenal's best)
    const WeaponDef* w = SlotGun(i, s_termField == 2 ? 1 : 0);
    float dmgMax = 1, rpmMax = 1, rangeMax = 1, heatMax = 0.01f;
    for (int k = 0; k < kWeaponCount; k++) {
        const WeaponDef& q = kWeapons[k];
        dmgMax = fmaxf(dmgMax, q.damage * (q.pellets > 1 ? q.pellets : 1)); rpmMax = fmaxf(rpmMax, q.rpm);
        rangeMax = fmaxf(rangeMax, q.range); heatMax = fmaxf(heatMax, q.heatPerShot);
    }
    dy += 8.0f * S;
    Bar(D, dy, W, S, w->damage * (w->pellets > 1 ? w->pellets : 1) / dmgMax, "DAMAGE", a); dy += 24.0f * S;
    Bar(D, dy, W, S, w->rpm / rpmMax, "FIRE RATE", a); dy += 24.0f * S;
    Bar(D, dy, W, S, w->range / rangeMax, "RANGE", a); dy += 24.0f * S;
    Bar(D, dy, W, S, 1.0f - w->heatPerShot / heatMax * 0.9f, "COOLING", a); dy += 34.0f * S;
    char b[128];
    sprintf_s(b, "%s: %s", AbilitySpecialTitle(s_load[i].special), SpecialLine(s_load[i].special));
    rnd::Text(rnd::F_SMALL, D, dy, 15.0f * S, rnd::Argb(0.9f * a, kGold), b);
    if (!OutfitAllowed(i, true)) rnd::Text(rnd::F_SMALL, D, dy + 22.0f * S, 15.0f * S, rnd::Argb(a, kAmber),
                                           c.base ? "Outfit built from the game's files at the next start." : "This outfit's game files are missing.");
    // hints
    char kb[200];
    sprintf_s(kb, "ARROWS / MOUSE move    %s %s page    TAB field    %s deploy    %s favourite    BACKSPACE reset loadout    %s back",
              VkName(cfg.kPrev), VkName(cfg.kNext), VkName(cfg.kFire), VkName(cfg.kAbility), VkName(cfg.kAim));
    const char* hint = g_in.pad ? "D-PAD move    LB / RB page    Y field    A deploy    X favourite    B back" : kb;
    rnd::Text(rnd::F_SMALL, x.vx + x.vw * 0.5f, x.vy + x.vh - 46.0f * S, 15.0f * S, rnd::Argb(0.75f * a, 0xC8D4DC), hint, rnd::A_CENTER);
}

// swap flash: a requisition beam sweeping down the screen
static void DrawSwapFx(const rnd::Ctx& x, float S, double now) {
    const float t = (float)(now - s_fxT);
    if (t < 0.0f || t > 0.7f) return;
    const float k = t / 0.7f, a = 1.0f - k;
    const float y = x.vy + x.vh * EaseOut(k);
    rnd::RectGrad(x.vx, y - 120.0f * S, x.vw, 120.0f * S, rnd::Argb(0.0f, kIce), rnd::Argb(0.16f * a, kIce));
    rnd::Rect(x.vx, y, x.vw, 2.0f * S, rnd::Argb(0.55f * a, kIce));
    if (s_fxClass >= 0 && t < 0.6f)
        rnd::Text(rnd::F_KORATAKI, x.vx + x.vw * 0.5f, x.vy + x.vh * 0.22f, 22.0f * S, rnd::Argb(a, kIce), kClasses[s_fxClass].name, rnd::A_CENTER);
}

void OutfitDrawPost(const rnd::Ctx& x) {
    if (!s_inited) return;
    const double now = x.t;
    const float S = x.scale;
    rnd::SetGlobalAlpha(1.0f);
    if (s_menu == MN_WHEEL) DrawWheel(x, S, now);
    else if (s_menu == MN_TERM) DrawTerminal(x, S, now);
    else if (now - s_closeT < 0.12) {   // a quick fade-out of the closed menu
        const float a = 1.0f - (float)(now - s_closeT) / 0.12f;
        rnd::SetGlobalAlpha(a);
        if (s_closedMenu == MN_WHEEL) DrawWheel(x, S, now); else DrawTerminal(x, S, now);
        rnd::SetGlobalAlpha(1.0f);
    }
    DrawSwapFx(x, S, now);
}
bool OutfitMenuOpen() { return s_menu != MN_NONE; }
bool OutfitHidesHud() { return s_menu == MN_TERM; }
void OutfitHome() {   // emergency reset: menus shut, the player's own outfit, whatever the settings
    if (s_menu != MN_NONE) Cancel();
    TsRestoreNow();
    s_keep = -1; s_want = -1;
}

// blaster HUD: the class above the weapon plate
void OutfitDrawHud(const rnd::Ctx& x, float S, float a) {
    if (!s_inited || !cfg.loadoutFollows) return;
    const float right = x.vx + x.vw - 48.0f * S, base = x.vy + x.vh - 64.0f * S;
    char b[96];
    sprintf_s(b, "%s", kClasses[s_cur].name);
    rnd::Text(rnd::F_SMALL, right, base - 62.0f * S, 15.0f * S, rnd::Argb(0.85f * a, kIce), b, rnd::A_RIGHT);
    sprintf_s(b, "%s %s", g_in.pad ? "Y" : VkName(cfg.kSwap), SlotGun(s_cur, s_slot ^ 1)->name);
    rnd::Text(rnd::F_SMALL, right, base + 26.0f * S, 14.0f * S, rnd::Argb(0.55f * a, 0xC8D4DC), b, rnd::A_RIGHT);
}

// ---------------------------------------------------------------------------------------------- setup / dev
void OutfitInstall(const Range& text) {
    if (uint8_t* f = FindSym(text, "Blaster_LuaSetTimeScale")) {   // mov ecx,[g_Game] at +0x15; call App::SetTimeScale at +0x21
        uint8_t* call = f + 0x21;
        if (f[0x15] == 0x8B && f[0x16] == 0x0D && call[0] == 0xE8) {
            uint8_t* set = call + 5 + *(int32_t*)(call + 1);
            if (set[0] == 0xF3 && set[1] == 0x0F && set[2] == 0x10 && set[6] == 0xF3 && set[7] == 0x0F && set[8] == 0x11 && set[9] == 0x81) {
                s_tsGlobal = *(uint32_t*)(f + 0x17);
                s_tsOff = *(uint32_t*)(set + 10);
            }
        }
    }
    if (!s_tsGlobal || s_tsOff > 0x1000) { s_tsGlobal = 0; Log("time scale not found - the class wheel opens without slow motion"); }
    LoadChoices();
    CheckAvail();
    // the swap engine: found now, installed at the first frame unless DK_VaderSwap runs its own (EngineCheck)
    costume::Resolve(GetModuleHandleA(nullptr), g_dir);
    s_inited = true;
    Log("classes: %d, last %s, costume engine %s, time scale %s", kN, kClasses[s_cur].id,
        costume::Resolved() ? "found (DK_VaderSwap's is used instead when it runs)" : costume::Problem(), s_tsGlobal ? "ok" : "off");
}
void OutfitShutdown() {
    if (s_menu != MN_NONE) Close(false, true);   // no game calls while unloading; the outfit goes home below
    TsRestoreNow();
    InputCapture(false);
    // back to the player's own: the engine runs it (ours: the next instance after a hot reload takes the request over)
    if (s_worn >= 0 && cfg.outfitFollows) ApiRequest(DK_COSTUME_HOME, nullptr);
    costume::Shutdown(g_api, kHandoff);
    if (s_dirty) SaveChoices();
}

const char* OutfitStatus() {
    static char b[512];
    DK_CostumeState st;
    const bool api = ApiState(&st);
    int avail = 0; for (int i = 0; i < kN; i++) avail += s_avail[i];
    sprintf_s(b, "class %s slot %d worn %s want %s keep %s req %d swaps %d refusals %d requests %d rearms %d foreign %d last '%s' ms %u max %u | "
                 "menu %s page %d hover %s opens %d/%d deploys %d moves %d ts %.2f owned %d gamehud %d | api %d eng %s hpkept %d vs '%s' ext %d result '%s' | avail %d/%d conv '%s' fx %d/%d | %s",
              kClasses[s_cur].id, s_slot, s_worn >= 0 ? kClasses[s_worn].id : "own", s_want >= 0 ? kClasses[s_want].id : "own",
              s_keep >= 0 ? kClasses[s_keep].id : "-", s_reqId, s_swaps, s_refusals, s_requests, s_rearms, s_foreign, s_lastResult, s_lastSwapMs, s_maxSwapMs,
              s_menu == MN_WHEEL ? "wheel" : s_menu == MN_TERM ? "terminal" : "none", s_page, s_hover >= 0 ? kClasses[s_hover].id : "-",
              s_wheelOpens, s_termOpens, s_deploys, s_hoverMoves, TsGet(), (int)s_tsOwned, s_hudHidden ? 0 : 1, (int)api, EngineName(), s_hpKept, api ? st.cur : "-", api ? st.external : 0,
              api ? st.result : "-", avail, kN, OutfitConvStatus(), s_fxVfx, s_fxSnd, InputPadStatus());
    return b;
}

int OutfitCommand(const char* cmd, const char* a1, const char* a2, char* out, int size) {
    if (!_stricmp(cmd, "class")) {   // class [<id>|next|prev|last|home|swap]
        if (!a1[0]) { _snprintf_s(out, size, _TRUNCATE, "%s", OutfitStatus()); return 1; }
        int i = !_stricmp(a1, "next") ? Step(1) : !_stricmp(a1, "prev") ? Step(-1) : !_stricmp(a1, "last") ? s_prev : ById(a1);
        if (!_stricmp(a1, "home")) { s_keep = -1; s_want = -1; strcpy_s(out, size, "own outfit requested"); return 1; }
        if (!_stricmp(a1, "swap")) { if (g_mode == M_BLASTER) OutfitSwapWeapon(); _snprintf_s(out, size, _TRUNCATE, "slot %d", s_slot); return 1; }
        const bool ok = Select(i, "dev", true);
        _snprintf_s(out, size, _TRUNCATE, "%s %s", ok ? "class" : "refused", i >= 0 ? kClasses[i].id : a1);
        return 1;
    }
    if (!_stricmp(cmd, "classes")) {
        int n = 0;
        for (int i = 0; i < kN && n < size - 80; i++)
            n += _snprintf_s(out + n, size - n, _TRUNCATE, "%s %s avail=%d fav=%d %s/%s/%s%s%s\n", kClasses[i].id, kClasses[i].actor, (int)s_avail[i],
                             (int)s_fav[i], SlotGun(i, 0)->id, SlotGun(i, 1)->id, s_load[i].special, i == s_cur ? " CUR" : "", i == s_worn ? " WORN" : "");
        return 1;
    }
    if (!_stricmp(cmd, "fav")) { int i = ById(a1); ToggleFav(i); _snprintf_s(out, size, _TRUNCATE, "fav %s %d", a1, i >= 0 ? (int)s_fav[i] : -1); return 1; }
    if (!_stricmp(cmd, "loadout")) {   // loadout <class> <primary> <secondary> <special> | reset
        const int i = ById(a1);
        if (i < 0) { strcpy_s(out, size, "unknown class"); return 1; }
        char g1[32] = {}, g2[32] = {}, sp[32] = {};
        if (sscanf_s(a2, "%31[^,],%31[^,],%31s", g1, (unsigned)sizeof g1, g2, (unsigned)sizeof g2, sp, (unsigned)sizeof sp) == 3 &&
            WeaponById(g1) && WeaponById(g2) && SpecialIdOf(sp)) {
            s_load[i].g[0] = WeaponById(g1); s_load[i].g[1] = WeaponById(g2); s_load[i].special = SpecialIdOf(sp);
        } else if (!_stricmp(a2, "reset")) DefaultLoadout(i);
        else { strcpy_s(out, size, "usage: loadout <class> <primary>,<secondary>,<special> | reset"); return 1; }
        s_dirty = true; SaveChoices();
        if (i == s_cur) ApplyLoadout(false);
        _snprintf_s(out, size, _TRUNCATE, "%s: %s/%s/%s custom %d", a1, SlotGun(i, 0)->id, SlotGun(i, 1)->id, s_load[i].special, (int)Custom(i));
        return 1;
    }
    if (!_stricmp(cmd, "wheel") || !_stricmp(cmd, "term")) {   // wheel|term open|close|cancel|page <n>|hover <id>|confirm|field <n>|value <d>
        const int menu = !_stricmp(cmd, "wheel") ? MN_WHEEL : MN_TERM;
        if (!_stricmp(a1, "open")) Open(menu, true);
        else if (!_stricmp(a1, "cancel")) Cancel();
        else if (!_stricmp(a1, "close")) Close(true);
        else if (!_stricmp(a1, "page")) { s_page = atoi(a2) % P_COUNT; s_hover = -1; if (menu == MN_TERM) { s_termSel = PageIndex(s_page, 0); s_termRestT = RealNow(); } }
        else if (!_stricmp(a1, "hover")) { int i = ById(a2); if (i >= 0) { s_page = kClasses[i].page; if (menu == MN_WHEEL) AimAt(i); else { s_termSel = i; s_termRestT = RealNow(); } } }
        else if (!_stricmp(a1, "confirm")) { const int i = menu == MN_WHEEL ? s_hover : s_termSel; if (i >= 0) Deploy(i, "dev"); else Close(true); }
        else if (!_stricmp(a1, "field")) s_termField = atoi(a2) & 3;
        else if (!_stricmp(a1, "value")) TermValue(atoi(a2) < 0 ? -1 : 1);
        else if (!_stricmp(a1, "fav")) ToggleFav(menu == MN_WHEEL ? s_hover : s_termSel);
        _snprintf_s(out, size, _TRUNCATE, "%s", OutfitStatus());
        return 1;
    }
    if (!_stricmp(cmd, "outfit")) { _snprintf_s(out, size, _TRUNCATE, "%s", OutfitStatus()); return 1; }
    if (!_stricmp(cmd, "padsim")) {   // padsim <hex buttons> <seconds>: pretend XInput buttons are held (tests)
        InputDevPad((WORD)strtoul(a1, nullptr, 16), a2[0] ? (float)atof(a2) : 0.2f);
        _snprintf_s(out, size, _TRUNCATE, "pad %s", a1);
        return 1;
    }
    return 0;
}

}  // namespace dkb
