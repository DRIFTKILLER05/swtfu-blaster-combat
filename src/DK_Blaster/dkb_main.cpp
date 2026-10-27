// DK_Blaster - Battlefront-style third-person blaster combat for Star Wars: The Force Unleashed (PC).
// Plugin for the shared DK_Loader (dinput8.dll). Design: BRIEF.md; notes and addresses: NOTES.md "DK_Blaster".
//
// Mode model: g_want = what the player asked for (blaster on / off), g_mode = what runs now. Situations the shooter
// must not touch (cutscene, QTE, finishing move, scripted camera, boss fight) suspend the mode and it comes back by
// itself when they end. Death, a checkpoint / level change and loading a save do the same (KeepMode=1: the wish is
// the player's choice until they toggle it; KeepMode=0: they clear it); the emergency hold and unloading the plugin
// clear the wish and leave everything vanilla. Nothing of this is ever saved.
#include "dkb.h"
#include "dkb_render.h"
#include "dk_keys.h"
#include <stdio.h>

using namespace dk;

namespace dkb {

const DK_LoaderApi* g_api;
char g_dir[MAX_PATH];
Config cfg;
Mode g_mode = M_SABER;

static bool s_active;              // hooks in place
static bool s_want;                // the player wants blaster mode
static const char* s_suspend;      // why it is suspended now (nullptr = not)
static double s_clearSince;        // when the suspend reason went away
static bool s_devStayCam;          // dev command staycam: a script camera does not hand back the saber (gallery portraits)
static DWORD s_ren;                // player ren we armed
static float s_lastGame; static double s_lastReal;
static float s_frameDt, s_realDt;
static double s_nextState;         // next Lua safety poll

static const WeaponDef* s_weapon;
static bool s_emergencyDone;
static float s_speed, s_stick[2]; static float s_prevPos[3]; static bool s_havePrev;
static double s_nextEnemyRay, s_nextHealth, s_nextSaberGuard;
static int s_saberCine, s_saberHilt;   // saber put away again after a cinematic's end / hilt shown again
bool g_aimHitOk;
float g_aimHit[3];
static double s_rollReady;   // real time the next roll is allowed
static int s_rolls;
static const float kRollCooldown = 0.9f;
static const float kRunSpeed = 5.3f;   // Starkiller's run speed, m/s (TieFactory_Act1, measured 2026-10-26)
static float s_moveCapV = -1.0f;       // the locomotion speed cap set now (DKB.movecap; -1 = none): aiming, crouch
static const float kCrouchDepth = 0.32f;   // hips drop of the crouch (m)
static bool s_crouch;                  // crouching (blaster drawn)
static double s_crouchAt;              // real time of the last crouch toggle (Alt+Tab undo)
static int s_crouchN;                  // crouches (dev status)
static float s_vy;                     // vertical speed (m/s): a jump or a fall stands you up
static bool s_gunUp;                   // the aimed gun pose is wanted (dkb_aim.cpp)
static bool s_sprintNow;               // sprinting this frame (status)
static double s_fireAt, s_rollAt;      // real time the trigger was last held / the last roll
static DWORD s_bossOff = 0x5450;
static float s_enterT;
static FILETIME s_iniTime;
static char s_ini[MAX_PATH];
extern float g_hudScopeAmt;
extern float g_hudBreath, g_hudHealth;
extern int g_hudBreathState;
void HudSetOnEnemy(bool on);

float FrameDt() { return s_frameDt; }
float RealDt() { return s_realDt; }

// ---------------------------------------------------------------------------------------------- config
// key names: DK_Common dk_keys.h (shared with DK_ModMenu's key options, so a key picked in MOD OPTIONS reads back)
static int ParseKey(const char* s, int def) { return dk::KeyParse(s, def); }
const char* VkName(int vk) {
    static char b[8][16]; static int k;
    return dk::KeyNameOf(vk, b[k++ & 7], sizeof b[0]);
}
static WORD ParsePad(const char* s) {
    static const struct { const char* n; WORD m; } b[] = {
        {"UP", 0x0001}, {"DOWN", 0x0002}, {"LEFT", 0x0004}, {"RIGHT", 0x0008}, {"START", 0x0010}, {"BACK", 0x0020},
        {"LS", 0x0040}, {"RS", 0x0080}, {"LB", 0x0100}, {"RB", 0x0200}, {"A", 0x1000}, {"B", 0x2000}, {"X", 0x4000}, {"Y", 0x8000}};
    WORD m = 0; char buf[128]; strncpy_s(buf, s, _TRUNCATE);
    char* ctx = nullptr;
    for (char* t = strtok_s(buf, "+ ,", &ctx); t; t = strtok_s(nullptr, "+ ,", &ctx))
        for (auto& x : b) if (!_stricmp(t, x.n)) m |= x.m;
    return m;
}

static const char* kDefaultIni =
    "; DK_Blaster settings (Star Wars: The Force Unleashed). Also in game: MOD OPTIONS > Blaster Combat.\n"
    "; Changes are picked up while the game runs.\n"
    "[Blaster]\n"
    "; 1 = mod active, 0 = off (everything vanilla)\n"
    "Enabled=1\n"
    "; Keys: a letter / digit, F1-F24, LMB RMB MMB MOUSE4 MOUSE5, LSHIFT LCTRL LALT SPACE TAB [ ] ; ' , . / or 0x.. (virtual key)\n"
    "ToggleKey=B\n"
    "FireKey=LMB\n"
    "AimKey=RMB\n"
    "VentKey=R\n"
    "GrenadeKey=G\n"
    "AbilityKey=F\n"
    "SprintKey=LSHIFT\n"
    "ShoulderKey=X\n"
    "NextClassKey=]\n"
    "PrevClassKey=[\n"
    "; Tap: the last class; hold: the class wheel\n"
    "ClassWheelKey=T\n"
    "RollKey=C\n"
    "SwapWeaponKey=Q\n"
    "; Crouch (and crouch walk); LALT = left Alt. CrouchToggle below: press or hold\n"
    "CrouchKey=LALT\n"
    "; Controller: RT fire, LT aim, X active cooldown, B roll, RB grenade, LB ability, Y weapon swap, D-pad down crouch,\n"
    "; left stick click sprint, right stick click shoulder, Back + D-pad left / right class, Back + D-pad up class wheel.\n"
    "; Toggle combo (all held), e.g. BACK+DOWN; 0 = off\n"
    "Gamepad=1\n"
    "GamepadToggle=BACK+DOWN\n"
    "; Look speed in blaster mode: controller (x the default turn rate) and mouse (x 0.06 degrees per count)\n"
    "LookSensitivity=1.0\n"
    "MouseSensitivity=1.0\n"
    "; 1 = invert look up / down in blaster mode (the game's own mouse invert options apply as well)\n"
    "InvertLookY=0\n"
    "; Look sensitivity while aiming / scoped (x normal)\n"
    "AimSensitivity=0.55\n"
    "ScopeSensitivity=0.35\n"
    "; Movement speed while aiming (x normal, 0.3-1)\n"
    "AimMoveSpeed=0.6\n"
    "; Controller aim assist (slow-down over enemies)\n"
    "AimAssist=1\n"
    "; Shoulder: 1 = right, -1 = left. Offsets in metres\n"
    "Shoulder=1\n"
    "CameraSide=0.62\n"
    "CameraUp=0.18\n"
    "; Camera distance behind the player in blaster mode (m); 0 = the level's own\n"
    "CameraDistance=3.0\n"
    "; Field of view in blaster mode (degrees, vertical); 0 = the game's\n"
    "FOV=0\n"
    "; Crosshair: 0 Battlefront, 1 dot, 2 circle, 3 cross. Colour RRGGBB. Size 0.5-2\n"
    "Crosshair=0\n"
    "CrosshairColor=FFFFFF\n"
    "CrosshairSize=1.0\n"
    "; HUD size (0.6-1.6)\n"
    "HudScale=1.0\n"
    "; Hit / kill marker sounds\n"
    "HitSound=1\n"
    "; Player bolt damage (x)\n"
    "DamageScale=1.0\n"
    "; 1 = big enemies (over 1600 health, like shock troopers) take up to 2x blaster damage, so they fall in seconds\n"
    ";     instead of minutes; 0 = the same damage for every enemy\n"
    "HeavyDamage=1\n"
    "; 1 = Starkiller keeps his automatic saber-reflex deflection of blaster bolts in blaster mode (easy); 0 = enemy\n"
    ";     bolts hit you while you hold a gun, and he never strikes the saber block pose at them\n"
    "AutoDeflect=0\n"
    "; damage enemy bolts do to you while you hold a gun (x; 1.0 = the game's own)\n"
    "DamageTaken=0.6\n"
    "; 1 = boss fights, QTEs and finishing moves put the saber back and the blaster returns afterwards\n"
    "BossReturn=1\n"
    "; blaster mode stays on through deaths, checkpoints, level changes and loading a save (0 = the saber after\n"
    ";     them)\n"
    "KeepMode=1\n"
    "; 1 = blaster kills give Force points like saber kills\n"
    "KillCredit=1\n"
    "; Trooper classes. OutfitFollows: 1 = drawing the blaster puts the class outfit on, holstering puts your own back\n"
    "; (needs DK_VaderSwap 1.3); 0 = outfits are worn in any mode, picked with the class keys / wheel\n"
    "OutfitFollows=1\n"
    "; 1 = the class picks your guns and special (0 = the E-11 and the gun's own special)\n"
    "LoadoutFollows=1\n"
    "; Hold the blaster two-handed like a stormtrooper (walk, run, strafe, idle and turns use the trooper's combat\n"
    ";     animations); 0 = Starkiller's own stance\n"
    "RifleStance=1\n"
    "; 1 = the gun comes up to the shoulder and points where you aim while you aim or fire (stormtrooper aim\n"
    ";     animations; it lowers again a moment after the last shot); 0 = the relaxed carry pose\n"
    "AimPose=1\n"
    "; 1 = shots kick the camera up / sideways (recoil climb); 0 = the view stays still (recoil shows in the spread\n"
    ";     and the gun)\n"
    "CameraRecoil=0\n"
    "; 1 = the crouch key toggles crouching; 0 = hold it to stay down\n"
    "CrouchToggle=1\n"
    "; movement speed while crouched (x the run speed, 0.3-0.8)\n"
    "CrouchSpeed=0.45\n"
    "; seconds of heat-free fire after a perfect vent (Battlefront II overdrive; a full overheat earns all of it, a\n"
    ";     small manual vent less); 0 = off\n"
    "OverdriveTime=3.0\n"
    "; 1 = the mouse pointer stays inside the game window while you play, so it can no longer drift onto another\n"
    ";     screen and click there (menus, pause and Alt+Tab free it)\n"
    "LockMouse=1\n"
    "; 1 = next / previous class goes through your favourites only (mark them in the wheel or terminal)\n"
    "CycleFavourites=0\n"
    "; 1 = the class wheel and the requisition terminal slow the game down while open\n"
    "WheelSlowMotion=1\n"
    "; 1 = the requisition terminal puts the highlighted outfit on as a live preview\n"
    "TerminalPreview=1\n"
    "; 1 = more log lines in DK_Blaster.log\n"
    "Debug=0\n";

static float GetF(const char* sec, const char* key, float def) {
    char v[64]; GetPrivateProfileStringA(sec, key, "", v, sizeof v, s_ini);
    if (!v[0]) return def;
    const size_t n = strlen(v);   // MOD OPTIONS sliders write percentages ("120%" = 1.2)
    return v[n - 1] == '%' ? (float)atof(v) / 100.0f : (float)atof(v);
}
static int GetI(const char* sec, const char* key, int def) { return (int)GetPrivateProfileIntA(sec, key, def, s_ini); }
static int GetKey(const char* key, int def) {
    char v[64]; GetPrivateProfileStringA("Blaster", key, "", v, sizeof v, s_ini);
    return v[0] ? ParseKey(v, def) : def;
}
static float Clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

void LoadConfig() {
    sprintf_s(s_ini, "%sDK_Blaster.ini", g_dir);
    if (GetFileAttributesA(s_ini) == INVALID_FILE_ATTRIBUTES) {
        FILE* f = nullptr;
        if (!fopen_s(&f, s_ini, "w") && f) { fputs(kDefaultIni, f); fclose(f); }
    }
    Config c;
    c.enabled = GetI("Blaster", "Enabled", 1) != 0;
    c.kToggle = GetKey("ToggleKey", c.kToggle); c.kFire = GetKey("FireKey", c.kFire); c.kAim = GetKey("AimKey", c.kAim);
    c.kVent = GetKey("VentKey", c.kVent); c.kGrenade = GetKey("GrenadeKey", c.kGrenade); c.kAbility = GetKey("AbilityKey", c.kAbility);
    c.kSprint = GetKey("SprintKey", c.kSprint); c.kShoulder = GetKey("ShoulderKey", c.kShoulder);
    c.kNext = GetKey("NextClassKey", c.kNext); c.kPrev = GetKey("PrevClassKey", c.kPrev); c.kWheel = GetKey("ClassWheelKey", c.kWheel);
    c.kRoll = GetKey("RollKey", c.kRoll); c.kSwap = GetKey("SwapWeaponKey", c.kSwap);
    c.kCrouch = GetKey("CrouchKey", c.kCrouch);
    c.gamepad = GetI("Blaster", "Gamepad", 1) != 0;
    char v[64]; GetPrivateProfileStringA("Blaster", "GamepadToggle", "BACK+DOWN", v, sizeof v, s_ini);
    c.pToggle = strcmp(v, "0") ? ParsePad(v) : 0;
    c.lookSens = Clampf(GetF("Blaster", "LookSensitivity", 1.0f), 0.2f, 3.0f);
    c.mouseSens = Clampf(GetF("Blaster", "MouseSensitivity", 1.0f), 0.1f, 5.0f);
    c.invertY = GetI("Blaster", "InvertLookY", 0) != 0;
    c.sensAds = Clampf(GetF("Blaster", "AimSensitivity", 0.55f), 0.1f, 2.0f);
    c.sensScope = Clampf(GetF("Blaster", "ScopeSensitivity", 0.35f), 0.05f, 2.0f);
    c.aimAssist = GetI("Blaster", "AimAssist", 1) != 0;
    c.adsMove = Clampf(GetF("Blaster", "AimMoveSpeed", 0.6f), 0.3f, 1.0f);
    c.shoulder = GetI("Blaster", "Shoulder", 1) < 0 ? -1 : 1;
    c.camSide = Clampf(GetF("Blaster", "CameraSide", 0.62f), 0.0f, 1.5f);
    c.camUp = Clampf(GetF("Blaster", "CameraUp", 0.18f), -0.5f, 1.0f);
    c.camDistance = Clampf(GetF("Blaster", "CameraDistance", 3.0f), 0.0f, 8.0f);
    c.fovHip = Clampf(GetF("Blaster", "FOV", 0.0f), 0.0f, 90.0f);
    c.crosshair = GetI("Blaster", "Crosshair", 0) & 3;
    GetPrivateProfileStringA("Blaster", "CrosshairColor", "FFFFFF", v, sizeof v, s_ini);
    c.crossColor = strtoul(v, nullptr, 16) & 0xFFFFFF;
    c.crossScale = Clampf(GetF("Blaster", "CrosshairSize", 1.0f), 0.5f, 2.0f);
    c.hudScale = Clampf(GetF("Blaster", "HudScale", 1.0f), 0.6f, 1.6f);
    c.hitSound = GetI("Blaster", "HitSound", 1) != 0;
    c.damageScale = Clampf(GetF("Blaster", "DamageScale", 1.0f), 0.1f, 10.0f);
    c.heavyDamage = GetI("Blaster", "HeavyDamage", 1) != 0;
    c.autoDeflect = GetI("Blaster", "AutoDeflect", 0) != 0;
    c.damageTaken = Clampf(GetF("Blaster", "DamageTaken", 0.6f), 0.2f, 1.5f);
    c.bossReturn = GetI("Blaster", "BossReturn", 1) != 0;
    c.keepMode = GetI("Blaster", "KeepMode", 1) != 0;
    c.killCredit = GetI("Blaster", "KillCredit", 1) != 0;
    c.outfitFollows = GetI("Blaster", "OutfitFollows", 1) != 0;
    c.loadoutFollows = GetI("Blaster", "LoadoutFollows", 1) != 0;
    c.rifleStance = GetI("Blaster", "RifleStance", 1) != 0;
    c.aimPose = GetI("Blaster", "AimPose", 1) != 0;
    c.cameraRecoil = GetI("Blaster", "CameraRecoil", 0) != 0;
    c.crouchToggle = GetI("Blaster", "CrouchToggle", 1) != 0;
    c.crouchMove = Clampf(GetF("Blaster", "CrouchSpeed", 0.45f), 0.3f, 0.8f);
    c.overdriveTime = Clampf(GetF("Blaster", "OverdriveTime", 3.0f), 0.0f, 6.0f);
    c.lockMouse = GetI("Blaster", "LockMouse", 1) != 0;
    c.cycleFavourites = GetI("Blaster", "CycleFavourites", 0) != 0;
    c.wheelSlowmo = GetI("Blaster", "WheelSlowMotion", 1) != 0;
    c.terminalPreview = GetI("Blaster", "TerminalPreview", 1) != 0;
    c.debug = GetI("Blaster", "Debug", 0) != 0;
    cfg = c;
    rnd::SetUserScale(cfg.hudScale);
}
// settings that act at once on a drawn blaster
static void AfterConfig() {
    if (!cfg.enabled && (g_mode == M_BLASTER || s_suspend)) {   // (a kept wish too: no return to a switched-off mod)
        s_want = false; s_suspend = nullptr;
        ModeExit("mod disabled in settings", false);
        return;
    }
    if (g_mode == M_BLASTER) { if (cfg.rifleStance) HoldApply(); else HoldRestore(); }
}

static void WatchConfig() {
    static double next;
    if (RealNow() < next) return;
    next = RealNow() + 1.0;
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(s_ini, GetFileExInfoStandard, &a)) return;
    if (CompareFileTime(&a.ftLastWriteTime, &s_iniTime) == 0) return;
    const bool first = !s_iniTime.dwLowDateTime && !s_iniTime.dwHighDateTime;
    s_iniTime = a.ftLastWriteTime;
    if (first) return;
    LoadConfig();
    Log("settings reloaded from DK_Blaster.ini");
    AfterConfig();
}

