// DK_Blaster: aim camera. CS::Update ends with builder(CS) = LookAt(eye CS+0x4850, target CS+0x4860, up CS+0x4870)
// with FOV CS+0x494C; our hook replaces eye / target / FOV for that one call and puts the fields back, so the game's
// follow camera keeps running underneath and never sees our view. While armed the rig owns yaw / pitch (right stick or
// raw mouse counts, read in the same frame as the view build: no added latency), orbits a shoulder point beside the
// player's head, keeps the boom out of walls (static-world rays) and blends from / to the game's own view. On exit
// the game's follow camera is turned to where we aimed. Everything eases in real time (identical at any frame rate).
#include "dkb.h"

using namespace dk;

namespace dkb {

CamOut g_cam;
int g_keepView = 1;                     // dev: leave our eye / target in the camera system after the build

typedef void(__thiscall* Build_t)(BYTE* cs);
static Build_t s_build;
static uint8_t* s_site;

static const int kStrafeMode = 21;      // the game's "Strafe" follow mode (CS+0x4884 while strafe locomotion runs)
static const float kPivotUp = 1.30f;    // pivot height above the player's origin (the game's strafe-camera target)
static const float kRad = 0.0174533f;

// eased state (game thread)
static float s_wTarget, s_w;            // rig weight (0 = the game's view)
static float s_side = 1.0f;             // -1 left .. 1 right shoulder (eased)
static float s_zoomTarget = 1.0f, s_zoom = 1.0f;
static float s_pullTarget, s_pull;      // ADS pull-in (m)
static float s_kp, s_ky, s_kf, s_kb;    // recoil offsets: pitch deg, yaw deg, fov deg, back m
static float s_vp, s_vy;                // recoil velocities (deg/s)
static float s_climb;                   // recoil climb still to add to the aim pitch (deg)
static float s_sway, s_swayTarget;      // scope breathing sway (0 steady .. 1.7 out of breath)
static float s_sens = 1.0f;
static float s_assistTarget, s_assist;
// aim
static float s_yaw, s_pitch;            // radians; forward = (sin yaw cos pitch, sin pitch, cos yaw cos pitch)
static bool s_rig;                      // yaw / pitch taken over from the game's view
static float s_turnHold;                // seconds the stick has been at full sideways deflection (turn boost)
static float s_pivot[3];
static bool s_havePivot;
// boom collision (rays in CamTick from the last build's geometry)
static float s_sideClamp = 1.0f, s_boomClamp = 1.0f;
static float s_lastPivot[3], s_lastShoulder[3], s_lastBoom[3];
static bool s_haveBoom;
static double s_lastBuild, s_handBackUntil;
static float s_aimPt[3];                 // dev / tests: aimat target, refined from the built view until s_aimUntil
static double s_aimUntil;
static float s_aimErr = -1.0f, s_aimMiss = -1.0f;   // last measured error (degrees, metres off the ray at the point)
static int s_applied, s_skipped, s_rays;
static const char* s_skipWhy = "";
int g_camLevel = 1;
static float s_roll, s_rollMax;         // the game's camera roll under our view (deg): last, largest
static int s_rollFrames;                // builds with more than 2 degrees of it
static float s_vroll, s_vrollMax;       // roll of the view matrix the build produced (what is rendered): last, largest
static int s_vrollFrames;               // builds whose view came out more than 1 degree off level

static inline void Sub(const float* a, const float* b, float* o) { o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2]; }
static inline float Dot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static inline void Cross(const float* a, const float* b, float* o) {
    o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0];
}
static inline float Norm(float* v) {
    float l = sqrtf(Dot(v, v));
    if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
    return l;
}
static inline float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float Ease(float cur, float target, float rate, float dt) {   // exponential approach, frame-rate independent
    return target + (cur - target) * expf(-rate * dt);
}

static bool OurView(BYTE* cs, const char** why) {
    const int mode = *(int*)(cs + 0x4884);
    if (mode != 0 && mode != kStrafeMode) { *why = "camera mode not follow"; return false; }
    if (CinematicNow()) { *why = "cinematic"; return false; }
    return true;
}

