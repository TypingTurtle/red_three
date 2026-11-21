// Function discovery for a DOL without symbols.
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "image.h"

struct JumpTable {
    uint32_t table = 0;
    std::vector<uint32_t> targets;
    bool guessed = false;  // no bounds check found: size inferred from the entries
};

struct Function {
    uint32_t entry = 0;
    std::vector<uint32_t> insns;            // sorted
    std::set<uint32_t> labels;              // branch targets inside the body
    std::map<uint32_t, JumpTable> tables;   // bctr address -> table
    std::set<uint32_t> tailEntries;         // fallthrough/branch into another entry: call + return
    // Addresses inside the body that the function puts into lr itself (lis/addi or ori, then
    // mtlr): hand-written code that jumps to a helper whose blr "returns" there (Rebel Strike's
    // locked-cache vertex loop, 0x8003BD44). They are labels; a blr checks lr against them.
    std::set<uint32_t> lrLabels;
};

struct Config {
    std::map<uint32_t, std::string> names;   // symbols.txt: addr -> name
    std::map<uint32_t, std::string> hooks;   // hooks.txt: replaced by hle_<name>
    std::set<uint32_t> saveCtx;              // functions that return twice (OSSaveContext)
    std::set<uint32_t> extraEntries;         // entries the analysis cannot see
    std::set<uint32_t> notEntries;           // false positives to drop
    std::set<uint32_t> idleLoops;            // spin-wait loop heads: block until an event is due
    std::vector<uint32_t> embedded;          // layout.txt "embedded <addr>": DOL images inside the DOL
    std::vector<uint32_t> embeddedZlib;      // layout.txt "embedded_zlib <addr>": the same, zlib-compressed
    bool load(const std::string& dir);
};

class Analysis {
public:
    Analysis(const Image& img, const Config& cfg) : img_(img), cfg_(cfg) {}
    void run();

    std::set<uint32_t> entries;
    std::map<uint32_t, Function> funcs;
    std::map<uint32_t, uint32_t> codeLimit;  // section start -> end of code (data follows)
    std::vector<std::string> notes;

private:
    bool inText(uint32_t a) const;
    bool inCode(uint32_t a) const;
    bool walk(uint32_t entry, Function* out, std::vector<uint32_t>* calls, bool limitToCode);
    bool findJumpTable(uint32_t bctr, JumpTable& jt) const;
    void scanConstants(std::set<uint32_t>& cands) const;
    void scanData(std::set<uint32_t>& cands, const std::set<uint32_t>& tableWords) const;
    void scanGaps(std::set<uint32_t>& cands) const;

    const Image& img_;
    const Config& cfg_;
};
