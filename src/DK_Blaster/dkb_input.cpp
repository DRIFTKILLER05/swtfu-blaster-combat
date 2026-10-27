// DK_Blaster: input. Our actions come from the devices themselves (keyboard / mouse through GetAsyncKeyState while
// the game has the focus, the controller through XInput, DK_Bridge's injected keys in background test mode), so
// they work with any in-game binding. While armed, the game's own attack / block / Force / lock-on buttons are muted
// in InputMgr::SetPadState (every input path ends there), mouse / stick look is scaled for aiming, and the dodge
// action pulses the game's Force Dash button.
// Controller combos use Back as the modifier (Back + D-pad: blaster, classes, wheel). Back alone is the game's
// PlayerOptions button and D-pad up its action camera, so while Back is held both are kept from the game; a Back tap
// with no other button is handed to the game on release (one frame), so the options menu still opens as usual.
#include "dkb.h"
#include <stdio.h>

using namespace dk;

namespace dkb {

InputState g_in;

typedef DWORD(WINAPI* XInputGetState_t)(DWORD, void*);
static XInputGetState_t s_xinput;
static ULONGLONG s_padRetry;

struct BgInput { DWORD magic; volatile LONG on; volatile ULONGLONG keyUntil[256]; volatile ULONGLONG btnUntil[8]; };

static double s_devUntil[A_COUNT];
static WORD s_devPad; static double s_devPadUntil;
void InputDevPad(WORD buttons, float seconds) { s_devPad = buttons; s_devPadUntil = RealNow() + seconds; }
// Back modifier (see the top): held now, another button joined during this hold, release to hand to the game
static volatile LONG s_backHeld, s_backCombo, s_backTapFrames;
static int s_backTaps, s_backCombos;

void InputDevSet(int act, float seconds) {
    if (act >= 0 && act < A_COUNT) s_devUntil[act] = RealNow() + seconds;
}

static bool GameFocused() {
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}
bool InputFocused() { return GameFocused(); }

static int MouseIndex(int vk) {
    switch (vk) {
    case VK_LBUTTON: return 0; case VK_RBUTTON: return 1; case VK_MBUTTON: return 2;
    case VK_XBUTTON1: return 3; case VK_XBUTTON2: return 4; default: return -1;
    }
}

static bool KeyHeld(int vk, bool focused, const BgInput* bi);
bool InputKey(int vk) {
    const BgInput* bi = g_api ? (const BgInput*)g_api->GetShared("DK_Bridge.input") : nullptr;
    return KeyHeld(vk, GameFocused(), bi);
}
static bool KeyHeld(int vk, bool focused, const BgInput* bi) {
    if (!vk) return false;
    if (focused && (GetAsyncKeyState(vk) & 0x8000)) return true;
    if (!bi || bi->magic != 0x44424749 || !bi->on) return false;
    const ULONGLONG now = GetTickCount64();
    int mi = MouseIndex(vk);
    if (mi >= 0) return now < bi->btnUntil[mi];
    UINT dik = MapVirtualKeyA((UINT)vk, 0 /*MAPVK_VK_TO_VSC*/);
    if (vk == VK_RSHIFT) dik = 0x36;
    return dik && dik < 256 && now < bi->keyUntil[dik];
}

void InputPoll(float realDt) {
    const bool focused = GameFocused();
    const BgInput* bi = g_api ? (const BgInput*)g_api->GetShared("DK_Bridge.input") : nullptr;
    bool kb[A_COUNT] = {};
    const int vks[A_COUNT] = { cfg.kFire, cfg.kAim, cfg.kVent, cfg.kGrenade, cfg.kAbility, cfg.kSprint, cfg.kShoulder,
                               cfg.kNext, cfg.kPrev, cfg.kWheel, cfg.kToggle, cfg.kRoll, cfg.kSwap, cfg.kCrouch };
    bool anyKb = false;
    for (int a = 0; a < A_COUNT; a++) { kb[a] = KeyHeld(vks[a], focused, bi); anyKb |= kb[a]; }

    bool pd[A_COUNT] = {};
    bool anyPad = false;
    g_in.padRX = g_in.padRY = 0.0f;
    const WORD prevButtons = g_in.padButtons;
    g_in.padButtons = 0;
    const bool devPad = RealNow() < s_devPadUntil;
    if (cfg.gamepad && ((s_xinput && focused && GetTickCount64() >= s_padRetry) || devPad)) {
        BYTE st[16] = {};   // XINPUT_STATE: packet, buttons, LT, RT, LX, LY, RX, RY
        const bool got = s_xinput && focused && GetTickCount64() >= s_padRetry && s_xinput(0, st) == ERROR_SUCCESS;
        if (got || devPad) {
            WORD b; memcpy(&b, st + 4, 2);
            if (devPad) b |= s_devPad;
            g_in.padButtons = b;
            const BYTE lt = st[6], rt = st[7];
            short rx, ry; memcpy(&rx, st + 12, 2); memcpy(&ry, st + 14, 2);
            const bool back = (b & 0x0020) != 0;
            pd[A_FIRE] = rt > 90;
            pd[A_AIM] = lt > 90;
            pd[A_VENT] = (b & 0x4000) && !back;       // X
            pd[A_GRENADE] = (b & 0x0200) && !back;    // RB (Back+RB is DK_VaderSwap's)
            pd[A_ABILITY] = (b & 0x0100) && !back;    // LB
            pd[A_SPRINT] = (b & 0x0040) != 0;         // left stick click
            pd[A_SHOULDER] = (b & 0x0080) != 0;       // right stick click
            pd[A_NEXT] = (b & 0x0008) && back;        // Back + D-pad right
            pd[A_PREV] = (b & 0x0004) && back;        // Back + D-pad left
            pd[A_WHEEL] = (b & 0x0001) && back;       // Back + D-pad up (opens the class wheel)
            pd[A_TOGGLE] = cfg.pToggle && (b & cfg.pToggle) == cfg.pToggle;
            pd[A_ROLL] = (b & 0x2000) && !back;       // B (the game's Force Push: muted while armed)
            pd[A_SWAP] = (b & 0x8000) && !back;       // Y (the game's Lightning: muted while armed)
            pd[A_CROUCH] = (b & 0x0002) && !back;     // D-pad down (Back + D-pad down = blaster toggle)
            // Back modifier: another button during the hold makes it a combo (no options menu on release)
            if (back) {
                if (!s_backHeld) { s_backHeld = 1; s_backCombo = 0; }
                if (b & ~0x0020) { if (!s_backCombo) s_backCombos++; s_backCombo = 1; }
            }
            for (int a = 0; a < A_COUNT; a++) anyPad |= pd[a];
            const float fx = rx / 32767.0f, fy = ry / 32767.0f;
            if (fabsf(fx) > 0.24f || fabsf(fy) > 0.24f || lt > 30 || rt > 30) anyPad = true;
            g_in.padRX = fx; g_in.padRY = fy;
        } else s_padRetry = GetTickCount64() + 2000;   // no controller: don't query a missing pad every frame
    }
    if (s_backHeld && !(g_in.padButtons & 0x0020)) {   // Back released: a clean tap goes to the game
        if (!s_backCombo) { s_backTapFrames = 2; s_backTaps++; }
        s_backHeld = 0;
    }
    g_in.padPressed = (WORD)(g_in.padButtons & ~prevButtons);
    if (anyPad) g_in.pad = true;
    else if (anyKb) g_in.pad = false;

    // edges from the devices' own state: an open menu clears g_in.held every frame (nothing fires meanwhile), and
    // edges taken from that cleared value made a held key a new press every frame (] in the wheel flipped pages)
    static bool down[A_COUNT];
    const double now = RealNow();
    for (int a = 0; a < A_COUNT; a++) {
        bool h = kb[a] || pd[a] || now < s_devUntil[a];
        g_in.pressed[a] = h && !down[a];
        g_in.released[a] = !h && down[a];
        down[a] = h;
        g_in.held[a] = h;
        g_in.holdTime[a] = h ? g_in.holdTime[a] + realDt : 0.0f;
    }
}

// ---------------------------------------------------------------------------------------------- SetPadState hook
// thiscall (ctrl, lx, ly, lz, rx, ry, rz, 6 more axes, u8* buttons, n), ret 0x3C. Two vtables share it.
typedef void(__thiscall* SetPad_t)(void* self, int ctrl, DWORD lx, DWORD ly, DWORD lz, float rx, float ry, DWORD rz,
                                   DWORD a7, DWORD a8, DWORD a9, DWORD a10, DWORD a11, DWORD a12, BYTE* buttons, int n);
static SetPad_t s_setPad;
static volatile LONG s_mute;
static double s_dashUntil;
static float s_moveX, s_moveY;      // the movement stick as the game got it last
static int s_dashFrames;
static float s_devRx, s_devRy; static double s_devLookUntil;
void InputDevLook(float rx, float ry, float seconds) { s_devRx = rx; s_devRy = ry; s_devLookUntil = RealNow() + seconds; }
static float s_devLx, s_devLy; static double s_devStickUntil;
void InputDevStick(float x, float y, float seconds) { s_devLx = x; s_devLy = y; s_devStickUntil = RealNow() + seconds; }
static int s_muted;
static volatile LONG s_capture;
static float s_menuMx, s_menuMy;
void InputCapture(bool on) {
    if (on && !s_capture) { s_menuMx = s_menuMy = 0.0f; }
    s_capture = on ? 1 : 0;
}
bool InputCaptured() { return s_capture != 0; }

// Look input for the aim camera. Controller: the right stick exactly as the game received it (raw XInput / 32767, no
// dead zone). Keyboard + mouse: the raw DirectInput mouse counts of this poll (InputMgr mouse buffer, filled by the
// mouse poll just before SetPadState), plus the game's own mouse invert options (Config.xml IsMouseX/YAxisInverted).
static float s_lookRx, s_lookRy, s_mouseX, s_mouseY;
static bool s_kbm, s_invX, s_invY;
static uint32_t s_mouseOff, s_mouseAcqOff;   // from the mouse poll's code (0xE1C / 0xCF4 in 1.2)
static bool s_mapperOk;

static void ReadLook(BYTE* self, float rx, float ry) {
    BYTE* map = s_mapperOk ? *(BYTE**)(self + sym::field::InputMgr::mapper) : nullptr;
    BYTE* flags = map ? *(BYTE**)(map + sym::field::InputMapper::config) : nullptr;
    s_kbm = flags && flags[0];
    if (s_kbm) {
        s_invX = flags[1] != 0; s_invY = flags[2] != 0;
        if (s_mouseOff && self[s_mouseAcqOff]) {
            s_mouseX += (float)*(int*)(self + s_mouseOff);
            s_mouseY += (float)*(int*)(self + s_mouseOff + 4);
        }
        s_lookRx = s_lookRy = 0.0f;
    } else { s_lookRx = rx; s_lookRy = ry; }
}

void InputDevMouse(float dx, float dy) { s_mouseX += dx; s_mouseY += dy; }

void InputMenuLook(float* mouseX, float* mouseY) {
    *mouseX = s_menuMx + (s_invX ? -s_mouseX : s_mouseX);
    *mouseY = s_menuMy + (s_invY ? -s_mouseY : s_mouseY);
    s_menuMx = s_menuMy = 0.0f; s_mouseX = s_mouseY = 0.0f;
}

void InputLook(float* stickX, float* stickY, float* mouseX, float* mouseY, bool* kbm) {
    if (s_capture) {   // a menu has the look input: the aim camera holds still, the counts wait for the menu
        s_menuMx += s_invX ? -s_mouseX : s_mouseX; s_menuMy += s_invY ? -s_mouseY : s_mouseY;
        s_mouseX = s_mouseY = 0.0f;
        *stickX = *stickY = *mouseX = *mouseY = 0.0f; *kbm = s_kbm;
        return;
    }
    float sx = s_lookRx, sy = s_lookRy;
    if (RealNow() < s_devLookUntil) { sx += s_devRx; sy += s_devRy; }
    *stickX = sx; *stickY = sy;
    *mouseX = s_invX ? -s_mouseX : s_mouseX;
    *mouseY = s_invY ? -s_mouseY : s_mouseY;
    s_mouseX = s_mouseY = 0.0f;
    *kbm = s_kbm;
}

// game button indices muted while armed: 7 CenterCamera (right stick click = our shoulder swap), 8 ForceDash, 9 LockOn,
// 11 Push, 12 Attack, 13 Lightning, 14 Block, 15 Grip
static const int kMuted[] = { 7, 8, 9, 11, 12, 13, 14, 15 };
static const int kMenuMuted[] = { 0, 7, 8, 9, 10, 11, 12, 13, 14, 15 };   // a menu open: + action camera, jump

static void __fastcall HkSetPad(void* self, void*, int ctrl, DWORD lx, DWORD ly, DWORD lz, float rx, float ry, DWORD rz,
                                DWORD a7, DWORD a8, DWORD a9, DWORD a10, DWORD a11, DWORD a12, BYTE* buttons, int n) {
    if (ctrl == 0) {
        __try { ReadLook((BYTE*)self, rx, ry); } __except (EXCEPTION_EXECUTE_HANDLER) { s_kbm = false; }
        if (RealNow() < s_devStickUntil) { memcpy(&lx, &s_devLx, 4); memcpy(&ly, &s_devLy, 4); }
        memcpy(&s_moveX, &lx, 4); memcpy(&s_moveY, &ly, 4);
        __try {   // controller Back modifier: PlayerOptions (5) and D-pad up's action camera (0) wait for the release
            if (buttons && n > 5) {
                if (s_backHeld) { buttons[5] = 0; buttons[0] = 0; }
                else if (s_backTapFrames > 0) { buttons[5] = s_backTapFrames == 2 ? 1 : 0; s_backTapFrames--; }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        if (s_capture) {
            __try {
                if (buttons) for (int i : kMenuMuted) if (i < n) buttons[i] = 0;
                if (s_mouseOff && self) { *(int*)((BYTE*)self + s_mouseOff) = 0; *(int*)((BYTE*)self + s_mouseOff + 4) = 0; }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
            rx = ry = 0.0f;
        }
        // only in gameplay: the pause menu, cutscenes and the game's own menus read the same buttons (the pause menu's
        // Esc / B back sat on a muted index, so it could not be closed while armed)
        if (s_mute && InGameplay()) {
            __try {
                if (buttons)
                    for (int i : kMuted) if (i < n && buttons[i]) { buttons[i] = 0; s_muted++; }
                if (buttons && n > 8 && RealNow() < s_dashUntil) { buttons[8] = 1; s_dashFrames++; }   // roll = the game's Force Dash
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
            rx = ry = 0.0f;   // the aim camera owns the look axes while armed
        }
    }
    s_setPad(self, ctrl, lx, ly, lz, rx, ry, rz, a7, a8, a9, a10, a11, a12, buttons, n);
}

void InputSetMute(bool armed) { s_mute = armed ? 1 : 0; if (!armed) s_dashUntil = 0; }
void InputMoveStick(float* x, float* y) {
    *x = s_moveX == s_moveX ? s_moveX : 0.0f; *y = s_moveY == s_moveY ? s_moveY : 0.0f;   // NaN guard
    if (*x < -1.0f) *x = -1.0f; if (*x > 1.0f) *x = 1.0f; if (*y < -1.0f) *y = -1.0f; if (*y > 1.0f) *y = 1.0f;
}
int InputDashCount() { return s_dashFrames; }
const char* InputPadStatus() {
    static char b[160];
    sprintf_s(b, "back held %d combo %d taps %d combos %d capture %d buttons %04X", (int)s_backHeld, (int)s_backCombo,
              s_backTaps, s_backCombos, (int)s_capture, g_in.padButtons);
    return b;
}
void InputDash(float seconds) { s_dashUntil = RealNow() + seconds; }
bool InputHookOk() { return s_setPad != nullptr; }
int InputMutedCount() { return s_muted; }
int InputMuteOn() { return (int)s_mute; }

DWORD* UniqueSlot(HMODULE exe, uint32_t fn, int* count);

bool InputInstall(HMODULE exe, const Range& text) {
    HMODULE xi = LoadLibraryA("xinput1_3.dll");
    s_xinput = xi ? (XInputGetState_t)GetProcAddress(xi, "XInputGetState") : nullptr;
    uint8_t* f = FindSym(text, "Blaster_SetPadState");
    if (!f) { Log("SetPadState not found - game buttons are not muted while armed"); return false; }
    Range rd = SectionRange(exe, ".rdata");
    int n = 0;
    void* hk = (void*)&HkSetPad;
    for (BYTE* p = rd.lo; p + 4 <= rd.hi; p += 4)
        if (*(DWORD*)p == (DWORD)f) { if (WriteMem(p, &hk, 4)) n++; }
    if (!n) { Log("SetPadState vtable slots not found"); return false; }
    s_setPad = (SetPad_t)f;
    // mouse buffer + acquired flag: lea edi,[esi+imm32] at +0x31, mov byte [esi+imm32],1 at +0x38 of the mouse poll
    if (uint8_t* mp = FindSym(text, "Blaster_MousePoll")) {
        if (mp[0x31] == 0x8D && mp[0x32] == 0xBE && mp[0x38] == 0xC6 && mp[0x39] == 0x86 && mp[0x3E] == 0x01) {
            s_mouseOff = *(uint32_t*)(mp + 0x33);
            s_mouseAcqOff = *(uint32_t*)(mp + 0x3A);
        }
    }
    {
        Range r = { (uint8_t*)sym::Input_IsKBAndMouseGet, (uint8_t*)sym::Input_IsKBAndMouseGet + 16 };
        s_mapperOk = FindUnique(r, sym::Input_IsKBAndMouseGet_bytes) == (uint8_t*)sym::Input_IsKBAndMouseGet;
    }
    if (s_mouseOff > 0x4000 || s_mouseAcqOff > 0x4000) s_mouseOff = s_mouseAcqOff = 0;
    Log("input: SetPadState hooked in %d vtable(s); XInput %s; mouse look %s (buffer +0x%X, acquired +0x%X); input mode %s",
        n, s_xinput ? "ok" : "missing", s_mouseOff ? "ok" : "UNAVAILABLE", s_mouseOff, s_mouseAcqOff,
        s_mapperOk ? "ok" : "UNAVAILABLE (controller look only)");
    return true;
}

// ---------------------------------------------------------------------------------------------- mouse lock
// The game reads the mouse through DirectInput (non-exclusive) and leaves the Windows pointer free: in a long fight it
// drifted to a screen edge or onto another monitor, and a click there took the focus away from the game. While the
// game window is in front and a level is being played, a small thread keeps the pointer in a box at the window centre
// (DirectInput still sees every movement). Menus, the pause screen, a cutscene, another window in front, a minimised
// game and DK_Bridge background test games are left alone; only our own clip is ever lifted. A thread, because the
// game thread stalls during loads and videos.
static volatile LONG s_lockPlay, s_lockStop, s_lockOn;
static HANDLE s_lockThread;
static RECT s_lockRect;
static volatile LONG s_lockClips;

static bool LockRect(RECT* r) {
    if (!s_lockPlay) return false;
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (!fg) return false;
    GetWindowThreadProcessId(fg, &pid);
    if (pid != GetCurrentProcessId() || IsIconic(fg)) return false;
    RECT c;
    if (!GetClientRect(fg, &c) || c.right - c.left < 64 || c.bottom - c.top < 64) return false;
    POINT m = { (c.left + c.right) / 2, (c.top + c.bottom) / 2 };
    if (!ClientToScreen(fg, &m)) return false;
    r->left = m.x - 8; r->top = m.y - 8; r->right = m.x + 8; r->bottom = m.y + 8;
    return true;
}
static void LockRelease() {
    RECT cur;
    if (GetClipCursor(&cur) && !memcmp(&cur, &s_lockRect, sizeof cur)) ClipCursor(nullptr);
    s_lockOn = 0;
}
static DWORD WINAPI LockThread(void*) {
    while (!s_lockStop) {
        RECT r;
        if (LockRect(&r)) {
            RECT cur;
            if (!s_lockOn || !GetClipCursor(&cur) || memcmp(&cur, &r, sizeof r)) {   // Windows drops a clip on focus changes
                if (ClipCursor(&r)) { s_lockRect = r; s_lockOn = 1; InterlockedIncrement(&s_lockClips); }
            }
        } else if (s_lockOn) LockRelease();
        Sleep(50);
    }
    if (s_lockOn) LockRelease();
    return 0;
}
// ---------------------------------------------------------------------------------------------- lone Alt
// Alt is the crouch key: Windows turns the key-up of a lone Alt into the window menu's keyboard mode (WM_SYSKEYUP ->
// SC_KEYMENU), which can hold a windowed game's message loop until the next key. While a level is played with Alt
// bound to an action, that key-up never reaches the game's window procedure (the game reads keys through DirectInput;
// Alt+Tab, Alt+F4 and Alt+Enter come from other messages and keep working). A message hook on the game thread only.
static HHOOK s_altHook;
static volatile LONG s_altEat, s_altEaten;
static bool s_altFailed;

static LRESULT CALLBACK AltHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && s_altEat) {
        MSG* m = (MSG*)lp;
        if (m->message == WM_SYSKEYUP && (m->wParam == VK_MENU || m->wParam == VK_LMENU || m->wParam == VK_RMENU)) {
            m->message = WM_NULL;
            if (wp == PM_REMOVE) InterlockedIncrement(&s_altEaten);
        }
    }
    return CallNextHookEx(s_altHook, code, wp, lp);
}
static bool AltBound() {
    const int vks[] = { cfg.kFire, cfg.kAim, cfg.kVent, cfg.kGrenade, cfg.kAbility, cfg.kSprint, cfg.kShoulder,
                        cfg.kNext, cfg.kPrev, cfg.kWheel, cfg.kRoll, cfg.kSwap, cfg.kCrouch };
    for (int vk : vks) if (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU) return true;
    return false;
}

void InputMouseLock(bool gameplay) {
    const BgInput* bi = g_api ? (const BgInput*)g_api->GetShared("DK_Bridge.input") : nullptr;
    const bool bgTest = bi && bi->magic == 0x44424749 && bi->on;   // a background test game never takes the pointer
    s_lockPlay = cfg.enabled && cfg.lockMouse && gameplay && !bgTest ? 1 : 0;
    if (s_lockPlay && !s_lockThread) {
        s_lockStop = 0;
        s_lockThread = CreateThread(nullptr, 0, &LockThread, nullptr, 0, nullptr);
        if (!s_lockThread) Log("mouse lock: thread not started (%lu)", GetLastError());
    }
    s_altEat = cfg.enabled && gameplay && AltBound() ? 1 : 0;
    if (s_altEat && !s_altHook && !s_altFailed) {   // called on the game thread, which pumps the window's messages
        HMODULE self = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&AltHook, &self);
        s_altHook = SetWindowsHookExA(WH_GETMESSAGE, &AltHook, self, GetCurrentThreadId());
        if (!s_altHook) { s_altFailed = true; Log("alt guard: message hook not set (%lu) - a lone Alt may open the window menu", GetLastError()); }
    }
}
void InputShutdown() {
    s_altEat = 0;
    if (s_altHook) { UnhookWindowsHookEx(s_altHook); s_altHook = nullptr; }
    s_lockPlay = 0;
    if (s_lockThread) {
        s_lockStop = 1;
        if (WaitForSingleObject(s_lockThread, 2000) != WAIT_OBJECT_0) Log("mouse lock: thread did not stop in time");
        CloseHandle(s_lockThread);
        s_lockThread = nullptr;
    }
    if (s_lockOn) LockRelease();
}
const char* InputLockStatus() {
    static char b[160];
    _snprintf_s(b, sizeof b, _TRUNCATE, "mouse lock want %ld on %ld clips %ld thread %d | alt guard %ld hook %d eaten %ld",
                s_lockPlay, s_lockOn, s_lockClips, s_lockThread != nullptr, s_altEat, s_altHook != nullptr, s_altEaten);
    return b;
}

}  // namespace dkb
