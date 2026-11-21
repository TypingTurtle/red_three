// Game-side hooks for Rebel Strike (GLRE64): the game's cameras widened for the display
// (hle_ViewportAspect). The rest of the port options Rogue Leader has at the source (cheats,
// passcode unlocks, cutscene skip) are not here yet: what the runtime asks of them answers
// "not available".
#include "funcs.h"
#include "runtime.h"
#include "savestate.h"

#include <atomic>
#include <cmath>
#include <cstring>

namespace rt {

bool cinematicSkip() { return false; }

// Widescreen at the source. Aurora widens perspective projections on a wider target, but the
// game culled with its own 4:3 view volume: at 21:9 the ground and objects at the screen's sides
// were missing (issue #1). The camera update (0x7F3AA460, camera r3) takes its aspect (+0x90 =
// this * the camera's factor +0x8C) from ViewportAspect (0x7F3AF42C; viewport r3): the viewport's
// size times the screen's pixel aspect (a 512-wide mission viewport, stretched 1.249 by the VI, is
// 4:3). From it and the fov come the tangents +0x6C/+0x70, the projection (0x7F20B5F0, the
// SDK's MTXPerspective from tan(fovY/2)) and the culling volume (0x7F3AA824: side planes from
// the tangents). So for the two callers that set up 3D cameras - the camera update and
// 0x7F5BC93C, which builds its own projection from it - ViewportAspect answers the display's
// aspect: the game renders and culls for it, and Aurora, told that projection is already wide,
// leaves it alone. A letterboxed frame (cinematics: 304 lines) keeps the game's own, which
// Aurora opens as before. R2_GAME_CULLING=1 keeps the game's own everywhere.
static std::atomic<float> s_displayAspect{4.f / 3.f};

void setGameAspect(double aspect) {
    const float previous = s_displayAspect.exchange(float(aspect), std::memory_order_relaxed);
    if (previous != float(aspect)) logf("wide", "game camera aspect: the display's, %.4f", aspect);
}

constexpr u32 kCameraUpdateRet = 0x7F3AA480u;  // 0x7F3AA47C in the camera update (camera r31)
constexpr u32 kSceneCameraRet = 0x7F5BC940u;   // 0x7F5BC93C: its aspect goes straight to MTXPerspective

void hle_ViewportAspect(Ctx& c) {
    const u32 lr = c.lr;
    const u32 camera = c.r[31];  // (the camera update keeps its camera there; preserved by the call)
    f_7F3AF42C_orig(c);
    if (lr != kCameraUpdateRet && lr != kSceneCameraRet) return;
    static const bool gameCulling = envFlag("R2_GAME_CULLING");
    const double display = s_displayAspect.load(std::memory_order_relaxed);
    const double aspect = c.f[1];
    const u32 lines = viXfbLines();
    if (gameCulling || display < 4.0 / 3.0 + 0.01 || !(aspect > 0.5 && aspect < 8.0) || (lines && lines < 408)) return;
    const double wide = rsingle(aspect * display * 0.75);
    c.f[1] = c.ps1[1] = wide;
    // the projection's aspect: the camera update multiplies by the camera's factor (+0x8C)
    double projection = wide;
    if (lr == kCameraUpdateRet) {
        const u32 bits = rd32(camera + 0x8C);
        float factor;
        memcpy(&factor, &bits, 4);
        if (factor > 0.1f && factor < 10.f) projection = rsingle(wide * factor);
    }
    gfx::noteWidenedAspect(float(projection), true);  // (mid-frame: Aurora gets it at the frame boundary)
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
