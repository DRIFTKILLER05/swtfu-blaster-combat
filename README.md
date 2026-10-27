# Blaster Combat for Star Wars: The Force Unleashed (PC)

Source code of the mod [Blaster Combat - Battlefront-style Troopers](https://www.nexusmods.com/starwarstheforceunleashed/mods/39)
by DK: play the game as a blaster trooper (third-person shooter camera, aiming, crouch, weapons, HUD) and switch back
to Starkiller's lightsaber at any time.

The download on Nexus holds two DLLs, both built from this repository:

| File | What it is | Version | SHA-256 of the released file |
|---|---|---|---|
| `dinput8.dll` | DK mod loader, shared by all DK mods for this game | 1.3.0 | `9a05c219e9fe3fdac4474abf2403ae9cd98544ccdf3783fbd30c3983053fe262` |
| `DK_Blaster.dll` | Blaster Combat | 1.2.3 | `50cebbf348c95abed981e9f387a235369d3f08137c2861a999728e423e0fdb80` |

## What the code does

**`dinput8.dll` (src/DK_Loader)** is a proxy DLL. The game loads `dinput8.dll` from its own folder; the loader loads
the real `dinput8.dll` from the Windows system folder and forwards `DirectInput8Create` and the other exports to it, so
input works as before. Only inside `SWTFU.exe` it then loads the plugins `DK_*.dll` next to the exe and offers them a
small service API (`src/DK_Common/dk_plugin.h`). It writes `DK_Loader.log`, and on a crash a report folder
`DK_Reports\crash_<time>\` (text report plus a minidump via `dbghelp.dll`) in the game folder. It has no network
code and changes no file outside the game folder.

**`DK_Blaster.dll` (src/DK_Blaster)** changes the running game in memory only, never the exe on disk. It finds the
game functions it hooks by byte signature (`src/DK_Common/dk_syms.h`), patches them with `VirtualProtect`, and runs its
own Lua (`dkb_luamod.cpp`) in the game's Lua state. At the first start it builds `LevelPacks\dk_*.lp` from the
player's own game files (weapon models, sounds, trooper outfits), so no game asset is shipped. Settings live in
`DK_ModOptions\DK_Blaster.ini`, log in `DK_Blaster.log`. No network code.

## Build

Requires Visual Studio 2022 or the free Build Tools for Visual Studio 2022 with the workload
"Desktop development with C++" (MSVC v143, x86). Then run:

```
build.bat
```

Output: `bin\dinput8.dll` and `bin\DK_Blaster.dll` (32-bit, same compiler and linker flags as the released files).
A rebuild matches the released files byte for byte except the fields MSVC stamps into every build (link time,
PE checksum and the PDB id); `python tools\compare_pe.py bin\DK_Blaster.dll <downloaded>\DK_Blaster.dll` checks that.

## Install from source

Copy `bin\dinput8.dll`, `bin\DK_Blaster.dll` and the contents of `package\` into the game folder (next to
`SWTFU.exe`). `package\DK_Blaster_README.txt` is the player's manual (controls, settings, uninstall).

## Layout

- `src/DK_Loader` - the loader (`dinput8.dll`)
- `src/DK_Common` - code shared by the loader and the mod (signatures, Lua access, pack builder)
- `src/DK_Blaster` - the mod
- `package` - the non-code files of the release (manual, settings file, uninstaller)

Copyright (c) 2026 DK. See [LICENSE.md](LICENSE.md).
