// dk_keys.h - key names shared by the DK mods' ini files and DK_ModMenu's key options (header only).
// A key is a Windows virtual key (VK_*). Names: a letter / digit, F1-F24, the mouse buttons LMB RMB MMB MOUSE4 MOUSE5,
// named keys (LSHIFT, SPACE, ENTER, UP, NUM5, [ ...) or 0x.. (any virtual key); NONE = unbound (0).
#pragma once
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

namespace dk {

struct KeyNameEntry { const char* n; int vk; };
static const KeyNameEntry kKeyNameTable[] = {
    {"LMB", VK_LBUTTON}, {"RMB", VK_RBUTTON}, {"MMB", VK_MBUTTON}, {"MOUSE4", VK_XBUTTON1}, {"MOUSE5", VK_XBUTTON2},
    {"LSHIFT", VK_LSHIFT}, {"RSHIFT", VK_RSHIFT}, {"LCTRL", VK_LCONTROL}, {"RCTRL", VK_RCONTROL}, {"LALT", VK_LMENU},
    {"RALT", VK_RMENU}, {"SPACE", VK_SPACE}, {"TAB", VK_TAB}, {"CAPSLOCK", VK_CAPITAL}, {"ENTER", VK_RETURN},
    {"ESC", VK_ESCAPE}, {"BACKSPACE", VK_BACK}, {"INSERT", VK_INSERT}, {"DELETE", VK_DELETE}, {"HOME", VK_HOME},
    {"END", VK_END}, {"PAGEUP", VK_PRIOR}, {"PAGEDOWN", VK_NEXT}, {"UP", VK_UP}, {"DOWN", VK_DOWN}, {"LEFT", VK_LEFT},
    {"RIGHT", VK_RIGHT}, {"[", VK_OEM_4}, {"]", VK_OEM_6}, {";", VK_OEM_1}, {"'", VK_OEM_7}, {",", VK_OEM_COMMA},
    {".", VK_OEM_PERIOD}, {"/", VK_OEM_2}, {"`", VK_OEM_3}, {"-", VK_OEM_MINUS}, {"=", VK_OEM_PLUS}, {"\\", VK_OEM_5},
    {"NUM0", VK_NUMPAD0}, {"NUM1", VK_NUMPAD1}, {"NUM2", VK_NUMPAD2}, {"NUM3", VK_NUMPAD3}, {"NUM4", VK_NUMPAD4},
    {"NUM5", VK_NUMPAD5}, {"NUM6", VK_NUMPAD6}, {"NUM7", VK_NUMPAD7}, {"NUM8", VK_NUMPAD8}, {"NUM9", VK_NUMPAD9},
    {"NUM*", VK_MULTIPLY}, {"NUM+", VK_ADD}, {"NUM-", VK_SUBTRACT}, {"NUM.", VK_DECIMAL}, {"NUM/", VK_DIVIDE},
    {"NUMLOCK", VK_NUMLOCK}, {"SCROLLLOCK", VK_SCROLL}, {"PAUSE", VK_PAUSE}, {"NONE", 0}};

// the virtual key named s (case-insensitive, surrounding spaces ignored); def for an empty or unknown name
inline int KeyParse(const char* s, int def) {
    if (!s) return def;
    while (*s == ' ' || *s == '\t') s++;
    char b[32]; size_t n = 0;
    while (s[n] && n < sizeof b - 1) { b[n] = s[n]; n++; }
    while (n && (b[n - 1] == ' ' || b[n - 1] == '\t' || b[n - 1] == '\r' || b[n - 1] == '\n')) n--;
    b[n] = 0;
    if (!n) return def;
    for (auto& k : kKeyNameTable) if (!_stricmp(b, k.n)) return k.vk;
    if (!_stricmp(b, "ESCAPE")) return VK_ESCAPE;
    if (!_stricmp(b, "RETURN")) return VK_RETURN;
    if ((b[0] == 'F' || b[0] == 'f') && b[1] >= '1' && b[1] <= '9') { int f = atoi(b + 1); if (f >= 1 && f <= 24) return VK_F1 + f - 1; }
    if (!b[1]) { char c = (char)toupper((unsigned char)b[0]); if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c; }
    if (!_strnicmp(b, "0x", 2)) { int v = (int)strtol(b, nullptr, 16); return v > 0 && v < 256 ? v : def; }
    return def;
}

// the name of virtual key vk (what KeyParse reads back) into buf
inline const char* KeyNameOf(int vk, char* buf, size_t size) {
    for (auto& k : kKeyNameTable) if (k.vk == vk) { strncpy_s(buf, size, k.n, _TRUNCATE); return buf; }
    if (vk >= VK_F1 && vk <= VK_F24) sprintf_s(buf, size, "F%d", vk - VK_F1 + 1);
    else if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) sprintf_s(buf, size, "%c", (char)vk);
    else sprintf_s(buf, size, "0x%02X", vk & 0xFF);
    return buf;
}

inline bool KeyIsMouse(int vk) {
    return vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2;
}

// a key a binding can take: not unbound, not a generic modifier (SHIFT / CTRL / ALT: the left / right ones are used),
// not one Windows keeps (the Windows keys, the application key)
inline bool KeyBindable(int vk) {
    return vk > 0 && vk < 256 && vk != VK_SHIFT && vk != VK_CONTROL && vk != VK_MENU && vk != VK_LWIN && vk != VK_RWIN &&
           vk != VK_APPS && vk != VK_CANCEL && vk != VK_SNAPSHOT && vk < 0xE5;   // (0xE5+: IME / OEM specials)
}

}  // namespace dk
