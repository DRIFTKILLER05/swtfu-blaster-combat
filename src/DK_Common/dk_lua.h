// DK plugins for SWTFU: run Lua in the game's main state from native code (game thread).
// Every call takes the engine's Lua lock and runs inside lua_cpcall, so no Lua error can reach the game (an unprotected
// error is a panic -> exit(1)). The game's _G is strict: chunks must use rawget/rawset for their own globals.
// Addresses come from dk_syms.h and are verified by their first bytes at Init; without them every call fails cleanly.
#pragma once
#include <windows.h>

namespace dk { namespace lua {

// Resolve and verify the Lua API (once; cheap afterwards). False = Lua unavailable (logged once).
bool Init();
bool Ready();   // Init succeeded and the game's Lua state exists

// Run a chunk; every result is converted with tostring and joined by '\t' into out. Returns false on any error
// (out = "error: <message>"). chunkName is shown in Lua error messages ("=DK_Blaster").
bool Run(const char* code, char* out, int size, const char* chunkName = "=DK");
// Same, into a static buffer (valid until the next call on this thread).
const char* Run(const char* code);
const char* Runf(const char* fmt, ...);
// Run a chunk and read up to max number results (nil / non-numbers become NaN). Returns the result count, -1 on error.
int RunNums(const char* code, float* out, int max);

// Number of failed calls since start (diagnostics).
int Errors();
const char* LastError();

}}  // namespace dk::lua
