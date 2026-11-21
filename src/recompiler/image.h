// DOL executable image.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../common/disc_image.h"

#include <zlib.h>

struct Section {
    std::string name;
    uint32_t addr = 0, size = 0, fileOff = 0;
    bool text = false;
    uint32_t end() const { return addr + size; }
};

struct Image {
    std::vector<uint8_t> file;
    std::vector<Section> sections;
    uint32_t entry = 0, bssAddr = 0, bssSize = 0;

    bool load(const std::string& path) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        file.resize(size_t(ftell(f)));
        fseek(f, 0, SEEK_SET);
        if (fread(file.data(), 1, file.size(), f) != file.size()) {
            fclose(f);
            return false;
        }
        fclose(f);
        // A disc image (ISO/GCM with the GameCube magic, or RVZ/WIA): take its main.dol.
        const bool rvz = file.size() >= 4 && (!memcmp(file.data(), "RVZ\x01", 4) || !memcmp(file.data(), "WIA\x01", 4));
        const bool iso = file.size() > 0x440 && file[0x1C] == 0xC2 && file[0x1D] == 0x33 && file[0x1E] == 0x9F && file[0x1F] == 0x3D;
        if (rvz || iso) {
            std::string err, dol;
            file.clear();
            auto disc = disc::open(path, err);
            if (!disc || !disc::readDol(*disc, dol, err)) {
                fprintf(stderr, "%s: %s\n", path.c_str(), err.c_str());
                return false;
            }
            file.assign(dol.begin(), dol.end());
            fprintf(stderr, "main.dol from the %s image (%zu bytes)\n", disc->kind(), file.size());
        }
        if (file.size() < 0x100) return false;
        auto be = [&](size_t o) {
            return uint32_t(file[o]) << 24 | uint32_t(file[o + 1]) << 16 | uint32_t(file[o + 2]) << 8 | file[o + 3];
        };
        for (int i = 0; i < 18; i++) {
            Section s;
            s.fileOff = be(i * 4);
            s.addr = be(0x48 + i * 4);
            s.size = be(0x90 + i * 4);
            s.text = i < 7;
            s.name = (i < 7 ? "T" : "D") + std::to_string(i < 7 ? i : i - 7);
            if (s.size) sections.push_back(s);
        }
        bssAddr = be(0xD8);
        bssSize = be(0xDC);
        entry = be(0xE0);
        return true;
    }
    // Rogue Leader embeds a second DOL-format image (its main code and data) inside one of
    // the DOL's sections and maps it to virtual addresses (0x7FC00000+) with its own MMU
    // page tables. Add that image's sections at their virtual addresses; the container
    // section stops being code.
    //
    // Rebel Strike stores its image zlib-compressed: <compressed size> <unpacked size> (both
    // big-endian u32) and the zlib stream. Unpacked, it is appended to `file` and read from there.
    bool addEmbedded(uint32_t at, bool compressed = false) {
        const Section* c = find(at);
        if (!c) return false;
        uint32_t base = c->fileOff + (at - c->addr);
        size_t limit = size_t(c->fileOff) + c->size;  // the image's bytes end here
        if (compressed) {
            if (base + 8 > limit) return false;
            auto rd = [&](size_t o) {
                return uint32_t(file[o]) << 24 | uint32_t(file[o + 1]) << 16 | uint32_t(file[o + 2]) << 8 | file[o + 3];
            };
            const uint32_t packed = rd(base), unpacked = rd(base + 4);
            if (packed > limit - base - 8 || unpacked < 0x100 || unpacked > 0x02000000u) return false;
            std::vector<uint8_t> out(unpacked);
            uLongf outLen = unpacked;
            if (uncompress(out.data(), &outLen, &file[base + 8], packed) != Z_OK || outLen != unpacked) return false;
            while (file.size() % 32) file.push_back(0);
            base = uint32_t(file.size());
            file.insert(file.end(), out.begin(), out.end());
            limit = file.size();
        }
        auto be = [&](size_t o) {
            o += base;
            return uint32_t(file[o]) << 24 | uint32_t(file[o + 1]) << 16 | uint32_t(file[o + 2]) << 8 | file[o + 3];
        };
        std::vector<Section> add;
        for (int i = 0; i < 18; i++) {
            Section s;
            s.fileOff = base + be(i * 4);
            s.addr = be(0x48 + i * 4);
            s.size = be(0x90 + i * 4);
            s.text = i < 7;
            s.name = std::string("V") + (i < 7 ? "T" : "D") + std::to_string(i < 7 ? i : i - 7);
            if (!s.size) continue;
            if (size_t(s.fileOff) + s.size > limit) return false;
            add.push_back(s);
        }
        for (auto& s : sections)
            if (s.addr == c->addr) s.text = false;
        sections.insert(sections.end(), add.begin(), add.end());
        embeddedBss.push_back({be(0xD8), be(0xDC)});
        return true;
    }
    std::vector<std::pair<uint32_t, uint32_t>> embeddedBss;

    // A raw file as one more text section at addr (r2recomp --extra: code the game loads from
    // its resources, extracted by tools/extract_resource_code.py), named X0, X1, ...
    bool addRaw(const std::string& path, uint32_t addr) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        const long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n <= 0) {
            fclose(f);
            return false;
        }
        while (file.size() % 32) file.push_back(0);
        const size_t at = file.size();
        file.resize(at + size_t(n));
        const bool ok = fread(&file[at], 1, size_t(n), f) == size_t(n);
        fclose(f);
        if (!ok) return false;
        int nX = 0;
        for (auto& s : sections) nX += s.name[0] == 'X';
        Section s;
        s.name = "X" + std::to_string(nX);
        s.addr = addr;
        s.size = uint32_t(n) & ~3u;
        s.fileOff = uint32_t(at);
        s.text = true;
        sections.push_back(s);
        return true;
    }

    const Section* find(uint32_t a) const {
        for (auto& s : sections)
            if (a >= s.addr && a < s.end()) return &s;
        return nullptr;
    }
    bool isText(uint32_t a) const {
        auto* s = find(a);
        return s && s->text;
    }
    uint32_t word(uint32_t a) const {
        auto* s = find(a);
        if (!s || a + 4 > s->end()) return 0;
        const uint8_t* p = &file[s->fileOff + (a - s->addr)];
        return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    }
};
