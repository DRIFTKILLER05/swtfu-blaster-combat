// Costume requests between DK plugins. One engine owns the player's costume swaps (a hook on the game's own
// UpdateCostumeChange call, with the fix-ups a swap needs: player bind, HUD, sector, boss carry-over). It is the shared
// engine in dk_costume.cpp: DK_VaderSwap's instance (with Vader's parts on its hooks) when DK_VaderSwap is installed,
// otherwise one a plugin installs for itself (DK_Blaster).
// Other plugins (DK_Blaster's trooper outfits) ask the owner through this interface instead of swapping on their own,
// so the two never fight over the player.
//
//   auto api = (const DK_CostumeApi*)loader->GetShared(DK_COSTUME_SHARED);   // nullptr: DK_VaderSwap missing / off
//   api->request("playerStormTrooper", nullptr, DK_COSTUME_EXTERNAL); api->state(&st);
// (the loader's shared registry, not GetProcAddress: plugins may run from shadow copies with other file names; the
// functions are also exported as DK_VaderSwap_RequestCostume / DK_VaderSwap_CostumeState.)
//
// One request is queued at a time (a newer one replaces a waiting one, which ends "superseded"). It runs on the game
// thread at the next safe moment: it waits while a timing rule holds (swap cool-down, a fresh spawn, a cinematic just
// ended; at most kDkCostumeWaitMs) and fails at once in an unsafe state (cutscene, QTE, death, boss fight ...), with the
// reason in DK_CostumeState::result. Read the shared pointer again each time (it changes when DK_VaderSwap reloads).
// After a level respawn while the player wore an EXTERNAL outfit (and its owner read the state in the last 3 s),
// DK_VaderSwap leaves the player as the level spawned him: the owner then asks for its outfit again or for HOME.
#pragma once

#define DK_COSTUME_API 1
#define DK_COSTUME_HOME "<home>"        // name: back to the costume DK_VaderSwap itself would have the player in
static const int kDkCostumeWaitMs = 5000;

enum DkCostumeFlags {
    DK_COSTUME_EXTERNAL = 1,             // an outfit the caller keeps: VaderSwap's re-apply after a respawn waits for it
};

struct DK_CostumeState {
    int size;              // in: sizeof(DK_CostumeState)
    int api;               // DK_COSTUME_API
    int seq;               // id of the last request accepted
    int done;              // id of the last request finished (== seq: nothing waiting)
    int ok;                // the finished request swapped (or was already worn)
    int external;          // the player wears a caller's outfit (since its last successful EXTERNAL request)
    unsigned swapMs;       // duration of the last swap
    unsigned curKey;       // asset key (stricrc32 of "<actor>.actor.xml") of the costume the player is in, 0 unknown
    char cur[64];          // its actor name ("playerStormTrooper"), "" unknown
    char home[64];         // what DK_COSTUME_HOME resolves to now
    char result[96];       // "ok", "already", or why it was refused / failed
    int swaps;             // swaps DK_VaderSwap has made since it loaded, for any reason (its own toggle too)
};

// returns the request id (> 0); 0 = not accepted (no game / plugin inactive / bad name: see result).
// base: a full player costume (e.g. "playerStormTrooper") whose pack is chained before the swap, for a slim outfit pack
// that carries only a model and its materials (its actor builds on that costume's animations and sounds); nullptr or
// "" = none. LevelPacks\<name>.actor.xml_pc.lp (and the base's) must exist.
typedef int(__cdecl* DK_RequestCostume_t)(const char* name, const char* base, int flags);
// fills *st (st->size set by the caller); 1 = ok. Calling it counts as the caller's heartbeat for EXTERNAL outfits.
typedef int(__cdecl* DK_CostumeState_t)(DK_CostumeState* st);

#define DK_COSTUME_SHARED "DK_VaderSwap.costume"
struct DK_CostumeApi {
    int api;                       // DK_COSTUME_API
    DK_RequestCostume_t request;
    DK_CostumeState_t state;
};
