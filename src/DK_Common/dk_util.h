// DK plugins for SWTFU: shared utilities (log, signature scan, recorded code patching, detours, symbols).
#pragma once
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "dk_syms.h"
struct DK_LoaderApi;   // dk_plugin.h

namespace dk {

// Log file, truncated on open unless `append` (a hot-reloaded instance continues the log); each line is
// "hh:mm:ss.mmm [tag] text". At maxLines lines the file rotates to <name>.old.log and a new one starts (no line is
// dropped; at most two files of about maxLines lines on disk).
void LogOpen(const char* path, const char* tag = "DK", int maxLines = 400, bool append = false);
void Log(const char* fmt, ...);
// Where log lines are mirrored (DK_Loader's event ring). Plugins find the loader by themselves at LogOpen.
typedef void (__cdecl* LogSink_t)(const char* tag, const char* line);
void SetLogSink(LogSink_t sink);
// The loader's API v2+ (nullptr with an older loader): found through the loader's DK_GetLoaderApi export.
const ::DK_LoaderApi* LoaderApi();
// Executable memory for a code cave / trampoline, registered with the loader (crash reports name the owner).
uint8_t* AllocCode(size_t size);

// Module .text range of the host exe.
struct Range { uint8_t* lo; uint8_t* hi; };
Range TextRange(HMODULE m);

// "55 8B EC ?? 0F" style pattern. Returns the address only if the pattern matches exactly once
// (count receives the number of matches); otherwise nullptr.
uint8_t* FindUnique(const Range& r, const char* pattern, int* count = nullptr);

// Find a signature, log found/missing/ambiguous under `name`, return address (+offset) or nullptr.
uint8_t* Sig(const Range& r, const char* name, const char* pattern, int offset = 0);

// All matches of a pattern; succeeds (returns true, fills out[]) only when exactly `expected` match.
bool SigAll(const Range& r, const char* name, const char* pattern, uint8_t** out, int expected);

// Resolve a symbol from the database (dk_syms.h) at runtime through its signature. Returns nullptr (and logs)
// when the signature is missing/ambiguous. Logs a warning when the result differs from the recorded address.
uint8_t* FindSym(const Range& text, const char* name);

// Section range of a module by name (".rdata", ".text", ...).
Range SectionRange(HMODULE m, const char* name);

template <class T> inline T Rd(const void* p) { T v; memcpy(&v, p, sizeof(T)); return v; }

// Write bytes into code/rdata with VirtualProtect + FlushInstructionCache. Every write is recorded (original
// bytes of the first write to an address) so RestorePatches can undo it.
bool WriteMem(void* dst, const void* src, size_t n);

// Undo every recorded write, newest first. A site whose bytes were changed by someone else since our write is
// left alone and counted as a conflict. Returns the number of conflicts.
int RestorePatches();
int PatchCount();

// 5-byte JMP detour. `stolen` = number of whole instructions bytes (>=5) at `target` that are
// position independent (verified by the caller's signature). Returns a trampoline that runs the
// stolen bytes and jumps back, or nullptr. The trampoline is never freed (a thread may still be in it).
void* Detour(uint8_t* target, void* hook, size_t stolen);

// Overwrite a rel32 CALL at `site` (E8 xx xx xx xx) to call `fn`; returns the old target.
void* RedirectCall(uint8_t* site, void* fn);

// Who a rel32 CALL / JMP at `site` goes to when it is not the game: writes the module holding the target
// ("DK_Blaster.dll") or "a code cave of DK_X" (loader code registry) into name and returns true. False = the target is
// inside SWTFU.exe (vanilla) or the site is not a rel32 call / jump. For logs before stacking on another plugin's hook.
bool HookedBy(const uint8_t* site, char* name, int size);

// How many of our rel32 call / jump redirects another DK plugin has since redirected to its own code: it stacked on
// our hook and calls our code as its "original". Unloading then leaves it calling freed memory (or, after a hot reload
// at the same base, our new instance: the two hooks call each other until the stack overflows). Check it FIRST in
// DK_PluginShutdown and refuse the unload while it is nonzero; nothing is changed. `who` = the first such plugin.
int StackedOn(char* who, int size);

// SEH-guarded memory access for diagnostics: false if the range is not readable/writable.
bool SafeRead(const void* src, void* dst, size_t n);
bool SafeWrite(void* dst, const void* src, size_t n);

}  // namespace dk
