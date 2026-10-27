// DK_Blaster native overlay. See dkb_render.h. Research: research\agent_blaster\hud\REPORT.md.
//
// Hook sites (symbol DB): Blaster_UiBucketCall (final pass: call that draws the Anark UI bucket) and
// Blaster_EndSceneCall (the frame's EndScene wrapper call) are redirected to draw, then call the original;
// Blaster_OnLostDeviceCall releases our state block before the device reset. The engine caches render states, so every
// state we touch is restored exactly through a recorded state block, plus stream 0 / FVF / declaration / shaders by hand.
#include "dkb_render.h"
#include "dk_pack.h"
#include <d3d9.h>
#include <math.h>
#include <stdio.h>
#include <vector>

using dk::Log;

namespace rnd {

typedef int(__thiscall* UiBucket_t)(void* self, void* view, void* list, int count, int zero);
typedef int(__thiscall* EndScene_t)(void* wrapper);
typedef int(__thiscall* OnLost_t)(void* wrapper);
static UiBucket_t o_ui;
static EndScene_t o_end;
static OnLost_t o_lost;
static DrawFn s_draw;
static bool s_ready, s_failed;
static int s_draws[2];
static char s_status[160] = "not installed";
static float s_userScale = 1.0f;
static char s_gameDir[MAX_PATH];

struct V { float x, y, z, rhw; DWORD c; float u, v; };
static const DWORD kFVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
static V s_v[24000];
static int s_nv;
static IDirect3DDevice9* s_dev;
static IDirect3DTexture9* s_cur;
static IDirect3DTexture9* s_white;
static IDirect3DStateBlock9* s_sb;
static float s_alpha = 1.0f;

// ---------------------------------------------------------------------------------------------- fonts
struct FontData {
    const char* path;          // asset path without extension
    IDirect3DTexture9* tex;
    std::vector<uint8_t> a2f;
    float texW, texH, lineH;
    uint32_t nChars, rec0;
    bool tried, ok;
};
static FontData s_fonts[F_COUNT] = {
    { "Scum/Assets/Anark/fonts/Eurostile_LT_Std_Ext_Two16" },
    { "Shared/Assets/Anark/fonts/PCDX/Eurostile_LT_Std_Bold16" },
    { "Scum/Assets/Anark/fonts/Eurostile_LT_Std_Ext_Two_Bold28" },
    { "Scum/Assets/Anark/fonts/Korataki28" },
};
static const char* const kFontPacks[] = { "sw_frontend.xni_pc.lp", "osd_customize.xni_pc.lp", "sw_wrapup.xni_pc.lp" };

// DDS (DXT1/3/5 or A8R8G8B8, mip 0 only) -> managed texture
static IDirect3DTexture9* TextureFromDds(const std::vector<uint8_t>& d) {
    if (d.size() < 128 || memcmp(d.data(), "DDS ", 4)) return nullptr;
    uint32_t h = dk::Rd<uint32_t>(&d[12]), w = dk::Rd<uint32_t>(&d[16]);
    uint32_t pfFlags = dk::Rd<uint32_t>(&d[80]), four = dk::Rd<uint32_t>(&d[84]), bits = dk::Rd<uint32_t>(&d[88]);
    D3DFORMAT fmt; uint32_t blockBytes = 0, rowBytes, rows;
    if (pfFlags & 4) {   // DDPF_FOURCC
        if (four == MAKEFOURCC('D', 'X', 'T', '1')) { fmt = D3DFMT_DXT1; blockBytes = 8; }
        else if (four == MAKEFOURCC('D', 'X', 'T', '3')) { fmt = D3DFMT_DXT3; blockBytes = 16; }
        else if (four == MAKEFOURCC('D', 'X', 'T', '5')) { fmt = D3DFMT_DXT5; blockBytes = 16; }
        else return nullptr;
        rowBytes = ((w + 3) / 4) * blockBytes; rows = (h + 3) / 4;
    } else if (bits == 32) { fmt = D3DFMT_A8R8G8B8; rowBytes = w * 4; rows = h; }
    else return nullptr;
    if (!w || !h || w > 4096 || h > 4096 || 128 + (size_t)rowBytes * rows > d.size()) return nullptr;
    IDirect3DTexture9* t = nullptr;
    if (FAILED(s_dev->CreateTexture(w, h, 1, 0, fmt, D3DPOOL_MANAGED, &t, nullptr)) || !t) return nullptr;
    D3DLOCKED_RECT lr;
    if (FAILED(t->LockRect(0, &lr, nullptr, 0))) { t->Release(); return nullptr; }
    for (uint32_t y = 0; y < rows; y++) memcpy((uint8_t*)lr.pBits + (size_t)y * lr.Pitch, &d[128 + (size_t)y * rowBytes], rowBytes);
    t->UnlockRect(0);
    return t;
}

static void LoadFont(FontData& f) {
    f.tried = true;
    char p[160];
    std::vector<uint8_t> dds;
    sprintf_s(p, "%s.a2f", f.path);
    bool a = dk::pack::Read(s_gameDir, p, kFontPacks, 3, f.a2f);
    sprintf_s(p, "%s.dds", f.path);
    bool b = a && dk::pack::Read(s_gameDir, p, kFontPacks, 3, dds);
    if (!a || !b || f.a2f.size() < 0x44) { Log("hud: font %s not found in the packs", f.path); return; }
    f.texW = dk::Rd<float>(&f.a2f[0x24]); f.texH = dk::Rd<float>(&f.a2f[0x28]);
    f.lineH = (float)dk::Rd<uint32_t>(&f.a2f[0x30]);
    f.nChars = dk::Rd<uint32_t>(&f.a2f[0x34]);
    f.rec0 = 0x10 + dk::Rd<uint32_t>(&f.a2f[0x40]);
    if (f.nChars < 128 || f.nChars > 0x10000 || 0x44 + f.nChars * 2 > f.a2f.size() || f.rec0 >= f.a2f.size() ||
        f.texW < 16 || f.lineH < 4) { Log("hud: font %s has an unexpected layout", f.path); return; }
    f.tex = TextureFromDds(dds);
    f.ok = f.tex != nullptr;
    Log("hud: font %s %s (%.0fx%.0f, line %.0f)", f.path, f.ok ? "loaded" : "texture FAILED", f.texW, f.texH, f.lineH);
}

bool FontsLoaded() { for (auto& f : s_fonts) if (!f.ok) return false; return true; }

// ---------------------------------------------------------------------------------------------- batching
void Flush() {
    if (!s_nv || !s_dev) { s_nv = 0; return; }
    s_dev->SetTexture(0, s_cur ? s_cur : s_white);
    s_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, s_nv / 3, s_v, sizeof(V));
    s_nv = 0;
}
static void UseTex(IDirect3DTexture9* t) { if (t != s_cur) { Flush(); s_cur = t; } }
static inline DWORD Mul(uint32_t c) {
    if (s_alpha >= 0.999f) return c;
    uint32_t a = (uint32_t)((c >> 24) * s_alpha + 0.5f);
    return (a << 24) | (c & 0xFFFFFF);
}
static inline V Vx(float x, float y, uint32_t c, float u = 0.5f, float v = 0.5f) { return V{ x, y, 0.0f, 1.0f, Mul(c), u, v }; }
static inline void Tri(const V& a, const V& b, const V& c) {
    if (s_nv + 3 > (int)(sizeof(s_v) / sizeof(s_v[0]))) Flush();
    s_v[s_nv++] = a; s_v[s_nv++] = b; s_v[s_nv++] = c;
}
static inline void Quad(const V& a, const V& b, const V& c, const V& d) { Tri(a, b, c); Tri(a, c, d); }   // a b c d around
static inline uint32_t Clear(uint32_t c) { return c & 0xFFFFFF; }