// look input -> yaw / pitch. Mouse: 0.06 deg per count at MouseSensitivity 1. Stick: radial dead zone, response
// curve, a turn boost after 0.25 s at full sideways deflection (hip only), aim-assist slow-down over targets.
static void Look(float dt, float sx, float sy, float mx, float my) {
    const float inv = cfg.invertY ? -1.0f : 1.0f;
    const float mk = 0.06f * kRad * cfg.mouseSens * s_sens;
    s_yaw -= mx * mk;                   // mouse right -> turn right (screen right = cross(forward, up))
    s_pitch -= my * mk * inv;           // DirectInput +Y = mouse pulled back -> look down
    const float m = sqrtf(sx * sx + sy * sy);
    const float dead = 0.17f;
    if (m > dead) {
        float n = (m - dead) / (1.0f - dead);
        if (n > 1.0f) n = 1.0f;
        const float c = powf(n, 1.7f) / m;
        const float x = sx * c, y = sy * c;
        if (n > 0.95f && fabsf(sx) > 0.8f * m && s_sens > 0.99f) s_turnHold += dt; else s_turnHold = 0.0f;
        const float boost = 1.0f + 0.6f * Clampf((s_turnHold - 0.25f) / 0.35f, 0.0f, 1.0f);
        const float slow = 1.0f - 0.45f * s_assist;
        const float k = cfg.lookSens * s_sens * slow * dt;
        s_yaw -= x * 190.0f * kRad * k * boost;
        s_pitch += y * 130.0f * kRad * k * inv;
    } else s_turnHold = 0.0f;
    if (s_climb != 0.0f) {              // recoil climb: most of it within ~0.1 s
        const float a = s_climb * (1.0f - expf(-25.0f * dt));
        s_pitch += a * kRad; s_climb -= a;
    }
    s_pitch = Clampf(s_pitch, -70.0f * kRad, 65.0f * kRad);
    if (s_yaw > 3.14159265f) s_yaw -= 6.2831853f;
    if (s_yaw < -3.14159265f) s_yaw += 6.2831853f;
}