// ---------------------------------------------------------------------------------------------- feedback
void Sound(const char* cue) {
    if (!cue || !*cue) return;
    LuaCall("DKB.sound('%s', true) return 'ok'", cue);
}
void Refuse(const char* reason) {
    HudToast(reason, 0xFF9A3C, 1.8f);
    Snd("deny");
    Log("refused: %s", reason);
}

// ---------------------------------------------------------------------------------------------- mode
// The boss field is written when a fight shows its boss meter and nothing clears it when the boss dies (it stays until
// the level ends): a boss counts only while its ren still has health left, as in DK_VaderSwap (without the health read,
// the field alone)
static DWORD BossOf(DWORD ren) {
    BYTE* pc = PlayerComp(ren);
    DWORD b = 0;
    if (pc) SafeRead(pc + s_bossOff, &b, 4);
    if (!b || !RenHealthOk()) return b;
    return RenHealth(b) > 0.0f ? b : 0;
}
static const char* StateText(const char* st) {
    if (!strcmp(st, "dead")) return "Not while down";
    if (!strcmp(st, "qte")) return "Finish the Force duel first";
    if (!strcmp(st, "sync")) return "Not during a finishing move";
    if (!strcmp(st, "cine")) return "Not during a cutscene";
    if (!strcmp(st, "cam")) return "Not with this camera";
    if (!strcmp(st, "noplayer")) return "No player";
    return st;
}