void SetGlobalAlpha(float a) { s_alpha = a < 0 ? 0 : (a > 1 ? 1 : a); }
uint32_t Lerp(uint32_t a, uint32_t b, float t) {
    if (t <= 0) return a;
    if (t >= 1) return b;
    uint32_t r = 0;
    for (int s = 0; s < 32; s += 8) {
        int x = (a >> s) & 255, y = (b >> s) & 255;
        r |= (uint32_t)(x + (y - x) * t + 0.5f) << s;
    }
    return r;
}

void Rect(float x, float y, float w, float h, uint32_t c) {
    UseTex(s_white);
    Quad(Vx(x, y, c), Vx(x + w, y, c), Vx(x + w, y + h, c), Vx(x, y + h, c));
}
void RectGrad(float x, float y, float w, float h, uint32_t top, uint32_t bottom) {
    UseTex(s_white);
    Quad(Vx(x, y, top), Vx(x + w, y, top), Vx(x + w, y + h, bottom), Vx(x, y + h, bottom));
}

void Line(float x0, float y0, float x1, float y1, float thick, uint32_t c, float f) {
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len < 0.01f) return;
    dx /= len; dy /= len;
    float nx = -dy, ny = dx, h = thick * 0.5f;
    x0 -= dx * f * 0.5f; y0 -= dy * f * 0.5f; x1 += dx * f * 0.5f; y1 += dy * f * 0.5f;
    UseTex(s_white);
    const float off[4] = { -h - f, -h, h, h + f };
    const uint32_t col[4] = { Clear(c), c, c, Clear(c) };
    for (int i = 0; i < 3; i++)
        Quad(Vx(x0 + nx * off[i], y0 + ny * off[i], col[i]), Vx(x1 + nx * off[i], y1 + ny * off[i], col[i]),
             Vx(x1 + nx * off[i + 1], y1 + ny * off[i + 1], col[i + 1]), Vx(x0 + nx * off[i + 1], y0 + ny * off[i + 1], col[i + 1]));
}

