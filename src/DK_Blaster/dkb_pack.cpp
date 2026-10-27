// DK_Blaster: the game assets in the DK pack. A level loads only what its own actors use, so many levels lack the
// guns, their bolt / muzzle effects and the stormtrooper bolt ability (Felucia, Raxus Prime, Tatooine, the Jedi
// Temple...). Our pack (LevelPacks\dk_blaster.actor.xml_pc.lp, built in dkb_sound.cpp) therefore also carries:
//  - the weapon props: actor XML + every asset in the rig folder of its GTO (models, LODs, collision, materials,
//    textures), and the thermal detonator prop
//  - the bolt / muzzle / grenade effects and the textures they name (looked up by name under Shared/effects/textures/);
//    an effect's own sound call is pointed at our cue (a cue whose bank is not loaded replays the previous sound)
//  - our ability group Scum/runtime/abilities/DK/DK_Blaster.abilityGroup.xml: one bolt entry per weapon and the
//    thermal detonator (Do Explosion), generated from the weapon table
// Copied byte for byte (records too) from the player's own level packs at the first start; nothing is shipped.
// Chaining the pack in a level makes all of it loadable (ren.LoadActor) and usable (projectile.Launch) - verified in
// Felucia_Act1, which has none of it.
#include "dkb.h"
#include <stdio.h>
#include <stdarg.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include "dk_pack.h"
#include "dkb_pack.h"

using namespace dk;

