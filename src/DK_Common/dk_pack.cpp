#include "dk_pack.h"
#include <stdio.h>
#include <share.h>
#include <string.h>

namespace dk { namespace pack {

uint32_t StriCrc32(const char* s) {
    static uint32_t tab[256];
    static bool init;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            tab[i] = c;
        }
        init = true;
    }
    uint32_t c = 0;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        unsigned ch = *p == '\\' ? '/' : *p;
        ch &= 0xDF;
        c = (c >> 8) ^ tab[(ch ^ c) & 0xFF];
    }
    return c;
}

static bool ReadFileAll(const char* path, std::vector<uint8_t>& out) {
    FILE* f = _fsopen(path, "rb", _SH_DENYNO);
    if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    bool ok = n > 0 && n < (64 << 20);
    if (ok) { out.resize(n); ok = fread(out.data(), 1, n, f) == (size_t)n; }
    fclose(f);
    return ok;
}

// DK_Mods\<Mod>\<asset path>: mods in alphabetical order, the last one wins (DK_AssetMods' rule); folders starting with
// '_' or '.' are disabled.
static bool ReadOverride(const char* gameDir, const char* assetPath, std::vector<uint8_t>& out) {
    char pat[MAX_PATH]; sprintf_s(pat, "%sDK_Mods\\*", gameDir);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    char best[MAX_PATH] = "", bestMod[MAX_PATH] = "";
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.' || fd.cFileName[0] == '_') continue;
        char p[MAX_PATH]; sprintf_s(p, "%sDK_Mods\\%s\\%s", gameDir, fd.cFileName, assetPath);
        for (char* c = p; *c; c++) if (*c == '/') *c = '\\';
        if (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES && _stricmp(fd.cFileName, bestMod) > 0) {
            strcpy_s(best, p); strcpy_s(bestMod, fd.cFileName);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return best[0] && ReadFileAll(best, out);
}

static bool ReadFromPack(const char* path, uint32_t key, std::vector<uint8_t>& out) {
    FILE* f = _fsopen(path, "rb", _SH_DENYNO);
    if (!f) return false;
    bool ok = false;
    uint32_t h[20];
    if (fread(h, 4, 20, f) == 20 && h[0] == 0x4150616B && h[1] == 5 && h[2] && h[2] < 200000) {
        uint32_t n = h[2], nt = 0x50 + h[6], rec0 = nt + h[7];
        std::vector<uint32_t> recs((size_t)n * 16);
        if (!fseek(f, rec0, SEEK_SET) && fread(recs.data(), 64, n, f) == n) {
            for (uint32_t i = 0; i < n && !ok; i++) {
                const uint32_t* r = &recs[(size_t)i * 16];
                if (r[0] != key) continue;
                uint32_t idx[4];
                if (fseek(f, h[8] + i * 16, SEEK_SET) || fread(idx, 4, 4, f) != 4) break;
                uint32_t size = r[10];
                if (!size || size > (64u << 20)) break;
                out.resize(size);
                ok = !fseek(f, h[9] + idx[1], SEEK_SET) && fread(out.data(), 1, size, f) == size;
                break;
            }
        }
    }
    fclose(f);
    return ok;
}

bool Read(const char* gameDir, const char* assetPath, const char* const* packs, int packCount, std::vector<uint8_t>& out) {
    if (ReadOverride(gameDir, assetPath, out)) return true;
    uint32_t key = StriCrc32(assetPath);
    for (int i = 0; i < packCount; i++) {
        char p[MAX_PATH]; sprintf_s(p, "%sLevelPacks\\%s", gameDir, packs[i]);
        if (ReadFromPack(p, key, out)) return true;
    }
    return false;
}

}}  // namespace dk::pack
