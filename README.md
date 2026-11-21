# Red Three

Red Three runs **Star Wars: Rogue Squadron III – Rebel Strike** (GameCube, USA) natively on PC.
It's a static recompilation: when you build it, the game's machine code is translated from your
own disc into C++ and compiled into a Windows or Linux program.

It isn't an emulator. This repository contains no game code or data; that all comes from your
disc at build time.

**Downloads:** Windows (zip) and Linux / Steam Deck (AppImage) builds are on the
[Releases](../../releases) page. They contain the game's code in recompiled form, but none of its
data: you still need your own disc image. Start the program and choose it the first time.

It's a fork of **[Red Two](https://github.com/gminustwo/red_two)**, which does the same for
Rogue Leader.

## Credit

Almost all of this is Red Two's work, by [gminustwo](https://github.com/gminustwo) and
contributors: the recompiler, the runtime, graphics, audio, input, menus and launcher. Red Three
adds what Rebel Strike needs on top of that. Every commit before `354ae8b` is Red Two's history.

Red Two's [MIT license](LICENSE) applies unchanged. Its original README is in
[docs/RED_TWO_README.md](docs/RED_TWO_README.md). Red Three isn't affiliated with Red Two's
authors, so please report Red Three problems here, not to them.

## Status

Early, but playable. The game boots, plays its cinematics with sound, saves to a memory card,
and runs missions at 60 fps, including two-player co-op.

Known problems (details in [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md)):

- A grey copy of the last frame shows after some cinematics fade out.
- A green screen can show briefly between levels.
- Widescreen and the cheats in the launcher's Cheats tab were made for Rogue Leader and are
  turned off or unreliable here. The game's own passcodes work.

## What you need

- Your own copy of Rebel Strike, USA version (`GLRE64`), as `.iso`, `.gcm`, `.rvz` or `.wia`.
  This repository contains no game code or data.
- Python 3.
- **Windows:** Visual Studio 2022 Build Tools with the C++ workload and its Clang tools, CMake
  3.25 or newer, Ninja.
- **Linux:** clang, lld, CMake 3.25 or newer, Ninja, and SDL3's build dependencies.

## Building

1. Get the code:

   ```
   git clone --recursive https://github.com/TypingTurtle/red_three
   cd red_three
   ```

2. Extract the code Rebel Strike keeps inside its data files (the cinematics run it). Without
   this step those parts run slowly and the sound breaks up:

   ```
   python tools/extract_resource_code.py "<path to your disc image>" ../rescode
   ```

3. Build. Use the full path to the `rescode` folder from step 2.

   Windows (PowerShell):

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Disc "<path to your disc image>" -BuildDir build\rs -CMakeArgs "-DR2_GAME=GLRE64 -DR2_EXTRA_CODE=<full path to rescode>"
   ```

   If you also have a newer Visual Studio installed, add
   `-VsPath "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"`.

   Linux:

   ```bash
   tools/build_linux.sh --disc "<path to your disc image>" --build-dir build/rs -DR2_GAME=GLRE64 -DR2_EXTRA_CODE=<full path to rescode>
   ```

   The first build takes 30 minutes or more. Later builds only rebuild what changed.

4. Run `build\rs\red_three.exe` (Windows) or `build/rs/red_three` (Linux). The launcher asks for
   your disc image the first time.

The C++ a build generates from your disc goes into `build/`. Don't commit or share it.

Without `-DR2_GAME=GLRE64` you get Red Two for Rogue Leader instead.

### Steam Deck

The Deck runs the Linux build, packed into a single AppImage file. Build it on a Linux PC, or on
Windows inside WSL2 with Ubuntu 24.04 (the Deck needs SteamOS 3.6 or newer):

1. Build and pack it in one step (it installs the packages it needs, so it asks for your
   password once):

   ```bash
   tools/steamdeck_build.sh "<path to your disc image>"
   ```

   This makes `RedThree-x86_64.AppImage` next to your disc image. The steps it runs are in the
   script if you'd rather do them by hand.
2. Copy the AppImage and your disc image into the same folder on the Deck (or the disc image into
   `~/Emulation/roms/gc`), make the AppImage executable, and add it to Steam as a non-Steam game.

Settings, saves and logs are in `~/.local/share/Red Three/` on the Deck. A ready-made AppImage is
on the [Releases](../../releases) page if you'd rather not build it.

## Playing

Settings are in the launcher and in the in-game menu (F1).

- **Controllers:** Xbox and PlayStation controllers and most others work. On an Xbox controller
  the face buttons match by letter (Xbox B is GameCube B). Buttons can be remapped on the
  Controls page.
- **Keyboard:** WASD or the arrow keys fly, Space is A, Left Shift is B, E is X, Q is Y, R is Z,
  1 and 2 are the L and R triggers, Enter is Start, T/F/G/H are the D-pad and I/J/K/L the C-stick.
- **Co-op and versus:** plug in a second controller and it plays player 2. To use the keyboard
  for player 2 instead, set Controls → Multiplayer → Player 2 to "Keyboard"; one controller and
  a keyboard is then enough for two players.
- **Graphics:** internal resolution, anti-aliasing and anisotropic filtering are on the Display
  page. HD texture packs in Dolphin's format load from the Mods page.
- **Keys:** F1 menu, F11 or Alt+Enter fullscreen, F5 save state, F8 load state, F6/F7 change
  save slot, F10 saves the next two frames' graphics commands to the logs folder (for bug
  reports).

## Reporting a problem

Open an issue with:

- what happened, and where in the game
- a screenshot if it's a graphics problem
- the session log from that run: `user\logs` next to `red_three.exe` on Windows,
  `~/.local/share/Red Three/logs` on Linux

## Legal

Red Three is released under the [MIT License](LICENSE), as Red Two is. It covers this
repository's own code only, not the game's code that a build (and the downloads) contain in
recompiled form.

This repository contains no code or assets from the game. Star Wars, Rogue Leader and Rebel
Strike are trademarks of Lucasfilm Ltd.; GameCube is a trademark of Nintendo. This project isn't
affiliated with or endorsed by them.
