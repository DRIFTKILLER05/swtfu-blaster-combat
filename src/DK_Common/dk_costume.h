// DK plugins for SWTFU: the player costume swap engine (dk_costume_api.h interface). One plugin owns it per game:
// DK_VaderSwap (Vader's kit built on it through Hooks) when it is installed, otherwise a plugin that runs without it
// (DK_Blaster). Both redirect the same game call, so a plugin installs it only when DK_VaderSwap has not published its
// engine (DK_COSTUME_SHARED) - one owner of the player's costume either way.
//
//   DK_PluginInit:      costume::Resolve(exe, gameDir)            addresses only, no patches
//   DK_PluginFrame:     if (!shared engine) costume::Install(...)  game thread; the hooks, then requests are served
//   DK_PluginShutdown:  costume::Shutdown(...), then dk::RestorePatches()
// (DK_VaderSwap: SetHooks, then Install at DK_PluginInit, before the first frame and level spawn.)
#pragma once
#include <windows.h>
#include "dk_costume_api.h"

struct DK_LoaderApi;

namespace dk { namespace costume {

// Extension points for the plugin that builds on the engine (DK_VaderSwap). Every hook runs on the game thread and
// any may be null; the defaults (no hooks) are the plain engine.
struct Hooks {
    // every frame while the engine is on, before (afterQueue false) and after the request queue had its turn (a swap
    // the queue ran ends the frame first). ren = the player, 0 = none. True = the hook ran the game's update itself
    // (Swap): the frame ends there.
    bool (*frame)(void* mgr, DWORD ren, bool afterQueue);
    // the player ren changed without a swap. outside = not by the level spawner (a scripted swap): the actor is unknown.
    void (*renChanged)(DWORD oldRen, DWORD newRen, bool outside);
    // a swap to 'name' is about to run (ren = the player now). False = it does not (the game update is not run).
    bool (*preSwap)(const char* name, DWORD ren);
    // the swap made newRen the player in 'name' (oldRen is gone; boss = the boss fight carried over, 0 = none)
    void (*postSwap)(const char* name, DWORD oldRen, DWORD newRen, DWORD boss);
    // the level spawner (level start, death / checkpoint respawn): before its spawn of 'key', and after it (ren = the
    // new player, 0 = no spawn). extOwner: the player wore an EXTERNAL outfit whose owner read the state in the last 3 s
    // (it puts its outfit back itself). With postSpawn set the engine leaves the spawn's log line to the hook.
    void (*preSpawn)(DWORD key);
    void (*postSpawn)(DWORD key, DWORD ren, bool extOwner);
    // what DK_COSTUME_HOME is now (default: the outfit the level spawned last)
    const char* (*home)();
    // a game call raised an exception (the engine is off from now on); default: logged
    void (*fault)(const char* where, DWORD code);
    unsigned spawnSettleMs;   // a request waits this long after a level spawn (0 = 1500)
    bool lateNames;           // costume names are read at the first frame / spawn, not at Install (Install before the game runs)
    bool unknownAfterNone;    // a player the spawner did not make after frames without one counts as a scripted swap too
};
void SetHooks(const Hooks& h);   // before Install

// Find every address the swap needs (dk_syms.h signatures). False = no costume swaps; the reason is logged (Problem()).
bool Resolve(HMODULE exe, const char* gameDir);
// Redirect the frame function's UpdateCostumeChange call and the player spawner's actor spawn, hook the HUD caches,
// and take over what a previous instance handed over under handoffKey (hot reload). False = another plugin redirected
// the calls, or a patch failed (logged; Problem()).
bool Install(const DK_LoaderApi* api, const char* handoffKey);
bool Resolved();
bool Installed();
const char* Problem();    // why the engine is not available ("" = it is)
// Before RestorePatches: a waiting request and what the player wears go to the next instance (hot reload).
void Shutdown(const DK_LoaderApi* api, const char* handoffKey);
// The engine off for the rest of the session (the owner plugin faulted): requests are refused, swaps stop.
void Off();
// This engine's dk_costume_api.h table (requests are refused until Install and the costume names are known).
const DK_CostumeApi* Api();
void Peek(DK_CostumeState* st);   // Api()->state without counting as the EXTERNAL outfit owner's heartbeat
const char* Status();     // one line for a dev command

// For hooks (game thread): a swap to 'name' now, with every fix-up. Returns true when the game's update ran.
bool Swap(void* mgr, const char* name, DWORD ren);
const char* UnsafeReason(DWORD ren);   // why a swap is not safe now (nullptr = safe)
DWORD ActiveBoss(DWORD ren);           // the boss of a running fight (alive), 0 = none
DWORD PlayerRen();
DWORD KeyOf(const char* actor);        // asset key of "<actor>.actor.xml", 0 = failed
const char* NameForKey(DWORD key);     // actor name of a costume pack in LevelPacks, nullptr = unknown
void AddName(const char* actor);       // a costume pack added after the names were read (dev builds)
int NameCount();
// What the engine knows (any thread, read only)
DWORD CurKey();          // the actor the player is now (0 = unknown)
DWORD CurRen();          // the player ren as last seen
DWORD NaturalKey();      // the actor the level spawned last
ULONGLONG RenSince();    // GetTickCount64 when CurRen became the player
ULONGLONG LastSwap();    // ... of the last swap
ULONGLONG LastCine();    // ... of the last frame with a cutscene
const char* CurName();   // actor name of CurKey ("" = unknown)
const char* HomeName();  // what DK_COSTUME_HOME is now

// Hot reload: what Shutdown hands over under handoffKey (a process-heap block that outlives the DLL). Public so a plugin
// can hand over from its own older format (DK_VaderSwap before 1.3.1).
struct Handoff {
    int version;   // 1
    DWORD naturalKey, curKey, curRen, extKey;
    bool extActive, waiting;
    char extName[64], req[64], reqBase[64]; int reqFlags;
    int swaps; unsigned lastSwapMs;
    struct { DWORD* vtSlot; void* inst[4]; int n; } hud[6];
};

}}  // namespace dk::costume