// Why blaster mode cannot start now (nullptr = it can). Native checks first (cheap), then the Lua state.
const char* ModeRefusal() {
    if (!cfg.enabled) return "Blaster Combat is off (MOD OPTIONS)";
    if (!s_active) return "Blaster Combat could not start (see DK_Blaster.log)";
    if (GameState() != 2) return "Not available here";
    if (CinematicNow()) return "Not during a cutscene";
    DWORD p = PlayerRen();
    if (!p) return "No player";
    if (cfg.bossReturn && BossOf(p)) return "Boss fight - the saber stays out";
    const char* st = LuaCall("return DKB.state()");
    if (!strncmp(st, "error", 5)) { Log("state check failed: %s", st); return "Not available right now"; }
    if (strcmp(st, "ok")) return StateText(st);
    return nullptr;
}

// The situation now, as a short code for the status line (tests wait for "ok": the player has the camera)
static const char* StateNow() {
    if (GameState() != 2) return "notgame";
    if (CinematicNow()) return "cine";
    const DWORD p = PlayerRen();
    if (!p) return "noplayer";
    if (cfg.bossReturn && BossOf(p)) return "boss";
    static char st[32];
    strncpy_s(st, LuaCall("return DKB.state()"), _TRUNCATE);
    return st;
}

static bool s_suspending;          // ModeExit for a suspend: the outfit stays
static bool s_weaponPinned;        // dev `weapon` before `on`: that gun, not the class's
static int s_armClass = -1;        // the class blaster mode was last armed with
const WeaponDef* SetWeapon(const WeaponDef* w);

static bool s_scopeHidden, s_scopeHideOff;
static int s_scopeHides;
static void ScopeHide(bool on) {   // scoped: the gun prop is hidden, the lens shows only the target
    on = on && !s_scopeHideOff;
    if (on == s_scopeHidden) return;
    s_scopeHidden = on;
    if (on) s_scopeHides++;
    LuaCall("return DKB.scopehide(%s)", on ? "true" : "false");
}
static void ApplyArmed(bool on) {
    ScopeHide(false);
    InputSetMute(on);
    CamSetSens(1.0f);
    CamSetAssist(0.0f);
    CamSetWeight(on ? 1.0f : 0.0f);
    CamSetAds(1.0f);
    HudSetVisible(on ? 1.0f : 0.0f);
    g_hudScopeAmt = 0.0f;
    CamSetSway(0.0f);
    g_hudHealth = -1.0f;
    if (!on) { SetFacing(nullptr, 0); SetStrafe(false); SetStickFix(false); }
}

// sniper scopes sway in a slow figure eight; holding sprint while scoped (standing) steadies the aim for up to 4 s,
// then the breath runs out: stronger sway until half of it is back, and the key must be pressed again
static float s_breath = 1.0f;
static bool s_breathOut, s_breathWas, s_breathRelease;
static int s_breathHolds, s_breathOuts;
static void Breath(bool scoped, bool hold, float dt) {
    if (!hold) s_breathRelease = false;
    const bool holding = scoped && hold && !s_breathOut && !s_breathRelease;
    if (holding) {
        if (!s_breathWas) s_breathHolds++;
        s_breath -= dt / 4.0f;
        if (s_breath <= 0.0f) { s_breath = 0.0f; s_breathOut = s_breathRelease = true; s_breathOuts++; }
    } else {
        s_breath = s_breath + dt / 3.0f > 1.0f ? 1.0f : s_breath + dt / 3.0f;
        if (s_breathOut && s_breath >= 0.5f) s_breathOut = false;
    }
    s_breathWas = holding;
    CamSetSway(!scoped || holding ? 0.0f : s_breathOut ? 1.7f : 1.0f);
    g_hudBreath = s_breath;
    g_hudBreathState = holding ? 1 : s_breathOut ? 2 : 0;
}

// a gun this level can show: ours from the pack, else only what the level itself has (the E-11 is the safest bet)
static const WeaponDef* Usable(const WeaponDef* w) {
    if (!w) w = WeaponById("e11s");
    if (PackChained() && !PackWeaponOk(w) && PackWeaponOk(WeaponById("e11s"))) {
        Log("weapon %s is not in our pack - using the E-11", w->id);
        w = WeaponById("e11s");
    }
    return w;
}

bool ModeEnter(const char* why) {
    SoundsEnsure();   // our pack first: the guns, effects and bolt abilities of every level
    // our own outfit swap for the class in hand (the draw's outfit going on) re-arms the new body with the gun in hand;
    // a new class brings its own (2026-10-26: the draw's swap turned a chosen E-11 into the class's A295)
    const bool keep = s_weaponPinned || (!strcmp(why, "outfit") && OutfitClass() == s_armClass);
    OutfitModeEnter();
    if (!keep) { if (const WeaponDef* lw = OutfitWeapon()) s_weapon = lw; }
    s_weaponPinned = false;
    s_armClass = OutfitClass();
    s_weapon = Usable(s_weapon);
    const char* r = LuaCall("return DKB.arm('%s')", s_weapon->actor);
    if (strcmp(r, "ok")) {
        Log("arm: %s", r);
        if (!strcmp(r, "noplayer") || !strncmp(r, "error", 5)) { LuaCall("return DKB.disarm()"); Refuse("Could not draw the blaster"); return false; }
    }
    s_ren = PlayerRen();
    CombatReset(s_weapon);
    AbilityEnsurePool();
    ApplyArmed(true);
    SetStrafe(true);
    HoldApply();   // the two-handed rifle stance (dkb_hold.cpp)
    g_mode = M_BLASTER;
    s_suspend = nullptr;
    s_enterT = GameNow();
    if (strcmp(why, "outfit")) Snd("equip");   // an outfit swap re-arms the new body: its own sound plays
    Log("blaster mode ON (%s) - %s", why, s_weapon->name);
    if (g_api && g_api->version >= 2) g_api->Event("DK_Blaster", "mode on");   // (v1 loaders: FPS Fix 1.0.2 / Lighting Fix 1.0.0)
    return true;
}

