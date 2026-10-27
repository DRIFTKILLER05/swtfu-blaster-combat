// DK_Blaster: the mod's sounds. A level loads the audio banks of its own packs only: the stormtrooper blaster sound is
// in 31 of the 48 levels, steam, reload clacks, the bowcaster or the rebel blasters in a handful. So the mod has its own
// cues (Scum/audio/cues/dk_blaster_*.cue.xml, our XML) playing the game's own samples, and it builds them once, at the
// first start, from the player's install (nothing of the game is shipped):
//   LevelPacks\dk_blaster.actor.xml_pc.lp           kaPA v5 pack holding the cue XMLs
//   LevelPacks\dk_blaster.actor.xml.re.nv.pcd/.pcp  aiMuse resident bank: the samples those cues play, copied byte
//                                                   for byte out of the game's banks (*.re.nv.pcd index -> .pcp data)
//   LevelPacks\dk_blaster.actor.xml.dkver           what was built (rebuilt when the cue table changes)
// In a level (game thread) SoundsEnsure chains the pack (ScumLib AssetCatalog::ChainAssetPack, a no-op once chained)
// and loads the bank (AudioBankManager::LoadBanks; banks are keyed stricrc32("<pack>0None") and dropped by every level
// load). Without the files or the functions every sound falls back to a vanilla cue that every level has (or silence).
// The same pack carries the game assets blaster mode needs in every level (guns, effects, our ability group): see
// dkb_pack.cpp.
#include "dkb.h"
#include "dkb_pack.h"
#include <stdio.h>
#include <stdarg.h>
#include <string>
#include <vector>
#include <algorithm>
#include "dk_pack.h"

using namespace dk;

