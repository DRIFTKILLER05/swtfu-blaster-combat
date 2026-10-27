// DK_Blaster: the shooter HUD, drawn by dkb_render in the game's own frame (PRE_UI: under the game HUD and menus).
// Imperial palette: white / ice cyan for state, amber for warnings, red for kills and danger, gold for headshots;
// every state also differs in shape, so colour is never the only cue. Sizes scale with the 3D viewport height
// (1080p = 1.0) times the user's HUD scale; everything animates in real time.
#include "dkb.h"
#include "dkb_render.h"
#include <stdio.h>

using namespace dk;

namespace dkb {

static const uint32_t kWhite = 0xFFFFFF, kIce = 0x7CE0FF, kAmber = 0xFF9A3C, kRed = 0xFF3B30, kGold = 0xFFC83C,
                      kVent = 0xFFE600,   // perfect vent: bright yellow
                      kDark = 0x0A0F14;

static float s_vis, s_visTarget;          // HUD fade
static double s_lastT;
struct Marker { double t; bool kill, head; };
static Marker s_marks[8]; static int s_markN;
struct DmgArc { double t; float yaw; };   // world yaw of the attacker
static DmgArc s_dmg[6]; static int s_dmgN;
static char s_toast[96]; static uint32_t s_toastColor; static double s_toastT; static float s_toastLen;
static volatile LONG s_onEnemy;
static int s_draws;
float g_hudAimEnemy;                      // 0..1 eased "crosshair on a living AI"
bool g_devHudOff;

void HudHit(bool kill, bool head) {
    Marker& m = s_marks[s_markN++ % 8];
    m.t = RealNow(); m.kill = kill; m.head = head;
}
void HudDamageFrom(const float src[3]) {
    float pp[3];
    if (!RenPos(PlayerRen(), pp)) return;
    DmgArc& d = s_dmg[s_dmgN++ % 6];
    d.t = RealNow(); d.yaw = atan2f(src[0] - pp[0], src[2] - pp[2]);
}
void HudToast(const char* text, uint32_t color, float seconds) {
    strncpy_s(s_toast, text, _TRUNCATE);
    s_toastColor = color; s_toastT = RealNow(); s_toastLen = seconds;
    Log("HUD: %s", text);
}
void HudSetVisible(float target) { s_visTarget = target; }
float HudAlpha() { return s_vis; }
void HudSetOnEnemy(bool on) { s_onEnemy = on ? 1 : 0; }

static float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static float EaseOut(float t) { t = Clamp01(t); return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }

// ---------------------------------------------------------------------------------------------- pieces
static void Crosshair(float cx, float cy, float S, float spreadPx, double now) {
    const float a = s_vis;
    const uint32_t tint = rnd::Lerp(cfg.crossColor | 0xFF000000u, kRed | 0xFF000000u, g_hudAimEnemy) & 0xFFFFFF;
    const float gap = spreadPx + 4.0f * S, len = 9.0f * S * cfg.crossScale, th = 2.0f * S * cfg.crossScale;
    switch (cfg.crosshair) {
    case 1:   // dot
        rnd::Disc(cx, cy, 2.6f * S * cfg.crossScale, rnd::Argb(a, tint), 1.0f);
        break;
    case 2:   // circle that breathes with the spread
        rnd::Arc(cx, cy, gap + 2.0f * S, th, 0.0f, 6.2832f, rnd::Argb(a * 0.85f, tint), 1.0f);
        rnd::Disc(cx, cy, 1.6f * S, rnd::Argb(a, tint), 1.0f);
        break;
    case 3:   // classic cross
        rnd::Line(cx - gap - len, cy, cx - gap, cy, th, rnd::Argb(a, tint));
        rnd::Line(cx + gap, cy, cx + gap + len, cy, th, rnd::Argb(a, tint));
        rnd::Line(cx, cy - gap - len, cx, cy - gap, th, rnd::Argb(a, tint));
        rnd::Line(cx, cy + gap, cx, cy + gap + len, th, rnd::Argb(a, tint));
        break;
    default: {   // Battlefront: centre pip + three arc brackets (left, right, bottom) that open with the spread
        rnd::Disc(cx, cy, 1.7f * S * cfg.crossScale, rnd::Argb(a, tint), 1.0f);
        const float r = gap + 3.0f * S;
        const float half = 0.42f;
        rnd::Arc(cx, cy, r, th, -1.5708f - half, -1.5708f + half, rnd::Argb(a * 0.95f, tint));
        rnd::Arc(cx, cy, r, th, 1.5708f - half, 1.5708f + half, rnd::Argb(a * 0.95f, tint));
        rnd::Arc(cx, cy, r, th, 3.1416f - half * 0.8f, 3.1416f + half * 0.8f, rnd::Argb(a * 0.95f, tint));
    } break;
    }
    // hit / kill / headshot markers: four diagonal ticks that pop out and fade
    for (int i = 0; i < 8; i++) {
        const Marker& m = s_marks[i];
        const float age = (float)(now - m.t);
        const float life = m.kill ? 0.55f : 0.28f;
        if (m.t <= 0 || age > life) continue;
        const float k = age / life, pop = EaseOut(age / 0.08f);
        const float al = a * (1.0f - k * k);
        const uint32_t c = m.head ? kGold : (m.kill ? kRed : kWhite);
        const float r0 = (6.0f + 5.0f * pop) * S + (m.kill ? 3.0f * S : 0.0f), r1 = r0 + (m.kill ? 11.0f : 8.0f) * S;
        const float tk = (m.kill ? 3.0f : 2.2f) * S;
        for (int q = 0; q < 4; q++) {
            const float ang = 0.7854f + q * 1.5708f, dx = sinf(ang), dy = -cosf(ang);
            rnd::Line(cx + dx * r0, cy + dy * r0, cx + dx * r1, cy + dy * r1, tk, rnd::Argb(al, c));
        }
        if (m.head) {   // headshot: a small diamond on top of the ticks (shape cue besides the gold)
            const float d = 4.0f * S * (1.0f + 0.3f * pop);
            const float pts[8] = { cx, cy - d, cx + d, cy, cx, cy + d, cx - d, cy };
            rnd::Poly(pts, 4, rnd::Argb(al, kGold));
        }
    }
}

static void HeatArc(float cx, float cy, float S, double now) {
    const Combat& c = g_cb;
    if (!c.w) return;
    const float a = s_vis;
    const float r = 34.0f * S, th = 4.0f * S;
    const float a0 = 0.55f, a1 = 2.59f;           // right side, top to bottom (clockwise from up)
    rnd::Arc(cx, cy, r, th + 2.0f * S, a0, a1, rnd::Argb(a * 0.35f, kDark));
    const float heat = Clamp01(c.heat);
    uint32_t col = heat < 0.6f ? rnd::Lerp(0xFFFFFFFF, 0xFF7CE0FF, heat / 0.6f) : rnd::Lerp(0xFF7CE0FF, 0xFFFF9A3C, (heat - 0.6f) / 0.4f);
    float fa = a;
    const float od = CombatOverdrive();
    if (od > 0.0f && cfg.overdriveTime > 0.0f) {   // overdrive: a shimmering gold arc that drains with the time left
        const float k = Clamp01(od / cfg.overdriveTime);
        const float sh = 0.75f + 0.25f * sinf((float)now * 11.0f);
        rnd::Arc(cx, cy, r, th, a1 - (a1 - a0) * k, a1, rnd::Argb(a * sh, kGold));
        rnd::Arc(cx, cy, r + 3.0f * S, 1.2f * S, a1 - (a1 - a0) * k, a1, rnd::Argb(a * 0.5f * sh, kWhite));
    }
    if (CombatSweeping()) {   // overheat lock (red) or a manual vent (cool blue): the sweep with its window
        col = c.overheated ? 0xFFFF3B30 : 0xFF7CE0FF;
        fa = c.overheated ? a * (0.65f + 0.35f * sinf((float)now * 18.0f)) : a;
        // active cooldown window + the sweeping marker
        const float w0 = a1 - (a1 - a0) * c.ventWindow1, w1 = a1 - (a1 - a0) * c.ventWindow0;
        if (!c.ventTried) rnd::Arc(cx, cy, r, th + 4.0f * S, w0, w1, rnd::Argb(a * 0.9f, kVent), 1.5f);
        const float m = a1 - (a1 - a0) * heat;
        const float mx = cx + sinf(m) * r, my = cy - cosf(m) * r;
        rnd::Disc(mx, my, 3.5f * S, rnd::Argb(a, kWhite));
    }
    if (heat > 0.001f) rnd::Arc(cx, cy, r, th, a1 - (a1 - a0) * heat, a1, rnd::Argb(fa, col & 0xFFFFFF));
    const float pv = GameNow() - c.ventPerfectAt;   // perfect vent: the whole arc flashes yellow and swells out
    if (c.ventPerfectAt > 0.0f && pv >= 0.0f && pv < 0.5f) {
        const float k = pv / 0.5f;
        rnd::Arc(cx, cy, r + 6.0f * S * k, th + 3.0f * S * (1.0f - k), a0, a1, rnd::Argb(a * (1.0f - k), kVent));
    }
    // charge ring (charge weapons): left side
    if (c.w->charge > 0.0f && c.chargeT > 0.0f) {
        const float k = Clamp01(c.chargeT / c.w->charge);
        const uint32_t cc = k >= 1.0f ? kGold : kIce;
        rnd::Arc(cx, cy, r, th, -a1, -a1 + (a1 - a0) * k, rnd::Argb(a, cc));
    }
}

static void WeaponPanel(const rnd::Ctx& x, float S) {
    const Combat& c = g_cb;
    if (!c.w) return;
    const float a = s_vis;
    const float right = x.vx + x.vw - 48.0f * S, base = x.vy + x.vh - 64.0f * S;
    // name plate: thin ice rule, weapon name, heat percent
    rnd::RectGrad(right - 300.0f * S, base - 2.0f * S, 300.0f * S, 2.0f * S, rnd::Argb(0.0f, kIce), rnd::Argb(a * 0.8f, kIce));
    rnd::Text(rnd::F_TITLE, right, base - 38.0f * S, 26.0f * S, rnd::Argb(a, kWhite), c.w->name, rnd::A_RIGHT);
    char b[48];
    uint32_t col = kIce;
    const float od = CombatOverdrive();
    if (c.overheated) { sprintf_s(b, "OVERHEATED"); col = kRed; }
    else if (c.venting) sprintf_s(b, "VENTING");
    else if (od > 0.0f) { sprintf_s(b, "OVERDRIVE %.1f", od); col = kGold; }
    else sprintf_s(b, "HEAT %d%%", (int)(c.heat * 100.0f + 0.5f));
    rnd::Text(rnd::F_BODY, right, base + 6.0f * S, 16.0f * S, rnd::Argb(a * 0.85f, col), b, rnd::A_RIGHT);
    const float cr = Clamp01(CrouchDepth() / 0.32f);   // stance: CROUCHED fades in left of the heat label
    if (cr > 0.02f) {
        const float wl = rnd::TextWidth(rnd::F_BODY, 16.0f * S, b);
        rnd::Text(rnd::F_BODY, right - wl - 20.0f * S, base + 6.0f * S, 16.0f * S, rnd::Argb(a * 0.7f * cr, kWhite), "CROUCHED", rnd::A_RIGHT);
    }
}

static void Toast(const rnd::Ctx& x, float S, double now) {
    if (s_toastT <= 0) return;
    const float age = (float)(now - s_toastT);
    if (age > s_toastLen + 0.35f) return;
    float al = age < 0.12f ? age / 0.12f : (age > s_toastLen ? 1.0f - (age - s_toastLen) / 0.35f : 1.0f);
    al = Clamp01(al);
    const float cx = x.vx + x.vw * 0.5f, y = x.vy + x.vh * 0.64f;
    const float w = rnd::TextWidth(rnd::F_BODY, 20.0f * S, s_toast) + 40.0f * S;
    rnd::RectGrad(cx - w * 0.5f, y - 6.0f * S, w, 34.0f * S, rnd::Argb(al * 0.55f, kDark), rnd::Argb(al * 0.25f, kDark));
    rnd::Rect(cx - w * 0.5f, y - 6.0f * S, 3.0f * S, 34.0f * S, rnd::Argb(al, s_toastColor));
    rnd::Text(rnd::F_BODY, cx, y, 20.0f * S, rnd::Argb(al, s_toastColor), s_toast, rnd::A_CENTER);
}

static void DamageArcs(const rnd::Ctx& x, float cx, float cy, float S, double now) {
    // camera yaw on the ground plane
    const float camYaw = atan2f(x.fwd[0], x.fwd[2]);
    for (int i = 0; i < 6; i++) {
        const DmgArc& d = s_dmg[i];
        const float age = (float)(now - d.t);
        if (d.t <= 0 || age > 1.4f) continue;
        const float al = (1.0f - age / 1.4f);
        float rel = d.yaw - camYaw;   // 0 = in front (top of the ring)
        while (rel > 3.14159f) rel -= 6.28318f;
        while (rel < -3.14159f) rel += 6.28318f;
        // screen clockwise = world right: the sign follows the camera handedness (right = fwd x up)
        const float sgn = (x.right[0] * x.fwd[2] - x.right[2] * x.fwd[0]) > 0 ? -1.0f : 1.0f;
        const float ang = rel * sgn;
        rnd::Arc(cx, cy, 120.0f * S, 7.0f * S, ang - 0.28f, ang + 0.28f, rnd::Argb(al * 0.85f, kRed), 3.0f);
    }
}

static void ScopeBreath(float cx, float cy, float R, float S);
static void Scope(const rnd::Ctx& x, float cx, float cy, float S) {
    const float a = s_vis;
    const float R = x.vh * 0.42f;
    // dark mask outside the lens: 4 rects up to the square around it, a solid ring over the square's corners
    // (R .. 1.45 R), a soft shadow inside the rim and a thin metal rim
    rnd::Rect(x.vx, x.vy, x.vw, cy - R - x.vy, rnd::Argb(a, 0x000000));
    rnd::Rect(x.vx, cy + R, x.vw, x.vy + x.vh - cy - R, rnd::Argb(a, 0x000000));
    rnd::Rect(x.vx, cy - R, cx - R - x.vx, 2 * R, rnd::Argb(a, 0x000000));
    rnd::Rect(cx + R, cy - R, x.vx + x.vw - cx - R, 2 * R, rnd::Argb(a, 0x000000));
    rnd::Arc(cx, cy, R * 1.225f, R * 0.45f, 0, 6.2832f, rnd::Argb(a, 0x000000), 2.0f);
    rnd::Arc(cx, cy, R - 14.0f * S, 28.0f * S, 0, 6.2832f, rnd::Argb(a * 0.85f, 0x000000), 34.0f * S);
    rnd::Arc(cx, cy, R + 1.0f * S, 2.5f * S, 0, 6.2832f, rnd::Argb(a * 0.6f, 0x39424B), 1.0f);
    // reticle: fine lines + ice range marks
    rnd::Line(cx - R, cy, cx - 12 * S, cy, 1.5f * S, rnd::Argb(a * 0.8f, kDark));
    rnd::Line(cx + 12 * S, cy, cx + R, cy, 1.5f * S, rnd::Argb(a * 0.8f, kDark));
    rnd::Line(cx, cy + 12 * S, cx, cy + R, 1.5f * S, rnd::Argb(a * 0.8f, kDark));
    for (int i = 1; i <= 4; i++) rnd::Line(cx - 6 * S, cy + i * 22 * S, cx + 6 * S, cy + i * 22 * S, 1.5f * S, rnd::Argb(a * 0.7f, kIce));
    rnd::Disc(cx, cy, 2.0f * S, rnd::Argb(a, kRed));
    ScopeBreath(cx, cy, R, S);
}

float g_hudScopeAmt;    // 0..1 set by the mode code while scoped
float g_hudBreath = 1.0f, g_hudHealth = -1.0f;   // scope breath left (0..1), the player's health fraction (-1 unknown)
int g_hudBreathState;   // 0 breathing, 1 holding, 2 out of breath

// low health (under 30 %): a red edge that beats faster as health drops (motion + colour, not colour alone)
static void LowHealth(const rnd::Ctx& x, float S, double now) {
    if (g_hudHealth < 0.0f || g_hudHealth >= 0.3f) return;
    const float k = 1.0f - g_hudHealth / 0.3f;
    const float rate = 1.0f + 1.4f * k;
    const float beat = 0.5f + 0.5f * sinf((float)fmod(now, 1000.0) * 6.2831853f * rate);
    const float al = s_vis * (0.22f + 0.4f * k) * (0.55f + 0.45f * beat);
    const float d = (60.0f + 60.0f * k) * S;
    rnd::RectGrad(x.vx, x.vy, x.vw, d, rnd::Argb(al, kRed), rnd::Argb(0.0f, kRed));
    rnd::RectGrad(x.vx, x.vy + x.vh - d, x.vw, d, rnd::Argb(0.0f, kRed), rnd::Argb(al, kRed));
    const int N = 12;   // the sides: stepped strips (the renderer's gradients run top to bottom)
    for (int i = 0; i < N; i++) {
        const float a = al * (1.0f - (float)i / N) * 0.5f, w = d / N;
        rnd::Rect(x.vx + i * w, x.vy, w, x.vh, rnd::Argb(a, kRed));
        rnd::Rect(x.vx + x.vw - (i + 1) * w, x.vy, w, x.vh, rnd::Argb(a, kRed));
    }
}

// scope breath: a bar under the reticle while it is not full, with the key that steadies the aim
static void ScopeBreath(float cx, float cy, float R, float S) {
    const float a = s_vis;
    const float y = cy + R * 0.62f, w = 120.0f * S;
    const uint32_t col = g_hudBreathState == 2 ? kAmber : kIce;
    if (g_hudBreath < 0.999f || g_hudBreathState) {
        rnd::Rect(cx - w * 0.5f, y, w, 3.0f * S, rnd::Argb(a * 0.3f, col));
        rnd::Rect(cx - w * 0.5f, y, w * g_hudBreath, 3.0f * S, rnd::Argb(a * 0.9f, col));
    }
    char b[48];
    sprintf_s(b, g_hudBreathState == 2 ? "OUT OF BREATH" : "%s  STEADY AIM", g_in.pad ? "L3" : VkName(cfg.kSprint));
    rnd::Text(rnd::F_SMALL, cx, y + 10.0f * S, 14.0f * S, rnd::Argb(a * (g_hudBreathState == 2 ? 0.95f : 0.8f), col), b, rnd::A_CENTER);
}

static void Draw(rnd::Layer layer, const rnd::Ctx& x) {
    if (layer != rnd::PRE_UI) return;
    const double now = x.t;
    float dt = (float)(now - s_lastT); s_lastT = now;
    if (dt < 0 || dt > 0.1f) dt = 0.1f;
    const bool show = s_visTarget > 0.0f && GameState() == 2 && !CinematicNow() && !OutfitHidesHud() && !g_devHudOff;
    const float tgt = show ? s_visTarget : 0.0f;
    s_vis = tgt + (s_vis - tgt) * expf(-12.0f * dt);
    g_hudAimEnemy = (s_onEnemy ? 1.0f : 0.0f) + (g_hudAimEnemy - (s_onEnemy ? 1.0f : 0.0f)) * expf(-16.0f * dt);
    if (s_vis < 0.01f) return;
    s_draws++;
    const float S = x.scale;
    const float cx = x.vx + x.vw * 0.5f, cy = x.vy + x.vh * 0.5f;
    LowHealth(x, S, now);
    if (g_hudScopeAmt > 0.5f) Scope(x, cx, cy, S);
    // spread cone -> pixels with the drawn camera's vertical FOV
    float fov = g_cam.fov > 1.0f ? g_cam.fov : 43.0f;
    const float spreadPx = tanf(CombatSpread() * 0.0174533f) / tanf(fov * 0.5f * 0.0174533f) * x.vh * 0.5f;
    if (g_hudScopeAmt <= 0.5f) Crosshair(cx, cy, S, spreadPx, now);
    HeatArc(cx, cy, S, now);
    DamageArcs(x, cx, cy, S, now);
    WeaponPanel(x, S);
    OutfitDrawHud(x, S, s_vis);
    AbilityDraw(x, S, s_vis);
    Toast(x, S, now);
}

// toasts must show in saber mode too (refusals): the HUD draws whenever a toast is live
static void DrawAll(rnd::Layer layer, const rnd::Ctx& x) {
    const double p0 = PerfNow();
    if (layer == rnd::POST_UI) { OutfitDrawPost(x); PerfAdd(1, PerfNow() - p0); return; }   // class wheel / terminal / swap flash: over the game's UI
    if (layer != rnd::PRE_UI) return;
    if (s_visTarget > 0.0f || s_vis > 0.01f) { Draw(layer, x); PerfAdd(1, PerfNow() - p0); return; }
    s_vis = 0.0f;   // faded out: Draw (which eases it) is no longer called
    const double now = x.t;
    s_lastT = now;
    if (s_toastT > 0 && now - s_toastT < s_toastLen + 0.35f) Toast(x, x.scale, now);
}

void HudInstall() {
    rnd::SetUserScale(cfg.hudScale);
    rnd::SetDrawCallback(&DrawAll);
}
void HudShutdown() { rnd::SetDrawCallback(nullptr); }
const char* HudStatus() {
    static char b[320];
    sprintf_s(b, "vis %.2f draws %d pre %d post %d fonts %d toast '%s' %.1fs | %s", s_vis, s_draws, rnd::DrawCount(rnd::PRE_UI),
              rnd::DrawCount(rnd::POST_UI), rnd::FontsLoaded() ? 1 : 0, s_toast, s_toastT > 0 ? (float)(RealNow() - s_toastT) : -1.0f, rnd::Status());
    return b;
}

}  // namespace dkb
