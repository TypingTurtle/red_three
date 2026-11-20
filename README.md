# Red Three

An experimental fork of **[Red Two](https://github.com/gminustwo/red_two)** that aims to bring
**Star Wars: Rogue Squadron III – Rebel Strike** (GameCube, USA, `GWRE64`) to PC the same way
Red Two brings Rogue Leader.

## Credit where it's due

**Red Three is not original work.** Almost all of it is Red Two, built by
[gminustwo](https://github.com/gminustwo) and Red Two's contributors: the static recompiler,
the runtime that stands in for the GameCube's hardware, the virtual-memory pager, the MusyX
audio emulation, the Aurora graphics bridge and its patches, the launcher and menus, save
states, mods, cheats and RetroAchievements support. None of that is ours, and nothing in this
repository claims otherwise.

What this fork adds is comparatively small: the changes needed to point Red Two's machinery at
a second game, and the addresses found in Rebel Strike's code (see
[What Red Three changes](#what-red-three-changes)). Every commit after Red Two's
`354ae8b` is the fork's; everything before it is Red Two's own history.

- Red Two's original README, unchanged: [docs/RED_TWO_README.md](docs/RED_TWO_README.md)
- Red Two's license (MIT, copyright its authors) applies to this repository and is kept as is:
  [LICENSE](LICENSE)
- Red Two builds on [Aurora](https://github.com/encounter/aurora) (rendering, window, input) and
  [rcheevos](https://github.com/RetroAchievements/rcheevos) (RetroAchievements), and so does
  this fork.

This fork is not affiliated with or endorsed by Red Two's authors. Please don't report Red
Three problems to Red Two; open them here.

## Status

**Early and untested: Red Three has not booted Rebel Strike yet.**

- Rebel Strike's code recompiles completely: about 19,900 functions, no untranslated
  instructions, and the generated C++ compiles.
- The runtime has what the game needs to start: its virtual-memory pager, the OS thread and
  context functions, the scheduler's idle loop, debug output, and MusyX's audio hooks.
- Whether it actually boots, and how far it gets, is the next thing to find out.

Things to expect:

- **Graphics problems.** Red Two's renderer patches were written for Rogue Leader.
- **Split-screen.** Rebel Strike's co-op and versus modes draw two views; Rogue Leader only
  ever draws one, so these will need their own work.
- **No game-side extras yet.** Rogue Leader's cheats, passcode unlocks, cutscene skip and
  widescreen culling fixes are tied to that game's code. For Rebel Strike, the menu says they
  aren't available.

## You need your own game

This repository contains **no game code or data**. The build recompiles the game from your
own disc image of Rebel Strike (USA, `GWRE64`) as `.iso`, `.gcm`, `.rvz` or `.wia`. The code
generated from your disc stays in your build folder: don't share it.

## Building

The requirements are Red Two's (see its [README](docs/RED_TWO_README.md#building-windows)).
Add `-DR2_GAME=GWRE64` to build Red Three instead of Red Two, and use a build folder of its
own.

**Windows** (Visual Studio 2022 Build Tools, CMake 3.25+, Ninja, LLVM clang-cl):

```powershell
git clone https://github.com/TypingTurtle/red_three
cd red_three
git checkout red3
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Disc "<your Rebel Strike.iso>" -BuildDir build\windows-rs -CMakeArgs "-DR2_GAME=GWRE64"
build\windows-rs\red_three.exe
```

**Linux** (clang, lld, CMake 3.25+, Ninja, SDL3's build dependencies):

```bash
tools/build_linux.sh --disc "<your Rebel Strike.iso>" -DR2_GAME=GWRE64 --build-dir build/linux-rs
build/linux-rs/red_three
```

The first build downloads the graphics library's dependencies and recompiles the game, so it
takes a while.

Building without `-DR2_GAME` (or with `-DR2_GAME=GSWE64`) still gives you Red Two for Rogue
Leader, unchanged.

Red Three keeps its settings, memory card (`GWRE64_A.raw`), save states and logs in a user
folder named "Red Three", separate from Red Two's: `user` next to `red_three.exe` on Windows,
`~/.local/share/Red Three` on Linux. It has no releases, so it never checks for updates.

## Reporting a problem

Open an issue with:

- the **session log** from the start that went wrong (the `logs` folder in the user folder
  above; each start writes a new one, named by date and time)
- what happened and where (boot, menus, which mission), and whether it happens every time
- a screenshot or short video if it's something you can see

## What Red Three changes

Compared with Red Two at `354ae8b`:

- **The game is a build option.** `R2_GAME` picks `config/<disc ID>/` (function names, hooks,
  layout), the game's own hooks (`src/runtime/hle_game_<id>.cpp`) and a profile in
  `src/runtime/game.h`: names, disc ID, and the handful of addresses the runtime itself relies
  on. Everything that was hardcoded to Rogue Leader (disc checks, user folder, memory card,
  save states, texture packs, Dolphin import, menu texts) follows the profile.
- **Compressed main program.** Rebel Strike stores the code it runs from virtual memory
  zlib-compressed inside `main.dol`. A new `embedded_zlib` layout entry has the recompiler
  unpack it (text at `0x7F200000`).
- **Hooks by name.** The generated `funcs.h` defines `R2_HOOK_<name>` and `orig_<name>()`, so
  runtime code shared by both games can call a hook's original body without a game-specific
  address.
- **Rebel Strike's addresses** in `config/GWRE64/` and `game.h`, found by analysis of the
  game's code:
  - the VM pager the page-fault entry calls (`0x800A1C28`, entry `0x8003D190`)
  - `OSSaveContext`, `OSLoadContext`, `OSInitContext`, `OSReport`, `PPCHalt` and the
    scheduler's idle loop
  - MusyX's `salBuildCommandList` (`0x7F664124`) and its AI interrupt state, which Rebel
    Strike lays out differently from Rogue Leader
- **Rogue Leader-only options hidden** for other games. Rogue Leader's hooks are in
  `src/runtime/hle_game_gswe64.cpp`, unchanged apart from the file name.

## Legal

Red Three is distributed under the [MIT License](LICENSE) it inherits from Red Two, which
covers this repository's code only, not the game's code that a build contains in recompiled
form.

This repository contains no code or assets from the game. Star Wars, Rogue Leader and Rebel
Strike are trademarks of Lucasfilm Ltd.; GameCube is a trademark of Nintendo. This project is
not affiliated with or endorsed by them, or by Red Two's authors.
