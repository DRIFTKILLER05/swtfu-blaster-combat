// DK plugin interface between DK_Loader (dinput8.dll) and the DK_*.dll plugins of SWTFU.exe.
//
// Plugin exports (all extern "C" __cdecl, all optional except DK_PluginInit):
//   void DK_PluginAttach(const DK_LoaderApi* api)  called before DK_PluginInit (loader 1.1+)
//   void DK_PluginInit(HMODULE exe)                 install hooks. The first load happens before the game's CRT starts.
//   void DK_PluginFrame(void)                       game thread, once per frame, just before Game::Tick
//   int  DK_PluginShutdown(void)                    hot reload: undo every patch, stop threads, close files.
//                                                   Return 1 when the plugin may be unloaded. Without this export
//                                                   a plugin is never unloaded.
#pragma once
#include <windows.h>

#define DK_LOADER_API_VERSION 3   // 2: events, code registry, ini overlay, reports; 3: patch registry (fields below)

struct DK_LoaderApi {
    int version;                                   // DK_LOADER_API_VERSION
    const char* gameDir;                           // folder of SWTFU.exe, with trailing backslash
    volatile LONG* frame;                          // frames seen by the loader's Game::Tick hook (0 = hook missing)
    int devReload;                                 // DK_Loader.ini DevReload
    // Ask the loader to reload a plugin (file name like "DK_VaderSwap.dll"; nullptr = every plugin whose file
    // changed). Runs at the start of the next frame, never from inside the caller.
    void (__cdecl* RequestReload)(const char* name);
    // Values that survive a plugin's reload (e.g. a D3D device vtable found by the previous instance).
    void* (__cdecl* GetShared)(const char* key);
    void (__cdecl* SetShared)(const char* key, void* value);
    // "name loaded|inactive generation path\n" per plugin. Returns the length written.
    int (__cdecl* ListPlugins)(char* buf, int size);

    // ---- version 2 (check version >= 2 before use)
    volatile LONG* lastFrameTick;                  // GetTickCount() of the last frame round (game thread)
    // Flight-recorder event line (any thread): kept in the loader's ring, survives plugin reloads, written to every
    // report. dk::Log lines of plugins built with DK_Common are mirrored here automatically.
    void (__cdecl* Event)(const char* tag, const char* text);
    int (__cdecl* CopyEvents)(char* buf, int size, DWORD lastMs);   // "hh:mm:ss.mmm [tag] text" lines, 0 = all
    // Executable memory a plugin allocated (trampolines, code caves), so a crash inside it names the plugin.
    void (__cdecl* RegisterCode)(const void* start, DWORD size, const char* owner);
    const char* (__cdecl* CodeOwner)(const void* addr);
    // ini overlay (dkmatrix): replaces keys of DK ini files for the plugins' GetPrivateProfile*A reads, in memory
    // (the files are never written). file = ini file name; value nullptr = remove the key; file nullptr = clear all.
    // A plugin sees new values at its next read (most read their ini at init: reload it).
    void (__cdecl* SetConfig)(const char* file, const char* section, const char* key, const char* value);
    int (__cdecl* ListConfig)(char* buf, int size);
    // Report contributors: fn(dir) adds files to a report folder (crash, hang, user report hotkey, dkb dump).
    void (__cdecl* RegisterDumper)(const char* name, void (__cdecl* fn)(const char* dir));
    void (__cdecl* UnregisterDumper)(const char* name);
    // Write a report now (kind "report"/"dump"/"hang"; dump/hang include a minidump); dirOut receives the folder.
    int (__cdecl* WriteReport)(const char* kind, const char* note, char* dirOut, int size);

    // ---- version 3 (check version >= 3 before use)
    // Patch registry: every plugin's VirtualProtect on SWTFU.exe's image is recorded by the loader (import hook, no
    // plugin cooperation needed): one line per (address, owner): "addr n owner gen state pre orig now\n", addr / pre
    // (the byte before addr) / orig (bytes before the owner's first write, up to 16) / now in hex, state active |
    // restored. Returns the length written. tools\dkpatches.py names and lints them.
    int (__cdecl* ListPatches)(char* buf, int size);
};

// The loader exports this (dinput8.dll); dk_util uses it to find the services from any plugin.
typedef const DK_LoaderApi* (__cdecl* DK_GetLoaderApi_t)();

// True when this plugin was loaded before in this game session (i.e. this is a hot reload). Marks it loaded.
inline bool DK_IsReload(const DK_LoaderApi* api, const char* key) {
    if (!api) return false;
    bool again = api->GetShared(key) != nullptr;
    api->SetShared(key, (void*)1);
    return again;
}