void ModeExit(const char* why, bool emergency) {
    const bool was = g_mode == M_BLASTER;
    LuaCall("return DKB.disarm()");   // (also lifts the aiming speed cap)
    HoldRestore();
    s_moveCapV = -1.0f;
    s_crouch = false;
    CrouchSet(0.0f);
    AbilityReset();
    if (!s_suspending) OutfitModeExit();
    ApplyArmed(false);
    g_mode = M_SABER;
    if (emergency) { CamSetWeight(0.0f); }
    else if (was) CamHandBack();
    if (was || emergency) {
        Log("blaster mode OFF (%s)%s", why, emergency ? " [emergency reset]" : "");
        if (g_api && g_api->version >= 2) g_api->Event("DK_Blaster", emergency ? "mode off (emergency)" : "mode off");
    }
}

// suspend: the shooter steps aside, the wish stays
static void Suspend(const char* reason) {
    if (g_mode != M_BLASTER) return;
    s_suspending = true;
    ModeExit(reason, false);
    s_suspending = false;
    s_suspend = reason;
    s_clearSince = 0;
    Log("suspended: %s", reason);
}

static void EmergencyReset(const char* why) {
    s_want = false;
    s_suspend = nullptr;
    ModeExit(why, true);
    OutfitHome();
    InputSetMute(false); CamSetSens(1.0f); SetStrafe(false); SetFacing(nullptr, 0); SetStickFix(false);
    HudToast("Blaster Combat reset - everything back to normal", 0x7CE0FF, 2.2f);
}

bool BlasterOn(const char* why) {
    if (g_mode == M_BLASTER) return true;
    if (const char* r = ModeRefusal()) { Refuse(r); return false; }
    if (!ModeEnter(why)) return false;
    s_want = true; s_suspend = nullptr;
    return true;
}
const WeaponDef* SetWeapon(const WeaponDef* w) {
    if (!w) return s_weapon;
    s_weapon = Usable(w);
    if (g_mode == M_BLASTER) {
        LuaCall("return DKB.setgun('%s')", s_weapon->actor);
        CombatReset(s_weapon);
    }
    return s_weapon;
}

