// The game this build is for (CMake: -DR2_GAME=<disc ID>, default GSWE64). The recompiled code,
// config/<ID>/ (symbols, hooks, layout) and the game's own hooks (hle_game_<id>.cpp) are per
// game; the runtime asks this header for everything else that differs between them: names, the
// disc to look for, and addresses in the game's loader the runtime itself relies on.
#pragma once
#include <cstdint>

#ifndef R2_GAME_ID
#define R2_GAME_ID "GSWE64"
#endif

namespace rt::game {

struct Profile {
    const char* id;         // disc ID: "GSWE64"
    const char* prefix;     // the game in any region: "GSW" (texture packs, Dolphin settings)
    const char* title;      // short name in messages: "Rogue Leader"
    const char* fullTitle;  // "Star Wars: Rogue Squadron II - Rogue Leader"
    const char* app;        // the port: window title, user folder name ("Red Two")
    const char* exe;        // program file name without extension ("red_two")
    const char* updateRepo; // GitHub owner/repo whose releases the updater follows; null: none
    uint8_t revision;       // the disc revision the build is made from

    // The loader's VM pager (vm.cpp): where the game keeps its DSI handler's pager function, and
    // (for R2_VM_SLOTS reports only; 0: not known) its slot array, page map, slot count and the
    // first virtual page the map covers.
    uint32_t pagerPtr;
    uint32_t vmSlotArrayPtr, vmSlotMapPtr, vmSlots, vmMapBase;
    // MusyX's salAIBufferBase (hw.cpp; 0: not known - the AI DMA is never taken for MusyX's)
    uint32_t salAIBufferBase;
    // A byte the stall reports show as "level" (profiler.cpp; 0: none)
    uint32_t stallLevelAddr;
    // The game's own code for Gecko/AR codes that write into code (codes.cpp): the SDK text and
    // the VM text
    uint32_t sdkTextLo, sdkTextHi, vmTextLo, vmTextHi;
};

#if defined(R2_GAME_GWRE64)
// Star Wars: Rogue Squadron III - Rebel Strike (USA). Its main code (VM text 0x7F200000) is
// stored zlib-compressed in main.dol (layout.txt: embedded_zlib); the pager pointer is the one
// its DSI entry 0x8003D190 calls (lis 0x800A / lwz 0x1C28).
inline constexpr Profile kGame = {
    "GWRE64", "GWR", "Rebel Strike", "Star Wars: Rogue Squadron III - Rebel Strike", "Red 3", "red_3", nullptr, 0,
    0x800A1C28,
    0, 0, 0, 0,
    0,
    0,
    0x80003100u, 0x80041E60u, 0x7F200000u, 0x7F6670A8u,
};
#else
inline constexpr Profile kGame = {
    "GSWE64", "GSW", "Rogue Leader", "Star Wars: Rogue Squadron II - Rogue Leader", "Red Two", "red_two",
    "gminustwo/red_two", 0,
    0x80095D00,
    0x80095CD0, 0x80095CE4, 1536, 0x7F000000,
    0x000965D4,
    0x800962EC,
    0x80003100u, 0x8005C360u, 0x7FC00000u, 0x7FD9E2F0u,
};
#endif

// Whether this build has Rogue Leader's game-side hooks (cheats, unlocks, widescreen at the
// source, cutscene skip, ...). Other games start without them.
#if defined(R2_GAME_GWRE64)
inline constexpr bool kRogueLeaderHooks = false;
#else
inline constexpr bool kRogueLeaderHooks = true;
#endif

}  // namespace rt::game
