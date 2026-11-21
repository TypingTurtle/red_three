// Tables emitted by the recompiler (gen/table.cpp).
#pragma once
#include "ctx.h"

namespace rt {

struct FuncEntry {
    u32 addr;
    GuestFn fn;
};
struct SymName {
    u32 addr;
    const char* name;
};

extern const FuncEntry g_funcTable[];
extern const u32 g_funcCount;
extern const SymName g_symNames[];  // terminated by {0, nullptr}
extern const FuncEntry g_origTable[];  // translated bodies of hooked functions, {0, nullptr}-terminated

// Code the game copies out of its resources (r2recomp --extra, gen/extra.cpp): found by its
// bytes wherever it is (interp.cpp). addr: where the recompiler had it; len: bytes from the
// entry to the last instruction; head/hash: FNV-1a of the first min(32, len) bytes / all len.
struct ExtraFn {
    u32 addr, len;
    u64 head, hash;
    GuestFn fn;
};
extern const ExtraFn g_extraFns[];  // {0, ...}-terminated
extern const u32 g_extraCount;

}  // namespace rt
