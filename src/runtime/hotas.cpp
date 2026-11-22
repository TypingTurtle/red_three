// HOTAS / flight stick support (experimental build): see hotas.h.
//
// hotas.ini (next to settings.ini), every line optional:
//   enabled=1
//   device=T.Flight          part of the stick's name (default: the first joystick that is not a gamepad)
//   port=1                   the GameCube port it plays as (1-4)
//   pitch=1                  axis for the main stick's up / down (the craft's pitch)
//   yaw=3                    axis for the main stick's left / right (the craft's turn)
//   cstick_x=-1  cstick_y=-1 axes for the C stick (-1: none)
//   throttle=2               axis of the throttle lever (-1: none)
//   throttle_mode=split      split: forward of the middle is boost (R), behind it brake (L)
//                            boost: the whole travel is boost (R), idle at the back
//   invert_pitch=0 invert_yaw=0 invert_throttle=0
//   deadzone=5               percent of each axis's travel around its centre
//   button0=A                stick button 0 (as SDL numbers them) is the GameCube's A
//                            (A B X Y Z L R START UP DOWN LEFT RIGHT; none to leave it unused)
//   hat=dpad                 the first hat switch is the D-pad (none: unused)
// R2_HOTAS_LOG=1 logs every axis and the pressed buttons twice a second (to find the numbers).
#include "hotas.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_joystick.h>
#include <dolphin/pad.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "mods.h"
#include "runtime.h"