void Arc(float cx, float cy, float r, float thick, float a0, float a1, uint32_t c, float f) {
    if (a1 < a0) { float t = a0; a0 = a1; a1 = t; }
    float span = a1 - a0;
    if (span <= 0.0001f) return;
    int n = (int)(span * (r + thick) / 3.0f) + 2;
    if (n > 256) n = 256;
    float h = thick * 0.5f;
    const float rad[4] = { r - h - f, r - h, r + h, r + h + f };
    const uint32_t col[4] = { Clear(c), c, c, Clear(c) };
    UseTex(s_white);
    float pa = a0, ps = sinf(pa), pc = cosf(pa);
    for (int i = 1; i <= n; i++) {
        float a = a0 + span * i / n, s = sinf(a), co = cosf(a);
        for (int k = 0; k < 3; k++) {
            float r0 = rad[k] < 0 ? 0 : rad[k], r1 = rad[k + 1] < 0 ? 0 : rad[k + 1];
            Quad(Vx(cx + ps * r0, cy - pc * r0, col[k]), Vx(cx + s * r0, cy - co * r0, col[k]),
                 Vx(cx + s * r1, cy - co * r1, col[k + 1]), Vx(cx + ps * r1, cy - pc * r1, col[k + 1]));
        }
        pa = a; ps = s; pc = co;
    }
}

void Disc(float cx, float cy, float r, uint32_t c, float f) {
    int n = (int)(r * 0.8f) + 12; if (n > 96) n = 96;
    UseTex(s_white);
    for (int i = 0; i < n; i++) {
        float a0 = 6.2831853f * i / n, a1 = 6.2831853f * (i + 1) / n;
        float s0 = sinf(a0), c0 = cosf(a0), s1 = sinf(a1), c1 = cosf(a1);
        Tri(Vx(cx, cy, c), Vx(cx + s0 * r, cy - c0 * r, c), Vx(cx + s1 * r, cy - c1 * r, c));
        Quad(Vx(cx + s0 * r, cy - c0 * r, c), Vx(cx + s1 * r, cy - c1 * r, c),
             Vx(cx + s1 * (r + f), cy - c1 * (r + f), Clear(c)), Vx(cx + s0 * (r + f), cy - c0 * (r + f), Clear(c)));
    }
}