static void __fastcall HkBuild(BYTE* cs, void*) {
    const double now = RealNow();
    float dt = s_lastBuild > 0.0 ? (float)(now - s_lastBuild) : 0.0f;
    s_lastBuild = now;
    dt = Clampf(dt, 0.0f, 0.1f);
    // look input is consumed on every build, so counts from before blaster mode never jump the view
    float sx, sy, mx, my; bool kbm;
    InputLook(&sx, &sy, &mx, &my, &kbm);

    const float w = s_w;
    const char* why = "";
    bool apply = w > 0.001f && cs && cs == CamSys();
    if (apply) {
        __try { apply = OurView(cs, &why); } __except (EXCEPTION_EXECUTE_HANDLER) { apply = false; why = "fault"; }
    } else why = "off";
    if (!apply) {
        if (w <= 0.001f) s_rig = false;
        s_skipWhy = why; s_skipped++;
        g_cam.valid = false;
        s_build(cs);
        return;
    }
    float* eye = (float*)(cs + 0x4850);
    float* tgt = (float*)(cs + 0x4860);
    float* fovp = (float*)(cs + 0x494C);
    float* minf = (float*)(cs + 0x98);
    const float e0[3] = { eye[0], eye[1], eye[2] }, t0[3] = { tgt[0], tgt[1], tgt[2] };
    const float fov0 = *fovp, min0 = *minf;
    float f0[3]; Sub(t0, e0, f0);
    const float d0 = Norm(f0);
    const float wup[3] = { 0.0f, 1.0f, 0.0f };

    if (!s_rig) {                       // take over from the game's view: no jump in direction
        s_yaw = atan2f(f0[0], f0[2]);
        s_pitch = asinf(Clampf(f0[1], -0.94f, 0.9f));
        s_rig = true; s_havePivot = false; s_turnHold = 0.0f; s_climb = 0.0f;
        s_sideClamp = s_boomClamp = 1.0f;
    }
    Look(dt, sx, sy, mx, my);

    const float cp = cosf(s_pitch), sp = sinf(s_pitch);
    float f[3] = { sinf(s_yaw) * cp, sp, cosf(s_yaw) * cp };
    float r[3]; Cross(f, wup, r); Norm(r);
    float u[3]; Cross(r, f, u); Norm(u);

    // pivot: the player + 1.3 m; height eased (stairs, landings), snapped after teleports
    float pp[3] = { t0[0], t0[1] - kPivotUp, t0[2] };
    const DWORD pr = PlayerRen();
    if (pr) RenPos(pr, pp);
    const float pw[3] = { pp[0], pp[1] + kPivotUp - 0.8f * CrouchDepth(), pp[2] };   // crouched: the view goes down too
    float dp[3]; Sub(pw, s_pivot, dp);
    if (!s_havePivot || Dot(dp, dp) > 9.0f) { s_pivot[0] = pw[0]; s_pivot[1] = pw[1]; s_pivot[2] = pw[2]; s_havePivot = true; }
    else { s_pivot[0] = pw[0]; s_pivot[2] = pw[2]; s_pivot[1] = Ease(s_pivot[1], pw[1], 16.0f, dt); }

    const float side = cfg.camSide * s_side;
    float dist = cfg.camDistance > 0.5f ? cfg.camDistance : (d0 > 0.5f ? d0 : 3.0f);
    dist = dist - s_pull + s_kb;
    if (dist < 0.4f) dist = 0.4f;
    float sh[3], ew[3];
    for (int i = 0; i < 3; i++) {
        sh[i] = s_pivot[i] + r[i] * side + wup[i] * cfg.camUp;
        s_lastPivot[i] = s_pivot[i]; s_lastShoulder[i] = sh[i]; s_lastBoom[i] = -f[i] * dist;
    }
    s_haveBoom = true;
    for (int i = 0; i < 3; i++) {
        const float sc = s_pivot[i] + (sh[i] - s_pivot[i]) * s_sideClamp;
        ew[i] = sc - f[i] * dist * s_boomClamp;
    }

    // blend with the game's view (entry / exit), then the visual recoil tilt
    float fe[3], ff[3];
    for (int i = 0; i < 3; i++) { fe[i] = e0[i] + (ew[i] - e0[i]) * w; ff[i] = f0[i] + (f[i] - f0[i]) * w; }
    Norm(ff);
    float fr[3]; Cross(ff, wup, fr); Norm(fr);
    float fu[3]; Cross(fr, ff, fu); Norm(fu);
    // scope breathing: a slow figure eight (4.2 s) on top of the recoil tilt; it moves the aim ray as well
    const float st = (float)fmod(now, 4200.0) * 1.49600f;
    const float swP = s_sway * 0.30f * sinf(2.0f * st + 0.6f), swY = s_sway * 0.42f * sinf(st);
    const float tp = tanf((s_kp + swP) * kRad), ty = tanf((s_ky + swY) * kRad);
    float fw[3];
    for (int i = 0; i < 3; i++) fw[i] = ff[i] + fu[i] * tp - fr[i] * ty;
    Norm(fw);
    float tw[3] = { fe[0] + fw[0] * 10.0f, fe[1] + fw[1] * 10.0f, fe[2] + fw[2] * 10.0f };
    if (s_aimUntil > 0.0) {   // aimat: measure the shown view ray against the point, correct the next frame's yaw / pitch
        float d[3]; Sub(s_aimPt, fe, d);
        const float l = Norm(d);
        if (l > 0.1f) {
            const float a = acosf(Clampf(Dot(d, fw), -1.0f, 1.0f));
            s_aimErr = a / kRad;
            s_aimMiss = l * sinf(a);
            if (now < s_aimUntil) {
                float ey = atan2f(d[0], d[2]) - atan2f(fw[0], fw[2]);
                if (ey > 3.14159265f) ey -= 6.2831853f;
                if (ey < -3.14159265f) ey += 6.2831853f;
                s_yaw += ey;
                s_pitch = Clampf(s_pitch + asinf(Clampf(d[1], -1.0f, 1.0f)) - asinf(Clampf(fw[1], -1.0f, 1.0f)),
                                 -70.0f * kRad, 65.0f * kRad);
            }
        }
    }

    float fov = fov0;
    if (cfg.fovHip > 1.0f) fov += (cfg.fovHip - fov0) * w;
    fov = fov * (1.0f + (s_zoom - 1.0f) * w) + s_kf;
    if (fov < 5.0f) fov = 5.0f;

    // the aim ray is the view as shown (the crosshair sits at the screen centre, recoil included)
    float kr[3]; Cross(fw, wup, kr); Norm(kr);
    float ku[3]; Cross(kr, fw, ku); Norm(ku);
    // the game's up vector carries its own camera roll: its follow camera banks up to ~6 degrees while it swings round
    // the body (fast pans, and the body flips of 1.2.0). Kept (camlevel 0, 1.2.0) the view came out tilted - "wonky";
    // camlevel 1 builds our view with the world up and hands the game's own up back afterwards
    float* upp = (float*)(cs + 0x4870);
    const float up0[3] = { upp[0], upp[1], upp[2] };
    s_roll = atan2f(Dot(up0, kr), Dot(up0, ku)) / kRad;
    if (fabsf(s_roll) > fabsf(s_rollMax)) s_rollMax = s_roll;
    if (fabsf(s_roll) > 2.0f) s_rollFrames++;

    eye[0] = fe[0]; eye[1] = fe[1]; eye[2] = fe[2];
    tgt[0] = tw[0]; tgt[1] = tw[1]; tgt[2] = tw[2];
    if (g_camLevel) { upp[0] = 0.0f; upp[1] = 1.0f; upp[2] = 0.0f; }
    *fovp = fov;
    if (min0 > fov) *minf = fov;
    s_build(cs);
    {   // the built view matrix (+0x4950): its side axis is the one whose partner axis matches our forward
        const float* m = (const float*)(cs + 0x4950);
        const float vc[3] = { m[2], m[6], m[10] }, vr[3] = { m[8], m[9], m[10] };
        const float sideY = fabsf(Dot(vc, fw)) >= fabsf(Dot(vr, fw)) ? m[4] : m[1];
        s_vroll = asinf(Clampf(sideY, -1.0f, 1.0f)) / kRad;
        if (fabsf(s_vroll) > fabsf(s_vrollMax)) s_vrollMax = s_vroll;
        if (fabsf(s_vroll) > 1.0f) s_vrollFrames++;
    }
    if (!g_keepView) {
        eye[0] = e0[0]; eye[1] = e0[1]; eye[2] = e0[2];
        tgt[0] = t0[0]; tgt[1] = t0[1]; tgt[2] = t0[2];
    }
    upp[0] = up0[0]; upp[1] = up0[1]; upp[2] = up0[2];
    *fovp = fov0; *minf = min0;
    for (int i = 0; i < 3; i++) { g_cam.eye[i] = fe[i]; g_cam.fwd[i] = fw[i]; g_cam.right[i] = kr[i]; g_cam.up[i] = ku[i]; }
    g_cam.fov = fov;
    g_cam.valid = true;
    s_applied++;
}