// ---------------------------------------------------------------------------------------------- frame
static bool s_faceWas;
static int s_faceToggles, s_sprintStarts, s_viewLost;   // status: facing on / off changes, sprint starts, frames without our view
static int s_faceMode = 3;   // dev A/B: 1 = body faces the aim, 2 = stick along the camera's own axes
int g_sprintBody = 1;
// The body over 100 degrees off the view while armed (rolls aside): counted for the status / tests. While the body should
// follow the aim (no sprint) for over 0.35 s it is logged with the state that decides the facing and put right: strafe
// taken again if the game dropped it, and the next facing call returns the full aim (the strafe snaps the body to it);
// again every 0.5 s while it stays off. 1.2.0 left the body facing the camera for seconds after a mouse turn while
// crouch-walking or reversing out of a sprint (user video 2026-10-27, tests\blaster_flick.py).
static int s_offFrames, s_offEvents;
static float s_offT, s_offLongest, s_offMax, s_offNext = 0.35f;
static bool s_offWatch = true;   // dev `offwatch 0`: count only (tests: 1.2.0 for comparison)
static void OffAimWatch(double rn, bool known, float off, bool face, bool sprint) {
    if (!known || off <= 100.0f || rn - s_rollAt < 0.8) { s_offT = 0.0f; s_offNext = 0.35f; return; }
    s_offFrames++;
    s_offT += s_realDt;
    if (off > s_offMax) s_offMax = off;
    if (s_offT > s_offLongest) s_offLongest = s_offT;
    if (!s_offWatch || !face || sprint || s_offT < s_offNext) return;
    const bool first = s_offNext < 0.4f;
    s_offNext = s_offT + 0.5f;
    const bool retake = !StrafeFlagSet();
    if (first) {
        s_offEvents++;
        int cm = -1;
        if (BYTE* cs = CamSys()) SafeRead(cs + 0x4884, &cm, 4);
        if (s_offEvents <= 20)
            Log("body %.0f deg off the aim for %.2f s (stick %.2f,%.2f speed %.2f, %s, %s, camera mode %d) - turned to the aim%s",
                off, s_offT, s_stick[0], s_stick[1], s_speed, StrafeStatus(), FacingStatus(), cm, retake ? ", strafe taken again" : "");
    }
    if (retake) StrafeRetake();
    FacingSnap();
}
static void Frame() {
    const double rn = RealNow();
    s_realDt = s_lastReal > 0 ? (float)(rn - s_lastReal) : 0.0f;
    if (s_realDt > 0.1f) s_realDt = 0.1f;
    s_lastReal = rn;
    const float gn = GameNow();
    s_frameDt = gn - s_lastGame;
    if (s_frameDt < 0.0f || s_frameDt > 0.25f) s_frameDt = 0.0f;
    s_lastGame = gn;

    WatchConfig();
    InputPoll(s_realDt);
    OutfitTick(s_realDt);   // outfit requests, class keys, wheel / terminal (an open menu takes the buttons)
    HoldTick();
    const DWORD p = PlayerRen();
    const bool playing = InGameplay();
    if (CinematicNow() || !p) { s_crouch = false; CrouchSet(0.0f); }   // cutscenes and respawns start standing
    CrouchTick(s_realDt, playing, CinematicNow() || !p);
    InputMouseLock(playing);

    // the player ren changed (death respawn, checkpoint, level change, costume swap): ours died with it
    if (p != s_ren && s_ren) {
        const bool armed = g_mode == M_BLASTER;
        StrafeForget();
        LuaCall("DKB.reset() return 'ok'");
        s_ren = 0;
        s_havePrev = false; s_vy = 0.0f;
        if (OutfitOwnsRenChange() && p) {   // our own outfit swap: the blaster goes onto the new body
            if (armed) {
                if (ModeEnter("outfit")) OutfitRearmed();
                else { s_want = false; s_suspend = nullptr; ModeExit("could not re-arm after the outfit swap", false); }
            }
        } else {
            if ((armed || s_suspend) && s_want && cfg.keepMode) {   // the choice outlives the body: back on the new one
                if (armed) Suspend("respawn / level change");
                s_suspend = "respawn / level change"; s_clearSince = 0;
                Log("kept: blaster mode comes back once play resumes");
            } else if (armed || s_suspend) {
                ModeExit("player respawned / level changed", false);
                s_want = false; s_suspend = nullptr;
            }
            OutfitRespawned();
        }
    }

    // emergency: hold the toggle 3 s (works in any mode); a normal press toggles on release
    if (g_in.holdTime[A_TOGGLE] >= 3.0f && !s_emergencyDone) { s_emergencyDone = true; EmergencyReset("toggle held 3 s"); }
    const bool toggled = g_in.released[A_TOGGLE] && !s_emergencyDone;
    if (!g_in.held[A_TOGGLE]) s_emergencyDone = false;

    if (toggled) {   // switched off in MOD OPTIONS: ModeRefusal says so
        const char* blocked = s_suspend ? ModeRefusal() : nullptr;
        if (s_suspend && s_want && blocked && cfg.enabled && s_active) {   // a press while a cutscene has it keeps the return
            static char msg[96];
            sprintf_s(msg, "%s (the blaster comes back after it)", blocked);
            Refuse(msg);
        } else if (s_suspend && s_want && !blocked) {   // over (inside the 0.8 s settle): back now
            Log("resuming after: %s (toggle)", s_suspend);
            s_suspend = nullptr;
            if (!ModeEnter("resume")) s_want = false;
        } else if (g_mode == M_BLASTER || s_suspend) {
            s_want = false; s_suspend = nullptr;
            Snd("holster");
            ModeExit("toggle", false);
        } else if (const char* why = ModeRefusal()) Refuse(why);
        else if (ModeEnter("toggle")) s_want = true;
    }

    // watchdog
    if (g_mode == M_BLASTER) {
        if (CinematicNow()) Suspend("cutscene");
        else if (cfg.bossReturn && p && BossOf(p)) Suspend("boss fight");
        else if (rn >= s_nextState && GameState() == 2) {
            s_nextState = rn + 0.1;
            const char* st = LuaCall("return DKB.state()");
            const char* gone = !strcmp(st, "dead") ? "player died" : !strcmp(st, "noplayer") ? "no player" : nullptr;
            if (gone && cfg.keepMode) Suspend(gone);   // the outfit stays on the fallen body; the respawn brings it back
            else if (gone) { s_want = false; ModeExit(gone, false); }
            else if (strcmp(st, "ok") && strncmp(st, "error", 5)) {
                static char why[32]; strncpy_s(why, st, _TRUNCATE);
                if ((cfg.bossReturn || strcmp(st, "qte")) && !(s_devStayCam && !strcmp(st, "cam"))) Suspend(why);
            }
        }
    } else if (s_suspend && s_want) {   // come back when the situation is over for 0.8 s
        bool clear = GameState() == 2 && !CinematicNow() && p && !(cfg.bossReturn && BossOf(p));
        if (clear && rn >= s_nextState) {
            s_nextState = rn + 0.1;
            clear = !strcmp(LuaCall("return DKB.state()"), "ok");
            if (!clear) s_clearSince = 0;
        }
        if (!clear) s_clearSince = 0;
        else if (!s_clearSince) s_clearSince = rn;
        else if (rn - s_clearSince > 0.8) {
            Log("resuming after: %s", s_suspend);
            s_suspend = nullptr;
            if (ModeEnter("resume")) HudToast("Blaster ready", 0x7CE0FF, 1.0f);
            else s_want = false;
        }
    }

    // armed
    const bool armed = g_mode == M_BLASTER && InGameplay();
    if (armed) {
        float pos[3];
        if (RenPos(p, pos)) {
            if (s_havePrev && s_frameDt > 0.0f) {
                const float dx = pos[0] - s_prevPos[0], dz = pos[2] - s_prevPos[2];
                const float sp = sqrtf(dx * dx + dz * dz) / s_frameDt;
                s_speed += (sp - s_speed) * (1.0f - expf(-10.0f * s_frameDt));
                s_vy += ((pos[1] - s_prevPos[1]) / s_frameDt - s_vy) * (1.0f - expf(-20.0f * s_frameDt));
            }
            memcpy(s_prevPos, pos, sizeof pos); s_havePrev = true;
        }
        const WeaponDef* w = g_cb.w;
        // scoped (a scope gun with aim held): the sprint key only steadies the aim. It used to start a sprint as soon as
        // the player moved, the sprint dropped the scope, and the slower scoped walk flipped it back (scope flicker).
        // A sprint follows the move stick, never the measured speed: it starts with the stick pushed (> 0.5) within 55
        // degrees of the view and holds up to 80 degrees. Sprinting sideways / backwards used to start the game's own
        // 180-degree run turn, which stopped the player, the speed rule dropped the sprint, strafe snapped the body back
        // to the view, the speed rose again and the sprint came back: the body spun on the spot.
        const bool steadyKey = w && w->scope && g_in.held[A_AIM];
        static bool s_sprintOn;
        InputMoveStick(&s_stick[0], &s_stick[1]);
        const float sm = sqrtf(s_stick[0] * s_stick[0] + s_stick[1] * s_stick[1]);
        const float sa = sm > 0.05f ? atan2f(fabsf(s_stick[0]), s_stick[1]) * 57.2958f : 180.0f;
        // the body against the view: the stick is read along the view, so a quick mouse turn while sprinting reversed the
        // run without touching the stick and the game's own run turn ran the body toward the camera for a second. A
        // sprint also needs the body within 55 degrees of the view to start and 100 to hold; past that strafe swings it.
        float bodyYaw = 0.0f, off = 0.0f;
        const bool offKnown = g_cam.valid && RenYaw(p, &bodyYaw);
        if (offKnown) {
            float d = bodyYaw - atan2f(g_cam.fwd[0], g_cam.fwd[2]);
            while (d > 3.14159265f) d -= 6.2831853f;
            while (d < -3.14159265f) d += 6.2831853f;
            off = fabsf(d) * 57.2958f;
        }
        const bool bodyNear = !g_sprintBody || !offKnown || off < (s_sprintOn ? 100.0f : 55.0f);
        const bool sprint = g_in.held[A_SPRINT] && !steadyKey && bodyNear &&
                            (s_sprintOn ? sm > 0.25f && sa < 80.0f : sm > 0.5f && sa < 55.0f);
        if (sprint && !s_sprintOn) s_sprintStarts++;
        s_sprintOn = sprint;
        s_sprintNow = sprint;
        const bool aim = g_in.held[A_AIM] && !sprint;
        if (g_in.pressed[A_SHOULDER]) cfg.shoulder = -cfg.shoulder;   // silent: the view sliding over is the feedback
        // crouch: the crouch key toggles it (or is held, CrouchToggle=0); sprinting, a roll, a jump / fall and a
        // knock-down stand you up. The Alt of an Alt+Tab toggled it just before the game lost the focus: undone.
        if (CrouchAvailable()) {
            const bool was = s_crouch;
            if (cfg.crouchToggle) {
                if (g_in.pressed[A_CROUCH]) { s_crouch = !s_crouch; s_crouchAt = InputFocused() ? rn : 0.0; }
                if (s_crouchAt > 0.0 && rn - s_crouchAt < 0.6 && !InputFocused()) { s_crouch = !s_crouch; s_crouchAt = 0.0; }
            } else s_crouch = g_in.held[A_CROUCH];
            if (sprint || g_in.pressed[A_ROLL] || fabsf(s_vy) > 2.5f || CrouchKnocked()) s_crouch = false;
            if (s_crouch != was) s_crouchN += s_crouch;   // silent: the body going down / up is the feedback
        } else s_crouch = false;
        CrouchSet(s_crouch ? kCrouchDepth : 0.0f);
        CamSetAds(aim && w ? w->adsZoom : 1.0f);
        CamSetSens(aim ? (w && w->scope ? cfg.sensScope : cfg.sensAds) : 1.0f);
        // aiming and crouching slow you: the game's own max run speed, set on changes only
        float capV = aim && cfg.adsMove < 0.99f ? kRunSpeed * cfg.adsMove : -1.0f;
        if (s_crouch) capV = capV < 0.0f ? kRunSpeed * cfg.crouchMove : fminf(capV, kRunSpeed * cfg.crouchMove);
        if (fabsf(capV - s_moveCapV) > 0.01f) {
            s_moveCapV = capV;
            LuaCall("return DKB.movecap(%.2f)", capV);
        }
        if (g_in.pressed[A_ROLL] && rn >= s_rollReady) {   // combat roll: the game's own Force Dash in the stick's direction
            InputDash(0.12f);
            s_rollReady = rn + kRollCooldown;
            s_rolls++;
            s_rollAt = rn;
            CamKick(-0.8f, 0.0f, 2.5f, 0.0f);
        }
        g_hudScopeAmt = aim && w && w->scope ? 1.0f : 0.0f;
        Breath(g_hudScopeAmt > 0.5f, g_in.held[A_SPRINT], s_frameDt);
        if (rn >= s_nextHealth) {   // low-health warning: 4 reads a second
            s_nextHealth = rn + 0.25;
            float hp[1];
            g_hudHealth = LuaNums(hp, 1, "return DKB.heal(0)") >= 1 ? hp[0] : -1.0f;
        }
        if (rn >= s_nextSaberGuard) {   // the saber stays away: a cinematic's end (radio dialogue too) hands it back
            s_nextSaberGuard = rn + 0.25;
            float g[2];
            if (LuaNums(g, 2, "return DKB.guard()") >= 2 && (g[0] > 0.0f || g[1] > 0.0f)) {
                s_saberCine += (int)g[0]; s_saberHilt += (int)g[1];
                Log("saber put away again (%s)", g[0] > 0.0f ? "cinematic end" : "hilt shown");
            }
        }
        SetStrafe(!sprint);
        const bool face = (s_faceMode & 1) && g_cam.valid && !sprint;
        if (face != s_faceWas) { s_faceWas = face; s_faceToggles++; }
        if (!g_cam.valid) s_viewLost++;
        if (face) SetFacing(g_cam.fwd, p); else SetFacing(nullptr, 0);
        OffAimWatch(rn, offKnown, off, face, sprint);
        SetStickFix((s_faceMode & 2) && g_cam.valid);
        CombatSetMotion(s_speed, aim, sprint);
        CombatTick(s_frameDt, true);
        // the aimed gun pose: up while aiming and for a moment after the trigger (hip fire); sprint, a roll and a
        // knock-down lower it
        if (g_in.held[A_FIRE]) s_fireAt = rn;
        s_gunUp = !sprint && rn - s_rollAt > 0.6 && !CrouchBehavior() && (aim || rn - s_fireAt < 1.1);
        AbilityTick(true);
        if (rn >= s_nextEnemyRay && g_cam.valid) {   // crosshair over a living AI: 15 checks per second
            s_nextEnemyRay = rn + 1.0 / 15.0;
            const float* e = g_cam.eye; const float* f = g_cam.fwd;
            float n[6];
            const float r = w ? w->range * 2.0f : 150.0f;
            int got = LuaNums(n, 6, "return DKB.ray(%.3f,%.3f,%.3f,%.3f,%.3f,%.3f)", e[0] + f[0] * 2.5f, e[1] + f[1] * 2.5f,
                              e[2] + f[2] * 2.5f, e[0] + f[0] * r, e[1] + f[1] * r, e[2] + f[2] * r);
            const bool onEnemy = got >= 6 && n[0] == 1.0f && n[5] == 1.0f;
            g_aimHitOk = got >= 4 && n[0] == 1.0f;
            if (g_aimHitOk) { g_aimHit[0] = n[1]; g_aimHit[1] = n[2]; g_aimHit[2] = n[3]; }
            HudSetOnEnemy(onEnemy);
            CamSetAssist(cfg.aimAssist && g_in.pad && onEnemy ? 1.0f : 0.0f);
        }
    } else {
        g_aimHitOk = false;
        CombatTick(s_frameDt, false);
        AbilityTick(false);
        if (g_mode == M_BLASTER) { SetFacing(nullptr, 0); SetStickFix(false); }
        if (!(g_mode == M_BLASTER && GameState() == 2 && !CinematicNow())) s_gunUp = false;   // a pause keeps the pose
        s_sprintNow = false;
    }
    ScopeHide(armed && g_hudScopeAmt > 0.5f);
    AimTick(g_mode == M_BLASTER && GameState() == 2 && !CinematicNow(), s_gunUp);
    CamTick(s_realDt);
    CombatFrameHits();
}