void Glow(float cx, float cy, float r, uint32_t c) {
    const int n = 40;
    UseTex(s_white);
    uint32_t mid = (((uint32_t)((c >> 24) * 0.35f)) << 24) | (c & 0xFFFFFF);
    for (int i = 0; i < n; i++) {
        float a0 = 6.2831853f * i / n, a1 = 6.2831853f * (i + 1) / n;
        float s0 = sinf(a0), c0 = cosf(a0), s1 = sinf(a1), c1 = cosf(a1), rm = r * 0.45f;
        Tri(Vx(cx, cy, c), Vx(cx + s0 * rm, cy - c0 * rm, mid), Vx(cx + s1 * rm, cy - c1 * rm, mid));
        Quad(Vx(cx + s0 * rm, cy - c0 * rm, mid), Vx(cx + s1 * rm, cy - c1 * rm, mid),
             Vx(cx + s1 * r, cy - c1 * r, Clear(c)), Vx(cx + s0 * r, cy - c0 * r, Clear(c)));
    }
}

void Poly(const float* xy, int n, uint32_t c, float f) {
    if (n < 3 || n > 32) return;
    float mx = 0, my = 0;
    for (int i = 0; i < n; i++) { mx += xy[i * 2]; my += xy[i * 2 + 1]; }
    mx /= n; my /= n;
    UseTex(s_white);
    float ox[32], oy[32];
    for (int i = 0; i < n; i++) {
        float dx = xy[i * 2] - mx, dy = xy[i * 2 + 1] - my, l = sqrtf(dx * dx + dy * dy);
        if (l < 0.001f) l = 1;
        ox[i] = xy[i * 2] + dx / l * f; oy[i] = xy[i * 2 + 1] + dy / l * f;
    }
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        Tri(Vx(mx, my, c), Vx(xy[i * 2], xy[i * 2 + 1], c), Vx(xy[j * 2], xy[j * 2 + 1], c));
        if (f > 0) Quad(Vx(xy[i * 2], xy[i * 2 + 1], c), Vx(xy[j * 2], xy[j * 2 + 1], c), Vx(ox[j], oy[j], Clear(c)), Vx(ox[i], oy[i], Clear(c)));
    }
}

// Glyph box in texels for character ch (0 = none).
static bool Glyph(const FontData& f, unsigned ch, float* u0, float* v0, float* u1, float* v1) {
    if (ch >= f.nChars) ch = '?';
    uint16_t g = dk::Rd<uint16_t>(&f.a2f[0x44 + ch * 2]);
    if (!g) { if (ch == '?') return false; return Glyph(f, '?', u0, v0, u1, v1); }
    size_t o = f.rec0 + (size_t)g * 32;
    if (o + 32 > f.a2f.size()) return false;
    float r[8]; memcpy(r, &f.a2f[o], 32);
    *u0 = fminf(fminf(r[0], r[2]), fminf(r[4], r[6])); *u1 = fmaxf(fmaxf(r[0], r[2]), fmaxf(r[4], r[6]));
    *v0 = fminf(fminf(r[1], r[3]), fminf(r[5], r[7])); *v1 = fmaxf(fmaxf(r[1], r[3]), fmaxf(r[5], r[7]));
    return true;
}

float TextWidth(Font fi, float px, const char* s) {
    const FontData& f = s_fonts[fi];
    if (!f.ok || !s) return 0;
    float k = px / f.lineH, w = 0;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        float u0, v0, u1, v1;
        if (Glyph(f, *p, &u0, &v0, &u1, &v1)) w += (u1 - u0) * f.texW * k;
    }
    return w;
}

static void TextRun(const FontData& f, float x, float y, float k, uint32_t c, const char* s) {
    UseTex(f.tex);
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        float u0, v0, u1, v1;
        if (!Glyph(f, *p, &u0, &v0, &u1, &v1)) continue;
        float w = (u1 - u0) * f.texW * k, h = (v1 - v0) * f.texH * k;
        float X = floorf(x + 0.5f) - 0.5f, Y = floorf(y + 0.5f) - 0.5f;
        Quad(Vx(X, Y, c, u0, v0), Vx(X + w, Y, c, u1, v0), Vx(X + w, Y + h, c, u1, v1), Vx(X, Y + h, c, u0, v1));
        x += w;
    }
}