namespace dkb {

const char* const kDkAbilityGroup = "Scum/runtime/abilities/DK/DK_Blaster.abilityGroup.xml";
const char* const kDkGrenadeEntry = "Thermal Detonator";
const int kDkGrenadeSpeeds[] = { 8, 10, 12, 14, 16, 18, 21 };
const int kDkGrenadeSpeedCount = sizeof kDkGrenadeSpeeds / sizeof kDkGrenadeSpeeds[0];
static const char* kTemplateAbility = "Scum/runtime/abilities/Stormtrooper/StormTrooper_BlasterDamage.abilityGroup.xml";

static const char* kGrenadeActor = "Scum/handheld/actor/oneHandSphere/thermalDetonator.actor.xml";
static const char* kFx = "Scum/effects/Library/Weapons/";
static const char* kGrenadeFx[] = { "Explosives/Explosives_DetonationPackExpl.vfx", "Explosives/Explosives_DetonationPackPulse.vfx",
                                    "Explosives/Explosives_DetonationPackPre.vfx" };
// effect sound calls -> our cues (the originals live in a few levels' banks only)
static const struct { const char* from; const char* to; } kFxCues[] = {
    { "Explosives_DetonationPackExpl", "dk_blaster_nade_boom" },
    { "Explosives_DetonationPackPulse", "dk_blaster_nade_beep" },
    { "Explosives_DetonationPackPre", "dk_blaster_nade_cook" },
    { "Rewards_ComboSpheres", "dk_blaster_swap" },
    { "jumptrooper_jetpack", "dk_blaster_jet_loop" },
};
// effects copied under our own name (levels that have the original keep it, with its own sound), with our cues or
// without sound: the outfit swap flash (the holocron reward flash) and the jump pack (the jump trooper's thruster: one
// nozzle carries the thrust loop, the other is silent; the ignition burst is silent, the ability plays its own cue)
static const struct { const char* src; const char* dst; const char* tag; bool silent; } kOwnFx[] = {
    { "Scum/effects/Library/Objects/Holocron/Rewards/Rewards_Flash.vfx", "Scum/effects/Library/DK/dk_swap_flash.vfx", "swap", false },
    { "Scum/effects/Library/NPC/jumptrooper/jumptrooper_jetpack.vfx", "Scum/effects/Library/DK/dk_jet.vfx", "jet", false },
    { "Scum/effects/Library/NPC/jumptrooper/jumptrooper_jetpack.vfx", "Scum/effects/Library/DK/dk_jet_ns.vfx", "jetns", true },
    { "Scum/effects/Library/NPC/jumptrooper/jumptrooper_jetpack_boost.vfx", "Scum/effects/Library/DK/dk_jet_boost.vfx", "jetboost", true },
};
static const int kOwnFxCount = sizeof kOwnFx / sizeof kOwnFx[0];
// the rifle stance (dkb_hold.cpp): the stormtrooper's combat locomotion; several levels have no troopers (or only some
// of these), so they come with us
static const char* kHoldDir = "Scum/animation/characters/PCDX/MaleAverage/stormtrooper/combat/";
static const char* kHoldAnims[] = {
    "stormTrooper_ma_com_strafe_run_B", "stormTrooper_ma_com_strafe_run_F", "stormTrooper_ma_com_strafe_run_L",
    "stormTrooper_ma_com_strafe_run_R", "stormtrooper_ma_com_idle", "stormTrooper_ma_com_walk", "stormtrooper_ma_com_run",
    "stormtrooper_ma_com_run_bank_L", "stormtrooper_ma_com_run_bank_R", "stormTrooper_ma_com_run_to_idle_lFoot",
    "stormTrooper_ma_com_run_to_idle_rFoot", "stormTrooper_ma_com_stepOrient_0_F", "stormTrooper_ma_com_stepOrient_90_L",
    "stormTrooper_ma_com_stepOrient_90_R", "stormTrooper_ma_com_stepOrient_180_L", "stormTrooper_ma_com_stepOrient_180_R",
    "stormtrooper_ma_com_runTurn_180_L", "stormtrooper_ma_com_runTurn_180_R",
};
static const int kHoldCount = sizeof kHoldAnims / sizeof kHoldAnims[0];
// the trooper's upper-body aim grid (yaw right / front / left x pitch up / down): the aimed gun pose (dkb_aim.cpp)
static const char* kAimAnims[] = {
    "stormtrooper_ma_com_blaster_aim_o_RU", "stormtrooper_ma_com_blaster_aim_o_RD", "stormtrooper_ma_com_blaster_aim_o_FU",
    "stormtrooper_ma_com_blaster_aim_o_FD", "stormtrooper_ma_com_blaster_aim_o_LU", "stormtrooper_ma_com_blaster_aim_o_LD",
};
static const int kAimCount = sizeof kAimAnims / sizeof kAimAnims[0];
static std::string HoldPath(const char* a) { return std::string(kHoldDir) + a + ".animations"; }

static bool s_weaponOk[16];
static bool s_abilityOk, s_grenadeOk, s_holdOk, s_aimOk;
static bool s_ownFxOk[kOwnFxCount];

static std::string Lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
static std::string Dir(const std::string& p) { size_t s = p.rfind('/'); return s == std::string::npos ? "" : p.substr(0, s); }
static std::string Base(const std::string& p) { size_t s = p.rfind('/'); return s == std::string::npos ? p : p.substr(s + 1); }
static std::string Stem(const std::string& p) { std::string b = Base(p); size_t d = b.find('.'); return d == std::string::npos ? b : b.substr(0, d); }
static std::string FxPath(const char* f) { return std::string(kFx) + f; }
static std::string FxPathOf(const char* vfx) {   // "Laser_Red_01.vfx" -> its library folder
    return std::string(kFx) + (!_strnicmp(vfx, "Laser_", 6) ? "Laser/" : !_strnicmp(vfx, "Blaster_", 8) ? "Blaster/" : "Explosives/") + vfx;
}

// ------------------------------------------------------------------------------------------------ ability XML
// the game's layout: CRLF, 4-space indents
static void Tag(std::string& x, const char* name, const char* fmt, ...) {
    char v[256]; va_list a; va_start(a, fmt); vsnprintf(v, sizeof v, fmt, a); va_end(a);
    x += "                    <E>\r\n                        <tag name=\""; x += name; x += "\">"; x += v; x += "</tag>\r\n                    </E>\r\n";
}
static std::string AbilityXml() {
    std::string x = "<AbilityGroup>\r\n    <mAbilities>\r\n";
    for (int i = 0; i < kWeaponCount; i++) {
        const WeaponDef& w = kWeapons[i];
        x += "        <E ver=\"1\">\r\n            <strName>"; x += w.id; x += "</strName>\r\n";
        x += "            <mAffectCheck>\r\n                <socialEffectFlags>36</socialEffectFlags>\r\n            </mAffectCheck>\r\n";
        x += "            <mExecution>\r\n                <funcAbility>Do Damage</funcAbility>\r\n            </mExecution>\r\n";
        x += "            <mAbilityAttribs>\r\n                <mTagValues>\r\n";
        Tag(x, "ability_is_ranged", "1");
        Tag(x, "ability_effect_type", "Single");
        Tag(x, "ability_damage1_percent", "%d", (int)(w.damage + 0.5f));   // (the damage hook sets the real value)
        Tag(x, "ability_damage1_type", "Blaster");
        Tag(x, "ability_projectile_moving_vfx", "%s", w.boltVfx);
        Tag(x, "ability_blaster_bolt_length", "%.2f", w.boltLen);
        Tag(x, "ability_blaster_muzzle_vfx", "%s", w.muzzleVfx);
        Tag(x, "ability_blaster_speed", "%.1f", w.speed);
        Tag(x, "ability_fire_down_weapon_tip_y", "1");
        x += "                </mTagValues>\r\n            </mAbilityAttribs>\r\n        </E>\r\n";
    }
    // thermal detonator: a grenade projectile that explodes on its fuse (maxTime) or on a character; one entry per
    // throw speed (projectile.Launch's speed is not used for grenades: the entry's own speed is)
    for (int si = 0; si < kDkGrenadeSpeedCount; si++) {
    x += "        <E ver=\"1\">\r\n            <strName>"; x += kDkGrenadeEntry; x += " "; x += std::to_string(kDkGrenadeSpeeds[si]); x += "</strName>\r\n";
    x += "            <mInitiateCheck>\r\n                <funcTarget>None</funcTarget>\r\n                <fMaxRange>0</fMaxRange>\r\n"
         "                <socialFlags>8</socialFlags>\r\n                <usageFlags>1</usageFlags>\r\n            </mInitiateCheck>\r\n";
    x += "            <mStartEvent>\r\n            </mStartEvent>\r\n";
    x += "            <mAffectCheck>\r\n                <socialEffectFlags>Enemy|Neutral|Object</socialEffectFlags>\r\n"
         "                <usageFlags>1</usageFlags>\r\n            </mAffectCheck>\r\n";
    x += "            <mExecution>\r\n                <funcAbility>Do Explosion</funcAbility>\r\n"
         "                <keyVisualFX>Explosives_DetonationPackExpl.vfx</keyVisualFX>\r\n            </mExecution>\r\n";
    x += "            <mAbilityAttribs>\r\n                <mTagValues>\r\n";
    Tag(x, "ability_is_ranged", "1");
    Tag(x, "ability_havok_force", "%d", 10000);
    Tag(x, "ability_effect_range", "%.1f", 4.5f);
    Tag(x, "ability_effect_type", "Sphere");
    Tag(x, "ability_damage1_percent", "%d", 950);   // the Hoth rebel soldier's grenade: a trooper at the blast dies
    Tag(x, "ability_damage1_type", "Heavy");
    Tag(x, "actor_projectile_grenade", "%s", kGrenadeActor);
    Tag(x, "ability_grenade_angular_velocity", "8.0f");
    Tag(x, "ability_grenade_speed_meters_per_second", "%d", kDkGrenadeSpeeds[si]);
    Tag(x, "ability_projectile_lifetime_expire_sfx", "");
    Tag(x, "ability_explode_on_character_contact", "1");
    Tag(x, "ability_explode_on_contact", "0");
    Tag(x, "ability_timer_cap_after_contact", "3.0");
    Tag(x, "ability_projectile_moving_vfx", "Explosives_DetonationPackPulse.vfx");
    Tag(x, "ability_projectile_moving2_vfx", "Explosives_DetonationPackPre.vfx");
    Tag(x, "ability_projectile_moving2_pct_of_lifetime", "0.70");
    Tag(x, "ability_grenade_max_linear_velocity_on_ground_collision", "1.2");
    Tag(x, "ability_grenade_max_angular_velocity_on_ground_collision", "3.0");
    x += "                </mTagValues>\r\n            </mAbilityAttribs>\r\n        </E>\r\n";
    }
    x += "    </mAbilities>\r\n</AbilityGroup>\r\n";
    return x;
}

// ------------------------------------------------------------------------------------------------ pack scan
struct Src { std::string pack; uint32_t rec[16]; uint32_t off; uint32_t size; std::string path; };

static bool ReadRange(HANDLE h, uint32_t off, uint32_t n, std::vector<uint8_t>& out) {
    out.resize(n);
    DWORD got = 0;
    return SetFilePointer(h, (LONG)off, nullptr, FILE_BEGIN) == off && (n == 0 || (ReadFile(h, out.data(), n, &got, nullptr) && got == n));
}

// one pass over every level pack: calls want(lowerPath) for each name; the first pack holding a wanted asset wins
template <class F>
static void ScanPacks(const std::string& lp, F want, std::unordered_map<std::string, Src>& found) {
    WIN32_FIND_DATAA fd;
    HANDLE ff = FindFirstFileA((lp + "*_pc.lp").c_str(), &fd);
    if (ff == INVALID_HANDLE_VALUE) return;
    std::vector<uint8_t> hdr, tab, idx;
    do {
        if (!_strnicmp(fd.cFileName, "dk_", 3)) continue;   // never our own (or another DK) pack
        const std::string file = lp + fd.cFileName;
        HANDLE h = CreateFileA(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        if (ReadRange(h, 0, 0x50, hdr)) {
            const uint32_t* H = (const uint32_t*)hdr.data();
            const uint32_t n = H[2], nt = 0x50 + H[6], rec0 = nt + H[7];
            if (H[0] == 0x4150616B && H[1] == 5 && n && n < 200000 && H[7] < (64u << 20) &&
                ReadRange(h, nt, H[7] + n * 64, tab) && ReadRange(h, H[8], n * 16, idx)) {
                const uint8_t* recs = tab.data() + H[7];
                for (uint32_t i = 0; i < n; i++) {
                    const uint32_t* r = (const uint32_t*)(recs + i * 64);
                    if (r[5] >= H[7]) continue;
                    const char* nm = (const char*)tab.data() + r[5];
                    const size_t len = strnlen(nm, H[7] - r[5]);
                    std::string low = Lower(std::string(nm, len));
                    if (found.count(low) || !want(low)) continue;
                    Src s;
                    s.pack = file; memcpy(s.rec, r, 64);
                    s.off = H[9] + ((const uint32_t*)idx.data())[i * 4 + 1];
                    s.size = r[10]; s.path.assign(nm, len);
                    found.emplace(low, s);
                }
                (void)rec0;
            }
        }
        CloseHandle(h);
    } while (FindNextFileA(ff, &fd));
    FindClose(ff);
}

static bool ReadSrc(const Src& s, std::string& out) {
    HANDLE h = CreateFileA(s.pack.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::vector<uint8_t> v;
    const bool ok = s.size < (32u << 20) && ReadRange(h, s.off, s.size, v);
    CloseHandle(h);
    if (ok) out.assign((const char*)v.data(), v.size());
    return ok;
}

// every <tag>value</tag> of the given tag in an XML text
static void Values(const std::string& x, const char* tag, std::vector<std::string>& out) {
    const std::string o = std::string("<") + tag + ">", c = std::string("</") + tag + ">";
    for (size_t p = x.find(o); p != std::string::npos; p = x.find(o, p + 1)) {
        size_t s = p + o.size(), e = x.find(c, s);
        if (e != std::string::npos && e > s) out.push_back(x.substr(s, e - s));
    }
}

static void Copy(const Src& s, const std::string& data, std::vector<PackAsset>& out) {
    PackAsset a;
    a.path = s.path; a.data = data;
    memcpy(a.rec, s.rec, 64);
    out.push_back(a);
}

// ------------------------------------------------------------------------------------------------ collect
std::string PackWantSignature() {
    std::string s = AbilityXml();
    for (int i = 0; i < kWeaponCount; i++) { s += kWeapons[i].actor; s += kWeapons[i].boltVfx; s += kWeapons[i].muzzleVfx; }
    s += kGrenadeActor;
    for (auto f : kGrenadeFx) s += f;
    for (auto& o : kOwnFx) { s += o.dst; s += o.silent ? "-" : "+"; }
    for (auto a : kHoldAnims) s += a;
    for (auto a : kAimAnims) s += a;
    for (auto& m : kFxCues) { s += m.from; s += m.to; }
    return s;
}

int PackCollect(const std::string& lp, std::vector<PackAsset>& out, std::string& ver) {
    std::unordered_map<std::string, Src> found;
    // pass 1: actors, effects, the template ability record
    std::vector<std::string> fx;
    for (int i = 0; i < kWeaponCount; i++) {
        fx.push_back(FxPathOf(kWeapons[i].boltVfx));
        fx.push_back(FxPathOf(kWeapons[i].muzzleVfx));
    }
    for (auto f : kGrenadeFx) fx.push_back(FxPath(f));
    std::unordered_map<std::string, int> exact;
    for (int i = 0; i < kWeaponCount; i++) exact[Lower(kWeapons[i].actor)] = 1;
    exact[Lower(kGrenadeActor)] = 1;
    for (auto& f : fx) exact[Lower(f)] = 1;
    exact[Lower(kTemplateAbility)] = 1;
    for (auto& o : kOwnFx) exact[Lower(o.src)] = 1;
    for (auto a : kHoldAnims) exact[Lower(HoldPath(a))] = 1;
    for (auto a : kAimAnims) exact[Lower(HoldPath(a))] = 1;
    ScanPacks(lp, [&](const std::string& p) { return exact.count(p) != 0; }, found);

    // pass 2: rig folders of the actors' models, the materials they name (DLC guns keep theirs outside the rig), the
    // textures the effects name
    std::vector<std::string> rigs;
    std::unordered_map<std::string, int> tex, matNames;
    std::string data;
    std::vector<std::vector<std::string>> weaponMats(kWeaponCount);
    auto rigOf = [&](const char* actor, std::vector<std::string>* mats) -> std::string {
        auto it = found.find(Lower(actor));
        if (it == found.end() || !ReadSrc(it->second, data)) return "";
        if (mats) {
            std::vector<std::string> m; Values(data, "mDefMaterial", m);
            for (auto& n : m) { mats->push_back(Lower(n)); matNames[Lower(n)] = 1; }
        }
        std::vector<std::string> v; Values(data, "mkeyGTOModel", v);
        return v.empty() ? "" : Lower(Dir(v[0])) + "/";
    };
    std::vector<std::string> weaponRig(kWeaponCount);
    for (int i = 0; i < kWeaponCount; i++) { weaponRig[i] = rigOf(kWeapons[i].actor, &weaponMats[i]); if (!weaponRig[i].empty()) rigs.push_back(weaponRig[i]); }
    const std::string grenadeRig = rigOf(kGrenadeActor, nullptr);
    if (!grenadeRig.empty()) rigs.push_back(grenadeRig);
    std::vector<std::string> texFx = fx;
    for (auto& o : kOwnFx) texFx.push_back(o.src);
    for (auto& f : texFx) {
        auto it = found.find(Lower(f));
        if (it == found.end() || !ReadSrc(it->second, data)) continue;
        std::vector<std::string> v; Values(data, "texture", v);
        for (auto& t : v) tex[Lower(t)] = 1;
    }
    ScanPacks(lp, [&](const std::string& p) {
        for (auto& r : rigs) if (!p.compare(0, r.size(), r)) return true;
        if (!p.compare(0, 24, "shared/effects/textures/") && p.size() > 4 && !p.compare(p.size() - 4, 4, ".dds")) return tex.count(Lower(Stem(p))) != 0;
        if (p.size() > 9 && !p.compare(p.size() - 9, 9, ".material")) return matNames.count(Stem(p)) != 0;
        return false;
    }, found);

    // pass 3: the folders of materials that are not in the weapon's own rig folder (their textures sit next to them)
    std::vector<std::vector<std::string>> weaponMatDirs(kWeaponCount);
    std::vector<std::string> matDirs;
    for (int i = 0; i < kWeaponCount; i++) {
        if (weaponRig[i].empty()) continue;
        for (auto& m : weaponMats[i]) {
            bool inRig = false; std::string other;
            for (auto& kv : found) {
                const std::string& p = kv.first;
                if (p.size() <= 9 || p.compare(p.size() - 9, 9, ".material") || Stem(p) != m) continue;
                if (!p.compare(0, weaponRig[i].size(), weaponRig[i])) { inRig = true; break; }
                if (other.empty()) other = Dir(p) + "/";
            }
            if (inRig || other.empty()) continue;
            bool have = false;
            for (auto& d : weaponMatDirs[i]) if (d == other) have = true;
            if (!have) { weaponMatDirs[i].push_back(other); matDirs.push_back(other); }
        }
    }
    if (!matDirs.empty())
        ScanPacks(lp, [&](const std::string& p) {
            for (auto& d : matDirs) if (!p.compare(0, d.size(), d) && p.find('/', d.size()) == std::string::npos) return true;
            return false;
        }, found);

    // copy what was found
    int copied = 0;
    auto add = [&](const std::string& low, bool fixCues) -> bool {
        auto it = found.find(low);
        if (it == found.end() || !ReadSrc(it->second, data)) return false;
        if (fixCues)
            for (auto& m : kFxCues) {
                const std::string from = std::string("Scum/audio/cues/") + m.from + ".cue.xml", to = std::string("Scum/audio/cues/") + m.to + ".cue.xml";
                for (size_t p = data.find(from); p != std::string::npos; p = data.find(from, p + to.size())) data.replace(p, from.size(), to);
            }
        Copy(it->second, data, out);
        copied++;
        return true;
    };
    std::unordered_map<std::string, int> done;
    auto addOnce = [&](const std::string& low, bool fixCues) -> bool {
        if (done.count(low)) return done[low] != 0;
        const bool ok = add(low, fixCues);
        done[low] = ok ? 1 : 0;
        return ok;
    };
    auto addRig = [&](const std::string& rig) -> int {
        int n = 0;
        for (auto& kv : found) if (!kv.first.compare(0, rig.size(), rig) && addOnce(kv.first, false)) n++;
        return n;
    };
    // effects (and their textures)
    int fxOk = 0;
    for (auto& f : fx) fxOk += addOnce(Lower(f), true);
    for (int i = 0; i < kOwnFxCount; i++) {   // our own copies (swap flash, jump pack)
        const auto& o = kOwnFx[i];
        auto it = found.find(Lower(o.src));
        if (it != found.end() && ReadSrc(it->second, data)) {
            if (o.silent) {   // the sequencer's sound keys go
                for (size_t p = data.find("<functype>sound</functype>"); p != std::string::npos; p = data.find("<functype>sound</functype>", p)) {
                    const size_t a = data.rfind("<DT_VISUALFX_SEQUENCERKEY>", p), b = data.find("</DT_VISUALFX_SEQUENCERKEY>", p);
                    if (a == std::string::npos || b == std::string::npos) break;
                    size_t s = a;
                    while (s > 0 && (data[s - 1] == '\t' || data[s - 1] == ' ')) s--;
                    size_t e = b + 27;
                    if (e < data.size() && data[e] == '\r') e++;
                    if (e < data.size() && data[e] == '\n') e++;
                    data.erase(s, e - s);
                    p = s;
                }
            } else
                for (auto& m : kFxCues) {
                    const std::string from = std::string("Scum/audio/cues/") + m.from + ".cue.xml", to = std::string("Scum/audio/cues/") + m.to + ".cue.xml";
                    for (size_t p = data.find(from); p != std::string::npos; p = data.find(from, p + to.size())) data.replace(p, from.size(), to);
                }
            Src s2 = it->second; s2.path = o.dst;
            Copy(s2, data, out);
            copied++; fxOk++; s_ownFxOk[i] = true;
            ver += std::string("+fx:") + o.tag + "\r\n";
        } else Log("pack: %s not in any level pack - %s", o.src, i == 0 ? "outfit swaps without the flash" : "the jump pack without thruster flames");
    }
    int texOk = 0;
    for (auto& kv : found)
        if (!kv.first.compare(0, 24, "shared/effects/textures/") && tex.count(Lower(Stem(kv.first)))) texOk += addOnce(kv.first, false);
    for (auto& t : tex) {
        bool have = false;
        for (auto& kv : found) if (!kv.first.compare(0, 24, "shared/effects/textures/") && Lower(Stem(kv.first)) == t.first) { have = true; break; }
        if (!have) Log("pack: effect texture %s not in any level pack", t.first.c_str());
    }
    {   // the rifle stance
        int n = 0;
        for (auto a : kHoldAnims) n += addOnce(Lower(HoldPath(a)), false);
        s_holdOk = n == kHoldCount;
        if (s_holdOk) ver += "+anim:hold\r\n";
        else Log("pack: %d of %d trooper animations found - the rifle stance is off", n, kHoldCount);
        int m = 0;
        for (auto a : kAimAnims) m += addOnce(Lower(HoldPath(a)), false);
        s_aimOk = m == kAimCount;
        if (s_aimOk) ver += "+anim:aim\r\n";
        else Log("pack: %d of %d trooper aim animations found - the aimed gun pose is off", m, kAimCount);
    }
    // weapons
    for (int i = 0; i < kWeaponCount && i < 16; i++) {
        const bool actor = addOnce(Lower(kWeapons[i].actor), false);
        int parts = weaponRig[i].empty() ? 0 : addRig(weaponRig[i]);
        for (auto& d : weaponMatDirs[i]) parts += addRig(d);
        s_weaponOk[i] = actor && parts > 0;
        if (s_weaponOk[i]) ver += std::string("+w:") + kWeapons[i].id + "\r\n";
        else Log("pack: weapon %s left out (actor %d, rig assets %d) - it is used only where the level has it", kWeapons[i].id, actor, parts);
    }
    // the ability group: the stormtrooper bolt ability's record as the template (asset type)
    auto tpl = found.find(Lower(kTemplateAbility));
    s_abilityOk = tpl != found.end();
    if (s_abilityOk) {
        PackAsset a;
        a.path = kDkAbilityGroup; a.data = AbilityXml();
        memcpy(a.rec, tpl->second.rec, 64);
        out.push_back(a);
        ver += "+ab\r\n";
    } else Log("pack: no ability template found - bolts use the stormtrooper ability where the level has it");
    // thermal detonator
    const bool gActor = addOnce(Lower(kGrenadeActor), false);
    const int gParts = grenadeRig.empty() ? 0 : addRig(grenadeRig);
    s_grenadeOk = s_abilityOk && gActor && gParts > 0 && done[Lower(FxPath(kGrenadeFx[0]))];
    if (s_grenadeOk) ver += "+gr\r\n";
    else Log("pack: thermal detonator unavailable (actor %d, rig %d, effects %d)", gActor, gParts, (int)done[Lower(FxPath(kGrenadeFx[0]))]);
    Log("pack: %d game assets copied (%d/%d effects, %d textures)", copied, fxOk, (int)fx.size(), texOk);
    return copied;
}

// ------------------------------------------------------------------------------------------------ converted outfits
// NPC troopers the game has no player costume for become player outfits. One slim pack per outfit,
// LevelPacks\<actor>.actor.xml_pc.lp: a new actor (the player stormtrooper costume's actor with the NPC's model,
// material mappings and lighting groups; never its AI, targeting, physics or player attributes) and the NPC's rig
// folder (model, LODs, materials, textures) plus the materials it names from elsewhere. DK_VaderSwap wears it with the
// stormtrooper costume as the base pack (playerBase, animations, sounds). Built from the installed game on the first
// start (background thread); nothing is shipped. Brute-rig NPCs (shock troopers 1.71x, royal guards 1.11x with staffs)
// do not fit the player skeleton cleanly and are left out.
struct ConvDef { const char* actor; const char* npc; };
static const ConvDef kConv[] = {
    { "dk_hothRebelSoldier", "Scum/DLC3Content/characters/actor/maleAverage/rebelSoldier/HOT_rebelSoldier.actor.xml" },
    { "dk_hothRebelElite", "Scum/DLC3Content/characters/actor/maleAverage/rebelElite/HOT_rebelElite.actor.xml" },
    { "dk_hothRebelPilot", "Scum/DLC3Content/characters/actor/maleAverage/rebelPilot/HOT_rebelPilot.actor.xml" },
    { "dk_hothRebelSniper", "Scum/DLC3Content/characters/actor/maleAverage/rebelSniper/HOT_rebelSniper.actor.xml" },
    { "dk_tatWeequay", "Scum/DLC2Content/characters/actor/maleAverage/weequay/TAT_weequay.actor.xml" },
    { "dk_tatTuskenSniper", "Scum/DLC2Content/characters/actor/maleAverage/tuskenSniper/TAT_tuskenSniper.actor.xml" },
};
static const int kConvCount = sizeof kConv / sizeof kConv[0];
static const char* kConvTemplate = "Scum/characters/actor/maleAverage/playerVariations/playerStormTrooper.actor.xml";
static const char* kConvDir = "Scum/characters/actor/maleAverage/playerVariations/";
static const int kConvVersion = 1;
static volatile LONG s_convOk[8];
static volatile LONG s_convState;            // 0 not yet, 1 building, 2 done
static char s_convInfo[160] = "not built yet";

// the <name>...</name> element (first), with its tags; "" when missing
static std::string Element(const std::string& x, const char* name) {
    const std::string o = std::string("<") + name + ">", c = std::string("</") + name + ">";
    size_t s = x.find(o);
    if (s == std::string::npos) return "";
    size_t e = x.find(c, s);
    return e == std::string::npos ? "" : x.substr(s, e + c.size() - s);
}
static bool ReplaceElement(std::string& x, const char* name, const std::string& with) {
    const std::string cur = Element(x, name);
    if (cur.empty()) return false;
    const size_t s = x.find(cur);
    if (x.find(cur, s + 1) != std::string::npos) return false;   // exactly one
    x.replace(s, cur.size(), with);
    return true;
}
// the NPC's material mappings, keeping those whose model (the GTO name before '-') is in the rig and whose material
// (after it) is in that rig's models: a few NPC actors carry mappings of another character's materials
static std::string MappingsFor(const std::string& npc, const std::unordered_map<std::string, int>& gtoStems,
                               const std::string& gtoText, int* kept, int* dropped) {
    const std::string all = Element(npc, "mMaterialMappings");
    const std::string o = "<ActorMaterialMapping>", c = "</ActorMaterialMapping>";
    std::string out = "<mMaterialMappings>\r\n";
    for (size_t p = all.find(o); p != std::string::npos; p = all.find(o, p + 1)) {
        const size_t e = all.find(c, p);
        if (e == std::string::npos) break;
        const std::string m = all.substr(p, e + c.size() - p);
        std::vector<std::string> g; Values(m, "mGtoMaterial", g);
        const size_t dash = g.empty() ? std::string::npos : g[0].find('-');
        if (dash == std::string::npos || !gtoStems.count(Lower(g[0].substr(0, dash))) ||
            gtoText.find(g[0].substr(dash + 1)) == std::string::npos) { (*dropped)++; continue; }
        out += "\t\t" + m + "\r\n";
        (*kept)++;
    }
    out += "\t</mMaterialMappings>";
    return out;
}

void OutfitPacksBuild(const std::string& lp) {
    const ULONGLONG t0 = GetTickCount64();
    InterlockedExchange(&s_convState, 1);
    std::string want;
    for (auto& c : kConv) { want += c.actor; want += c.npc; }
    char sig[96];
    sprintf_s(sig, "DK_Blaster outfits %d %08X %d", kConvVersion, pack::StriCrc32((want + kConvTemplate).c_str()), kConvCount);
    const std::string verPath = lp + "dk_blaster_outfits.dkver";
    // up to date: the signature matches and every outfit it lists is still there
    {
        HANDLE h = CreateFileA(verPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        std::string txt;
        if (h != INVALID_HANDLE_VALUE) {
            char buf[4096]; DWORD got = 0;
            if (ReadFile(h, buf, sizeof buf - 1, &got, nullptr)) txt.assign(buf, got);
            CloseHandle(h);
        }
        if (!txt.compare(0, strlen(sig), sig)) {
            int n = 0; bool all = true;
            for (int i = 0; i < kConvCount; i++) {
                const bool listed = txt.find(std::string("\n+o:") + kConv[i].actor + "\r") != std::string::npos;
                const bool there = GetFileAttributesA((lp + Lower(kConv[i].actor) + ".actor.xml_pc.lp").c_str()) != INVALID_FILE_ATTRIBUTES;
                if (listed && !there) all = false;
                InterlockedExchange(&s_convOk[i], listed && there ? 1 : 0);
                n += listed && there;
            }
            if (all) {
                sprintf_s(s_convInfo, "up to date: %d/%d converted outfits", n, kConvCount);
                Log("outfits: %s", s_convInfo);
                InterlockedExchange(&s_convState, 2);
                return;
            }
        }
    }
    // pass 1: the template and the NPC actors
    std::unordered_map<std::string, Src> found;
    std::unordered_map<std::string, int> exact;
    exact[Lower(kConvTemplate)] = 1;
    for (auto& c : kConv) exact[Lower(c.npc)] = 1;
    ScanPacks(lp, [&](const std::string& p) { return exact.count(p) != 0; }, found);
    std::string tpl, data;
    auto t = found.find(Lower(kConvTemplate));
    if (t == found.end() || !ReadSrc(t->second, tpl)) {
        sprintf_s(s_convInfo, "no stormtrooper costume actor found - converted outfits unavailable");
        Log("outfits: %s", s_convInfo);
        InterlockedExchange(&s_convState, 2);
        return;
    }
    // pass 2: rig folders + the materials the NPCs name
    std::vector<std::string> npcXml(kConvCount), rig(kConvCount);
    std::vector<std::vector<std::string>> mats(kConvCount);
    std::unordered_map<std::string, int> matNames;
    for (int i = 0; i < kConvCount; i++) {
        auto it = found.find(Lower(kConv[i].npc));
        if (it == found.end() || !ReadSrc(it->second, npcXml[i])) continue;
        std::vector<std::string> g; Values(npcXml[i], "mkeyGTOModel", g);
        if (g.empty()) continue;
        std::string gp = g[0]; for (auto& ch : gp) if (ch == '\\') ch = '/';
        rig[i] = Lower(Dir(gp)) + "/";
        std::vector<std::string> m; Values(npcXml[i], "mDefMaterial", m);
        for (auto& n : m) { mats[i].push_back(Lower(n)); matNames[Lower(n)] = 1; }
    }
    ScanPacks(lp, [&](const std::string& p) {
        for (auto& r : rig) if (!r.empty() && !p.compare(0, r.size(), r)) return true;
        if (p.size() > 9 && !p.compare(p.size() - 9, 9, ".material")) return matNames.count(Stem(p)) != 0;
        return false;
    }, found);
    // pass 3: folders of the materials outside the rig (their textures sit next to them)
    std::vector<std::vector<std::string>> matDirs(kConvCount);
    std::vector<std::string> allDirs;
    for (int i = 0; i < kConvCount; i++) {
        if (rig[i].empty()) continue;
        for (auto& m : mats[i]) {
            bool inRig = false; std::string other;
            for (auto& kv : found) {
                const std::string& p = kv.first;
                if (p.size() <= 9 || p.compare(p.size() - 9, 9, ".material") || Stem(p) != m) continue;
                if (!p.compare(0, rig[i].size(), rig[i])) { inRig = true; break; }
                if (other.empty()) other = Dir(p) + "/";
            }
            if (inRig || other.empty()) continue;
            if (std::find(matDirs[i].begin(), matDirs[i].end(), other) == matDirs[i].end()) { matDirs[i].push_back(other); allDirs.push_back(other); }
        }
    }
    if (!allDirs.empty())
        ScanPacks(lp, [&](const std::string& p) {
            for (auto& d : allDirs) if (!p.compare(0, d.size(), d) && p.find('/', d.size()) == std::string::npos) return true;
            return false;
        }, found);

    std::string ver = std::string(sig) + "\r\n";
    int built = 0;
    size_t bytes = 0;
    for (int i = 0; i < kConvCount; i++) {
        InterlockedExchange(&s_convOk[i], 0);
        if (rig[i].empty()) { Log("outfits: %s left out (%s not in any level pack)", kConv[i].actor, kConv[i].npc); continue; }
        std::vector<PackAsset> assets;
        std::string gtoText;   // every model of the rig, for the mapping check
        std::unordered_map<std::string, int> gtoStems;
        int files = 0;
        auto addDir = [&](const std::string& d, bool deep) {
            for (auto& kv : found) {
                const std::string& p = kv.first;
                if (p.compare(0, d.size(), d) || (!deep && p.find('/', d.size()) != std::string::npos)) continue;
                if (!ReadSrc(kv.second, data)) continue;
                if (p.size() > 4 && !p.compare(p.size() - 4, 4, ".gto")) { gtoText += data; gtoStems[Stem(p)] = 1; }
                Copy(kv.second, data, assets);
                files++;
            }
        };
        addDir(rig[i], true);
        for (auto& d : matDirs[i]) addDir(d, false);
        // the actor: the template's attributes and rules, the NPC's model, mappings and look-related groups
        std::string a = tpl;
        int kept = 0, dropped = 0;
        const std::string maps = MappingsFor(npcXml[i], gtoStems, gtoText, &kept, &dropped);
        bool ok = ReplaceElement(a, "mkeyGTOModel", Element(npcXml[i], "mkeyGTOModel")) && kept > 0 &&
                  ReplaceElement(a, "mMaterialMappings", maps);
        std::string groups;
        for (const char* gname : { "ATTRIB_GROUP_LIGHTING_GROUPS_RUNTIME", "ATTRIB_GROUP_CLOTH", "ATTRIB_GROUP_MESHJIGGLE" }) {
            const std::string g = Element(npcXml[i], gname);
            if (g.empty()) continue;
            groups += g + "\r\n\t\t";
            std::vector<std::string> items; Values(g, "mClothItem", items);   // cloth items are their own assets
            for (auto& it : items) {
                std::string ip = Lower(it); for (auto& ch : ip) if (ch == '\\') ch = '/';
                auto f = found.find(ip);
                if (f != found.end() && ReadSrc(f->second, data)) { Copy(f->second, data, assets); files++; }
            }
        }
        const size_t ev = a.find("<ATTRIB_GROUP_ACTOR_EVENTMATERIALS>");
        if (!groups.empty()) { if (ev != std::string::npos) a.insert(ev, groups); else ok = false; }
        if (!ok || !files) { Log("outfits: %s left out (actor %d, rig files %d, mappings %d)", kConv[i].actor, (int)ok, files, kept); continue; }
        PackAsset act;
        act.path = std::string(kConvDir) + kConv[i].actor + ".actor.xml";
        act.data = a;
        memcpy(act.rec, t->second.rec, 64);
        assets.insert(assets.begin(), act);
        const std::vector<uint8_t> pk = MakePack(assets);
        const std::string file = lp + Lower(kConv[i].actor) + ".actor.xml_pc.lp";
        if (!WriteReplace(file, pk)) {
            const bool there = GetFileAttributesA(file.c_str()) != INVALID_FILE_ATTRIBUTES;
            Log("outfits: %s could not be written%s", kConv[i].actor, there ? " (in use: the previous build stays)" : "");
            if (there) { InterlockedExchange(&s_convOk[i], 1); ver += std::string("+o:") + kConv[i].actor + "\r\n"; }
            continue;
        }
        InterlockedExchange(&s_convOk[i], 1);
        ver += std::string("+o:") + kConv[i].actor + "\r\n";
        built++; bytes += pk.size();
        Log("outfits: %s <- %s: %d files, %d mappings (%d foreign dropped), %u KB", kConv[i].actor, Base(kConv[i].npc).c_str(), files,
            kept, dropped, (unsigned)(pk.size() / 1024));
    }
    WriteReplace(verPath, std::vector<uint8_t>(ver.begin(), ver.end()));
    sprintf_s(s_convInfo, "built %d/%d converted outfits (%u KB) in %llu ms", built, kConvCount, (unsigned)(bytes / 1024), GetTickCount64() - t0);
    Log("outfits: %s", s_convInfo);
    InterlockedExchange(&s_convState, 2);
}

bool OutfitConvReady(const char* actor) {
    for (int i = 0; i < kConvCount; i++) if (!_stricmp(kConv[i].actor, actor)) return s_convOk[i] != 0;
    return false;
}
int OutfitConvState() { return (int)s_convState; }
const char* OutfitConvStatus() { return s_convInfo; }

// a .dkver from an earlier build: what it holds
void PackFromVer(const std::string& txt) {
    for (int i = 0; i < kWeaponCount && i < 16; i++) s_weaponOk[i] = txt.find(std::string("\n+w:") + kWeapons[i].id + "\r") != std::string::npos;
    s_abilityOk = txt.find("\n+ab\r") != std::string::npos;
    s_grenadeOk = txt.find("\n+gr\r") != std::string::npos;
    for (int i = 0; i < kOwnFxCount; i++) s_ownFxOk[i] = txt.find(std::string("\n+fx:") + kOwnFx[i].tag + "\r") != std::string::npos;
    s_holdOk = txt.find("\n+anim:hold\r") != std::string::npos;
    s_aimOk = txt.find("\n+anim:aim\r") != std::string::npos;
}
bool PackAimOk() { return s_aimOk; }
bool PackSwapFxOk() { return s_ownFxOk[0]; }
bool PackJetFxOk() { return s_ownFxOk[1] && s_ownFxOk[2] && s_ownFxOk[3]; }
bool PackHoldOk() { return s_holdOk; }

bool PackWeaponOk(const WeaponDef* w) { return w && w >= kWeapons && w < kWeapons + kWeaponCount && s_weaponOk[w - kWeapons]; }
bool PackAbilityOk() { return s_abilityOk; }
bool PackGrenadeOk() { return s_grenadeOk; }

const char* PackStatus() {
    static char b[200];
    int n = sprintf_s(b, "ability %d grenade %d weapons", (int)s_abilityOk, (int)s_grenadeOk);
    for (int i = 0; i < kWeaponCount && i < 16 && n < 180; i++) n += sprintf_s(b + n, sizeof b - n, " %s=%d", kWeapons[i].id, (int)s_weaponOk[i]);
    return b;
}

}  // namespace dkb
