#!/usr/bin/env bash
# Builds the Steam Deck AppImage in one go, on Linux or in WSL2 (Ubuntu 24.04):
#   tools/steamdeck_build.sh [--release] "<path to your disc image>" [<folder to copy the AppImage to>]
# --release: a download build (updates itself from the GitHub releases, -DR2_RELEASE=ON) in its
# own build folder, and RedThree-linux-x64.zip with the AppImage and a readme next to it.
# Installs the build packages (asks for your password once), extracts the code in the game's
# data files, builds Red Three and packs RedThree-x86_64.AppImage.
# In WSL the build runs in your Linux home folder (~/red_three_deck): building on /mnt/c is
# several times slower. The AppImage is copied next to the disc image unless you name a folder.
set -euo pipefail
release=0
if [ "${1:-}" = "--release" ]; then release=1; shift; fi
[ $# -ge 1 ] || { echo "usage: $0 <disc image> [output folder]" >&2; exit 2; }
disc="$1"
[ -f "$disc" ] || { echo "no disc image at $disc" >&2; exit 1; }
disc="$(realpath "$disc")"
outdir="${2:-$(dirname "$disc")}"
here="$(cd "$(dirname "$0")/.." && pwd)"

echo "== packages"
pkgs=(git cmake ninja-build clang lld pkg-config python3 python3-pil curl squashfs-tools patchelf
    libx11-dev libx11-xcb-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev
    libxfixes-dev libxtst-dev libxinerama-dev libxkbcommon-dev libwayland-dev wayland-protocols
    libdecor-0-dev libegl-dev libgl-dev libgles-dev libvulkan-dev libdrm-dev libgbm-dev
    libasound2-dev libpulse-dev libpipewire-0.3-dev libudev-dev libdbus-1-dev libibus-1.0-dev
    libfreetype-dev libpng-dev)
missing=()
for p in "${pkgs[@]}"; do dpkg -s "$p" >/dev/null 2>&1 || missing+=("$p"); done
if [ ${#missing[@]} -gt 0 ]; then
    sudo apt-get update
    sudo apt-get install -y "${missing[@]}"
fi

# the source: this checkout when it is on a Linux file system, otherwise a copy in ~/red_three_deck
src="$here"
case "$here" in
/mnt/*)
    src="$HOME/red_three_deck"
    echo "== copying the source to $src"
    if [ ! -d "$src/.git" ]; then
        git clone --recursive https://github.com/TypingTurtle/red_three "$src"
    fi
    # the files of this checkout, with any local changes (not its build folders)
    tar -C "$here" --exclude=./build --exclude=./extern --exclude=./.git -cf - . | tar -C "$src" -xf -
    git -C "$src" submodule update --init --recursive
    ;;
esac

echo "== code from the game's data files"
rescode="$src/build/rescode"
if [ ! -f "$rescode/rescode.bin" ]; then
    python3 "$src/tools/extract_resource_code.py" "$disc" "$rescode"
fi

echo "== build (the first one takes 30 minutes or more)"
bdir="$src/build/deck"; rel=OFF
[ $release -eq 1 ] && { bdir="$src/build/deck-release"; rel=ON; }
"$src/tools/build_linux.sh" --disc "$disc" --build-dir "$bdir" \
    -DR2_GAME=GLRE64 "-DR2_EXTRA_CODE=$rescode" "-DR2_RELEASE=$rel"

echo "== AppImage"
runtime="$src/build/runtime-x86_64"
if [ ! -s "$runtime" ]; then
    curl -fL -o "$runtime" \
        https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64
fi
"$src/tools/make_appimage.sh" --runtime "$runtime" --build-dir "$bdir"

mkdir -p "$outdir"
cp "$bdir/RedThree-x86_64.AppImage" "$outdir/"
echo
echo "Done: $outdir/RedThree-x86_64.AppImage"
if [ $release -eq 1 ]; then
    stage="$bdir/release-linux/Red Three"
    rm -rf "$bdir/release-linux"; mkdir -p "$stage"
    cp "$bdir/RedThree-x86_64.AppImage" "$stage/"
    chmod +x "$stage/RedThree-x86_64.AppImage"
    cp "$src/LICENSE" "$stage/LICENSE.txt"
    cat > "$stage/README.txt" <<'TXT'
Red Three - Star Wars: Rogue Squadron III - Rebel Strike, recompiled for Linux and Steam Deck
https://github.com/TypingTurtle/red_three

You need your own Rebel Strike disc image (USA, GLRE64) as .iso, .gcm, .rvz or .wia.

1. Unzip this folder anywhere (on the Deck: in Desktop mode).
2. Make RedThree-x86_64.AppImage executable (right-click > Properties > Permissions, or
   chmod +x RedThree-x86_64.AppImage).
3. Start it and choose your disc image the first time. On the Deck, add the AppImage to Steam
   as a non-Steam game to play it in Game mode.

Settings, saves and logs: ~/.local/share/Red Three/
Known issues: https://github.com/TypingTurtle/red_three/blob/main/docs/KNOWN_ISSUES.md
TXT
    python3 "$src/tools/third_party_notices.py" "$bdir" "$stage/THIRD_PARTY_NOTICES.txt" || true
    (cd "$bdir/release-linux" && rm -f ../RedThree-linux-x64.zip && python3 - <<'PY'
import os, zipfile
with zipfile.ZipFile("../RedThree-linux-x64.zip", "w", zipfile.ZIP_DEFLATED) as z:
    for d, _, files in os.walk("Red Three"):
        for f in files:
            p = os.path.join(d, f)
            i = zipfile.ZipInfo.from_file(p, p)
            i.compress_type = zipfile.ZIP_DEFLATED
            with open(p, "rb") as fh: z.writestr(i, fh.read())  # keeps the executable bit
PY
    )
    cp "$bdir/RedThree-linux-x64.zip" "$outdir/"
    echo "Done: $outdir/RedThree-linux-x64.zip"
fi
echo "Copy it to the Deck, make it executable, and add it to Steam as a non-Steam game."