float Text(Font fi, float x, float y, float px, uint32_t c, const char* s, Align a, bool shadow) {
    const FontData& f = s_fonts[fi];
    if (!f.ok || !s || !*s) return 0;
    float w = TextWidth(fi, px, s), k = px / f.lineH;
    if (a == A_CENTER) x -= w * 0.5f; else if (a == A_RIGHT) x -= w;
    if (shadow) {
        uint32_t sh = ((uint32_t)((c >> 24) * 0.55f) << 24) | 0x000000;
        float o = px > 20 ? 2.0f : 1.0f;
        TextRun(f, x + o, y + o, k, sh, s);
    }
    TextRun(f, x, y, k, c, s);
    return w;
}

bool Project(const Ctx& c, const float p[3], float* sx, float* sy) {
    const float* m = c.vp;
    float x = p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12];
    float y = p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13];
    float w = p[0] * m[3] + p[1] * m[7] + p[2] * m[11] + m[15];
    if (w <= 0.01f) return false;
    *sx = c.vx + (x / w * 0.5f + 0.5f) * c.vw;
    *sy = c.vy + (0.5f - y / w * 0.5f) * c.vh;
    return true;
}

// ---------------------------------------------------------------------------------------------- frame
static IDirect3DDevice9* DeviceIfDrawable(void** wrapperOut) {
    void* w = nullptr;
    if (!dk::SafeRead((void*)dk::sym::gD3DDevice, &w, 4) || !w) return nullptr;
    uint8_t lost = 1, inScene = 0; IDirect3DDevice9* d = nullptr;
    if (!dk::SafeRead((uint8_t*)w + 0x1A4, &lost, 1) || lost) return nullptr;
    if (!dk::SafeRead((uint8_t*)w + 0x217D, &inScene, 1) || !inScene) return nullptr;
    if (!dk::SafeRead((uint8_t*)w + 0x2C, &d, 4) || !d) return nullptr;
    if (wrapperOut) *wrapperOut = w;
    return d;
}

static void SetOurStates(IDirect3DDevice9* d, float bw, float bh) {
    D3DVIEWPORT9 vp = { 0, 0, (DWORD)bw, (DWORD)bh, 0.0f, 1.0f };
    d->SetViewport(&vp);
    d->SetVertexShader(nullptr); d->SetPixelShader(nullptr);
    d->SetFVF(kFVF);
    d->SetRenderState(D3DRS_ZENABLE, FALSE); d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE); d->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA); d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE); d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_FOGENABLE, FALSE); d->SetRenderState(D3DRS_LIGHTING, FALSE);
    d->SetRenderState(D3DRS_STENCILENABLE, FALSE); d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF); d->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID); d->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    d->SetRenderState(D3DRS_CLIPPING, TRUE); d->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, FALSE);
    d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE); d->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    d->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE); d->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    d->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    d->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    d->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE); d->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR); d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    d->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    d->SetTexture(0, nullptr);
    d->SetTexture(1, nullptr);
}

static bool EnsureResources(IDirect3DDevice9* d) {
    if (!s_white) {
        if (FAILED(d->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &s_white, nullptr)) || !s_white) return false;
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(s_white->LockRect(0, &lr, nullptr, 0))) { *(DWORD*)lr.pBits = 0xFFFFFFFF; s_white->UnlockRect(0); }
    }
    if (!s_sb) {
        if (FAILED(d->BeginStateBlock())) return false;
        SetOurStates(d, 1, 1);
        d->SetStreamSource(0, nullptr, 0, 0);
        d->SetIndices(nullptr);
        if (FAILED(d->EndStateBlock(&s_sb)) || !s_sb) { s_sb = nullptr; return false; }
    }
    for (auto& f : s_fonts) if (!f.tried) LoadFont(f);
    return true;
}

