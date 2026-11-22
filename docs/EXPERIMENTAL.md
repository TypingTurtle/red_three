# Experimental features

The `experimental` branch of Red Three holds features that are not ready for the main build. Build it with
`build_experimental.bat` (Windows), which puts it in `Experimental Features\`, apart from the main build and the
Playtesting copy. It has its own user folder there, so its settings and saves don't touch the main build's.

## HOTAS / flight sticks

A joystick that SDL doesn't treat as a gamepad (a HOTAS like a Thrustmaster T.Flight, Logitech X56 or
Extreme 3D Pro, a VKB or Virpil stick) flies the craft. Each axis is mapped on its own:

- **pitch**: the stick's forward and back (axis 1 by default)
- **yaw**: the twist or rudder pedals (axis 3 on a stick with 4 or more axes, otherwise the stick's left and right)
- **throttle**: the lever. In `split` mode, forward of the middle boosts and behind it brakes. In `boost` mode the
  whole travel boosts.
- **buttons**: in order A, B, X, Y, Z, L, R, START; the first hat switch is the D-pad

The first time a stick is seen, Red Three writes `hotas.ini` into the user folder with what it found (the stick's
name, how many axes and buttons it has) and the mapping above. Edit it to change any axis or button. Run with
`R2_HOTAS_LOG=1` to see every axis value and pressed button in the log twice a second, which is the quickest
way to find which number is which on your stick.

A controller still works alongside the stick (for the menus, say). Where the stick is centred, the controller's
stick wins.

## Online multiplayer (co-op and versus)

Two copies of Red Three play one game together: player 1 hosts, player 2 joins. It's the same as two
controllers on one console, so the co-op campaign and every versus mode work.

- Host: start with `--netplay-host` (or `Netplay - Host.bat`). It waits for player 2 on TCP port 27911. For
  play over the internet that port has to be forwarded on the host's router (or use a VPN such as Tailscale or
  ZeroTier, which needs no port forwarding).
- Join: start with `--netplay-join <host's address>` (or edit and run `Netplay - Join.bat`).

Both machines need the same Red Three build and the same disc. The host's memory card and the settings that
change how the game plays (frame rate, fast loading, the cheats) go to player 2 for the session, so player 2
doesn't need the same saves. Player 2's own saves aren't touched.

How it works: each machine sends its controller to the other every frame, and both run the game with both
controllers, a few frames late (the input delay, 4 frames by default: `R2_NETPLAY_DELAY`). The two games stay
the same because the game's clock no longer follows the PC's clock: it moves only with the game itself
(`setDeterministic` in `events.cpp`), so the same input gives the same game on both machines. Every 10 seconds
the two compare a checksum of the game's memory and the log says `DESYNC` if they ever differ.

Limits:

- With a slow connection, both games slow down to wait for the other's input rather than skip ahead.
- Save states, cheat codes and pausing in the menu aren't kept in step; don't use them during a session.

## VR (planned)

The goal: the space missions in VR, cockpit view only, looking around with the headset while flying with a
HOTAS. This one is a long project. Here's the plan, roughly in the order it would be built:

1. **Free look** (no headset needed): the renderer turns every 3D draw of the scene by a head rotation (in view
   space, after the game's camera), so you can look around the cockpit with the mouse or the right stick while
   the craft flies straight on. The HUD stays put. This is the core of VR, and it's useful on a flat screen too.
2. **Culling for the whole view**: the game only draws what's in front of its own camera, so turning the head
   shows empty space. This needs the game's culling widened to a full field of view, which is the same work as
   issue #1's wide culling (and its overflow crash) taken further.
3. **Stereo rendering**: every 3D draw rendered twice, once per eye, with the eyes apart by the headset's IPD.
   Render-to-texture passes (shadows, the water's ripples) can stay mono; only the scene needs both eyes.
4. **OpenXR**: a session with the headset's runtime (SteamVR, Oculus, Windows Mixed Reality) on the D3D12 device
   Dawn renders with, the two eyes' pictures handed to its swapchains, and the head pose from OpenXR each frame
   instead of the mouse.

Dolphin's VR branch did roughly steps 1 to 4 for its own renderer. The work here goes into Aurora (the
renderer) and the bridge between the game and Aurora, much like the widescreen work.