// ---------------------------------------------------------------------------------------------- dev commands
static const char* kActNames[A_COUNT] = { "fire", "aim", "vent", "grenade", "ability", "sprint", "shoulder", "next", "prev", "wheel", "toggle", "roll", "swap", "crouch" };

static struct { double sum, max; long n; } s_perf[2];
double PerfNow() {
    static LARGE_INTEGER f;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    return (double)q.QuadPart * 1e6 / (double)f.QuadPart;
}
void PerfAdd(int slot, double us) {
    if (slot < 0 || slot > 1) return;
    s_perf[slot].sum += us; s_perf[slot].n++;
    if (us > s_perf[slot].max) s_perf[slot].max = us;
}

static int Command(const char* args, char* out, int size) {
    char cmd[64] = {}, a1[128] = {}, a2[64] = {};
    sscanf_s(args ? args : "", "%63s %127s %63s", cmd, (unsigned)sizeof cmd, a1, (unsigned)sizeof a1, a2, (unsigned)sizeof a2);
    if (!cmd[0] || !_stricmp(cmd, "status")) {
        _snprintf_s(out, size, _TRUNCATE,
            "DK_Blaster %s active=%d enabled=%d mode=%s want=%d suspend=%s weapon=%s speed=%.2f stick=%.2f,%.2f\n"
            "cam: %s\ncombat: %s\nhud: %s\ninput: mute %d muted %d pad %d rolls %d dashFrames %d | state %s | boss %08X | lua errors %d (%s)\n"
            "crouch: on %d n %d vy %.2f cap %.2f sprint %d gunUp %d | %s\naim: %s\n%s\n"
            "flow: face toggles %d strafe retakes %d sprint starts %d view lost %d saber back %d/%d\n%s\n"
            "offaim: frames %d longest %.2f s max %.0f deg events %d | %s",
            DK_BLASTER_VERSION, s_active, cfg.enabled, g_mode == M_BLASTER ? "blaster" : "saber", s_want,
            s_suspend ? s_suspend : "-", s_weapon ? s_weapon->id : "-", s_speed, s_stick[0], s_stick[1], CamStatus(), CombatStatus(), HudStatus(),
            g_mode == M_BLASTER, InputMutedCount(), g_in.pad, s_rolls, InputDashCount(), StateNow(),
            PlayerRen() ? BossOf(PlayerRen()) : 0, lua::Errors(), lua::LastError(), (int)s_crouch, s_crouchN, s_vy, s_moveCapV, (int)s_sprintNow, (int)s_gunUp,
            CrouchStatus(), AimStatus(), InputLockStatus(), s_faceToggles, StrafeRetakes(), s_sprintStarts, s_viewLost,
            s_saberCine, s_saberHilt, FacingStatus(), s_offFrames, s_offLongest, s_offMax, s_offEvents, StrafeStatus());
        return 1;
    }
    if (!_stricmp(cmd, "crouch")) {   // crouch on|off|dump
        if (!_stricmp(a1, "dump")) return CrouchDump(out, size);
        if (!_stricmp(a1, "on") || !_stricmp(a1, "off")) {
            s_crouch = !_stricmp(a1, "on");
            _snprintf_s(out, size, _TRUNCATE, "crouch %s", s_crouch ? "on" : "off");
            return 1;
        }
        _snprintf_s(out, size, _TRUNCATE, "%s", CrouchStatus());
        return 1;
    }
    if (!_stricmp(cmd, "on")) {
        if (g_mode == M_BLASTER) { strcpy_s(out, size, "already on"); return 1; }
        const char* why = ModeRefusal();
        if (why) { Refuse(why); _snprintf_s(out, size, _TRUNCATE, "refused: %s", why); return 1; }
        bool ok = ModeEnter("dev");
        if (ok) s_want = true;
        strcpy_s(out, size, ok ? "on" : "failed");
        return 1;
    }
    if (!_stricmp(cmd, "off")) { s_want = false; s_suspend = nullptr; ModeExit("dev", false); strcpy_s(out, size, "off"); return 1; }
    if (!_stricmp(cmd, "emergency")) { EmergencyReset("dev"); strcpy_s(out, size, "reset"); return 1; }
    if (!_stricmp(cmd, "hold")) {   // hold <action> <seconds>
        for (int i = 0; i < A_COUNT; i++) if (!_stricmp(a1, kActNames[i])) {
            InputDevSet(i, a2[0] ? (float)atof(a2) : 0.15f);
            _snprintf_s(out, size, _TRUNCATE, "holding %s", a1);
            return 1;
        }
        strcpy_s(out, size, "unknown action"); return 1;
    }
    if (!_stricmp(cmd, "look")) {   // look <rx> <ry> <seconds>: added to the look axes (mouse / stick path)
        float rx = 0, ry = 0, t = 0.5f;
        sscanf_s(args, "%*s %f %f %f", &rx, &ry, &t);
        InputDevLook(rx, ry, t);
        _snprintf_s(out, size, _TRUNCATE, "look %.2f %.2f for %.2f s", rx, ry, t);
        return 1;
    }
    if (!_stricmp(cmd, "stick")) {   // stick <x> <y> <seconds>: the game's move stick reads x / y (analog pad)
        float x = 0, y = 0, t = 0.5f;
        sscanf_s(args, "%*s %f %f %f", &x, &y, &t);
        InputDevStick(x, y, t);
        _snprintf_s(out, size, _TRUNCATE, "stick %.2f %.2f for %.2f s", x, y, t);
        return 1;
    }
    if (!_stricmp(cmd, "camlevel")) { g_camLevel = atoi(a1); _snprintf_s(out, size, _TRUNCATE, "camlevel %d", g_camLevel); return 1; }
    if (!_stricmp(cmd, "facefrom")) { g_faceFrom = atoi(a1); _snprintf_s(out, size, _TRUNCATE, "facefrom %d", g_faceFrom); return 1; }
    if (!_stricmp(cmd, "rollreset")) { CamRollReset(); _snprintf_s(out, size, _TRUNCATE, "roll counters reset"); return 1; }
    if (!_stricmp(cmd, "sprintbody")) { g_sprintBody = atoi(a1); _snprintf_s(out, size, _TRUNCATE, "sprintbody %d", g_sprintBody); return 1; }
    if (!_stricmp(cmd, "offwatch")) { s_offWatch = atoi(a1) != 0; _snprintf_s(out, size, _TRUNCATE, "offwatch %d", (int)s_offWatch); return 1; }
    if (!_stricmp(cmd, "offreset")) {   // tests: the body-off-aim counters start again
        s_offFrames = s_offEvents = 0; s_offLongest = s_offMax = 0.0f;
        strcpy_s(out, size, "offaim counters reset"); return 1;
    }
    if (!_stricmp(cmd, "mouse")) {   // mouse <dx> <dy>: raw counts into the aim camera's mouse path
        float dx = 0, dy = 0;
        sscanf_s(args, "%*s %f %f", &dx, &dy);
        InputDevMouse(dx, dy);
        _snprintf_s(out, size, _TRUNCATE, "mouse %.0f %.0f", dx, dy);
        return 1;
    }
    if (!_stricmp(cmd, "aimat")) {   // aimat <x> <y> <z>: point the aim camera's crosshair at a world position
        float p[3] = {};
        if (sscanf_s(args, "%*s %f %f %f", p, p + 1, p + 2) != 3) { strcpy_s(out, size, "usage: aimat x y z"); return 1; }
        const bool ok = CamAimAt(p);
        float y, pi; CamAimAngles(&y, &pi);
        _snprintf_s(out, size, _TRUNCATE, "%s yaw %.2f pitch %.2f", ok ? "aimed" : "aim camera not active", y, pi);
        return 1;
    }
    if (!_stricmp(cmd, "sounds")) {  // sound pack state, per-cue play counts; `sounds play <id>` plays one
        if (!_stricmp(a1, "play") && a2[0]) {
            const char* c = SoundCue(a2);
            if (!c) { _snprintf_s(out, size, _TRUNCATE, "no cue for '%s' here", a2); return 1; }
            if (SoundIsLoop(a2)) { _snprintf_s(out, size, _TRUNCATE, "%s is a loop: its effect plays it (the jump pack)", a2); return 1; }
            Snd(a2);
            _snprintf_s(out, size, _TRUNCATE, "played %s -> %s", a2, c);
            return 1;
        }
        if (!_stricmp(a1, "ids")) { _snprintf_s(out, size, _TRUNCATE, "%s", SoundsIds()); return 1; }
        if (!_stricmp(a1, "ensure")) SoundsEnsure();
        _snprintf_s(out, size, _TRUNCATE, "%s", SoundsStatus());
        return 1;
    }
    if (!_stricmp(cmd, "aimerr")) {  // how far the shown crosshair ray is from the last aimat point
        float d, m; CamAimError(&d, &m);
        _snprintf_s(out, size, _TRUNCATE, "aimerr %.3f deg miss %.3f m", d, m);
        return 1;
    }
    if (!_stricmp(cmd, "weapon")) {
        const WeaponDef* w = WeaponById(a1);
        if (!w) { strcpy_s(out, size, "unknown weapon"); return 1; }
        s_weapon = w = Usable(w);
        s_weaponPinned = g_mode != M_BLASTER;   // the next draw uses it instead of the class gun
        if (g_mode == M_BLASTER) {
            const char* r = LuaCall("return DKB.setgun('%s')", w->actor);
            CombatReset(w);
            _snprintf_s(out, size, _TRUNCATE, "weapon %s (%s)", w->id, r);
        } else _snprintf_s(out, size, _TRUNCATE, "weapon %s (next time)", w->id);
        return 1;
    }
    if (!_stricmp(cmd, "cam")) {   // raw camera system fields
        BYTE* cs = CamSys();
        if (!cs) { strcpy_s(out, size, "no camera"); return 1; }
        float f[16] = {}; int mode = -1, blend = -1;
        SafeRead(cs + 0x4884, &mode, 4); SafeRead(cs + 0xF0, &blend, 4);
        SafeRead(cs + 0x7C, f, 8); SafeRead(cs + 0x4850, f + 2, 12); SafeRead(cs + 0x4860, f + 5, 12); SafeRead(cs + 0x494C, f + 8, 4);
        SafeRead(cs + 0x30, f + 9, 4);
        _snprintf_s(out, size, _TRUNCATE, "cs %p mode %d blend %d orbit %.3f pitch %.3f eye %.2f %.2f %.2f target %.2f %.2f %.2f fov %.2f dist %.2f | built eye %.2f %.2f %.2f fwd %.3f %.3f %.3f valid %d",
                    cs, mode, blend, f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], g_cam.eye[0], g_cam.eye[1], g_cam.eye[2],
                    g_cam.fwd[0], g_cam.fwd[1], g_cam.fwd[2], g_cam.valid);
        return 1;
    }
    if (!_stricmp(cmd, "addr")) {   // dev: object addresses for memory inspection
        const DWORD r = PlayerRen();
        _snprintf_s(out, size, _TRUNCATE, "ren %08X playercomp %p camsys %p", r, r ? PlayerComp(r) : nullptr, CamSys());
        return 1;
    }
    if (!_stricmp(cmd, "facemode")) { s_faceMode = atoi(a1); _snprintf_s(out, size, _TRUNCATE, "facemode %d", s_faceMode); return 1; }
    if (!_stricmp(cmd, "keepview")) { g_keepView = atoi(a1); _snprintf_s(out, size, _TRUNCATE, "keepview %d", g_keepView); return 1; }
    if (!_stricmp(cmd, "shoulder")) { cfg.shoulder = -cfg.shoulder; _snprintf_s(out, size, _TRUNCATE, "shoulder %d", cfg.shoulder); return 1; }
    if (!_stricmp(cmd, "spreadmul")) { g_devSpread = (float)atof(a1); _snprintf_s(out, size, _TRUNCATE, "spread x%.2f", g_devSpread); return 1; }
    if (!_stricmp(cmd, "debug")) { cfg.debug = atoi(a1) != 0; _snprintf_s(out, size, _TRUNCATE, "debug %d", cfg.debug); return 1; }
    if (!_stricmp(cmd, "cfgreload")) {   // re-read DK_Blaster.ini now (tests: after a bridge `cfg set` overlay)
        LoadConfig();
        AfterConfig();
        _snprintf_s(out, size, _TRUNCATE, "settings reloaded: enabled %d", cfg.enabled);
        return 1;
    }
    if (!_stricmp(cmd, "stance")) { _snprintf_s(out, size, _TRUNCATE, "%s", !_stricmp(a1, "chores") ? HoldChores() : HoldStatus()); return 1; }
    if (!_stricmp(cmd, "pack")) { _snprintf_s(out, size, _TRUNCATE, "%s | chained here %d", PackStatus(), (int)PackChained()); return 1; }
    if (!_stricmp(cmd, "chain")) { _snprintf_s(out, size, _TRUNCATE, "chain %s -> %d", a1, ChainPackDev(a1)); return 1; }
    if (!_stricmp(cmd, "heat")) { _snprintf_s(out, size, _TRUNCATE, "%s", CombatHeatStatus()); return 1; }
    if (!_stricmp(cmd, "perf")) {   // perf [reset]: the mod's cost per frame since the last reset
        _snprintf_s(out, size, _TRUNCATE, "frames %ld frame_avg_us %.1f frame_max_us %.1f draws %ld draw_avg_us %.1f draw_max_us %.1f",
                    s_perf[0].n, s_perf[0].n ? s_perf[0].sum / s_perf[0].n : 0.0, s_perf[0].max,
                    s_perf[1].n, s_perf[1].n ? s_perf[1].sum / s_perf[1].n : 0.0, s_perf[1].max);
        if (!_stricmp(a1, "reset")) memset(s_perf, 0, sizeof s_perf);
        return 1;
    }
    if (!_stricmp(cmd, "scopehide")) {   // tests: scopehide 0 = keep the gun drawn while scoped (control arm)
        s_scopeHideOff = a1[0] == '0';
        _snprintf_s(out, size, _TRUNCATE, "scope hide %s", s_scopeHideOff ? "off" : "on");
        return 1;
    }
    if (!_stricmp(cmd, "hudoff")) {   // hudoff 1|0: the combat HUD fades out / back (gallery shots without the overlay)
        g_devHudOff = a1[0] == '1';
        _snprintf_s(out, size, _TRUNCATE, "hud %s", g_devHudOff ? "off" : "on");
        return 1;
    }
    if (!_stricmp(cmd, "staycam")) {   // staycam 1|0: stay armed under a script camera (the rig steps aside by itself)
        s_devStayCam = a1[0] == '1';
        _snprintf_s(out, size, _TRUNCATE, "staycam %s", s_devStayCam ? "on" : "off");
        return 1;
    }
    if (!_stricmp(cmd, "viewsim")) {   // viewsim <aspect, e.g. 2.37 for 2560x1080, 1.33 for 4:3 | 0>
        rnd::SetViewSim((float)atof(a1));
        _snprintf_s(out, size, _TRUNCATE, "view simulation %.2f", rnd::ViewSim());
        return 1;
    }
    if (!_stricmp(cmd, "uiscale")) {   // uiscale <0.6-1.6>: the HUD size for this session (the ini's HudScale otherwise)
        rnd::SetUserScale((float)atof(a1));
        _snprintf_s(out, size, _TRUNCATE, "hud scale %s", a1);
        return 1;
    }
    if (!_stricmp(cmd, "breath")) {
        _snprintf_s(out, size, _TRUNCATE, "breath %.3f state %d holds %d outs %d scope %.0f hidden %d hides %d health %.3f", s_breath,
                    g_hudBreathState, s_breathHolds, s_breathOuts, g_hudScopeAmt, (int)s_scopeHidden, s_scopeHides, g_hudHealth);
        return 1;
    }
    if (!_stricmp(cmd, "ability")) { _snprintf_s(out, size, _TRUNCATE, "%s", AbilityStatus()); return 1; }
    if (!_stricmp(cmd, "abready")) { AbilityDevReady(); strcpy_s(out, size, "ok"); return 1; }
    if (!_stricmp(cmd, "special")) {   // special <id|-> : the class special (tests; outfits set it in play)
        AbilitySetSpecial(strcmp(a1, "-") ? a1 : nullptr);
        _snprintf_s(out, size, _TRUNCATE, "special %s", AbilitySpecialName());
        return 1;
    }
    if (!_stricmp(cmd, "nadeg")) { if (a1[0]) g_nadeGravity = (float)atof(a1); _snprintf_s(out, size, _TRUNCATE, "g %.3f", g_nadeGravity); return 1; }
    if (!_stricmp(cmd, "pool")) { _snprintf_s(out, size, _TRUNCATE, "pool %d", (int)AbilityEnsurePool()); return 1; }
    if (!_stricmp(cmd, "toast")) { HudToast(a1, 0x7CE0FF, 2.0f); strcpy_s(out, size, "ok"); return 1; }
    if (OutfitCommand(cmd, a1, a2, out, size)) return 1;
    if (!_stricmp(cmd, "snap")) {   // vanilla-state snapshot (tests compare before / after round trips)
        DWORD p = PlayerRen();
        BYTE* pc = PlayerComp(p);
        int strafeRef = -1, strafeFlag = -1;
        if (pc) { SafeRead(pc + 0x5458, &strafeRef, 4); BYTE b = 0; SafeRead(pc + 0x5454, &b, 1); strafeFlag = b; }
        BYTE* cs = CamSys();
        float fd = 0, fov = 0, off[3] = {}, csFov = 0;
        int csMode = -1;
        if (cs) { SafeRead(cs + 0x30, &fd, 4); SafeRead(cs + 0x94, &fov, 4); SafeRead(cs + 0x40, off, 12); SafeRead(cs + 0x494C, &csFov, 4); SafeRead(cs + 0x4884, &csMode, 4); }
        const char* lua = LuaCall("local p = ren.GetPlayer(0) local h = p and ai.GetMeleeWeapon(p) "
                                  "local v = h and ren.IsVisible(h) local ok, at = pcall(attachment.GetAttachmentRens, p) "
                                  "return tostring(v), camera.GetPositionMode(), tostring(ronin.InputIsMapOnStack('playerMiniGame', 0)), "
                                  "tostring(DKB.gun ~= nil), tostring(DKB.saberOff == true), ok and type(at) == 'table' and #at or -1");
        _snprintf_s(out, size, _TRUNCATE, "mode=%d strafeRef=%d strafeFlag=%d mute=%d camw=%d keepView=%d %s hud=%.2f "
                    "followDist=%.3f fov=%.3f off=%.3f,%.3f,%.3f csMode=%d csFov=%.2f hilt/cam/qte/gun/saberOff/attach=%s",
                    g_mode, strafeRef, strafeFlag, InputMuteOn(), CamOwnsView(), g_keepView, EngineSnap(), HudAlpha(),
                    fd, fov, off[0], off[1], off[2], csMode, csFov, lua);
        return 1;
    }
    _snprintf_s(out, size, _TRUNCATE, "commands: status | on | off | emergency | hold <fire|aim|vent|grenade|ability|sprint|shoulder|next|prev|wheel|toggle|roll|swap> <s> | weapon <id> | shoulder | debug 0/1 | toast <text> | snap | "
                "class [<id>|next|prev|last|home|swap] | classes | fav <id> | loadout <class> <g1>,<g2>,<special>|reset | wheel|term open|cancel|close|page|hover|confirm|field|value|fav | outfit | padsim <hex> <s> | stick <x> <y> <s> | camlevel 0/1 | facefrom 0/1 | rollreset");
    return 1;
}

}  // namespace dkb