static float s_simAspect, s_simOrig[4];   // dev view simulation (0 = off)
static bool FillCtx(Ctx& c, void* wrapper) {
    uint32_t bw = 0, bh = 0;
    dk::SafeRead((uint8_t*)wrapper + 0x1ED8, &bw, 4); dk::SafeRead((uint8_t*)wrapper + 0x1EDC, &bh, 4);
    if (bw < 320 || bh < 200 || bw > 16384 || bh > 16384) return false;
    c.bbW = (float)bw; c.bbH = (float)bh;
    c.vx = 0; c.vy = 0; c.vw = c.bbW; c.vh = c.bbH;
    void* renderer = nullptr; void* view = nullptr;
    bool cam = false;
    if (dk::SafeRead((void*)0x3864468, &renderer, 4) && renderer && dk::SafeRead((uint8_t*)renderer + 0xC40, &view, 4) && view) {
        float rect[4];
        if (dk::SafeRead((uint8_t*)view + 0x240, rect, 16) && rect[2] > 100 && rect[3] > 100) { c.vx = rect[0]; c.vy = rect[1]; c.vw = rect[2]; c.vh = rect[3]; }
        float blk[0x220 / 4];
        if (dk::SafeRead((uint8_t*)view + 0x20, blk, sizeof(blk))) {
            memcpy(c.vp, &blk[0x1A0 / 4], 64);
            const float* w = &blk[0x40 / 4];   // camera world: right, up, back (RH), position
            for (int i = 0; i < 3; i++) { c.right[i] = w[i]; c.up[i] = w[4 + i]; c.fwd[i] = -w[8 + i]; c.eye[i] = w[12 + i]; }
            const float* pr = &blk[0x80 / 4];
            c.aspect = pr[0] > 0.0001f ? pr[5] / pr[0] : c.vw / c.vh;
            cam = true;
        }
    }
    if (!cam) { memset(c.vp, 0, 64); c.aspect = c.vw / c.vh; }
    static LARGE_INTEGER f0, q0;
    if (!f0.QuadPart) { QueryPerformanceFrequency(&f0); QueryPerformanceCounter(&q0); }
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    c.t = (double)(q.QuadPart - q0.QuadPart) / (double)f0.QuadPart;
    s_simOrig[0] = c.vx; s_simOrig[1] = c.vy; s_simOrig[2] = c.vw; s_simOrig[3] = c.vh;
    if (s_simAspect > 0.1f) {   // dev: lay the overlay out for another aspect ratio, letterboxed inside this window
        const float cur = c.vw / c.vh;
        if (s_simAspect > cur) { const float h = c.vw / s_simAspect; c.vy += (c.vh - h) * 0.5f; c.vh = h; }
        else { const float w = c.vh * s_simAspect; c.vx += (c.vw - w) * 0.5f; c.vw = w; }
    }
    // 1080p = 1; below 1080 lines the layout stops shrinking at 0.85 so small text stays readable (720p: 12 px
    // labels instead of 8). The dev aspect simulation stays an exact scaled-down preview.
    const float fit = c.vh / 1080.0f;
    c.scale = (s_simAspect > 0.1f ? fit : (fit < 0.85f ? 0.85f : fit)) * s_userScale;
    return true;
}

static int s_faults;
static void DrawLayerSafe(Layer layer) {
    void* w = nullptr;
    IDirect3DDevice9* d = DeviceIfDrawable(&w);
    if (!d) return;
    Ctx c;
    if (!FillCtx(c, w)) return;
    s_dev = d;
    if (!EnsureResources(d)) { s_dev = nullptr; return; }
    // save what the state block does not cover reliably
    IDirect3DVertexBuffer9* vb = nullptr; UINT off = 0, stride = 0;
    IDirect3DVertexDeclaration9* decl = nullptr; IDirect3DIndexBuffer9* ib = nullptr;
    IDirect3DVertexShader9* vs = nullptr; IDirect3DPixelShader9* ps = nullptr;
    IDirect3DBaseTexture9* t0 = nullptr; IDirect3DBaseTexture9* t1 = nullptr;
    DWORD fvf = 0;
    d->GetStreamSource(0, &vb, &off, &stride); d->GetVertexDeclaration(&decl); d->GetFVF(&fvf); d->GetIndices(&ib);
    d->GetVertexShader(&vs); d->GetPixelShader(&ps); d->GetTexture(0, &t0); d->GetTexture(1, &t1);
    s_sb->Capture();
    SetOurStates(d, c.bbW, c.bbH);
    s_nv = 0; s_cur = s_white; s_alpha = 1.0f;
    s_draw(layer, c);
    if (layer == POST_UI && s_simAspect > 0.1f) {   // the simulated screen's bars
        const float* o = s_simOrig;
        const uint32_t k = 0xFF000000;
        Rect(o[0], o[1], o[2], c.vy - o[1], k); Rect(o[0], c.vy + c.vh, o[2], o[1] + o[3] - c.vy - c.vh, k);
        Rect(o[0], o[1], c.vx - o[0], o[3], k); Rect(c.vx + c.vw, o[1], o[0] + o[2] - c.vx - c.vw, o[3], k);
    }
    Flush();
    s_sb->Apply();
    d->SetStreamSource(0, vb, off, stride);
    if (decl) d->SetVertexDeclaration(decl); else d->SetFVF(fvf);
    d->SetIndices(ib); d->SetVertexShader(vs); d->SetPixelShader(ps); d->SetTexture(0, t0); d->SetTexture(1, t1);
    if (vb) vb->Release();
    if (decl) decl->Release();
    if (ib) ib->Release();
    if (vs) vs->Release();
    if (ps) ps->Release();
    if (t0) t0->Release();
    if (t1) t1->Release();
    s_draws[layer]++;
    s_dev = nullptr;
}

