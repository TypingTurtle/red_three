// Online multiplayer (experimental build, netplay.cpp): two copies of Red Three run the same game
// in lockstep - co-op or versus, as on one console with two controllers. Each machine sends its
// controller to the other; both feed the same two controllers to the game, a few frames late
// (the input delay, to hide the connection's latency). The guest clock is deterministic for it
// (events.cpp setDeterministic), so the same input gives the same game on both machines.
//
//   host:  red_three --netplay-host [port]          (default port 27911; player 1)
//   join:  red_three --netplay-join <address>[:port] (player 2)
// or R2_NETPLAY=host[:port] / R2_NETPLAY=join:<address>[:port]. The host's memory card and the
// settings that change the game (frame rate, fast loading, cheats...) are sent to the one
// joining; its own card is left alone. R2_NETPLAY_DELAY=<frames> sets the input delay (default 4).
#pragma once
#include "hw/gx_fifo.h"
#include "runtime.h"

namespace rt::netplay {

// from main, after the window and the disc and before the game boots: parses the command line /
// R2_NETPLAY and, for a session, connects (blocking, with a message in the log) and agrees on the
// game. False: no session (or it failed: the game then runs alone).
bool start(int argc, char** argv);
bool active();
// once per controller poll (input::poll, every retrace), after the local controllers are read:
// sends port 1's local state as this player's and fills both players' ports with the states both
// machines use for this poll (waiting for the other machine if its input has not come yet)
void exchange();
std::string status();  // a line for the menu / log ("" when no session)

}  // namespace rt::netplay