void CamSetWeight(float w) { s_wTarget = Clampf(w, 0.0f, 1.0f); }
void CamSetAds(float zoom) {
    s_zoomTarget = zoom;
    s_pullTarget = zoom < 0.999f ? 0.85f : 0.0f;
}
void CamSetSens(float mul) { s_sens = mul; }
void CamSetSway(float amount) { s_swayTarget = amount; }
void CamSetAssist(float slow) { s_assistTarget = Clampf(slow, 0.0f, 1.0f); }
void CamKick(float pitchDeg, float yawDeg, float fovDeg, float back) {
    s_kf += fovDeg;               // the FOV punch and push-back always play; they move the view, never turn it
    s_kb += back;
    if (!cfg.cameraRecoil) return;   // CameraRecoil=0 (default): shots never rotate the camera
    s_vp += pitchDeg * 40.0f;     // velocity impulses: the springs turn them into a fast rise (peak ~ the kick in
    s_vy += yawDeg * 40.0f;       // degrees after ~70 ms) and a smooth return
    s_climb += pitchDeg * 0.3f;   // and a part stays: the muzzle climbs under sustained fire
}
void CamAimAngles(float* yawDeg, float* pitchDeg) { *yawDeg = s_yaw / kRad; *pitchDeg = s_pitch / kRad; }

// dev / tests: the view ray passes through the shoulder point (the eye sits behind it on the same line), so aim from
// there; the shoulder depends on the yaw, so a few rounds settle it
bool CamAimAt(const float p[3]) {
    if (!s_rig || !s_havePivot) return false;
    const float wup[3] = { 0.0f, 1.0f, 0.0f };
    for (int it = 0; it < 4; it++) {
        const float cp = cosf(s_pitch);
        float f[3] = { sinf(s_yaw) * cp, sinf(s_pitch), cosf(s_yaw) * cp };
        float r[3]; Cross(f, wup, r); Norm(r);
        float d[3];
        for (int i = 0; i < 3; i++) d[i] = p[i] - (s_pivot[i] + (r[i] * cfg.camSide * s_side + wup[i] * cfg.camUp) * s_sideClamp);
        if (Norm(d) < 0.1f) return false;
        s_yaw = atan2f(d[0], d[2]);
        s_pitch = Clampf(asinf(Clampf(d[1], -1.0f, 1.0f)), -70.0f * kRad, 65.0f * kRad);
    }
    s_climb = 0.0f;
    for (int i = 0; i < 3; i++) s_aimPt[i] = p[i];
    s_aimUntil = RealNow() + 0.5;
    return true;
}
void CamAimError(float* deg, float* miss) { *deg = s_aimErr; *miss = s_aimMiss; }
void CamHandBack() { if (s_rig) s_handBackUntil = RealNow() + 1.0; }

