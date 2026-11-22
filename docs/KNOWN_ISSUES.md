# Known issues

Problems that are understood but not properly fixed yet. Once Issues are enabled on GitHub, these move there.

## Hangar shadows flicker with the exact box offset / viewport placement

The hangar's shadows (co-op and single-player ship select) flicker when drawing areas are placed exactly where the GameCube puts them. The water's ripple map needs that exact placement, so for now Red Three uses it for the ripple map only and the old placement everywhere else (commit 343ddb3).

### Background

The game sets the scissor box offset field to 171. The SDK writes it as `(offset + 342) >> 1`, so that means no offset. Dolphin places viewports at `xOrig - wd - 2 * field`, which agrees.

Red Three's bridge (`src/runtime/gfx/gx_bridge.cpp`) used to read the field as `2 * field - 340`, which gives an offset of 2. Aurora reads viewport origins with a bias of 340 instead of 342. The bridge made up for the 2 pixels by shifting the projection inside a viewport that sat 2 pixels lower and to the right. Geometry landed in the right place, but the first two rows and columns of every viewport were clipped and never drawn.

- **Water:** the ripple map is drawn every frame into a 256x256 viewport at the EFB's corner, copied to IA8 and tiled across the water. The undrawn rows and columns showed as a line along every tile edge. The exact placement fixes it.
- **Hangar shadows:** with the exact placement everywhere, they flicker and show as blocky dark squares on the floor, at 1x, 3x and "Window size". With the old placement they're fine.

### What's known

Comparing F10 captures from the two modes, the bridge sends Aurora identical scissors and equivalent viewports and projections. The only difference is which edge pixels get drawn.

The shadow silhouettes are drawn into 96x96 and 224x224 viewports at the EFB's corner, with a scissor one pixel inside (343-436 for 96x96). They're copied twice (clear off, then clear on), plus a 96x96 pass at EFB x=512. With the old placement, the silhouettes have a 2-pixel empty border at the top and left; with the exact placement, 1 pixel. My guess is that something downstream needs the wider border, such as how the shadow textures are reduced or sampled near their edges. Not confirmed.

### To reproduce

- `R2_BOXBIAS=342` places every viewport exactly (`run_exact_offset.bat` on the test PC).
- Go to the hangar or ship select and watch the floor shadows.
- F10 dumps include `SENT` lines showing what the bridge passes to Aurora.

### Fix

Find what in Aurora or the bridge depends on the old placement for the shadow passes, then use the exact placement everywhere and remove `rippleMapViewport()` and the `legacyBoxBias()` path.

## Short pauses at cuts into cutscenes and at the hangar launch

The picture holds for about 0.13–0.15 s when a mission cuts to a cutscene (Relics of Geonosis: the ships arriving, the transport leaving) and for a few frames at the hangar launch. It's noticeable at 60 fps.

### What's known

- The game does this itself: it stops drawing while it loads the cutscene's data and sets it up, switches video modes (VI off, then 304-line letterbox: two retraces), and pages code and data in from ARAM. A GameCube pauses (or shows black) here too, for longer.
- Red Three made it much longer; fixed in November 2026: the timer thread slept ~0.5 ms after every event, so each of the game's ~640 back-to-back 4 KiB disc reads took that long (mission load 2.3 s → 0.4 s, the cut 0.5 s → 0.15 s); fast loading's disc command overhead is 0.05 ms; frames the game draws while the VI is off are shown; with widescreen, the one frame drawn for the old XFB height at a cut is skipped.
- Tested and ruled out: keeping cutscenes letterboxed in widescreen (`R2_NO_LETTERBOX_FILL=1`), near-instant disc reads (`R2_DVD_OVERHEAD=5`: removes the small hitch at the hangar launch, ~18 ms off the cut).
- `R2_STALL_MS=<ms>` with `R2_STALL_DETAIL=1` reports short hitches with every function's share; `R2_COPY_LOG=1` shows the frames around a cut.

### Fix ideas

Have the game load the next cutscene in the background while it keeps playing (prefetch its resources ahead of the trigger), or come back with fresh ideas once more of the game is understood.
