// HOTAS / flight stick support (experimental build, hotas.cpp): a joystick that is not a gamepad
// (Thrustmaster, Logitech, VKB, Saitek...) flies the craft with its axes mapped one by one -
// pitch and yaw on separate axes (the stick's Y and its twist or pedals), the throttle lever as
// boost / brake - and its buttons mapped to the GameCube's. Configured in hotas.ini next to
// settings.ini (written with the detected axes the first time a stick is seen).
#pragma once
#include <string>

#include "runtime.h"

struct PADStatus;

namespace rt::hotas {

// once per retrace from input::poll, right after PADRead (before the sticks are shaped for the
// game): the stick's axes and buttons go into the configured port. joy: the input thread's
// controller lock is held (SDL's joystick API may be called); without it the last reading is used.
void apply(PADStatus* ports, bool joy);
std::string deviceName();  // the stick in use (empty: none)

}  // namespace rt::hotas
