// DK_Blaster: native 2D overlay inside the game's final render pass (D3D9).
// Two layers: PRE_UI is drawn just before the game's Anark UI bucket (under the game HUD and pause menu: crosshair, heat,
// markers, scope), POST_UI just before the frame's EndScene (over everything: radial wheel, class select). Both run once
// per drawn frame (DK_FPSFix redraws included) on the game thread, with the interpolated camera of that frame.
#pragma once
#include <windows.h>
#include <stdint.h>
#include "dk_util.h"

namespace rnd {

enum Layer { PRE_UI = 0, POST_UI = 1 };
enum Font { F_SMALL = 0, F_BODY, F_TITLE, F_KORATAKI, F_COUNT };   // Ext Two 16, Ext Two Bold 28, Korataki 28 ...
enum Align { A_LEFT = 0, A_CENTER = 1, A_RIGHT = 2 };

// Frame context, valid inside the draw callback.
struct Ctx {
    float bbW, bbH;            // back buffer
    float vx, vy, vw, vh;      // 3D viewport (the game's view 0)
    float vp[16];              // view-projection of the drawn camera (row vectors)
    float eye[3], right[3], up[3], fwd[3];
    float aspect;              // 3D aspect (proj[1][1] / proj[0][0])
    double t;                  // seconds (QPC), monotonic
    float scale;               // HUD scale: viewport height / 1080 * user scale
};

typedef void(__cdecl* DrawFn)(Layer layer, const Ctx& c);

bool Install(const dk::Range& text, const char* gameDir);   // hooks; false = HUD off (logged)
void SetDrawCallback(DrawFn fn);
void Shutdown();               // release D3D objects (patches are undone by dk::RestorePatches)
bool Ready();                  // hooks in place
int DrawCount(Layer l);        // frames drawn per layer (diagnostics)
const char* Status();          // one-line state for logs / dev commands
void SetUserScale(float s);
void SetViewSim(float aspect);   // dev: lay out for this aspect ratio, letterboxed (0 = off)
float ViewSim();
bool FontsLoaded();

// ---- drawing (inside the callback only). Colors are 0xAARRGGBB; the global alpha multiplies every color.
void SetGlobalAlpha(float a);
void Rect(float x, float y, float w, float h, uint32_t color);
void RectGrad(float x, float y, float w, float h, uint32_t top, uint32_t bottom);
// Anti-aliased line of the given thickness (feather = soft edge width in px).
void Line(float x0, float y0, float x1, float y1, float thick, uint32_t color, float feather = 1.0f);
// Arc band from a0 to a1 (radians, 0 = up, clockwise), radius r = centre of the band.
void Arc(float cx, float cy, float r, float thick, float a0, float a1, uint32_t color, float feather = 1.0f);
void Disc(float cx, float cy, float r, uint32_t color, float feather = 1.0f);
void Glow(float cx, float cy, float r, uint32_t color);   // soft radial glow (alpha falls to 0 at r)
// Convex polygon (screen points, n <= 32), soft edge.
void Poly(const float* xy, int n, uint32_t color, float feather = 1.0f);
// Text in one of the game's fonts at a pixel height; returns the width. Shadow = soft dark copy behind.
float Text(Font f, float x, float y, float px, uint32_t color, const char* s, Align a = A_LEFT, bool shadow = true);
float TextWidth(Font f, float px, const char* s);
void Flush();

// World point -> screen (false when behind the camera).
bool Project(const Ctx& c, const float p[3], float* sx, float* sy);

inline uint32_t Argb(float a, uint32_t rgb) {
    int ia = (int)(a * 255.0f + 0.5f); if (ia < 0) ia = 0; if (ia > 255) ia = 255;
    return ((uint32_t)ia << 24) | (rgb & 0xFFFFFF);
}
uint32_t Lerp(uint32_t a, uint32_t b, float t);

}  // namespace rnd
