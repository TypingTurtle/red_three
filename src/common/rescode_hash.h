// Hash of resource code (code the game copies out of its resources; tools/
// extract_resource_code.py), the same in r2recomp (gen/extra.cpp) and the runtime (interp.cpp).
//
// The game relocates that code when it copies it: each lis rD,0+imm with a zero base register
// gets the high half of (entry + offset), and the loads, stores and addi that use such a
// register (as rA) get the low half. The hash skips those immediates, so the code matches on
// the disc and in memory; opcode and register fields, which relocation never touches, decide
// what is skipped.
#pragma once
#include <cstdint>

namespace rescode {

template <class WordAt>  // uint32_t(uint32_t byte offset): big-endian instruction words
inline uint64_t hash(WordAt word, uint32_t len) {
    uint64_t h = 0xcbf29ce484222325ull;  // FNV-1a over the masked words' bytes
    uint32_t lisRegs = 0;
    for (uint32_t i = 0; i + 4 <= len; i += 4) {
        uint32_t w = word(i);
        const uint32_t op = w >> 26, d = (w >> 21) & 31, a = (w >> 16) & 31;
        if (op == 15 && a == 0) {
            w &= 0xFFFF0000u;
            lisRegs |= 1u << d;
        } else if ((op == 14 || (op >= 32 && op <= 55)) && a != 0 && ((lisRegs >> a) & 1)) {
            w &= 0xFFFF0000u;
        }
        for (int k = 0; k < 4; k++) {
            h ^= (w >> (24 - 8 * k)) & 0xFF;
            h *= 0x100000001b3ull;
        }
    }
    return h;
}

}  // namespace rescode
