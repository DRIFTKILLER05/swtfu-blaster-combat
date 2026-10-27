// DK_Blaster: Battlefront-style third-person blaster combat for SWTFU (DK_Loader plugin). Shared declarations.
#pragma once
#include <windows.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dk_plugin.h"
#include "dk_util.h"
#include "dk_syms.h"
#include "dk_lua.h"

#define DK_BLASTER_VERSION "1.2.3"

namespace rnd { struct Ctx; }
namespace dkb {

extern const DK_LoaderApi* g_api;
extern char g_dir[MAX_PATH];

// ---------------------------------------------------------------------------------------------- time
double RealNow();                 // seconds, QPC
float GameNow();                  // game clock (stops while paused / in menus), seconds
float FrameDt();                  // game seconds since the previous DK frame (0 while paused)
float RealDt();                   // real seconds since the previous DK frame (clamped 0..0.1)

// ---------------------------------------------------------------------------------------------- config
struct Keys { int vk[2]; };        // up to two keys per action (0 = none)
struct Config {
    bool enabled = true;
    // keyboard / mouse (virtual-key codes)
    int kToggle = 'B', kFire = VK_LBUTTON, kAim = VK_RBUTTON, kVent = 'R', kGrenade = 'G', kAbility = 'F',
        kSprint = VK_LSHIFT, kShoulder = 'X', kNext = VK_OEM_6, kPrev = VK_OEM_4, kWheel = 'T', kRoll = 'C', kSwap = 'Q',
        kCrouch = VK_LMENU;
    // controller (XInput button masks; triggers are separate)
    bool gamepad = true;
    WORD pToggle = 0x0020 | 0x0002;   // Back + D-pad down
    float lookSens = 1.0f;            // controller look speed (x the default turn rate)
    float mouseSens = 1.0f;           // mouse look (x 0.06 deg per count)
    bool invertY = false;             // extra look-Y invert for blaster mode (the game's mouse invert options apply too)
    float sensAds = 0.55f;            // mouse / stick sensitivity while aiming (x hip)
    float sensScope = 0.35f;
    float adsMove = 0.6f;             // movement speed while aiming (x the stick)
    bool aimAssist = true;            // controller only: slow-down over targets + light magnetism
    int shoulder = 1;                 // 1 right, -1 left
    float camSide = 0.62f, camUp = 0.18f, camDistance = 3.0f;   // OTS offset (m), follow distance (m, 0 = the game's)
    float fovHip = 0.0f;              // 0 = the game's FOV
    int crosshair = 0;                // 0 Battlefront, 1 dot, 2 circle, 3 classic cross
    uint32_t crossColor = 0xFFFFFF;
    float crossScale = 1.0f;
    float hudScale = 1.0f;
    bool hitSound = true;
    float damageScale = 1.0f;         // player bolt damage multiplier
    bool heavyDamage = true;          // enemies over 1600 max health take up to 2x (sqrt of their health ratio)
    bool autoDeflect = false;         // keep Starkiller's automatic bolt deflection in blaster mode
    float damageTaken = 0.6f;         // enemy bolt damage to the player while armed (x)
    bool bossReturn = true;           // bosses / QTE hand control back to the saber
    bool keepMode = true;             // blaster mode stays on through deaths, checkpoints and level changes
    bool outfitFollows = true;        // blaster mode wears the class outfit, saber mode the saber outfit
    bool loadoutFollows = true;       // the class picks the guns and the special
    bool rifleStance = true;          // two-handed stormtrooper rifle stance (dkb_hold.cpp)
    bool aimPose = true;              // the gun is raised and aimed while aiming / firing (dkb_hold.cpp overlay)
    bool crouchToggle = true;         // crouch key: true = press to crouch / stand, false = hold
    float crouchMove = 0.45f;         // movement speed while crouched (x the run speed)
    bool cameraRecoil = false;        // shots kick the camera (pitch / yaw + climb); off = the view stays still
    float overdriveTime = 3.0f;       // seconds of heat-free fire after a perfect vent (0 = off)
    bool lockMouse = true;            // keep the pointer inside the game window while it has the focus
    bool cycleFavourites = false;     // next / previous class cycles the favourites only (when there are any)
    bool wheelSlowmo = true;          // the class wheel / terminal slow the game down
    bool terminalPreview = true;      // the terminal puts the highlighted outfit on as a preview
    bool killCredit = true;
    bool debug = false;
};
extern Config cfg;
void LoadConfig();
const char* VkName(int vk);

// ---------------------------------------------------------------------------------------------- engine access
DWORD PlayerRen();                          // 0 when none
BYTE* PlayerComp(DWORD ren);                // PlayerMgr entry of the player ren
bool RenPos(DWORD ren, float out[3]);
float RenHealth(DWORD ren);                 // -1 = no health component
bool RenHealthOk();                         // the health read was found (RenHealth can answer)
float RenMaxHealth(DWORD ren);              // -1 = no health component / max health read not resolved
void StrafeForget();                        // the player ren changed
int StrafeRetakes();                        // times the game dropped our strafe and it was taken again
void StrafeRetake();                        // release our strafe and take it again (the body stopped following it)
const char* StrafeStatus();                 // "strafe flag F refs N ours 0|1"
bool StrafeFlagSet();                       // the player's strafe flag (pc+0x5454) is on
bool RenYaw(DWORD ren, float* yaw);         // the body's own yaw (Ren_Forward, atan2 x, z), any time
extern int g_sprintBody;                    // dev: 1 = a sprint also needs the body near the view (1.2.1), 0 = 1.2.0
void SetFacing(const float* fwd, DWORD ren);   // body faces this (flattened) direction; nullptr = vanilla
bool FacingOk();
bool BodyYaw(float* yaw);                       // the player body's current yaw (atan2 x, z) while the facing hook runs
const char* FacingStatus();
void FacingSnap();                          // the next facing call returns the full aim (no turn rate limit)
extern int g_faceFrom;                          // dev: 1 = body turns start from the body's own forward, 0 = 1.2.0
const char* EngineSnap();                   // "face=0|1 stick=0F86|E9.." (tests: vanilla after every exit)
void SetStickFix(bool on);                   // move stick along the camera's own axes (armed only)
BYTE* CamSys();                             // viewport-0 CameraSystem
bool InGameplay();                          // state 2, simulation running, no cinematic, no loading
bool CinematicNow();
int GameState();
bool EngineInit(HMODULE exe);               // resolve symbols + install hooks
void EngineShutdown();

// Strafe locomotion (body faces the camera). Our own reference: on/off at most once each.
void SetStrafe(bool on);
// Aim point for the player's AimScumPlayer setter (StoredAimPos): world position, written each frame while armed.
void SetAimPoint(const float p[3], bool on);
// What the crosshair is on (camera ray, 15 checks a second while armed): world point, valid while g_aimHitOk.
extern bool g_devHudOff;                     // dev command hudoff: the combat HUD fades out (clean gallery shots)
extern bool g_aimHitOk;
extern float g_aimHit[3];

// ---------------------------------------------------------------------------------------------- camera (dkb_cam.cpp)
struct CamOut { float eye[3], fwd[3], right[3], up[3]; float fov; bool valid; };
extern CamOut g_cam;                        // the last built gameplay camera (after our offsets)
extern int g_keepView;
extern int g_camLevel;                      // dev: 1 = our view is built with the world up (no inherited roll)
void CamSetWeight(float w);                 // 0 vanilla .. 1 full OTS (eased by the caller)
void CamSetAds(float zoom);                 // FOV multiplier target (1 = hip)
void CamKick(float pitchDeg, float yawDeg, float fovDeg, float back);   // recoil impulse
void CamTick(float dt);                     // springs / easing / boom collision (game thread, per frame)
void CamSetSens(float mul);                 // look speed multiplier (ADS / scope)
void CamSetAssist(float slow);              // 0..1 aim-assist slow-down (controller, crosshair on a target)
void CamSetSway(float amount);              // scope sway (0 = steady, 1 = breathing, more = out of breath)
void CamHandBack();                         // turn the game's follow camera to our aim direction (on exit)
void CamAimAngles(float* yawDeg, float* pitchDeg);
bool CamAimAt(const float p[3]);             // dev / tests: crosshair onto a world position (refined for 0.5 s)
void CamAimError(float* deg, float* miss);   // the shown view ray vs the last aimat point (-1 = never measured)
bool CamOwnsView();                         // our offsets were applied in the last build
bool CamHookOk();
const char* CamStatus();
void CamRollReset();                        // dev: zero the roll maxima / counters (per test phase)

// ---------------------------------------------------------------------------------------------- input (dkb_input.cpp)
enum Act { A_FIRE, A_AIM, A_VENT, A_GRENADE, A_ABILITY, A_SPRINT, A_SHOULDER, A_NEXT, A_PREV, A_WHEEL, A_TOGGLE, A_ROLL,
           A_SWAP, A_CROUCH, A_COUNT };
struct InputState {
    bool held[A_COUNT], pressed[A_COUNT], released[A_COUNT];
    float holdTime[A_COUNT];               // real seconds held
    bool pad;                              // the last device used is the controller
    float padRX, padRY;                    // right stick (-1..1) for aim assist
    WORD padButtons, padPressed;           // raw XInput buttons (menus: wheel / terminal), pressed = this frame's new ones
};
extern InputState g_in;
void InputPoll(float realDt);
bool InputKey(int vk);                      // a key / mouse button is down now (focused game or DK_Bridge background keys)
// Menus (class wheel / terminal): the look input goes to the menu instead of the camera, and the game's own attack /
// Force / look buttons are muted, in either mode. InputMenuLook returns the mouse counts gathered since the last call.
void InputCapture(bool on);
bool InputCaptured();
void InputMenuLook(float* mouseX, float* mouseY);
const char* InputPadStatus();               // Back-combo state (tests)
void InputDevPad(WORD buttons, float seconds);   // dev: pretend these XInput buttons are held (tests)
void InputSetMute(bool armed);              // mute the game's attack / Force buttons while armed
// look input since the last call: right stick (-1..1, raw) and raw mouse counts (KBM mode, game invert applied)
void InputLook(float* stickX, float* stickY, float* mouseX, float* mouseY, bool* kbm);
bool InputHookOk();
void InputDash(float seconds);              // pulse the game's Force Dash button (dodge)
bool InputFocused();                        // the game window is in front
void InputMouseLock(bool gameplay);         // every frame: keep the pointer in the window while playing
void InputShutdown();                       // stop the mouse lock thread, free the pointer
const char* InputLockStatus();
void InputMoveStick(float* x, float* y);    // the movement stick as the game got it last (-1..1, y = forward)
int InputDashCount();
int InputMutedCount();
int InputMuteOn();                          // the game's attack / Force buttons are muted now
void InputDevSet(int act, float seconds);   // dev: hold an action for N real seconds (tests)
void InputDevLook(float rx, float ry, float seconds);   // dev: add to the aim camera's stick input (tests)
void InputDevMouse(float dx, float dy);     // dev: raw mouse counts into the aim camera (tests)
void InputDevStick(float x, float y, float seconds);   // dev: the game's move stick reads x / y (analog pad, tests)

// ---------------------------------------------------------------------------------------------- Lua module
bool LuaEnsure();                           // DKB table present in the game's Lua state (re-created after loads)
const char* LuaCall(const char* fmt, ...);  // runs "return DKB.<...>" style code, returns the text result
int LuaNums(float* out, int max, const char* fmt, ...);

// ---------------------------------------------------------------------------------------------- weapons / combat
struct WeaponDef {
    const char* id;            // "e11"
    const char* name;          // "E-11 Blaster Rifle"
    const char* actor;         // prop actor path
    const char* abilityGroup;  // projectile ability group path
    const char* abilityEntry;  // entry name
    const char* fireCue;       // audio cue
    float rpm;                 // shots per minute
    int burst;                 // 0 = auto, 1 = semi, n = burst of n
    float damage;              // HP per bolt (player-side, before headshot / scale)
    float headMul;
    float speed;               // bolt m/s
    float heatPerShot;         // 0..1
    float coolRate;            // per second after coolDelay
    float coolDelay;
    float overheatLock;        // seconds
    float spreadHip, spreadAds, spreadMove, spreadPerShot, spreadMax, spreadRecover;   // degrees / per second
    float recoilPitch, recoilYaw, recoilFov, recoilBack;
    float adsZoom;             // FOV multiplier
    int pellets;               // > 1: shotgun
    float charge;              // > 0: hold to charge (seconds) for full damage
    bool scope;
    float range;               // effective range (m) for falloff and aim assist
    uint32_t boltColor;        // HUD tint
    const char* boltVfx;       // our ability's bolt effect (Scum/effects/Library/Weapons/Laser/)
    const char* muzzleVfx;     // muzzle flash (.../Weapons/Blaster/)
    float boltLen;             // metres
};
extern const WeaponDef kWeapons[];
extern const int kWeaponCount;
const WeaponDef* WeaponById(const char* id);

// dkb_pack.cpp: the game assets in our pack (guns, effects, our ability group) - what made it in
extern const char* const kDkAbilityGroup;   // Scum/runtime/abilities/DK/DK_Blaster.abilityGroup.xml (entries = weapon ids)
extern const char* const kDkGrenadeEntry;          // "Thermal Detonator <speed>": one entry per throw speed (the engine
extern const int kDkGrenadeSpeeds[];               // flies a grenade at its ability's own speed, not Launch's)
extern const int kDkGrenadeSpeedCount;
bool PackWeaponOk(const WeaponDef* w);
bool PackAbilityOk();
bool PackGrenadeOk();
bool PackJetFxOk();                          // the jump pack thruster effects are in our pack
bool PackAimOk();                            // the trooper aim grid animations (aimed gun pose) are in our pack
bool PackHoldOk();                           // the trooper locomotion animations (rifle stance) are in our pack
const char* PackStatus();
bool PackChained();                         // dkb_sound.cpp: our pack is chained in this level

struct Combat {
    const WeaponDef* w;
    float heat;                // 0..1
    bool overheated;
    float lockUntil;           // game time
    float lastShot;            // game time
    float nextShot;
    float bloom;               // current extra spread (deg)
    int burstLeft;
    float chargeT;
    float ventWindow0, ventWindow1;   // active cooldown sweet spot (heat fraction)
    bool ventTried;
    float ventPerfectAt;              // game time of the last perfect vent (HUD flash)
    bool venting;              // a manual vent (vent key below overheat): the same sweep, shorter, firing locked
    float ventH0, ventT;       // the sweep: heat at its start, its length (s); heat = ventH0 * time left / ventT
    float overdriveUntil;      // game time: a perfect vent's heat-free fire lasts until then
    bool heatWarned;           // the 80 % warning beeped on this climb
    int shots, hits, kills, heads;
};
extern Combat g_cb;
void CombatReset(const WeaponDef* w);
void CombatTick(float dt, bool armed);       // fire / heat / bloom
bool CombatHookOk();
void CombatOnBolt(DWORD bolt, const WeaponDef* w, DWORD target, bool headAim);   // registry of our bolts
void CombatSetMotion(float speed, bool aiming, bool sprinting);
float CombatSpread();                       // current cone half-angle (degrees)
extern float g_devSpread;                   // dev / tests: spread multiplier
void CombatFrameHits();                     // hit / kill markers from the damage hook (DK frame)
const char* CombatStatus();
const char* CombatHeatStatus();
bool CombatSweeping();                      // the gun is cooling down (overheated or a manual vent): no fire
float CombatOverdrive();                    // seconds of heat-free fire left after a perfect vent (0 = none)

// class specials acting on the gun (dkb_ability.cpp)
struct Boost { float heatMul = 1.0f, rpmMul = 1.0f, dmgMul = 1.0f; int charged = 0; };
extern Boost g_boost;

// abilities (dkb_ability.cpp): thermal detonator + class special, HUD part drawn by the HUD
void AbilityInstall(const dk::Range& text);

// dkb_hold.cpp: the two-handed rifle stance (the player's locomotion re-pointed to the trooper's combat animations)
bool HoldInstall(HMODULE exe, const dk::Range& text);
void* AnimCompOfRen(DWORD ren);                // a ren's animation object (ChoreManager), nullptr if none

// aimed gun pose (dkb_aim.cpp): the trooper's upper-body aim grid on the player's LightningC_Hold overlay block
bool AimInstall(const dk::Range& text);
void AimTick(bool armed, bool want);           // every frame: drawn / gun up wanted (aim, fire)
void AimShutdown();
bool AimUp();
const char* AimStatus();

// crouch (dkb_crouch.cpp): the player's own animated pose lowered (hips, IK legs with planted feet, slight lean) at the
// start of the animation post pass, so every walk / strafe clip becomes a crouch walk
bool CrouchInstall(const dk::Range& text);
bool CrouchAvailable();
void CrouchSet(float depth);                   // wanted depth in m (0 = stand), eased
void CrouchTick(float dt, bool running, bool cut);   // game thread, every frame (running: gameplay; cut: stand at once)
bool CrouchKnocked();                          // a knock-down hit the crouched player since the last call
bool CrouchBehavior();                         // Euphoria / ragdoll owns the player's pose now
float CrouchDepth();                           // depth now (m)
void CrouchAim(bool on, const float* aimRel, float bodyYaw);   // aim overlay up: the pose hook turns the chest onto
                                               // aimRel (aim point - player position, world); off: eased out
const char* CrouchStatus();
int CrouchDump(char* out, int size);
void HoldApply();                            // on draw (and settings change): wanted on
void HoldRestore();                          // on holster / unload / reset: wanted off
void HoldTick();                             // every frame: moves each chore over once the player is out of it
const char* HoldStatus();
const char* HoldChores();                    // dev: `stance chores`
bool AbilityEnsurePool();                   // blaster draw: the detonator's pool actors exist in this level
void AbilityTick(bool armed);
void AbilityReset();                        // mode off: cook cancelled, special ended, boosts cleared
void AbilitySetSpecial(const char* id);     // the class's special (nullptr = the gun's own)
const char* AbilitySpecialName();
const char* AbilityStatus();
void AbilityDevReady();
void AbilityDraw(const rnd::Ctx& x, float S, float a);
extern float g_nadeGravity;
int ChainPackDev(const char* name);              // dev `heat`: heat timeline in game time

// HUD events (dkb_hud.cpp)
void HudHit(bool kill, bool head);
void HudDamageFrom(const float srcPos[3]);
void HudToast(const char* text, uint32_t color, float seconds);
void HudInstall();
void HudSetVisible(float target);           // 0..1 eased
float HudAlpha();                           // the HUD's current (eased) visibility
void HudShutdown();
const char* HudStatus();

// ---------------------------------------------------------------------------------------------- mode
enum Mode { M_SABER = 0, M_BLASTER = 1 };
extern Mode g_mode;
const char* ModeRefusal();                  // why the toggle is refused now (nullptr = allowed)
bool ModeEnter(const char* why);
void ModeExit(const char* why, bool emergency);
void Refuse(const char* reason);            // HUD line + sound + log
bool BlasterOn(const char* why);            // draw the blaster now (refusal shown); true = blaster mode is on
const WeaponDef* SetWeapon(const WeaponDef* w);   // the gun to use (drawn at once when armed); returns the one used

// trooper classes (dkb_outfit.cpp)
void OutfitInstall(const dk::Range& text);
void OutfitShutdown();
void OutfitTick(float realDt);              // after InputPoll: requests, menus (a menu consumes the buttons)
void OutfitModeEnter();                     // the class special + (OutfitFollows) its outfit; the gun is ModeEnter's
void OutfitModeExit();                      // (OutfitFollows) the player's own outfit
void OutfitRespawned();                     // the level spawned a new player (not our swap)
void OutfitRearmed();
bool OutfitOwnsRenChange();                 // the player ren changed because of our own outfit swap
const WeaponDef* OutfitWeapon();            // the class gun for blaster mode (nullptr = LoadoutFollows off)
const char* OutfitWornId();                 // the class whose outfit the player wears ("" = his own)
int OutfitClass();                          // the selected class (index)
bool OutfitMenuOpen();
bool OutfitHidesHud();   // the requisition terminal is up: the blaster HUD steps aside
// the mod's own cost per frame (dev `perf`): 0 = frame logic, 1 = overlay drawing
double PerfNow();
void PerfAdd(int slot, double us);
void OutfitHome();       // emergency reset: the player's own outfit
void OutfitDrawPost(const rnd::Ctx& x);     // POST_UI: wheel, terminal, swap flash
void OutfitDrawHud(const rnd::Ctx& x, float S, float a);   // PRE_UI: class line by the weapon plate
const char* OutfitStatus();
int OutfitCommand(const char* cmd, const char* a1, const char* a2, char* out, int size);
int AbilitySpecialCount();
const char* AbilitySpecialId(int i);
const char* AbilitySpecialTitle(const char* id);
float AbilitySpecialCooldown(const char* id);
void Sound(const char* cue);                // play a cue (full asset path) on the player (no-op when not loaded)

// dkb_sound.cpp: our cues (dk_blaster_*) built from the game's own samples, chained per level
void SoundsInstall(const dk::Range& text);      // resolve chain / bank functions, start the background build
void SoundsShutdown();
bool SoundsEnsure();                        // game thread, in a level: our pack + bank live (false = fallbacks)
const char* SoundCue(const char* id);       // cue path for one of our ids (ours or the vanilla fallback; nullptr = silent)
const char* SoundFireCue(const WeaponDef* w);
void Snd(const char* id);                   // play one of ours on the player (counted; first use per level logged)
bool SoundIsLoop(const char* id);           // a loop cue: its effect plays and stops it, Snd does not
const char* SoundsStatus();
const char* SoundsIds();                    // every cue id, space separated

}  // namespace dkb
