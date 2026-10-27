DK Blaster Combat 1.2.3 - Battlefront-style blaster combat for Star Wars: The Force Unleashed (PC)
=================================================================================================

Holster the lightsaber and fight as a trooper: an over-the-shoulder blaster mode with aiming, heat, recoil, grenades,
class abilities and 26 trooper classes whose outfits you wear and swap on the fly. Press B (controller: Back + D-pad
down) at any time in a level; press it again and everything is exactly as the game made it.

Blaster mode
------------
  * Over-the-shoulder camera, shoulder swap, aim down sights with a per-weapon zoom, sniper scope with breath sway.
  * Bolts are the game's own projectiles going through the game's own damage: hit reactions, ragdolls, Force-wielders
    deflecting them, Force points for kills. Headshots do 2.5x damage (sniper rifles 3x).
  * No saber reflexes: with a gun in your hands nothing gets deflected or batted back, and Starkiller no longer
    strikes the saber block pose at incoming bolts - enemy bolts hit you (damage direction arcs, the low-health
    warning), at 60 % of their usual punch (DamageTaken). Put the gun away and the lightsaber deflects as always;
    AutoDeflect=1 keeps Starkiller's automatic deflection in blaster mode for an easier game. Big enemies (shock
    troopers) take up to double damage, so every fight lasts seconds, not minutes.
  * Heat: every shot heats the gun, an overheat locks it and vents. Active cooldown (R / X): after an overheat, or any
    time the gun is warm (a manual vent), hit the bright window on the heat arc and the gun vents at once - a PERFECT
    VENT also gives OVERDRIVE, a few seconds of heat-free fire (a full overheat earns all 3 s, a small vent less; the
    arc turns gold and counts down). Miss the window and it jams for longer. Slow guns (DLT-20A, Tusken Cycler,
    Bowcaster, FC-11, pistols) build heat too, so they get their vents and overdrives.
  * Crosshair bloom (moving, jumping, sustained fire), recoil that recovers, hit / kill / headshot markers and sounds,
    damage direction arcs, low-health warning. Everything runs on real time: the same at 30, 60, 144 FPS or uncapped.
  * Aimed gun pose: aiming down the sights or firing from the hip raises the gun to the shoulder and points it where
    the crosshair is (the stormtrooper's own aim animations, up / down / left / right); it lowers again a moment after
    the last shot. The legs keep walking and strafing underneath, and the upper body turns the rest of the way so the
    gun stays on the crosshair while you strafe or crouch (AimPose).
  * Crouch (Left Alt / D-pad down, press or hold): lower stance, steadier gun (less spread), smaller target, the
    camera goes down with you. Crouch walk in every direction - every walk and strafe of the game becomes a crouch
    walk with the feet planted. Sprint, roll or jump to stand up.
  * Strafe and backpedal while armed, sprint (lowers the gun; it goes where you look - holding sprint while moving
    sideways or back just strafes), combat roll, thermal detonators (hold to cook, arc preview), a class special on a
    cooldown, a second gun per class.
  * The camera stays still when you fire (CameraRecoil=1 brings the kick back), and the mouse pointer stays inside the
    game window while you play, so it can't drift onto another screen and click there (LockMouse).
  * 11 weapons: E-11 Blaster Rifle, E-11 Blaster, DLT-20A Longblaster, DH-17, Z-6 Rotary Blaster, FC-11 Flechette,
    Bowcaster, DL-44, A295, DL-18, Tusken Cycler Rifle. Specials: RAPID FIRE, SCAN PULSE (marks enemies through walls),
    BACTA STIM, JUMP PACK (the pack's thrusters fire and lift you along your move; keep the key held to hover for up
    to 1.6 s and land softly), OVERCHARGE.
  * Rifle stance: with the blaster out you hold it two-handed like a stormtrooper - idle, walk, run, strafe and turns
    (RifleStance=0 keeps Starkiller's own moves).
  * Bosses, QTEs and finishing moves put the saber back in your hand (your class outfit stays on); the blaster returns
    after them.
  * Anything the mode can't do right now (a cutscene, a grab, a boss phase...) says why on screen, with a sound.
  * Controller: aim assist (slow-down over enemies), look speed and invert options.

Trooper classes and outfits
---------------------------
  * 26 classes on three pages - Imperial: Stormtrooper, Shadow Trooper, Trooper Commander, Scout Trooper, Snowtrooper,
    Kashyyyk Trooper, Jumptrooper, EVO Trooper, Shadow EVO Trooper, Imperial Officer, Imperial Gunner, Incinerator
    Trooper. Rebel: Rebel Fighter, Rebel Elite, Rebel Saboteur, Rebel Veteran, Hoth Commando, Hoth Rebel Soldier, Hoth
    Rebel Elite, Rebel Pilot, Rebel Sniper. Specialist: Imperial Commando, Clone Trooper, Jango Fett, Weequay
    Mercenary, Tusken Sniper.
  * Each class wears its outfit and carries its own guns and special. Drawing the blaster puts your class on,
    holstering puts your own outfit back (option).
  * Six of them (Hoth soldier, elite, pilot and sniper, Weequay, Tusken sniper) are enemy / ally characters turned into
    wearable outfits. They are built from your own game files on the first start (a few seconds in the background);
    nothing from the game is included in this download.
  * Next / previous class: ] / [ (controller: Back + D-pad right / left).
  * Tap T: back to your previous class. Hold T (controller: Back + D-pad up): the class wheel, in slow motion -
    point at a class and let go (or click / press A). F (X) marks favourites, [ ] (LB / RB) turn the page.
  * TAB in the wheel (controller: Y) opens the Requisition Terminal: every class with its guns, special and weapon
    stats, a live preview of the outfit on you, and a custom loadout per class (choose primary, secondary and special).
  * Q (controller: Y): swap to the class's second gun.
  * Outfit changes are paced (one every 0.3 s, the last request always wins) and play a requisition flash. Your
    health stays as it was: a new outfit is no free heal.
  * Outfits change only in normal play: not in a cutscene, a QTE, a finishing move, a boss fight or while you're down
    (the screen says why). Your class outfit stays on through a boss fight; it is never written into your save.
  * Your choice is remembered: blaster mode (and your class) comes back by itself after a cutscene, a death, a
    checkpoint, a level change or loading a save. Only the toggle changes it - toggle back to the saber and the saber
    stays. Remembered until you quit the game (KeepMode=0 or MOD OPTIONS: the old way, a death or a new level ends it).

Controls (every keyboard / mouse key can be rebound)
----------------------------------------------------
Rebind in game: MOD OPTIONS > Blaster Combat - choose an action (DRAW / HOLSTER, FIRE...), then press the key you want
(ESC cancels; a key another action already uses is swapped). Needs Mod Options Menu 1.1 or newer: 1.0 shows these
rows as ON / OFF switches that only put back the default key. Or edit DK_Blaster.ini.
The on-screen hints (class wheel, terminal, grenade / special buttons) show your keys.
  Action                    Keyboard / mouse      Controller
  Draw / holster blaster    B                     Back + D-pad down
  Fire / aim                LMB / RMB             RT / LT
  Active cooldown           R                     X
  Grenade (hold = cook)     G                     RB
  Class special             F                     LB
  Swap gun                  Q                     Y
  Crouch                    Left Alt              D-pad down
  Sprint / roll             LShift / C            Left stick click / B
  Shoulder swap             X                     Right stick click
  Next / previous class     ] / [                 Back + D-pad right / left
  Last class / class wheel  tap T / hold T        Back + D-pad up
  Reset everything          hold B 3 s            hold Back + D-pad down 3 s
Movement, jump, camera and pause stay as they are. Force powers and saber attacks rest only while the blaster is out.
A quick tap on Back alone still opens the game's options as usual.

Install
-------
Copy everything into the game folder (where SWTFU.exe is), e.g.
  ...\Steam\steamapps\common\Star Wars The Force Unleashed\
Files: dinput8.dll (DK_Loader, shared by all DK mods - keep the newest), DK_Blaster.dll, DK_ModOptions\DK_Blaster.ini,
DK_Blaster_Uninstall.bat. Nothing else is needed. Optional: Mod Options Menu for the in-game settings page:
https://www.nexusmods.com/starwarstheforceunleashed/mods/38 (any DK mod's loader runs this mod).
Steam "Verify integrity of game files" deletes dinput8.dll: copy it back afterwards.
Source code and build steps: https://github.com/DRIFTKILLER05/swtfu-blaster-combat

Settings
--------
In game: MOD OPTIONS > Blaster Combat (with Mod Options Menu installed), changes work at once. Or edit DK_Blaster.ini in the
game folder (created on the first start, re-read while the game runs):
  Enabled=1                       0 = the mod does nothing
  ToggleKey=B                     blaster mode on / off
  FireKey=LMB / AimKey=RMB        fire / aim down sights
  VentKey=R                       active cooldown (vent)
  GrenadeKey=G / AbilityKey=F     thermal detonator (hold = cook) / class special
  SprintKey=LSHIFT / RollKey=C    sprint (also steadies a scope) / combat roll
  ShoulderKey=X                   shoulder swap
  NextClassKey=] / PrevClassKey=[ next / previous class
  ClassWheelKey=T                 tap: your last class, hold: the class wheel
  SwapWeaponKey=Q                 primary / sidearm
  CrouchKey=LALT                  crouch / crouch walk (controller: D-pad down)
                                  keys: a letter / digit, F1-F24, LMB RMB MMB MOUSE4 MOUSE5, LSHIFT LCTRL LALT
                                  SPACE TAB [ ] ; ' , . / or 0x.. (a virtual key code)
  Gamepad=1                       controller support
  GamepadToggle=BACK+DOWN         the controller combo for blaster mode (0 = off)
  LookSensitivity=1.0             controller look speed in blaster mode
  MouseSensitivity=1.0            mouse look speed in blaster mode
  InvertLookY=0                   1 = invert up / down in blaster mode
  AimSensitivity=0.55             look speed while aiming (x normal)
  ScopeSensitivity=0.35           look speed in a scope (x normal)
  AimMoveSpeed=0.6                movement speed while aiming (0.3-1)
  AimAssist=1                     controller aim assist
  Shoulder=1                      1 = right shoulder, -1 = left
  CameraSide=0.62 / CameraUp=0.18 shoulder camera offsets (m)
  CameraDistance=3.0              camera distance (m); 0 = the level's own
  FOV=0                           field of view in blaster mode (degrees); 0 = the game's
  Crosshair=0                     0 Battlefront, 1 dot, 2 circle, 3 cross
  CrosshairColor=FFFFFF           RRGGBB
  CrosshairSize=1.0               0.5-2
  HudScale=1.0                    0.6-1.6
  HitSound=1                      hit / kill marker sounds
  DamageScale=1.0                 your bolts' damage (x)
  HeavyDamage=1                   1 = big enemies (over 1600 health, like shock troopers) take up to 2x blaster
                                  damage, so they fall in seconds instead of minutes; 0 = the same damage for every
                                  enemy
  AutoDeflect=0                   1 = Starkiller keeps his automatic saber-reflex deflection of blaster bolts in
                                  blaster mode (easy); 0 = enemy bolts hit you while you hold a gun, and no saber
                                  block pose
  DamageTaken=0.6                 damage enemy bolts do to you while you hold a gun (x; 1.0 = the game's own)
  BossReturn=1                    the saber for bosses, QTEs and finishing moves
  KeepMode=1                      blaster mode stays on through deaths, checkpoints, level changes and loading a save
                                  (0 = the saber after them)
  KillCredit=1                    Force points for blaster kills
  OutfitFollows=1                 1 = drawing the blaster puts the class outfit on, holstering puts yours back;
                                  0 = outfits stay on in both modes (centre of the wheel = your own outfit)
  LoadoutFollows=1                1 = the class picks your guns and special
  RifleStance=1                   Hold the blaster two-handed like a stormtrooper (walk, run, strafe, idle and turns
                                  use the trooper's combat animations); 0 = Starkiller's own stance
  AimPose=1                       1 = the gun comes up to the shoulder and points where you aim while you aim or fire
                                  (stormtrooper aim animations; it lowers again a moment after the last shot); 0 =
                                  the relaxed carry pose
  CameraRecoil=0                  1 = shots kick the camera up / sideways (recoil climb); 0 = the view stays still
                                  (recoil shows in the spread and the gun)
  CrouchToggle=1                  1 = the crouch key toggles crouching; 0 = hold it to stay down
  CrouchSpeed=0.45                movement speed while crouched (x the run speed, 0.3-0.8)
  OverdriveTime=3.0               seconds of heat-free fire after a perfect vent (Battlefront II overdrive; a full
                                  overheat earns all of it, a small manual vent less); 0 = off
  LockMouse=1                     1 = the mouse pointer stays inside the game window while you play, so it can no
                                  longer drift onto another screen and click there (menus, pause and Alt+Tab free it)
  CycleFavourites=0               1 = next / previous class goes through your favourites only
  WheelSlowMotion=1               slow motion while the wheel / terminal is open
  TerminalPreview=1               the terminal puts the highlighted outfit on as a preview
Your classes, favourites and loadouts are kept in DK_Blaster_Classes.ini.

Compatibility
-------------
  * Nothing about blaster mode is written into a save: a save holds the saber and your own outfit (blaster mode
    comes back on top of it while the game runs, see KeepMode). Saves made with the mod work without it. Achievements are untouched.
  * No game file is replaced or changed on disk; all code changes are found by signature at start-up. A game version
    whose code doesn't match turns the affected part off (DK_Blaster.log says which) instead of crashing.
  * Works with DK Vader Swap (with both installed, Vader Swap does the outfit changes for both mods; F5 Vader still
    works), DK FPS Fix, DK Body Persistence, DK Lighting Fix and DK ModMenu.

Uninstall
---------
Run DK_Blaster_Uninstall.bat in the game folder (or delete by hand: DK_Blaster.dll, DK_Blaster.ini,
DK_Blaster_Classes.ini, DK_Blaster*.log, DK_Blaster_README.txt, DK_ModOptions\DK_Blaster.ini, LevelPacks\dk_blaster*.*,
LevelPacks\dk_hothrebel*.lp, LevelPacks\dk_tat*.lp). Keep dinput8.dll if other DK mods are installed.

Troubleshooting
---------------
  * DK_Blaster.log (game folder) lists what was found at start-up, every mode change and every refusal.
  * Outfits never change: DK_Blaster.log has a "costume:" line at start-up that says what is missing. With DK Vader
    Swap installed, it must be 1.3.0 or newer (its log has the "costume" lines then).
  * The gun stays in the relaxed carry pose when you aim: DK_Blaster.log says why on its "aim pose:" / "pack:"
    lines. After an update the mod's asset pack is rebuilt at the first game start (the aim animations come with it).
  * The mouse pointer is kept inside the game window while you play (menus, the pause screen and Alt+Tab free it);
    LockMouse=0 turns that off.

Version history
---------------
=== CHANGELOG
1.2.3: Fixed: with the oldest DK loader (from DK 60 FPS Fix 1.0.2 or DK Lighting Fix 1.0.0) blaster mode would not
       switch on - it went straight back to the saber (it called a loader function that loader does not have).
       Changed: DK_Blaster.log names the loader's API version on its first line.
1.2.2: Fixed: radio dialogue (Juno, Kota or PROXY talking over the comms while you keep playing) switched blaster
       mode off and holstered the gun. It now only steps aside for real cutscenes; through radio talk you keep
       shooting, in the same outfit.
       Fixed: pressing the toggle during a cutscene cancelled the blaster's return and put your own outfit back. It
       now says "(the blaster comes back after it)" and the blaster returns with the same outfit when the cutscene
       ends; right after a cutscene the press brings it back at once.
       Changed: your choice of mode is remembered. Blaster mode used to end at every death, checkpoint and level
       change (your class outfit went home too); now it comes back by itself, in the same class, after a death, a
       checkpoint, a level change or loading a save, and only the toggle changes it. New setting KeepMode=1 (MOD
       OPTIONS: "Keep blaster mode after death / level change"; 0 = the old behaviour). Switching the mod off in the
       settings also drops a remembered blaster mode.
       Fixed: dying in blaster mode with KeepMode=0 showed "Outfit unchanged - Player is dead".
       Changed: with a gun in your hands Starkiller no longer strikes the saber block pose at incoming bolts - the
       deflection animation is gone too, not only the deflection itself. Saber mode and AutoDeflect=1 unchanged.
       Fixed: after radio dialogue (or any cinematic) the lightsaber could ignite in Starkiller's hand in the next
       fight while blaster mode was still on. The saber now stays away until you holster the gun.
1.2.1: Fixed: after a quick mouse turn while crouch-walking or reversing out of a sprint, Starkiller could stay
       facing (and walking toward) the camera for seconds. The body now turns from where it really faces, a big turn
       keeps going one way instead of hesitating at 180 degrees, a sprint waits until the body has come round to the
       view, and if the body ever stays turned away from your aim it is turned back within half a second (the
       DK_Blaster log notes it).
       Fixed: the camera could tilt sideways ("wonky" view) while it swung round; the view now always stays level.
       Changed: no sound any more for crouching / standing up, swapping shoulders, or running out of held breath.
1.2.0: Added: crouch (Left Alt / D-pad down, rebindable as CrouchKey; CrouchToggle=1 press to toggle, 0 = hold): a
       lower stance with a steadier gun (25 % less spread) and the camera going down with you. Crouch walk in every
       direction: the game's own walk and strafe animations become a crouch walk with the feet planted (speed:
       CrouchSpeed, 45 % of the run speed). Sprint, roll, jump or a knock-down stand you up; a CROUCHED tag shows next
       to the heat label.
       Added: aimed gun pose (AimPose=1): aiming down the sights or firing from the hip raises the gun to the shoulder
       and points it at the crosshair, up / down / left / right (the stormtrooper's own aim animations), held
       two-handed; it lowers a moment after the last shot and while you sprint or roll. The legs keep walking and
       strafing underneath, and the upper body turns the rest of the way so the gun stays on the crosshair while you
       strafe or crouch.
       Added: a lone Alt press (the crouch key) can no longer put a windowed game into Windows' window-menu mode,
       which could hold the game until the next key press (Alt+Tab, Alt+F4 and Alt+Enter work as before).
       Added: Battlefront II overdrive: a PERFECT VENT gives a few seconds of heat-free fire (OverdriveTime, 3 s for a
       full overheat, less for a small vent; 0 = off). The heat arc turns gold and counts down, and the panel shows
       OVERDRIVE.
       Added: manual vent: the vent key (R / X) works any time the gun is warm, not only after an overheat - same
       sweep and perfect window, shorter for less heat.
       Added: the mouse pointer stays inside the game window while you play (LockMouse=1), so it can no longer drift
       onto another screen and click there; menus, the pause screen and Alt+Tab free it.
       Changed: the camera no longer turns when you fire (CameraRecoil=0, the new default): recoil shows in the spread
       and the gun; CameraRecoil=1 brings back the camera kick and climb.
       Changed: heat retuned so slow guns overheat too and get their vents and overdrives: DLT-20A, Tusken Cycler,
       Bowcaster, FC-11, DL-44, DL-18 and DH-17 (their heat now waits longer than one shot before it cools). A
       charging gun holds its heat while charging.
       Fixed: sprinting in a direction away from the camera spun the character around (the game's 180-degree run turn
       started and stopped over and over). The sprint now follows the move stick: it starts within 55 degrees of the
       view; sprint held while moving sideways or back is a normal strafe / backpedal facing your aim.
       Fixed: the mouse pointer could leave the game window during play and click on other windows.
       Fixed: with a gun in his hands Starkiller could still bat enemy bolts back at the shooter (the game's second,
       saber-reflect path). Armed, he no longer deflects or reflects anything; lightsaber mode keeps all of the game's
       deflection, and AutoDeflect=1 keeps both paths in blaster mode.
1.1.0: Added: rebind every key in game: MOD OPTIONS > Blaster Combat, choose an action and press the key or mouse
       button you want (ESC cancels; a key another action already uses is swapped; needs DK ModMenu 1.1).
       Added: rifle stance (new setting RifleStance=1, on by default; 0 = Starkiller's own stance): with the
       blaster out you hold it two-handed like a stormtrooper - idle, walk, run, strafe, sprint and turns use the
       trooper's combat animations, on every class.
       Added: the jump pack is a real jump pack: the thrusters on the pack fire (flame, orange glow, thrust sound),
       it lifts you along your move, and keeping the key held hovers for up to 1.6 s with a soft landing.
       Changed: the on-screen key hints (class wheel, Requisition Terminal, grenade and special buttons, page
       arrows) show your own keys.
       Changed: a shot that hits with several pellets plays one hit sound (the strongest: kill, headshot, hit)
       instead of a stack of ticks.
       Fixed: most blaster sounds were silent in sustained fire: the game plays one copy of a sound per object, so
       each shot's sound was dropped while the previous one still rang (12 fast Z-6 shots were heard as about 2).
       Every shot, hit tick and feedback sound now plays as its own sound, and the sound pack allows enough copies
       at once (Z-6 24, other guns 16, hit ticks 8).
       Fixed: ESC could not close the pause menu while the blaster was out.
       Fixed: holding [ or ] cycled classes / wheel pages every frame; one press is one step now.
       Fixed: steadying a sniper scope with the sprint key while moving flickered the scope (the sprint key only
       steadies while scoped now).
       Fixed: the perfect vent flash is bright yellow (it was blue like the normal vent).
       Internal: looping sounds (the jump pack thrust) are started and stopped only by their effect, never as a
       one-shot sound.
1.0.0: First release: blaster mode (shoulder camera, aiming, scope, heat with active cooldown, bloom, recoil, hit
       markers, headshots, sprint, roll, grenades, class specials, aim assist), 11 weapons, 26 trooper classes with
       outfits (6 built from NPCs), class wheel, Requisition Terminal with loadouts and live preview, favourites,
       enemy bolts hit you while you hold a gun (DamageTaken, AutoDeflect options), heavy enemies take up to 2x damage,
       MOD OPTIONS page.
       Fixed in release testing: once a boss is beaten the blaster comes back (the saber used to stay out for the
       rest of the level); the gun in your hands stays when your class outfit goes on as you draw (it used to switch
       to the class's primary); trooper outfits work on their own (they needed DK Vader Swap; with it installed, its
       outfit engine is used); changing class no longer refills your health; a developer hot reload that another mod
       blocks (one hooked on top of this one) no longer leaves this mod half switched off; the uninstaller always
       refuses while the game is running (on PCs with Git or other Unix tools installed its check could be skipped)
       and also removes this README.

Author: DK
