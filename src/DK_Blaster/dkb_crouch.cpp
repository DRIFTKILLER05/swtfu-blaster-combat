// DK_Blaster: crouch. The game has no player crouch and no crouch strafe clips, so the stance is made from the pose the
// game itself animated: at the start of the animation post pass, stage 2 (0x589A80: the final model-space pose exists,
// the game's own IK included, and nothing has read it yet) the player's hips go down, the spine leans in a little with
// the chest counter-rotated (the gun keeps its aim) and each leg is bent by a two-bone IK that keeps the foot exactly
// where the animation planted it. Every locomotion clip therefore becomes its own crouch walk, in every direction.
// Everything that reads the pose later sees the crouch: hit proxies, jiggle, skinning (renderer, shadow, DK_FPSFix),
// cloth and the attachments (the gun prop). The ren position, capsule and camera target stay where they are; the aim
// camera drops by itself (CamTick). Research: research\agent_blaster\crouch\REPORT.md.
// The same pass finishes the aimed gun pose (dkb_aim.cpp): the trooper aim grid reaches +-45 degrees from the hips, so a
// strafe clip's hip twist (or the crouch lean) left the gun up to ~25 degrees off the crosshair. While the overlay is up
// the chest turns the rest of the way: the barrel onto the aim point. The barrel is the gun bone's local axis
// (0, 0.119, 0.993): measured 2026-10-27 from the E-11's muzzle (DKB.tip) in the carry and the aimed pose alike; the
// gun props hang on rWeapon[0] along its +Z. (The left hand is no guide: in the aim clips it sits 5 cm from the bone.)
#include "dkb.h"
#include <stdio.h>
#include <math.h>

using namespace dk;