namespace dkb {

static const char* kPackName = "dk_blaster.actor.xml";
static const int kSoundsVersion = 4;   // 2: + game assets (dkb_pack.cpp); 3: instance limits (mMaxNumInstances); 4: jump pack loop

// One of our cues. wavs: sample paths separated by '|' (several = shuffled, like the game's own cues). pitchRand: the
// game's +-N random pitch (its guns use 10). The volume is a fixed dB adjust plus, for guns, a +-1 dB random one.
struct CueDef {
    const char* id;            // dk_blaster_<id>
    const char* wavs;
    int volume;                // dB
    int pitchRand;             // 0 = none
    bool volRand;
    const char* submix;
    int priority;
    bool threeD;
    int minDist, maxDist;
    const char* fallback;      // vanilla cue every level has (used when ours are not available), or ""
    bool loop;                 // loops until its effect stops (the jump pack thrust)
};

#define SFX "Scum/audio/source/SFX/"
static const CueDef kCues[] = {
    // weapons (3D on the gun)
    { "fire_e11", SFX "Weapons/wep_stormtrooper_fire_01|" SFX "Weapons/wep_stormtrooper_fire_02|" SFX "Weapons/wep_stormtrooper_fire_03",
      0, 10, true, "Wep_Guns", 70, true, 20, 80, "Blocks_Laser1" },
    { "fire_dlt", SFX "Weapons/SniperRifle/wep_Sniper_Rifle_fire_01", 0, 6, true, "Wep_Guns", 72, true, 30, 120, "Blocks_Laser1" },
    { "fire_dh17", "Scum/DLC3Content/Audio/source/SFX/Weapons/wep_a295_rebel_blaster_01|Scum/DLC3Content/Audio/source/SFX/Weapons/wep_a295_rebel_blaster_02|"
      "Scum/DLC3Content/Audio/source/SFX/Weapons/wep_a295_rebel_blaster_03", 0, 10, true, "Wep_Guns", 70, true, 20, 80, "Blocks_Laser1" },
    { "fire_z6", SFX "Weapons/wep_ewebturret_fire_01", -2, 10, true, "Wep_Guns", 70, true, 25, 100, "Blocks_Laser1" },
    { "fire_fc11", SFX "Weapons/HeavyDefender/wep_HeavyDefenderGun_fireD_01|" SFX "Weapons/HeavyDefender/wep_HeavyDefenderGun_fireD_02|"
      SFX "Weapons/HeavyDefender/wep_HeavyDefenderGun_fireD_03", 0, 8, true, "Wep_Guns", 70, true, 25, 100, "Blocks_Laser1" },
    { "fire_bowcaster", SFX "Weapons/wep_bowcaster_fire_01", 0, 8, true, "Wep_Guns", 72, true, 25, 100, "Blocks_Laser1" },
    { "fire_dl44", SFX "Weapons/wep_EVOTrooper_Fire_01", 0, 8, true, "Wep_Guns", 72, true, 25, 90, "Blocks_Laser1" },
    { "fire_a295", "Scum/DLC3Content/Audio/source/SFX/Weapons/wep_a295_rebel_blaster_01|Scum/DLC3Content/Audio/source/SFX/Weapons/wep_a295_rebel_blaster_02|"
      "Scum/DLC3Content/Audio/source/SFX/Weapons/wep_a295_rebel_blaster_03", -1, 6, true, "Wep_Guns", 70, true, 20, 90, "Blocks_Laser1" },
    { "fire_dl18", SFX "Weapons/wep_ShadowTroopr_Fire_01|" SFX "Weapons/wep_ShadowTroopr_Fire_02", -1, 10, true, "Wep_Guns", 70, true, 20, 80, "Blocks_Laser1" },
    { "fire_cycler", "Scum/DLC2Content/Audio/Source/Weapons/wep_tusken_sniper_fire_02|Scum/DLC2Content/Audio/Source/Weapons/wep_tusken_sniper_fire_03",
      0, 6, true, "Wep_Guns", 74, true, 40, 160, "Blocks_Laser1" },
    { "charge", SFX "Characters/ImperialInterrogator/drd_ImperialInterrogator_fire_charge_01", -4, 0, false, "Wep_Guns", 60, true, 10, 40, "" },
    // heat
    { "overheat", SFX "Other/VFX/Steam_Decompression_01", 0, 0, false, "Wep_Guns", 75, true, 10, 50, "" },
    { "vent", SFX "Other/VFX/Steam_small_light_release_01|" SFX "Other/VFX/Steam_small_light_release_02", 0, 6, false, "Wep_Guns", 70, true, 10, 40, "" },
    { "vent_perfect", SFX "Characters/HeavyDefender/chr_HeavyDefender_reload_newclip_01", 0, 0, false, "Wep_Guns", 75, true, 10, 40, "ui_forcepowers_select" },
    { "vent_miss", SFX "Characters/HeavyDefender/chr_HeavyDefender_reload_clip_01", -2, 0, false, "Wep_Guns", 70, true, 10, 40, "ui_forcepowers_failiure" },
    { "ready", SFX "Weapons/Bowcaster/wep_bowcaster_foley_clip", -2, 0, false, "Wep_Guns", 65, true, 10, 40, "" },
    { "heat_warn", SFX "Weapons/DetPac/DetPack_Beep_fast_01", -4, 0, false, "UI", 60, false, 100, 100, "" },
    // handling
    { "equip", SFX "Characters/Generic/Weapons/wep_gun_small_foley_hit_01|" SFX "Characters/Generic/Weapons/wep_gun_small_foley_hit_02|"
      SFX "Characters/Generic/Weapons/wep_gun_small_foley_hit_04", 0, 6, false, "Wep_Guns", 65, true, 10, 40, "" },
    { "holster", SFX "Weapons/Bowcaster/wep_bowcaster_foley_unclip", -2, 0, false, "Wep_Guns", 65, true, 10, 40, "" },
    // feedback (2D, the game's own UI sounds)
    { "hit", SFX "Other/UI/new_ui_sabermenu_scroll_01", -8, 0, false, "UI", 85, false, 100, 100, "ui_lightsaber_move" },
    { "head", SFX "Other/UI/new_ui_sabermenu_select_01", -4, 0, false, "UI", 88, false, 100, 100, "ui_lightsaber_crystalselect" },
    { "kill", SFX "Other/UI/new_ui_forcemenu_upgrade_01", -8, 0, false, "UI", 88, false, 100, 100, "ui_forcepowers_upgrade" },
    { "deny", SFX "Other/UI/new_ui_forcemenu_upgrade_fail_01", -4, 0, false, "UI", 90, false, 100, 100, "ui_forcepowers_failiure" },
    { "move", SFX "Other/UI/new_ui_generic_03", -6, 0, false, "UI", 90, false, 100, 100, "ui_misc_move" },
    { "open", SFX "Other/UI/new_ui_generic_02", -4, 0, false, "UI", 90, false, 100, 100, "ui_map_open" },
    { "close", SFX "Other/UI/new_ui_generic_01", -4, 0, false, "UI", 90, false, 100, 100, "ui_map_close" },
    { "swap", SFX "Other/UI/new_ui_forcemenu_upgrade_01", -2, 0, false, "UI", 90, false, 100, 100, "ui_costume_option_confirm" },
    // thermal detonator
    { "nade_beep", SFX "Weapons/DetPac/DetPack_Beep_slow_01", -2, 0, false, "Wep_Explosions", 70, true, 10, 40, "" },
    { "nade_cook", SFX "Weapons/DetPac/DetPack_Beep_fast_01", -2, 0, false, "Wep_Explosions", 70, true, 10, 40, "" },
    { "nade_boom", SFX "Other/Explosions/exp_grenadeOrng_01|" SFX "Other/Explosions/exp_grenadeOrng_02", 0, 10, true, "Wep_Explosions", 80, true, 80, 200, "" },
    { "nade_throw", SFX "Weapons/DetPac/DetPack_Throw_01|" SFX "Weapons/DetPac/DetPack_Throw_02", 0, 8, true, "Wep_Explosions", 70, true, 10, 40, "" },
    // class specials (2D on the player: they are yours)
    { "ab_rapid", "Scum/DLC3Content/Audio/source/SFX/Characters/RebelAutoTurret/RebelAutoTurret_Fire_Charge_01", -2, 0, false, "Wep_Guns", 80, false, 100, 100, "" },
    { "ab_overcharge", SFX "Characters/ImperialWallCannon/ImperialWallCannon_Fire_Charge_01", -2, 0, false, "Wep_Guns", 80, false, 100, 100, "" },
    { "ab_scan", SFX "Other/UI/new_ui_generic_windowappear_01", -2, 0, false, "UI", 90, false, 100, 100, "ui_misc_move" },
    { "ab_stim", SFX "Other/VFX/Steam_small_strong_release_01|" SFX "Other/VFX/Steam_small_strong_release_02", -4, 4, false, "Wep_Guns", 80, false, 100, 100, "" },
    { "ab_jump", SFX "Characters/StormTrooper/chr_StormTrooper_Jetpack_booster_01", 0, 4, false, "Wep_Guns", 80, false, 100, 100, "" },
    // played by the jump pack's thruster effect (dkb_pack.cpp), not by Snd
    { "jet_loop", SFX "Characters/StormTrooper/chr_StormTrooper_Jetpack_lp_01", -1, 1, false, "Wep_Guns", 78, false, 100, 100, "", true },
    { "ab_ready", SFX "Other/UI/new_ui_generic_04", -8, 0, false, "UI", 85, false, 100, 100, "ui_misc_move" },
};
#undef SFX
static const int kCueCount = sizeof kCues / sizeof kCues[0];

// which fire cue each weapon plays
static const struct { const char* weapon; const char* cue; } kFire[] = {
    { "e11", "fire_e11" }, { "e11s", "fire_e11" }, { "dlt20a", "fire_dlt" }, { "dh17", "fire_dh17" }, { "z6", "fire_z6" },
    { "fc11", "fire_fc11" }, { "bowcaster", "fire_bowcaster" }, { "dl44", "fire_dl44" }, { "a295", "fire_a295" },
    { "dl18", "fire_dl18" }, { "cycler", "fire_cycler" },
};

// ------------------------------------------------------------------------------------------------ state
static volatile LONG s_buildState;   // 0 not started, 1 building, 2 ready, 3 failed
static HANDLE s_buildThread;
static char s_buildInfo[160] = "not built";
static int s_cueOk[kCueCount];          // cue made it into the pack (all its samples were found)
static int s_plays[kCueCount];
static int s_firstLogged[kCueCount];
static int s_ensures, s_bankLoads, s_chains;
static char s_levelInfo[96] = "-";

static uint32_t Crc(const char* s) { return pack::StriCrc32(s); }
static const char* BareName(const char* path) { const char* s = strrchr(path, '/'); return s ? s + 1 : path; }

static void Split(const char* wavs, std::vector<std::string>& out) {
    out.clear();
    for (const char* p = wavs; *p;) {
        const char* e = strchr(p, '|');
        out.emplace_back(p, e ? e - p : strlen(p));
        if (!e) break;
        p = e + 1;
    }
}

// the cue XML in the game's own layout (CRLF, tabs): samples -> shuffler (2+) -> pitch -> volume (-> random volume)
// How many of a cue may sound at once. The engine's default is low (the game sets 20 on its own E-Web turret cue):
// without it sustained fire dropped most shots of the Z-6 (user report 2026-10-26: "a lot of the blaster sounds have no
// audio") and the hit ticks of fast guns. 0 = the engine's default.
static int MaxInstances(const CueDef& c) {
    if (!strcmp(c.id, "fire_z6")) return 24;
    if (!strncmp(c.id, "fire_", 5)) return 16;
    if (!strcmp(c.id, "hit") || !strcmp(c.id, "head") || !strcmp(c.id, "kill") || !strcmp(c.id, "nade_boom")) return 8;
    if (c.loop) return 2;
    return 0;
}

static std::string MakeCue(const CueDef& c) {
    std::vector<std::string> w; Split(c.wavs, w);
    std::string x;
    char b[512];
    auto L = [&](int tabs, const char* fmt, ...) {
        for (int i = 0; i < tabs; i++) x += '\t';
        va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
        x += b; x += "\r\n";
    };
    L(0, "<AimuseCue project=\"Scum\" timestamp=\"2026/10/25_12:00:00\">");
    L(1, "<mSamples>");
    for (auto& s : w) { L(2, "<AimuseSample>"); L(3, "<mWav>%s</mWav>", s.c_str()); L(2, "</AimuseSample>"); }
    L(1, "</mSamples>");
    L(1, "<mVolumeAdjusts>");
    L(2, "<AimuseVolumeAdjust>"); L(3, "<mAmount>");
    if (c.volume) L(4, "<mValue>%d</mValue>", c.volume);
    L(3, "</mAmount>"); L(2, "</AimuseVolumeAdjust>");
    if (c.volRand) {
        L(2, "<AimuseVolumeAdjust>"); L(3, "<mAmount>");
        L(4, "<mValueType>Rand</mValueType>"); L(4, "<mTypeIndex>0</mTypeIndex>");
        L(3, "</mAmount>"); L(2, "</AimuseVolumeAdjust>");
    }
    L(1, "</mVolumeAdjusts>");
    L(1, "<mPitchAdjusts>");
    L(2, "<AimusePitchAdjust>"); L(3, "<mAmount>");
    if (c.pitchRand) L(4, "<mValueType>Rand</mValueType>");
    L(3, "</mAmount>"); L(2, "</AimusePitchAdjust>");
    L(1, "</mPitchAdjusts>");
    if (c.pitchRand) { L(1, "<mIntRands>"); L(2, "<IntRand>"); L(3, "<mMin>%d</mMin>", -c.pitchRand); L(3, "<mMax>%d</mMax>", c.pitchRand); L(2, "</IntRand>"); L(1, "</mIntRands>"); }
    if (c.volRand) { L(1, "<mFloatRands>"); L(2, "<FloatRand>"); L(3, "<mMin>-1</mMin>"); L(3, "<mMax>1</mMax>"); L(2, "</FloatRand>"); L(1, "</mFloatRands>"); }
    L(1, "<mNodes>");
    const int n = (int)w.size();
    for (int i = 0; i < n; i++) { L(2, "<AimuseNode>"); L(3, "<mTypeIndex>%d</mTypeIndex>", i); L(2, "</AimuseNode>"); }
    int last = n - 1;
    if (n > 1) {
        L(2, "<AimuseNode>"); L(3, "<mInputs>");
        for (int i = 0; i < n; i++) L(4, "<Int>%d</Int>", i);
        L(3, "</mInputs>"); L(3, "<mNodeType>Shuffler</mNodeType>"); L(2, "</AimuseNode>");
        last = n;
    }
    auto Node = [&](const char* type, int typeIndex) {
        L(2, "<AimuseNode>"); L(3, "<mInputs>"); L(4, "<Int>%d</Int>", last); L(3, "</mInputs>");
        L(3, "<mNodeType>%s</mNodeType>", type); L(3, "<mTypeIndex>%d</mTypeIndex>", typeIndex); L(2, "</AimuseNode>");
        last++;
    };
    if (c.loop) {   // like the game's jump trooper loop: no type index
        L(2, "<AimuseNode>"); L(3, "<mInputs>"); L(4, "<Int>%d</Int>", last); L(3, "</mInputs>");
        L(3, "<mNodeType>Loop</mNodeType>"); L(2, "</AimuseNode>");
        last++;
    }
    Node("PitchAdjust", 0);
    Node("VolumeAdjust", 0);
    if (c.volRand) Node("VolumeAdjust", 1);
    L(1, "</mNodes>");
    L(1, "<mPriority>%d</mPriority>", c.priority);
    if (const int mi = MaxInstances(c)) L(1, "<mMaxNumInstances>%d</mMaxNumInstances>", mi);
    L(1, "<mSubmix>%s</mSubmix>", c.submix);
    if (!c.threeD) L(1, "<threeDFlag>0</threeDFlag>");
    L(1, "<minDistance>%d</minDistance>", c.minDist);
    x += "\t<maxDistance>"; x += std::to_string(c.maxDist); x += "</maxDistance>\r\n</AimuseCue>";
    return x;
}

static std::string CuePath(const char* id) { return std::string("Scum/audio/cues/dk_blaster_") + id + ".cue.xml"; }

// ------------------------------------------------------------------------------------------------ files
static bool ReadAll(const char* path, std::vector<uint8_t>& out) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD sz = GetFileSize(h, nullptr), got = 0;
    out.resize(sz);
    bool ok = sz != INVALID_FILE_SIZE && (sz == 0 || (ReadFile(h, out.data(), sz, &got, nullptr) && got == sz));
    CloseHandle(h);
    return ok;
}
static bool ReadAt(const char* path, DWORD off, DWORD n, std::vector<uint8_t>& out) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    out.resize(n);
    DWORD got = 0;
    bool ok = SetFilePointer(h, (LONG)off, nullptr, FILE_BEGIN) == off && ReadFile(h, out.data(), n, &got, nullptr) && got == n;
    CloseHandle(h);
    return ok;
}
// write to <path>.tmp, then replace: a file the game still has open (a hot reload mid-level) stays as it was
bool WriteReplace(const std::string& path, const std::vector<uint8_t>& data) {
    std::string tmp = path + ".dktmp";
    HANDLE h = CreateFileA(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) { Log("sounds: cannot write %s (error %lu)", tmp.c_str(), GetLastError()); return false; }
    DWORD put = 0;
    bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &put, nullptr) && put == data.size();
    CloseHandle(h);
    if (ok && !MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        Log("sounds: cannot replace %s (error %lu - in use?)", path.c_str(), GetLastError());
        ok = false;
    }
    if (!ok) DeleteFileA(tmp.c_str());
    return ok;
}
static void Put32(std::vector<uint8_t>& v, uint32_t x) { v.insert(v.end(), (uint8_t*)&x, (uint8_t*)&x + 4); }
static void Pad16(std::vector<uint8_t>& v) { while (v.size() % 16) v.push_back(0); }