namespace rt::hotas {
namespace {

struct Config {
    bool enabled = true;
    std::string device;
    int port = 0;  // 0-based
    int pitch = 1, yaw = -1, cstickX = -1, cstickY = -1, throttle = -1;
    bool split = true;
    bool invPitch = false, invYaw = false, invThrottle = false;
    float deadzone = 0.05f;
    std::map<int, u16> buttons;  // SDL button -> PAD_BUTTON_* (0: unused)
    bool hatDpad = true;
    bool yawSet = false, throttleSet = false, buttonsSet = false;
};

u16 buttonBit(const std::string& n) {
    static const struct {
        const char* name;
        u16 bit;
    } names[] = {{"A", PAD_BUTTON_A},       {"B", PAD_BUTTON_B},         {"X", PAD_BUTTON_X},
                 {"Y", PAD_BUTTON_Y},       {"Z", PAD_TRIGGER_Z},        {"L", PAD_TRIGGER_L},
                 {"R", PAD_TRIGGER_R},      {"START", PAD_BUTTON_START}, {"UP", PAD_BUTTON_UP},
                 {"DOWN", PAD_BUTTON_DOWN}, {"LEFT", PAD_BUTTON_LEFT},   {"RIGHT", PAD_BUTTON_RIGHT}};
    std::string u = n;
    std::transform(u.begin(), u.end(), u.begin(), ::toupper);
    for (auto& e : names)
        if (u == e.name) return e.bit;
    return 0;
}

std::string iniPath() { return mods::userDir() + "hotas.ini"; }

Config loadConfig() {
    Config c;
    std::ifstream f(iniPath());
    std::string line;
    while (std::getline(f, line)) {
        if (const auto h = line.find('#'); h != std::string::npos) line.resize(h);
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto trim = [](std::string s) {
            while (!s.empty() && isspace(u8(s.back()))) s.pop_back();
            size_t i = 0;
            while (i < s.size() && isspace(u8(s[i]))) i++;
            return s.substr(i);
        };
        const std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        const int n = atoi(v.c_str());
        if (k == "enabled") c.enabled = n != 0;
        else if (k == "device") c.device = v;
        else if (k == "port") c.port = std::clamp(n, 1, 4) - 1;
        else if (k == "pitch") c.pitch = n;
        else if (k == "yaw") c.yaw = n, c.yawSet = true;
        else if (k == "cstick_x") c.cstickX = n;
        else if (k == "cstick_y") c.cstickY = n;
        else if (k == "throttle") c.throttle = n, c.throttleSet = true;
        else if (k == "throttle_mode") c.split = v != "boost";
        else if (k == "invert_pitch") c.invPitch = n != 0;
        else if (k == "invert_yaw") c.invYaw = n != 0;
        else if (k == "invert_throttle") c.invThrottle = n != 0;
        else if (k == "deadzone") c.deadzone = std::clamp(float(atof(v.c_str())) / 100.f, 0.f, 0.5f);
        else if (k == "hat") c.hatDpad = v != "none";
        else if (k.rfind("button", 0) == 0 && k.size() > 6 && isdigit(u8(k[6]))) {
            c.buttons[atoi(k.c_str() + 6)] = buttonBit(v);
            c.buttonsSet = true;
        }
    }
    return c;
}

// Defaults for a stick with n axes and b buttons (SDL's usual order: X, Y, then throttle /
// twist; a stick with 4+ axes usually has the twist (rudder) on axis 3, the throttle on 2)
void fillDefaults(Config& c, int axes, int buttons) {
    if (!c.yawSet) c.yaw = axes >= 4 ? 3 : 0;
    if (!c.throttleSet) c.throttle = axes >= 3 ? 2 : -1;
    if (!c.buttonsSet) {
        static const u16 order[] = {PAD_BUTTON_A, PAD_BUTTON_B, PAD_BUTTON_X,   PAD_BUTTON_Y,
                                    PAD_TRIGGER_Z, PAD_TRIGGER_L, PAD_TRIGGER_R, PAD_BUTTON_START};
        for (int i = 0; i < buttons && i < int(sizeof order / sizeof order[0]); i++) c.buttons[i] = order[i];
    }
}

void writeTemplate(const char* name, int axes, int buttons, int hats, const Config& c) {
    if (FILE* t = fopen(iniPath().c_str(), "r")) {
        fclose(t);
        return;
    }
    FILE* f = fopen(iniPath().c_str(), "w");
    if (!f) return;
    fprintf(f,
            "# Red Three (experimental): HOTAS / flight stick. Found: %s (%d axes, %d buttons, %d hats).\n"
            "# Axis and button numbers as SDL counts them. Run with R2_HOTAS_LOG=1 to see them move in the log.\n"
            "enabled=1\n# device=%s\nport=1\npitch=%d\nyaw=%d\ncstick_x=-1\ncstick_y=-1\nthrottle=%d\n"
            "# split: forward of the middle boosts (R), behind it brakes (L); boost: the whole travel boosts\n"
            "throttle_mode=split\ninvert_pitch=0\ninvert_yaw=0\ninvert_throttle=0\ndeadzone=5\nhat=dpad\n",
            name, axes, buttons, hats, name, c.pitch, c.yaw, c.throttle);
    for (auto& [b, bit] : c.buttons) {
        const char* n = "none";
        for (const char* s : {"A", "B", "X", "Y", "Z", "L", "R", "START", "UP", "DOWN", "LEFT", "RIGHT"})
            if (buttonBit(s) == bit) n = s;
        fprintf(f, "button%d=%s\n", b, n);
    }
    fclose(f);
    logf("hotas", "wrote %s (edit it to change the axes and buttons)", iniPath().c_str());
}

struct State {
    SDL_Joystick* joy = nullptr;
    SDL_JoystickID id = 0;
    std::string name;
    Config cfg;
    bool configured = false;
    // the last reading (used when the controller lock is busy)
    bool have = false;
    float stickX = 0, stickY = 0, cX = 0, cY = 0, boost = 0, brake = 0;
    u16 buttons = 0;
    u64 nextLook = 0, nextLog = 0;
} s;

float axis(int n, bool invert, float deadzone) {
    if (!s.joy || n < 0 || n >= SDL_GetNumJoystickAxes(s.joy)) return 0.f;
    float v = float(SDL_GetJoystickAxis(s.joy, n)) / 32767.f;
    v = std::clamp(v, -1.f, 1.f);
    if (invert) v = -v;
    const float a = std::abs(v);
    if (a <= deadzone) return 0.f;
    return std::copysign((a - deadzone) / (1.f - deadzone), v);
}

// finds (or keeps) the stick: the first joystick that is not a gamepad, or the one named in hotas.ini
void findStick() {
    if (s.joy && SDL_JoystickConnected(s.joy)) return;
    if (s.joy) {
        logf("hotas", "%s disconnected", s.name.c_str());
        SDL_CloseJoystick(s.joy);
        s.joy = nullptr;
        s.have = false;
    }
    const u64 now = tbHostNow();
    if (now < s.nextLook) return;
    s.nextLook = now + kTbClock;  // look again in a second
    Config cfg = loadConfig();
    if (!cfg.enabled) return;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    for (int i = 0; ids && i < count; i++) {
        const char* n = SDL_GetJoystickNameForID(ids[i]);
        const std::string name = n ? n : "joystick";
        bool match;
        if (!cfg.device.empty()) {
            std::string a = name, b = cfg.device;
            std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            std::transform(b.begin(), b.end(), b.begin(), ::tolower);
            match = a.find(b) != std::string::npos;
        } else {
            match = !SDL_IsGamepad(ids[i]);  // gamepads stay Aurora's
        }
        if (!match) continue;
        SDL_Joystick* j = SDL_OpenJoystick(ids[i]);
        if (!j) continue;
        const int axes = SDL_GetNumJoystickAxes(j), buttons = SDL_GetNumJoystickButtons(j), hats = SDL_GetNumJoystickHats(j);
        if (axes < 2) {
            SDL_CloseJoystick(j);
            continue;
        }
        s.joy = j;
        s.id = ids[i];
        s.name = name;
        fillDefaults(cfg, axes, buttons);
        s.cfg = cfg;
        logf("hotas", "using %s: %d axes, %d buttons, %d hats; pitch axis %d, yaw axis %d, throttle axis %d (%s), port %d",
             name.c_str(), axes, buttons, hats, cfg.pitch, cfg.yaw, cfg.throttle, cfg.split ? "split" : "boost",
             cfg.port + 1);
        writeTemplate(name.c_str(), axes, buttons, hats, cfg);
        break;
    }
    SDL_free(ids);
}

void read() {
    const Config& c = s.cfg;
    s.stickY = -axis(c.pitch, c.invPitch, c.deadzone);  // SDL: forward is negative, the GameCube's up is positive
    s.stickX = axis(c.yaw, c.invYaw, c.deadzone);
    s.cX = axis(c.cstickX, false, c.deadzone);
    s.cY = -axis(c.cstickY, false, c.deadzone);
    s.boost = s.brake = 0;
    if (c.throttle >= 0 && c.throttle < SDL_GetNumJoystickAxes(s.joy)) {
        // levers read forward as -1 on most sticks
        float t = -float(SDL_GetJoystickAxis(s.joy, c.throttle)) / 32767.f;
        if (c.invThrottle) t = -t;
        t = std::clamp(t, -1.f, 1.f);
        if (c.split) {
            if (t > c.deadzone) s.boost = (t - c.deadzone) / (1.f - c.deadzone);
            else if (t < -c.deadzone) s.brake = (-t - c.deadzone) / (1.f - c.deadzone);
        } else {
            s.boost = std::clamp((t + 1.f) * 0.5f, 0.f, 1.f);
            if (s.boost < c.deadzone) s.boost = 0;
        }
    }
    s.buttons = 0;
    const int nb = SDL_GetNumJoystickButtons(s.joy);
    for (auto& [b, bit] : c.buttons)
        if (b < nb && SDL_GetJoystickButton(s.joy, b)) s.buttons |= bit;
    if (c.hatDpad && SDL_GetNumJoystickHats(s.joy) > 0) {
        const u8 h = SDL_GetJoystickHat(s.joy, 0);
        if (h & SDL_HAT_UP) s.buttons |= PAD_BUTTON_UP;
        if (h & SDL_HAT_DOWN) s.buttons |= PAD_BUTTON_DOWN;
        if (h & SDL_HAT_LEFT) s.buttons |= PAD_BUTTON_LEFT;
        if (h & SDL_HAT_RIGHT) s.buttons |= PAD_BUTTON_RIGHT;
    }
    s.have = true;
    static const bool log = getenv("R2_HOTAS_LOG") && atoi(getenv("R2_HOTAS_LOG"));
    const u64 now = tbHostNow();
    if (log && now >= s.nextLog) {
        s.nextLog = now + kTbClock / 2;
        std::string a, b;
        char t[32];
        for (int i = 0; i < SDL_GetNumJoystickAxes(s.joy); i++) {
            snprintf(t, sizeof t, " %d:%+.2f", i, float(SDL_GetJoystickAxis(s.joy, i)) / 32767.f);
            a += t;
        }
        for (int i = 0; i < nb; i++)
            if (SDL_GetJoystickButton(s.joy, i)) b += " " + std::to_string(i);
        logf("hotas", "axes%s; buttons down:%s; hat %u", a.c_str(), b.empty() ? " none" : b.c_str(),
             SDL_GetNumJoystickHats(s.joy) ? SDL_GetJoystickHat(s.joy, 0) : 0);
    }
}

s8 toStick(float v) { return s8(std::lround(std::clamp(v, -1.f, 1.f) * 127.f)); }

}  // namespace

void apply(PADStatus* ports, bool joy) {
    if (joy) {
        findStick();
        if (s.joy) read();
    }
    if (!s.joy || !s.have) return;
    PADStatus& p = ports[s.cfg.port];
    p.err = PAD_ERR_NONE;  // the stick alone plays as a controller on its port
    // the stick's axes replace the controller's where they are off centre; a controller on the
    // same port still works (a gamepad for the menus, the stick for flying)
    if (s.stickX != 0.f || s.stickY != 0.f) {
        p.stickX = toStick(s.stickX);
        p.stickY = toStick(s.stickY);
    }
    if (s.cX != 0.f || s.cY != 0.f) {
        p.substickX = toStick(s.cX);
        p.substickY = toStick(s.cY);
    }
    p.triggerRight = std::max<u8>(p.triggerRight, u8(std::lround(s.boost * 255.f)));
    p.triggerLeft = std::max<u8>(p.triggerLeft, u8(std::lround(s.brake * 255.f)));
    if (s.boost > 0.9f) p.button |= PAD_TRIGGER_R;
    if (s.brake > 0.9f) p.button |= PAD_TRIGGER_L;
    p.button |= s.buttons;
}

std::string deviceName() { return s.joy ? s.name : std::string(); }

}  // namespace rt::hotas
