// DK_Blaster: assets of the DK pack (dkb_pack.cpp collects the game's own, dkb_sound.cpp writes the pack)
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace dkb {

struct PackAsset {
    std::string path;          // asset path as the game names it
    std::string data;
    uint32_t rec[16];          // kaPA record (asset type, flags...); the writer sets the CRCs, name offset and sizes
};

std::string PackWantSignature();                                                 // what the pack should hold (rebuild when it changes)
int PackCollect(const std::string& lp, std::vector<PackAsset>& out, std::string& ver);   // copy the game assets + our ability group
void PackFromVer(const std::string& ver);                                       // an earlier build's contents
void OutfitPacksBuild(const std::string& lp);                                   // converted NPC outfits (build thread)

// dkb_sound.cpp: the pack writer
std::vector<uint8_t> MakePack(const std::vector<PackAsset>& assets);
bool WriteReplace(const std::string& path, const std::vector<uint8_t>& data);

}  // namespace dkb
