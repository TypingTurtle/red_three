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

## Wide culling (issue #1): water looks off

`R2_WIDE_CULLING=1` widens the game's own camera so objects at the sides of 21:9 and 16:9 screens are no longer culled. It plays through Geonosis without crashing now (the crash was a texgen count of 9, clamped in `patches/aurora/0037`), and the edges look right. But the water looks wrong with it on and fine with it off, so it stays off by default.

### What's known

- The water is drawn with 2 indirect stages and 6 texgens: a ripple map (256x256, drawn every frame at the EFB corner) and most likely a copy of the screen for refraction.
- With wide culling, the camera's projection reaches Aurora already widened (aspect 2.528 at 21:9) and Aurora leaves it alone. Without it, Aurora widens the 4:3 projection itself. If one of the water's texgens maps positions to the screen copy with a matrix built from the camera, the two cases sample the copy differently.
- An F10 dump over water with wide culling on: `gx_frame_3157.txt` (first level, 21:9).

- Screenshot (21:9, first level): the river is almost black, with faint diagonal lines where the water's tiles meet.
- The dump shows no separate reflection camera: the frame draws the ripple map (256x256), a 32x32 and a 48x48 pass, an 8x8 pass at EFB x 514 (most likely the sun-visibility test the game reads back with EFB peeks), then the scene. The water's texgens use world-space matrices (0x84, 0xA8) and post matrices 0x584 / 0x5D8, not a screen projection, so the screen-copy idea above looks less likely.
- Edges cull again while the game turns the player around at a map boundary. That camera probably doesn't go through the camera update (0x7F3AA460) the HLE widens; MTXPerspective's third caller (0x7F59EB10) is a candidate.

### Next

Compare with wide culling off at the same spot (is the water dark there too?). Get a real picture of it (F10 only dumps GX commands), compare the water's texgen matrices with and without wide culling, and see which texgen reads the screen copy.

### Stutter comes first

Sam's call after the Geonosis run: the cutscene and hangar stutter above matters more than the culling. Widescreen isn't worth much if the game doesn't feel smooth.