static void DrawLayer(Layer layer) {
    if (!s_draw || s_failed) return;
    __try { DrawLayerSafe(layer); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        s_nv = 0;
        if (++s_faults >= 3) { s_failed = true; sprintf_s(s_status, "HUD stopped after 3 faults (last %08X)", GetExceptionCode()); }
        Log("hud: exception %08X while drawing layer %d (%d)", GetExceptionCode(), (int)layer, s_faults);
    }
}

static int __fastcall HkUiBucket(void* self, void*, void* view, void* list, int count, int zero) {
    DrawLayer(PRE_UI);
    return o_ui(self, view, list, count, zero);
}
static int __fastcall HkEndScene(void* self, void*) {
    DrawLayer(POST_UI);
    return o_end(self);
}
static int __fastcall HkOnLost(void* self, void*) {
    if (s_sb) { s_sb->Release(); s_sb = nullptr; }   // a state block must not survive a device reset
    return o_lost(self);
}

bool Install(const dk::Range& text, const char* gameDir) {
    strcpy_s(s_gameDir, gameDir);
    uint8_t* ui = dk::FindSym(text, "Blaster_UiBucketCall");
    uint8_t* es = dk::FindSym(text, "Blaster_EndSceneCall");
    uint8_t* lo = dk::FindSym(text, "Blaster_OnLostDeviceCall");
    if (!ui || !es || !lo || ui[0] != 0xE8 || es[0] != 0xE8 || lo[0] != 0xE8) {
        sprintf_s(s_status, "HUD off: render hook signatures missing");
        Log("hud: %s", s_status);
        return false;
    }
    o_lost = (OnLost_t)dk::RedirectCall(lo, (void*)HkOnLost);
    o_ui = (UiBucket_t)dk::RedirectCall(ui, (void*)HkUiBucket);
    o_end = (EndScene_t)dk::RedirectCall(es, (void*)HkEndScene);
    s_ready = o_ui && o_end && o_lost;
    sprintf_s(s_status, s_ready ? "HUD hooks in place" : "HUD off: redirect failed");
    Log("hud: %s (pre-UI %p, post-UI %p, lost %p)", s_status, ui, es, lo);
    return s_ready;
}

void SetDrawCallback(DrawFn fn) { s_draw = fn; }
bool Ready() { return s_ready && !s_failed; }
int DrawCount(Layer l) { return s_draws[l]; }
const char* Status() { return s_status; }
void SetUserScale(float s) { s_userScale = s < 0.5f ? 0.5f : (s > 2.0f ? 2.0f : s); }
void SetViewSim(float aspect) { s_simAspect = aspect; }
float ViewSim() { return s_simAspect; }

void Shutdown() {
    s_draw = nullptr;
    __try {
        if (s_sb) { s_sb->Release(); s_sb = nullptr; }
        if (s_white) { s_white->Release(); s_white = nullptr; }
        for (auto& f : s_fonts) if (f.tex) { f.tex->Release(); f.tex = nullptr; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

}  // namespace rnd