namespace dkb {

struct Rec { float t[4]; float q[4]; };   // one bone of a pose: translation, quaternion x y z w (Hamilton, w last)

typedef void*(__cdecl* Stage2_t)(DWORD ren);
typedef int(__fastcall* FindBone_t)(void* si, void* edx, const char* name, int beyondLod);   // __thiscall, ret 8
static Stage2_t s_orig;
static FindBone_t s_findBone;
static uint8_t* s_site;

static const int kMaxBones = 256;
enum { B_HIPS, B_SPINE0, B_SPINE2, B_LUP, B_LLO, B_LFT, B_RUP, B_RLO, B_RFT, B_LHAND, B_RWEAPON, B_COUNT };
static const char* kBones[B_COUNT] = { "hips[0]", "spine[0]", "spine[2]", "lUpperLeg[0]", "lLowerLeg[0]", "lFoot[0]",
                                       "rUpperLeg[0]", "rLowerLeg[0]", "rFoot[0]", "lHand[0]", "rWeapon[0]" };
static int s_b[B_COUNT];
static bool s_sub[B_COUNT][kMaxBones];   // bone i is in the subtree of s_b[k]
static void* s_si; static int s_lod = -1, s_n;
static bool s_ready, s_broken;

static volatile DWORD s_playerRen;
static float s_h, s_hTarget;             // depth now / wanted (m); eased on the game thread, read by the hook
static const float kLean = 9.0f * 0.0174533f;
static Rec s_outHips, s_outHand; static bool s_haveOut;
static long s_hits, s_applied, s_stale, s_behavior, s_faults, s_unreach;
static volatile LONG s_knocked;          // the hook met a knock-down / ragdoll while crouched (the main loop stands up)
// aim correction: the aim point in model space (game thread -> hook), its weight (eased on the game thread) and the
// last turn the hook made (status)
static float s_aimMdl[3];
static volatile LONG s_aimOn;
static float s_aimW, s_aimYaw, s_aimPitch;
static long s_aimed;
static const float kAimMaxYaw = 50.0f * 0.0174533f, kAimMaxPitch = 35.0f * 0.0174533f;

// ---------------------------------------------------------------------------------------------- vector / quaternion
static inline void QMul(const float* a, const float* b, float* o) {
    const float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    const float y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    const float z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    const float w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    o[0] = x; o[1] = y; o[2] = z; o[3] = w;
}
static inline void QRot(const float* q, const float* v, float* o) {   // o = q v q*
    const float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]), ty = 2.0f * (q[2] * v[0] - q[0] * v[2]),
                tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
    o[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    o[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    o[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}
static inline float Dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static inline float Len3(const float* a) { return sqrtf(Dot3(a, a)); }
static void QNorm(float* q) {
    const float l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (l < 1e-8f) { q[0] = q[1] = q[2] = 0.0f; q[3] = 1.0f; return; }
    for (int i = 0; i < 4; i++) q[i] /= l;
}
static void FromTo(const float* u, const float* v, float* q) {   // shortest rotation taking direction u to v
    const float lu = Len3(u), lv = Len3(v);
    if (lu < 1e-6f || lv < 1e-6f) { q[0] = q[1] = q[2] = 0.0f; q[3] = 1.0f; return; }
    q[0] = u[1] * v[2] - u[2] * v[1]; q[1] = u[2] * v[0] - u[0] * v[2]; q[2] = u[0] * v[1] - u[1] * v[0];
    q[3] = Dot3(u, v) + lu * lv;
    if (q[3] < 1e-6f * lu * lv) { q[0] = q[1] = q[2] = 0.0f; q[3] = 1.0f; return; }   // opposite: never in a leg
    QNorm(q);
}
static void AxisAngle(const float* axis, float ang, float* q) {
    const float s = sinf(ang * 0.5f);
    q[0] = axis[0] * s; q[1] = axis[1] * s; q[2] = axis[2] * s; q[3] = cosf(ang * 0.5f);
}
// rotate the subtree of bone k by q about point c (model space)
static void RotSub(Rec* m, int k, const float* c, const float* q) {
    for (int i = 0; i < s_n; i++) {
        if (!s_sub[k][i]) continue;
        const float d[3] = { m[i].t[0] - c[0], m[i].t[1] - c[1], m[i].t[2] - c[2] };
        float r[3]; QRot(q, d, r);
        m[i].t[0] = c[0] + r[0]; m[i].t[1] = c[1] + r[1]; m[i].t[2] = c[2] + r[2];
        float o[4]; QMul(q, m[i].q, o); QNorm(o);
        memcpy(m[i].q, o, sizeof o);
    }
}

// ---------------------------------------------------------------------------------------------- skeleton plan
static bool Resolve(BYTE* si, int lod, int n, const int* par) {
    s_si = si; s_lod = lod; s_n = n; s_ready = false; s_haveOut = false;
    for (int k = 0; k < B_COUNT; k++) {
        s_b[k] = s_findBone ? s_findBone(si, nullptr, kBones[k], 0) : -1;
        if (s_b[k] < 0 || s_b[k] >= n) { Log("crouch: bone %s not in the player's skeleton (lod %d, %d bones) - crouch off", kBones[k], lod, n); return false; }
    }
    for (int k = 0; k < B_COUNT; k++) {
        bool* in = s_sub[k];
        memset(in, 0, sizeof s_sub[k]);
        in[s_b[k]] = true;
        for (int pass = 0; pass < 4; pass++) {   // parents come first in this engine; extra passes in case one does not
            bool more = false;
            for (int i = 0; i < n; i++) {
                const int p = par[i];
                if (!in[i] && p >= 0 && p < n && in[p]) { in[i] = true; more = true; }
            }
            if (!more) break;
        }
    }
    // the legs must hang off the hips and the feet off the lower legs, or the IK would tear the mesh
    if (!s_sub[B_HIPS][s_b[B_LUP]] || !s_sub[B_HIPS][s_b[B_RUP]] || !s_sub[B_LUP][s_b[B_LFT]] || !s_sub[B_RUP][s_b[B_RFT]] ||
        !s_sub[B_LLO][s_b[B_LFT]] || !s_sub[B_RLO][s_b[B_RFT]] || s_sub[B_SPINE0][s_b[B_LUP]]) {
        Log("crouch: unexpected bone hierarchy - crouch off");
        return false;
    }
    s_ready = true;
    Log("crouch: skeleton %p lod %d, %d bones; hips %d spine %d/%d legs %d %d %d / %d %d %d", si, lod, n, s_b[B_HIPS],
        s_b[B_SPINE0], s_b[B_SPINE2], s_b[B_LUP], s_b[B_LLO], s_b[B_LFT], s_b[B_RUP], s_b[B_RLO], s_b[B_RFT]);
    return true;
}

static void Leg(Rec* m, int up, int lo, int ft, float h, const float* fwd) {
    const int iu = s_b[up], il = s_b[lo], iff = s_b[ft];
    float qf0[4]; memcpy(qf0, m[iff].q, sizeof qf0);
    const float* A = m[iu].t;
    const float K[3] = { m[il].t[0], m[il].t[1], m[il].t[2] };
    const float F[3] = { m[iff].t[0], m[iff].t[1], m[iff].t[2] };
    float T[3] = { F[0], F[1] + h, F[2] };   // where the animation put the foot (the hips subtree moved it down by h)
    const float ka[3] = { K[0] - A[0], K[1] - A[1], K[2] - A[2] }, fk[3] = { F[0] - K[0], F[1] - K[1], F[2] - K[2] };
    const float a = Len3(ka), b = Len3(fk);
    if (a < 0.05f || b < 0.05f) return;
    float dv[3] = { T[0] - A[0], T[1] - A[1], T[2] - A[2] };
    float d = Len3(dv);
    if (d < 1e-4f) return;
    const float dmax = 0.999f * (a + b), dmin = fabsf(a - b) + 0.01f;
    if (d > dmax) { s_unreach++; d = dmax; }
    if (d < dmin) d = dmin;
    const float dl = Len3(dv);
    const float dir[3] = { dv[0] / dl, dv[1] / dl, dv[2] / dl };
    T[0] = A[0] + dir[0] * d; T[1] = A[1] + dir[1] * d; T[2] = A[2] + dir[2] * d;
    const float x = (a * a - b * b + d * d) / (2.0f * d);
    const float y = sqrtf(fmaxf(0.0f, a * a - x * x));
    // the knee bends the way the animation bent it, nudged forward so a straight leg still folds the right way
    float pole[3];
    for (int i = 0; i < 3; i++) pole[i] = ka[i] + fwd[i] * 0.1f * a;
    const float pd = Dot3(pole, dir);
    for (int i = 0; i < 3; i++) pole[i] -= dir[i] * pd;
    float pl = Len3(pole);
    if (pl < 1e-4f) { memcpy(pole, fwd, sizeof pole); pl = 1.0f; }
    for (int i = 0; i < 3; i++) pole[i] /= pl;
    const float Kn[3] = { A[0] + dir[0] * x + pole[0] * y, A[1] + dir[1] * x + pole[1] * y, A[2] + dir[2] * x + pole[2] * y };
    const float A0[3] = { A[0], A[1], A[2] };
    float q[4];
    const float kn[3] = { Kn[0] - A0[0], Kn[1] - A0[1], Kn[2] - A0[2] };
    FromTo(ka, kn, q);
    RotSub(m, up, A0, q);
    const float f1[3] = { m[iff].t[0] - Kn[0], m[iff].t[1] - Kn[1], m[iff].t[2] - Kn[2] };
    const float tk[3] = { T[0] - Kn[0], T[1] - Kn[1], T[2] - Kn[2] };
    FromTo(f1, tk, q);
    RotSub(m, lo, Kn, q);
    // the foot keeps the animation's orientation (flat on the ground, toes where they were)
    const float cq[4] = { -m[iff].q[0], -m[iff].q[1], -m[iff].q[2], m[iff].q[3] };
    float rf[4]; QMul(qf0, cq, rf); QNorm(rf);
    const float fc[3] = { m[iff].t[0], m[iff].t[1], m[iff].t[2] };
    RotSub(m, ft, fc, rf);
}

void* AnimCompOfRen(DWORD ren);   // dkb_hold.cpp

// turn the chest so the barrel points at the aim point: yaw about the vertical, half at the lower spine and half at the
// chest, then pitch at the chest. w scales both (fade in / out with the overlay).
static const float kBarrel[3] = { 0.0f, 0.1190f, 0.9929f };
static void AimFix(Rec* m, float w) {
    const int ig = s_b[B_RWEAPON];
    float b[3], t[3];
    QRot(m[ig].q, kBarrel, b);
    for (int i = 0; i < 3; i++) t[i] = s_aimMdl[i] - m[ig].t[i];
    if (Len3(b) < 0.5f || Len3(t) < 1.0f) return;
    float dy = atan2f(t[0], t[2]) - atan2f(b[0], b[2]);
    if (dy > 3.14159265f) dy -= 6.2831853f;
    if (dy < -3.14159265f) dy += 6.2831853f;
    dy = (dy > kAimMaxYaw ? kAimMaxYaw : (dy < -kAimMaxYaw ? -kAimMaxYaw : dy)) * w;
    const float up[3] = { 0.0f, 1.0f, 0.0f };
    float q[4], c[3];
    AxisAngle(up, dy * 0.5f, q);
    memcpy(c, m[s_b[B_SPINE0]].t, sizeof c);
    RotSub(m, B_SPINE0, c, q);
    memcpy(c, m[s_b[B_SPINE2]].t, sizeof c);
    RotSub(m, B_SPINE2, c, q);
    QRot(m[ig].q, kBarrel, b);
    for (int i = 0; i < 3; i++) t[i] = s_aimMdl[i] - m[ig].t[i];
    const float hb = sqrtf(b[0] * b[0] + b[2] * b[2]), ht = sqrtf(t[0] * t[0] + t[2] * t[2]);
    float dp = 0.0f;
    if (hb > 0.1f) {
        dp = atan2f(t[1], ht) - atan2f(b[1], hb);
        dp = (dp > kAimMaxPitch ? kAimMaxPitch : (dp < -kAimMaxPitch ? -kAimMaxPitch : dp)) * w;
        const float ax[3] = { -b[2] / hb, 0.0f, b[0] / hb };   // horizontal, across the barrel: + raises it
        AxisAngle(ax, dp, q);
        memcpy(c, m[s_b[B_SPINE2]].t, sizeof c);
        RotSub(m, B_SPINE2, c, q);
    }
    s_aimYaw = dy * 57.29578f; s_aimPitch = dp * 57.29578f;
    s_aimed++;
}

static void Apply(DWORD ren) {
    BYTE* P = (BYTE*)AnimCompOfRen(ren);
    if (!P) return;
    if (*(BYTE*)(P + 0xCEC)) { s_behavior++; s_knocked = 1; return; }   // Euphoria / ragdoll owns the pose
    BYTE* si = *(BYTE**)(P + 0x24);
    if (!si) return;
    const int lod = *(int*)(si + 0x168);
    if (lod < 0 || lod > 7) return;
    const int n = *(int*)(si + 0xE4 + lod * 8);
    const int* par = *(const int**)(si + 0xC);
    Rec* m = *(Rec**)(P + 0x34);
    if (n < 16 || n > kMaxBones || !par || !m) return;
    if (si != s_si || lod != s_lod || n != s_n) Resolve(si, lod, n, par);
    if (!s_ready) return;
    // the pose was not rebuilt since our last edit (pause, a skipped update): it is crouched already
    if (s_haveOut && !memcmp(&m[s_b[B_HIPS]], &s_outHips, sizeof(Rec)) && !memcmp(&m[s_b[B_LHAND]], &s_outHand, sizeof(Rec))) {
        s_stale++;
        return;
    }
    const float h = s_h;
    if (h > 0.002f) {
        // body axes in model space: Y up, the character faces +Z
        const float fwd[3] = { 0.0f, 0.0f, 1.0f }, right[3] = { 1.0f, 0.0f, 0.0f };
        // 1. hips (and everything on them) down
        for (int i = 0; i < n; i++) if (s_sub[B_HIPS][i]) m[i].t[1] -= h;
        // 2. lean in from the lower spine, the chest turned back (arms, head and gun keep their orientation)
        const float lean = kLean * (h / 0.32f);
        float q[4];
        float c[3] = { m[s_b[B_SPINE0]].t[0], m[s_b[B_SPINE0]].t[1], m[s_b[B_SPINE0]].t[2] };
        AxisAngle(right, lean, q);
        RotSub(m, B_SPINE0, c, q);
        c[0] = m[s_b[B_SPINE2]].t[0]; c[1] = m[s_b[B_SPINE2]].t[1]; c[2] = m[s_b[B_SPINE2]].t[2];
        AxisAngle(right, -lean, q);
        RotSub(m, B_SPINE2, c, q);
        // 3. the legs fold to keep the feet planted
        Leg(m, B_LUP, B_LLO, B_LFT, h, fwd);
        Leg(m, B_RUP, B_RLO, B_RFT, h, fwd);
    }
    // 4. the aimed gun onto the crosshair
    const float aw = s_aimW;
    if (aw > 0.001f) AimFix(m, aw);
    s_outHips = m[s_b[B_HIPS]]; s_outHand = m[s_b[B_LHAND]]; s_haveOut = true;
    s_applied++;
}

static void* __cdecl HkStage2(DWORD ren) {
    if (ren && ren == s_playerRen && (s_h > 0.002f || s_aimW > 0.001f) && !s_broken) {
        s_hits++;
        __try { Apply(ren); }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            if (++s_faults >= 3) { s_broken = true; Log("crouch: exception 0x%08X in the pose edit - crouch off for this session", GetExceptionCode()); }
        }
    }
    return s_orig(ren);
}

// ---------------------------------------------------------------------------------------------- game thread
bool CrouchInstall(const Range& text) {
    s_findBone = (FindBone_t)FindSym(text, "Skeleton_FindBone");
    uint8_t* site = FindSym(text, "Anim_PostStage2Call");
    if (!site || site[0] != 0xE8 || !s_findBone) { Log("crouch: pose hook site %p / FindBone %p not found - crouch off", site, s_findBone); return false; }
    char who[96];
    if (HookedBy(site, who, sizeof who)) Log("crouch: the pose call is already redirected by %s - chaining onto it", who);
    s_orig = (Stage2_t)RedirectCall(site, (void*)&HkStage2);
    s_site = s_orig ? site : nullptr;
    if (!s_site) { Log("crouch: could not redirect the pose call - crouch off"); return false; }
    return true;
}
bool CrouchAvailable() { return s_site && !s_broken; }

void CrouchSet(float depth) { s_hTarget = depth < 0.0f ? 0.0f : (depth > 0.5f ? 0.5f : depth); }

void CrouchTick(float dt, bool running, bool cut) {
    s_playerRen = PlayerRen();
    if (!s_site || cut) { s_h = 0.0f; s_aimW = 0.0f; return; }   // a cutscene / respawn: standing at once
    if (!running) return;   // pause / load: the stance holds
    // the aim correction follows the overlay's own blend (in ~0.25 s, out with its 0.22 s fade)
    const float aimT = s_aimOn ? 1.0f : 0.0f;
    s_aimW += (aimT - s_aimW) * (1.0f - expf((aimT > s_aimW ? -9.0f : -14.0f) * dt));
    if (fabsf(aimT - s_aimW) < 0.002f) s_aimW = aimT;
    // eased: most of the way down in ~0.2 s, up a little quicker
    s_h += (s_hTarget - s_h) * (1.0f - expf((s_hTarget > s_h ? -12.0f : -15.0f) * dt));
    if (fabsf(s_hTarget - s_h) < 0.002f) s_h = s_hTarget;
}
float CrouchDepth() { return s_site && !s_broken ? s_h : 0.0f; }
void CrouchAim(bool on, const float* aimRel, float bodyYaw) {
    if (!on || !aimRel) { s_aimOn = 0; return; }
    // world -> model: undo the body's yaw (model +Z = the body's forward (sin yaw, 0, cos yaw))
    const float c = cosf(bodyYaw), s = sinf(bodyYaw);
    s_aimMdl[0] = aimRel[0] * c - aimRel[2] * s;
    s_aimMdl[1] = aimRel[1];
    s_aimMdl[2] = aimRel[0] * s + aimRel[2] * c;
    s_aimOn = 1;
}
bool CrouchKnocked() { return InterlockedExchange(&s_knocked, 0) != 0; }
bool CrouchBehavior() {   // Euphoria / ragdoll owns the player's pose now (a knock-down)
    BYTE* P = (BYTE*)AnimCompOfRen(PlayerRen());
    BYTE b = 0;
    return P && SafeRead(P + 0xCEC, &b, 1) && b;
}

const char* CrouchStatus() {
    static char b[256];
    _snprintf_s(b, sizeof b, _TRUNCATE, "hook %s depth %.3f target %.3f hits %ld applied %ld stale %ld behavior %ld unreach %ld faults %ld bones %d lod %d"
                " | aimfix w %.2f yaw %.1f pitch %.1f n %ld",
                s_site ? (s_broken ? "broken" : "ok") : "off", s_h, s_hTarget, s_hits, s_applied, s_stale, s_behavior, s_unreach,
                s_faults, s_n, s_lod, s_aimW, s_aimYaw, s_aimPitch, s_aimed);
    return b;
}

// dev: the skeleton as the hook sees it (bones, parents, model-space positions)
int CrouchDump(char* out, int size) {
    const DWORD ren = PlayerRen();
    BYTE* P = (BYTE*)AnimCompOfRen(ren);
    if (!P) { _snprintf_s(out, size, _TRUNCATE, "no player anim object"); return 1; }
    __try {
        BYTE* si = *(BYTE**)(P + 0x24);
        const int lod = *(int*)(si + 0x168);
        const int n = *(int*)(si + 0xE4 + lod * 8);
        const int* par = *(const int**)(si + 0xC);
        Rec* m = *(Rec**)(P + 0x34);
        int len = _snprintf_s(out, size, _TRUNCATE, "P %p si %p (P+0x2C %p) lod %d n %d model %p local %p skin %p behavior %d\n", P, si,
                              *(void**)(P + 0x2C), lod, n, m, *(void**)(P + 0x30), *(void**)(P + 0x38), *(BYTE*)(P + 0xCEC));
        static const char* names[] = { "root[0]", "root[1]", "hips[0]", "spine[0]", "spine[1]", "spine[2]", "spine[3]", "neck[0]",
                                       "lUpperLeg[0]", "lUpperLegWeight[0]", "lUpperLegWeight[1]", "lLowerLeg[0]", "lFoot[0]", "lFoot[1]",
                                       "rUpperLeg[0]", "rLowerLeg[0]", "rFoot[0]", "rFoot[1]", "rShoulder", "rHand[0]", "rWeapon[0]",
                                       "lHand[0]", "waist0000[0]", "collar[0]" };
        for (const char* nm : names) {
            const int i = s_findBone ? s_findBone(si, nullptr, nm, 0) : -1;
            if (len < 0 || len >= size - 140) break;
            if (i < 0 || i >= n) { len += _snprintf_s(out + len, size - len, _TRUNCATE, "%s -\n", nm); continue; }
            len += _snprintf_s(out + len, size - len, _TRUNCATE, "%s %d par %d  %.3f %.3f %.3f  q %.4f %.4f %.4f %.4f\n", nm, i, par[i],
                               m[i].t[0], m[i].t[1], m[i].t[2], m[i].q[0], m[i].q[1], m[i].q[2], m[i].q[3]);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { _snprintf_s(out, size, _TRUNCATE, "exception 0x%08X", GetExceptionCode()); }
    return 1;
}

}  // namespace dkb
