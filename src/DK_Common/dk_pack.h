// DK plugins for SWTFU: read assets straight from the game's "kaPA" v5 packs (LevelPacks\*.lp), e.g. HUD fonts and
// textures that a plugin draws itself. Asset overrides in DK_Mods\<Mod>\<asset path> win, as with DK_AssetMods.
// Any thread; plain file reads (the packs are opened shareable).
#pragma once
#include <windows.h>
#include <stdint.h>
#include <vector>

namespace dk { namespace pack {

// ScumLib stricrc32: CRC32 table, init 0, no final xor, '\' -> '/', every byte & 0xDF (case-insensitive).
uint32_t StriCrc32(const char* s);

// Read an asset by its full path (e.g. "Scum/Assets/Anark/fonts/Korataki28.a2f") from DK_Mods overrides first, then
// from the given packs in order (file names relative to <gameDir>LevelPacks\). Returns false when not found.
bool Read(const char* gameDir, const char* assetPath, const char* const* packs, int packCount, std::vector<uint8_t>& out);

}}  // namespace dk::pack