using namespace dkb;

extern "C" __declspec(dllexport) void __cdecl DK_PluginAttach(const DK_LoaderApi* api) {
    if (api && api->version >= 1) g_api = api;
}

extern "C" __declspec(dllexport) void __cdecl DK_PluginInit(HMODULE exe) {
    GetModuleFileNameA(exe, g_dir, MAX_PATH);
    if (char* s = strrchr(g_dir, '\\')) s[1] = 0;
    char log[MAX_PATH]; sprintf_s(log, "%sDK_Blaster.log", g_dir);
    const bool reload = DK_IsReload(g_api, "DK_Blaster.loaded");
    LogOpen(log, "DK_Blaster", 600, reload);
    if (reload) Log("---- hot reload ----");
    LoadConfig();
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (GetFileAttributesExA(s_ini, GetFileExInfoStandard, &a)) s_iniTime = a.ftLastWriteTime;
    Log("DK_Blaster %s - Enabled=%d toggle %s, fire %s, aim %s, pad %d, loader API v%d", DK_BLASTER_VERSION, cfg.enabled, VkName(cfg.kToggle),
        VkName(cfg.kFire), VkName(cfg.kAim), cfg.gamepad, g_api ? g_api->version : 0);
    __try {
        s_active = EngineInit(exe);
        if (s_active) {
            Range t = TextRange(exe);
            bool bossFound = false;
            if (uint8_t* b = FindSym(t, "UIUpdaterBoss_Update")) {   // boss ren offset on the player component
                for (int i = 0; i < 0x40; i++)
                    if (b[i] == 0x8B && b[i + 1] == 0x80 && *(DWORD*)(b + i + 2) > 0x1000 && *(DWORD*)(b + i + 2) < 0x10000) { s_bossOff = *(DWORD*)(b + i + 2); bossFound = true; break; }
            }
            if (!bossFound) Log("boss offset not found in UIUpdaterBoss_Update - using 0x%X (game 1.2.94564)", s_bossOff);
            if (!rnd::Install(t, g_dir)) Log("HUD overlay off");
            HudInstall();
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("exception 0x%08X while installing - mod off", GetExceptionCode());
        RestorePatches();
        s_active = false;
    }
    Log("ready: %s (boss offset 0x%X, %d patches)", s_active ? "active" : "INACTIVE", s_bossOff, PatchCount());
}

extern "C" __declspec(dllexport) void __cdecl DK_PluginFrame() {
    if (!s_active) return;
    const double p0 = PerfNow();
    __try { Frame(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        static int n;
        if (n++ < 5) Log("exception 0x%08X in the frame - back to the saber", GetExceptionCode());
        if (g_mode == M_BLASTER || s_want || s_suspend) {   // the full way back: gun, saber, strafe, stick fix, HUD, wish
            __try { EmergencyReset("exception in the frame"); }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                __try { InputSetMute(false); CamSetWeight(0.0f); SetFacing(nullptr, 0); SetStrafe(false); SetStickFix(false);
                        HudSetVisible(0.0f); s_want = false; s_suspend = nullptr; g_mode = M_SABER; } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
    }
    PerfAdd(0, PerfNow() - p0);
}

extern "C" __declspec(dllexport) int __cdecl DK_PluginCommand(const char* args, char* out, int size) {
    if (!out || size <= 0) return 0;
    out[0] = 0;
    __try { return Command(args, out, size); }
    __except (EXCEPTION_EXECUTE_HANDLER) { _snprintf_s(out, size, _TRUNCATE, "exception 0x%08X", GetExceptionCode()); return 1; }
}

extern "C" __declspec(dllexport) int __cdecl DK_PluginShutdown() {
    // a plugin stacked on one of our hooks calls our code as its original: refuse BEFORE anything is undone
    char who[300];
    if (StackedOn(who, sizeof(who))) { Log("shutdown: %s is stacked on one of our hooks - refusing unload (still active)", who); return 0; }
    __try {
        if (s_active) {
            if (g_mode == M_BLASTER || s_suspend) ModeExit("plugin unloading", true);
            HoldRestore();
            OutfitShutdown();
            EngineShutdown();
            SoundsShutdown();
            HudShutdown();
            rnd::Shutdown();
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    __try { InputShutdown(); AimShutdown(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (RestorePatches()) { Log("shutdown: another plugin is chained onto one of our hooks - refusing unload (still active)"); return 0; }
    Log("shut down");
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h);
    return TRUE;
}
