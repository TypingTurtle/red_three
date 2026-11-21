# Red Three

An experimental fork of **[Red Two](https://github.com/gminustwo/red_two)** that aims to bring
**Star Wars: Rogue Squadron III – Rebel Strike** (GameCube, USA, `GLRE64`) to PC.

## Credit

**Red Three is not original work.** Nearly all of it is Red Two, built by
[gminustwo](https://github.com/gminustwo) and its contributors: the recompiler, the runtime,
the audio and graphics, the menus and everything else. This fork only adds what's needed to
point Red Two at a second game. Every commit before `354ae8b` is Red Two's own history.

Red Two's [MIT license](LICENSE) is kept as is, and its original README is in
[docs/RED_TWO_README.md](docs/RED_TWO_README.md). This fork is not affiliated with or endorsed
by Red Two's authors, so please report Red Three problems here, not to them.

## Building

You need your own disc image of Rebel Strike (USA, `GLRE64`): `.iso`, `.gcm`, `.rvz` or
`.wia`. This repository contains no game code or data. Don't share the code your build
generates.

**Windows** (Visual Studio 2022 Build Tools, CMake 3.25+, Ninja, LLVM clang-cl):

```powershell
git clone --recursive https://github.com/TypingTurtle/red_three
cd red_three
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Disc "<your Rebel Strike.iso>" -BuildDir build\windows-rs -CMakeArgs "-DR2_GAME=GLRE64"
build\windows-rs\red_three.exe
```

**Linux** (clang, lld, CMake 3.25+, Ninja, SDL3's build dependencies):

```bash
git clone --recursive https://github.com/TypingTurtle/red_three
cd red_three
tools/build_linux.sh --disc "<your Rebel Strike.iso>" -DR2_GAME=GLRE64 --build-dir build/linux-rs
build/linux-rs/red_three
```

Rebel Strike also runs code it keeps inside its resource packs (the cinematics need it). Extract
it once with Python 3 and pass the folder to the build, or that code runs slowly in the
interpreter:

```
python tools/extract_resource_code.py "<your Rebel Strike.iso>" ../rescode
```

then add `-DR2_EXTRA_CODE=<full path to rescode>` next to `-DR2_GAME=GLRE64`. Like the rest of
the generated code, it is your game's: keep it out of the repository.

Without `-DR2_GAME=GLRE64` the build gives you Red Two for Rogue Leader, unchanged.

## Reporting a problem

Open an issue with the session log from the run that went wrong (`user\logs` next to
`red_three.exe` on Windows, `~/.local/share/Red Three/logs` on Linux) and what happened.

## Legal

The MIT license covers this repository's code only, not the game's code in a build. Star Wars,
Rogue Leader and Rebel Strike are trademarks of Lucasfilm Ltd.; GameCube is a trademark of
Nintendo. This project is not affiliated with or endorsed by them.
