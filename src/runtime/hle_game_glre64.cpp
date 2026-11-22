// Game-side hooks for Rebel Strike (GLRE64): the game's cameras widened for the display
// (hle_ViewportAspect). The rest of the port options Rogue Leader has at the source (cheats,
// passcode unlocks, cutscene skip) are not here yet: what the runtime asks of them answers
// "not available".
#include "funcs.h"
#include "runtime.h"
#include "savestate.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
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
// Aurora opens as before.
// Off unless R2_WIDE_CULLING=1 (the camera update) or 2 (both callers): with the wider volume the game crashed in the first mission frame
// ("invalid alpha comp 16711935" from its display-list drawing, 0x80008EB8 < 0x7F3B021C): most
// likely one of its fixed-size per-frame buffers overflowing with the extra objects, as Rogue
// Leader's terrain tables did (hle_game_gswe64.cpp). Until that buffer is found and enlarged the
// game keeps its own 4:3 volumes and Aurora widens the picture alone.
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
    // R2_WIDE_CULLING=1: the camera update only; 2: 0x7F5BC93C too (it builds small render-to-
    // texture views - the crash came right after a 96x96 one)
    static const int wideCulling = [] {
        const char* e = getenv("R2_WIDE_CULLING");
        return e ? atoi(e) : 0;
    }();
    const bool gameCulling = wideCulling <= 0 || (lr == kSceneCameraRet && wideCulling < 2);
    const double display = s_displayAspect.load(std::memory_order_relaxed);
    const double aspect = c.f[1];
    const u32 lines = viXfbLines();
    if (gameCulling || display < 4.0 / 3.0 + 0.01 || !(aspect > 0.5 && aspect < 8.0)) return;
    // The picture's shape as the game draws it: 4:3, or in a letterboxed frame (the 304-line
    // cinematics, the turn-around at a map's edge) 4:3 stretched by 480 / lines, which Aurora
    // stretches further to fill the display (letterbox fill). Its camera widens by what the
    // display adds on top; without it the sides were culled again during the turn-around.
    double picture = 4.0 / 3.0;
    if (lines && lines < 408) {
        static const bool noFill = getenv("R2_NO_LETTERBOX_FILL") && atoi(getenv("R2_NO_LETTERBOX_FILL"));
        if (noFill || lines < 200) return;
        picture = 4.0 / 3.0 * 480.0 / double(lines);
    }
    if (display < picture * 1.005) return;  // (the display is no wider than the picture)
    const double wide = rsingle(aspect * display / picture);
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