struct Sample {
    std::string path;          // as the cue names it
    uint32_t hash;             // stricrc32 of the bare name (the bank's key)
    std::vector<uint8_t> block;   // WAVEFORMATEX + data, as in the source .pcp
    uint32_t fmtLen;           // data starts here inside the block
    std::string from;          // source bank (log)
};

// find every sample in the game's resident banks (LevelPacks\*.re.nv.pcd: u32 magic, 6, count, extra; count x
// {hash, fmtOffset, dataOffset, size(fmt+data), -1, 0, 0}; the .pcp holds the blocks)
static int FindSamples(const std::string& lp, std::vector<Sample>& ss) {
    int found = 0;
    WIN32_FIND_DATAA fd;
    HANDLE f = FindFirstFileA((lp + "*.re.nv.pcd").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return 0;
    std::vector<uint8_t> idx, blk;
    do {
        if (!_strnicmp(fd.cFileName, "dk_", 3)) continue;   // never our own (or another DK) bank
        std::string pcd = lp + fd.cFileName;
        if (!ReadAll(pcd.c_str(), idx) || idx.size() < 16) continue;
        const uint32_t* h = (const uint32_t*)idx.data();
        const uint32_t n = h[2];
        if (h[1] != 6 || idx.size() < 16 + (size_t)n * 28) continue;
        std::string pcp = pcd; pcp.back() = 'p';
        for (uint32_t i = 0; i < n; i++) {
            const uint32_t* e = (const uint32_t*)(idx.data() + 16 + i * 28);
            for (auto& s : ss) {
                if (!s.block.empty() || s.hash != e[0]) continue;
                const uint32_t fo = e[1], dof = e[2], sz = e[3];
                if (dof <= fo || dof - fo > 256 || sz <= dof - fo || sz > (4u << 20)) continue;
                if (!ReadAt(pcp.c_str(), fo, sz, blk)) continue;
                const uint16_t tag = *(uint16_t*)blk.data(), ch = *(uint16_t*)(blk.data() + 2);
                if ((tag != 1 && tag != 2) || ch < 1 || ch > 2) continue;   // PCM / MS-ADPCM, mono or stereo
                s.block = blk; s.fmtLen = dof - fo; s.from = fd.cFileName;
                found++;
            }
        }
    } while (found < (int)ss.size() && FindNextFileA(f, &fd));
    FindClose(f);
    return found;
}

static std::vector<uint8_t> MakeBank(std::vector<Sample*> ss, uint32_t magic, std::vector<uint8_t>& pcp) {
    std::sort(ss.begin(), ss.end(), [](const Sample* a, const Sample* b) { return a->hash < b->hash; });
    pcp.clear(); Put32(pcp, magic);
    std::vector<uint8_t> pcd;
    Put32(pcd, magic); Put32(pcd, 6); Put32(pcd, (uint32_t)ss.size()); Put32(pcd, 0);
    for (auto* s : ss) {
        const uint32_t fo = (uint32_t)pcp.size();
        pcp.insert(pcp.end(), s->block.begin(), s->block.end());
        Put32(pcd, s->hash); Put32(pcd, fo); Put32(pcd, fo + s->fmtLen); Put32(pcd, (uint32_t)s->block.size());
        Put32(pcd, 0xFFFFFFFF); Put32(pcd, 0); Put32(pcd, 0);
    }
    for (auto* s : ss) {
        const char* bare = BareName(s->path.c_str());
        Put32(pcd, (uint32_t)strlen(bare));
        pcd.insert(pcd.end(), bare, bare + strlen(bare));
    }
    return pcd;
}

// kaPA v5 pack (layout in NOTES.md / tools\packs\mkpack.py): 0x50 header | name table | n x 64-byte records |
// n x 16 index | 16-aligned data. Each asset brings its record (type, flags); CRCs, name offset and sizes are set here.
std::vector<uint8_t> MakePack(const std::vector<PackAsset>& assets) {
    static const char kStart[] = "/**** START asset name table ****/", kEnd[] = "/**** END asset name table ****/";
    const uint32_t n = (uint32_t)assets.size();
    std::vector<uint8_t> names(kStart, kStart + sizeof kStart);
    std::vector<uint32_t> noff;
    for (auto& a : assets) { noff.push_back((uint32_t)names.size()); names.insert(names.end(), a.path.begin(), a.path.end()); names.push_back(0); }
    names.insert(names.end(), kEnd, kEnd + sizeof kEnd);
    Pad16(names);
    const uint32_t rec0 = 0x50 + (uint32_t)names.size(), idx = rec0 + n * 64, base = idx + n * 16;
    std::vector<uint8_t> recs, index, blob;
    uint32_t end = 0;
    for (uint32_t k = 0; k < n; k++) {
        const std::string& nm = assets[k].path;
        const std::string& d = assets[k].data;
        const size_t slash = nm.rfind('/');
        uint32_t r[16];
        memcpy(r, assets[k].rec, sizeof r);
        r[0] = Crc(nm.c_str());
        r[4] = Crc(nm.c_str() + slash + 1);
        r[5] = noff[k];
        r[6] = Crc(nm.substr(0, slash).c_str());
        r[10] = r[11] = (uint32_t)d.size();
        recs.insert(recs.end(), (uint8_t*)r, (uint8_t*)r + 64);
        Put32(index, k); Put32(index, (uint32_t)blob.size()); Put32(index, 0); Put32(index, 0);
        blob.insert(blob.end(), d.begin(), d.end());
        end = (uint32_t)blob.size();
        Pad16(blob);
    }
    const uint32_t size = base + (uint32_t)blob.size();
    uint32_t h[20] = { 0x4150616B, 5, n, 0, 0, 0xFFFFFFFF, 0, (uint32_t)names.size(), idx, base, end, n, base, size,
                       0, 0, 0, base, size, 0 };
    std::vector<uint8_t> out((uint8_t*)h, (uint8_t*)h + sizeof h);
    out.insert(out.end(), names.begin(), names.end());
    out.insert(out.end(), recs.begin(), recs.end());
    out.insert(out.end(), index.begin(), index.end());
    out.insert(out.end(), blob.begin(), blob.end());
    return out;
}

static DWORD WINAPI BuildThread(LPVOID) {
    const ULONGLONG t0 = GetTickCount64();
    const std::string lp = std::string(g_dir) + "LevelPacks\\";
    const std::string base = lp + kPackName;
    // what we want to build: a signature over the cue table and the wanted game assets
    std::string all;
    for (auto& c : kCues) all += MakeCue(c);
    all += PackWantSignature();
    char sig[96];
    sprintf_s(sig, "DK_Blaster sounds %d %08X %d", kSoundsVersion, Crc(all.c_str()), kCueCount);
    std::vector<uint8_t> v;
    const bool have = GetFileAttributesA((base + "_pc.lp").c_str()) != INVALID_FILE_ATTRIBUTES &&
                      GetFileAttributesA((base + ".re.nv.pcd").c_str()) != INVALID_FILE_ATTRIBUTES &&
                      GetFileAttributesA((base + ".re.nv.pcp").c_str()) != INVALID_FILE_ATTRIBUTES;
    // the cue list after the signature line of a .dkver says which cues made it into that build
    auto UsePrevious = [&](const char* why) {
        std::string txt(v.begin(), v.end());
        for (int i = 0; i < kCueCount; i++) s_cueOk[i] = txt.find(std::string("\n+") + kCues[i].id + "\r") != std::string::npos;
        PackFromVer(txt);
        sprintf_s(s_buildInfo, "%s", why);
        InterlockedExchange(&s_buildState, 2);
    };
    const bool haveVer = have && ReadAll((base + ".dkver").c_str(), v) && v.size() > 20 && !memcmp(v.data(), sig, 18);
    if (haveVer && v.size() > strlen(sig) && !memcmp(v.data(), sig, strlen(sig))) {
        char why[96]; sprintf_s(why, "up to date (%s)", sig + 18);
        UsePrevious(why);
        return 0;
    }
    // samples
    std::vector<Sample> ss;
    std::vector<std::string> w;
    for (auto& c : kCues) {
        Split(c.wavs, w);
        for (auto& p : w) {
            bool dup = false;
            for (auto& s : ss) dup |= s.path == p;
            if (!dup) { Sample s; s.path = p; s.hash = Crc(BareName(p.c_str())); s.fmtLen = 0; ss.push_back(s); }
        }
    }
    const int found = FindSamples(lp, ss);
    for (auto& s : ss) if (s.block.empty()) Log("sounds: sample %s not in any of the game's banks", s.path.c_str());
    // cues whose samples are all there
    std::vector<PackAsset> assets;
    std::vector<Sample*> used;
    std::string ver = std::string(sig) + "\r\n";
    for (int i = 0; i < kCueCount; i++) {
        Split(kCues[i].wavs, w);
        bool ok = true;
        for (auto& p : w) for (auto& s : ss) if (s.path == p && s.block.empty()) ok = false;
        s_cueOk[i] = ok;
        if (!ok) { Log("sounds: cue %s left out (samples missing) - it falls back to '%s'", kCues[i].id, *kCues[i].fallback ? kCues[i].fallback : "silence"); continue; }
        PackAsset a;
        a.path = CuePath(kCues[i].id); a.data = MakeCue(kCues[i]);
        memset(a.rec, 0, sizeof a.rec);
        a.rec[1] = 0x426C6609;                              // cue XML asset type (as the game's cues)
        a.rec[13] = 0x25400000;
        assets.push_back(a);
        for (auto& p : w) for (auto& s : ss) if (s.path == p && std::find(used.begin(), used.end(), &s) == used.end()) used.push_back(&s);
        ver += std::string("+") + kCues[i].id + "\r\n";
    }
    const int cues = (int)assets.size();
    const int gameAssets = PackCollect(lp, assets, ver);
    if (assets.empty()) {
        sprintf_s(s_buildInfo, "failed: nothing found in %s", lp.c_str());
        Log("sounds: %s - vanilla fallbacks only", s_buildInfo);
        InterlockedExchange(&s_buildState, 3);
        return 0;
    }
    size_t bytes = 0;
    for (auto* s : used) bytes += s->block.size();
    std::vector<uint8_t> pcp;
    const std::vector<uint8_t> pcd = MakeBank(used, Crc("DK_Blaster.re.nv bank"), pcp);
    const std::vector<uint8_t> lpk = MakePack(assets);
    bool ok = WriteReplace(base + ".re.nv.pcp", pcp) && WriteReplace(base + ".re.nv.pcd", pcd) && WriteReplace(base + "_pc.lp", lpk);
    if (ok) ok = WriteReplace(base + ".dkver", std::vector<uint8_t>(ver.begin(), ver.end()));
    if (!ok && haveVer) {   // the game has the old files open (a hot reload mid-session): keep using them until a restart
        UsePrevious("previous build kept (files in use - the update is written at the next game start)");
        Log("sounds: %s", s_buildInfo);
        return 0;
    }
    sprintf_s(s_buildInfo, "%s: %d cues, %d/%d samples (%u KB), %d game assets, pack %u KB in %llu ms", ok ? "built" : "write failed",
              cues, found, (int)ss.size(), (unsigned)(bytes / 1024), gameAssets, (unsigned)(lpk.size() / 1024), GetTickCount64() - t0);
    Log("sounds: %s -> LevelPacks\\%s_pc.lp + .re.nv bank", s_buildInfo, kPackName);
    InterlockedExchange(&s_buildState, ok ? 2 : 3);
    return 0;
}
static void OutfitsEntry() { OutfitPacksBuild(std::string(g_dir) + "LevelPacks\\"); }
static DWORD WINAPI BuildAll(LPVOID p) {   // the DK pack, then the converted outfits (dkb_pack.cpp)
    BuildThread(p);
    __try { OutfitsEntry(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("outfits: exception 0x%08X while building the converted outfits", GetExceptionCode()); }
    return 0;
}

void SoundsStartBuild() {
    if (s_buildThread || s_buildState) return;
    InterlockedExchange(&s_buildState, 1);
    s_buildThread = CreateThread(nullptr, 0, BuildAll, nullptr, 0, nullptr);
    if (!s_buildThread) { InterlockedExchange(&s_buildState, 3); strcpy_s(s_buildInfo, "failed: no thread"); }
}

void SoundsShutdown() {
    if (s_buildThread) { WaitForSingleObject(s_buildThread, 10000); CloseHandle(s_buildThread); s_buildThread = nullptr; }
}

// ------------------------------------------------------------------------------------------------ in game
typedef bool(__thiscall* ChainPack_t)(void* catalog, const char* pack, int flags);
typedef int(__cdecl* LoadBanks_t)(const char* pack);
static ChainPack_t s_chain;
static void** s_catalog;
static LoadBanks_t s_loadBanks;
static BYTE** s_bankMgr;
static uint32_t s_bankKey;

void SoundsInstall(const Range& text) {
    if (HMODULE scum = GetModuleHandleA("ScumLibLEC_Retail.dll")) {
        s_chain = (ChainPack_t)GetProcAddress(scum, "?ChainAssetPack@AssetCatalog@@QAE_NPBDW4AssetPackChainFlags@@@Z");
        s_catalog = (void**)GetProcAddress(scum, "?gpAssetCatalog@@3PAVAssetCatalog@@A");
    }
    s_loadBanks = (LoadBanks_t)FindSym(text, "AudioBankManager_LoadBanks");
    s_bankMgr = (BYTE**)FindSym(text, "gAudioBankManager");
    char key[96]; sprintf_s(key, "%s0None", kPackName);   // language-independent bank: type 0, "None"
    s_bankKey = Crc(key);
    if (!s_chain || !s_catalog || !s_loadBanks || !s_bankMgr)
        Log("sounds: pack/bank functions not found (chain %d, catalog %d, LoadBanks %d, manager %d) - vanilla fallback sounds only",
            !!s_chain, !!s_catalog, !!s_loadBanks, !!s_bankMgr);
    SoundsStartBuild();
}

static int BankState() {   // 1 loaded, 0 not, -1 unreadable
    __try {
        BYTE* m = *s_bankMgr;
        if (!m) return -1;
        for (BYTE** p = *(BYTE***)(m + 4), **e = *(BYTE***)(m + 8); p != e; ++p) if (*p && *(DWORD*)(*p + 0x10) == s_bankKey) return 1;
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static int SafeLoadBanks() {
    __try { return s_loadBanks(kPackName); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("sounds: LoadBanks exception 0x%08X", GetExceptionCode()); return -1; }
}
static int SafeChain() {
    __try { void* c = *s_catalog; return c ? (s_chain(c, kPackName, 0) ? 1 : 0) : -1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("sounds: ChainAssetPack exception 0x%08X", GetExceptionCode()); return -1; }
}

// dev: chain any LevelPacks\<name>_pc.lp (research: what a chained pack makes available)
int ChainPackDev(const char* name) {
    if (!s_chain || !s_catalog) return -2;
    __try { void* c = *s_catalog; return c ? (s_chain(c, name, 0) ? 1 : 0) : -1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -3; }
}

static bool s_ourCues;          // our pack + bank are live in this level
static bool s_chainedHere;      // our pack is chained in this level (its assets and abilities are usable)
bool PackChained() { return s_chainedHere; }
static ULONGLONG s_retryAt;
// our cues usable right now: every level load drops the bank (checked on each use: a walk over ~50 bank pointers)
static bool Live() {
    if (!s_ourCues) return false;
    if (BankState() != 1) { s_ourCues = false; s_chainedHere = false; Log("sounds: our bank is gone (level load) - loading it again on the next sound"); return false; }
    return true;
}
// game thread, in a level: chain our pack and load our bank when this level does not have them yet (a failure is
// retried every 5 s at most)
bool SoundsEnsure() {
    s_ensures++;
    if (Live()) return true;
    if (s_buildState != 2 || !s_chain || !s_catalog || !s_loadBanks || !s_bankMgr || GetTickCount64() < s_retryAt) return false;
    int st = BankState();
    const ULONGLONG t0 = GetTickCount64();
    int chained = SafeChain();
    s_chains++;
    s_chainedHere = chained == 1;
    int r = 0;
    if (st == 0) { r = SafeLoadBanks(); s_bankLoads++; st = BankState(); }
    s_ourCues = st == 1 && chained >= 0;
    if (!s_ourCues) s_retryAt = GetTickCount64() + 5000;
    sprintf_s(s_levelInfo, "chain %d, bank %s (LoadBanks %d), %llu ms", chained, st == 1 ? "loaded" : st == 0 ? "MISSING" : "unreadable", r, GetTickCount64() - t0);
    Log("sounds: %s", s_levelInfo);
    memset(s_firstLogged, 0, sizeof s_firstLogged);
    return s_ourCues;
}

static int CueIndex(const char* id) {
    for (int i = 0; i < kCueCount; i++) if (!strcmp(kCues[i].id, id)) return i;
    return -1;
}
// the cue to play for one of our ids: ours when live, else the vanilla fallback (nullptr = silent)
const char* SoundCue(const char* id) {
    static char b[8][128];
    static int k;
    const int i = CueIndex(id);
    if (i < 0) return nullptr;
    char* o = b[k++ & 7];
    if (s_ourCues && s_cueOk[i]) sprintf_s(o, 128, "%s", CuePath(id).c_str());
    else if (*kCues[i].fallback) sprintf_s(o, 128, "Scum/audio/cues/%s.cue.xml", kCues[i].fallback);
    else return nullptr;
    return o;
}
const char* SoundFireCue(const WeaponDef* w) {
    if (!Live()) SoundsEnsure();
    const char* id = "fire_e11";
    for (auto& f : kFire) if (w && !strcmp(f.weapon, w->id)) id = f.cue;
    const int i = CueIndex(id);
    if (i >= 0) s_plays[i]++;
    const char* c = SoundCue(id);
    return c ? c : (w ? w->fireCue : "");
}
// play one of our sounds at the player (feedback; the gun's fire cue goes through D.shoot at the muzzle); both are new
// sources at a position each time, so repeats overlap (DKB.sound)
void Snd(const char* id) {
    if (!Live() && g_mode == M_BLASTER) SoundsEnsure();
    const int i = CueIndex(id);
    const char* cue = SoundCue(id);
    if (i >= 0) s_plays[i]++;
    if (i >= 0 && !s_firstLogged[i]) { s_firstLogged[i] = 1; Log("sound %s -> %s", id, cue ? cue : "(silent)"); }
    if (i >= 0 && kCues[i].loop) return;   // a loop belongs to its effect (which stops it); a one-shot source never would
    if (cue) Sound(cue);
}

bool SoundIsLoop(const char* id) {
    const int i = CueIndex(id);
    return i >= 0 && kCues[i].loop;
}

const char* SoundsIds() {
    static char b[600];
    int n = 0;
    for (int i = 0; i < kCueCount && n < (int)sizeof b - 24; i++) n += sprintf_s(b + n, sizeof b - n, "%s%s", i ? " " : "", kCues[i].id);
    return b;
}

const char* SoundsStatus() {
    static char b[900];
    int n = sprintf_s(b, "sounds: %s | level: %s | ours %d | ensures %d chains %d bankLoads %d | plays:", s_buildInfo, s_levelInfo,
                      s_ourCues, s_ensures, s_chains, s_bankLoads);
    for (int i = 0; i < kCueCount && n < (int)sizeof b - 40; i++)
        if (s_plays[i] || !s_cueOk[i]) n += sprintf_s(b + n, sizeof b - n, " %s=%d%s", kCues[i].id, s_plays[i], s_cueOk[i] ? "" : "(missing)");
    return b;
}

}  // namespace dkb