void CamTick(float dt) {
    if (dt > 0.1f) dt = 0.1f;
    s_w = Ease(s_w, s_wTarget, 9.0f, dt);
    if (fabsf(s_w - s_wTarget) < 0.002f) s_w = s_wTarget;
    s_side = Ease(s_side, (float)cfg.shoulder, 11.0f, dt);
    s_zoom = Ease(s_zoom, s_zoomTarget, 14.0f, dt);
    s_pull = Ease(s_pull, s_pullTarget, 12.0f, dt);
    s_assist = Ease(s_assist, s_assistTarget, s_assistTarget > s_assist ? 12.0f : 6.0f, dt);
    s_sway = Ease(s_sway, s_swayTarget, s_swayTarget < s_sway ? 7.0f : 2.5f, dt);
    // recoil springs: critically damped toward 0
    const float k = 220.0f, c = 2.0f * sqrtf(k);
    for (float left = dt; left > 1e-6f; left -= 1.0f / 240.0f) {   // fixed sub-steps: same curve at any frame rate
        const float h = left < 1.0f / 240.0f ? left : 1.0f / 240.0f;
        s_vp += (-k * s_kp - c * s_vp) * h; s_kp += s_vp * h;
        s_vy += (-k * s_ky - c * s_vy) * h; s_ky += s_vy * h;
    }
    s_kf = Ease(s_kf, 0.0f, 10.0f, dt);
    s_kb = Ease(s_kb, 0.0f, 12.0f, dt);

    // boom collision against the static world: pivot -> shoulder, then the clamped shoulder -> eye
    if (s_w > 0.001f && s_haveBoom) {
        float n[2];
        const float* p = s_lastPivot; const float* s = s_lastShoulder; const float* b = s_lastBoom;
        const int got = LuaNums(n, 2, "return DKB.boom(%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,0.3)",
                                p[0], p[1], p[2], s[0], s[1], s[2], b[0], b[1], b[2]);
        s_rays++;
        float ws = 1.0f, wb = 1.0f;
        if (got == 2) { ws = Clampf(n[0], 0.0f, 1.0f); wb = Clampf(n[1], 0.12f, 1.0f); }
        // in at once (never show the inside of a wall), out smoothly
        s_sideClamp = ws < s_sideClamp ? ws : Ease(s_sideClamp, ws, 5.0f, dt);
        s_boomClamp = wb < s_boomClamp ? wb : Ease(s_boomClamp, wb, 4.0f, dt);
    } else { s_sideClamp = s_boomClamp = 1.0f; }

    // hand-back: once the game's camera is back in its follow mode, turn it to our aim
    if (s_handBackUntil > 0.0) {
        BYTE* cs = CamSys();
        int mode = -1;
        if (cs) __try { mode = *(int*)(cs + 0x4884); } __except (EXCEPTION_EXECUTE_HANDLER) { mode = -1; }
        if (mode == 0) {
            const char* r = LuaCall("pcall(camera.SetOrbit, %.3f) pcall(camera.SetPitch, %.3f) return 'ok'",
                                    -s_yaw / kRad, s_pitch / kRad);
            if (strcmp(r, "ok")) Log("camera hand-back failed: %s", r);
            s_handBackUntil = 0.0;
        } else if (RealNow() > s_handBackUntil) s_handBackUntil = 0.0;
    }
}

bool CamOwnsView() { return g_cam.valid; }
bool CamHookOk() { return s_build != nullptr; }
const char* CamStatus() {
    static char b[320];
    sprintf_s(b, "w %.2f yaw %.1f pitch %.1f side %.2f zoom %.2f pull %.2f clamp %.2f/%.2f kick %.2f/%.2f assist %.2f fov %.1f "
                 "applied %d skipped %d (%s) rays %d roll %.1f max %.1f frames %d level %d view roll %.2f max %.2f frames %d",
              s_w, s_yaw / kRad, s_pitch / kRad, s_side, s_zoom, s_pull, s_sideClamp, s_boomClamp, s_kp, s_ky, s_assist,
              g_cam.fov, s_applied, s_skipped, s_skipWhy, s_rays, s_roll, s_rollMax, s_rollFrames, g_camLevel, s_vroll,
              s_vrollMax, s_vrollFrames);
    return b;
}
void CamRollReset() { s_rollMax = s_vrollMax = 0.0f; s_rollFrames = s_vrollFrames = 0; }

bool CamInstall(const Range& text) {
    s_site = FindSym(text, "Blaster_CamBuildCall");
    if (!s_site || s_site[0] != 0xE8) { Log("camera build call not found - aim camera off"); return false; }
    s_build = (Build_t)RedirectCall(s_site, (void*)&HkBuild);
    return s_build != nullptr;
}

}  // namespace dkb
