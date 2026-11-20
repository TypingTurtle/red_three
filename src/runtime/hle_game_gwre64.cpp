// Game-side hooks for Rebel Strike (GWRE64). None yet: the game runs as it is, without the port
// options Rogue Leader has at the source (cheats, passcode unlocks, cutscene skip, the game's
// cameras widened). What the rest of the runtime asks of this file answers "not available".
#include "runtime.h"
#include "savestate.h"

namespace rt {

bool cinematicSkip() { return false; }

// Widescreen comes from Aurora alone (it widens perspective projections); the game still culls
// for 4:3, so objects at the screen's sides may pop in late.
void setGameAspect(double aspect) {
    static double s_logged = 0;
    if (aspect != s_logged) {
        s_logged = aspect;
        logf("wide", "display aspect %.4f (the game's own cameras stay 4:3 in this build)", aspect);
    }
}

namespace unlocks {
const char* name(int) { return ""; }
const char* help(int) { return ""; }
int state(int) { return -1; }
void set(int, bool) {}
}  // namespace unlocks

void hleSaveState(state::Writer& w) { w.section("HLE "); }
void hleLoadState(state::Reader& r) { r.section("HLE "); }
void hleAfterLoad() {}

}  // namespace rt
