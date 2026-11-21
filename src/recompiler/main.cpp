// r2recomp: translates the game's main.dol into C++ (one function per guest entry point).
//
//   r2recomp <main.dol> <config dir> <out dir> [--files N]
//
// Writes <out>/funcs.h, <out>/code_NNN.cpp, <out>/table.cpp and <out>/functions.txt.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>
#include <string>

#include <exception>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "analysis.h"
#include "emit.h"

static int run(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: r2recomp <main.dol> <config dir> <out dir> [--files N]\n");
        return 2;
    }
    int files = 48;
    // --extra <file>@<hex addr>: more code as a text section (tools/extract_resource_code.py)
    // --extra-entries <file>: "<hex addr> [name]" lines, function entries in it
    std::vector<std::pair<std::string, uint32_t>> extras;
    std::vector<std::string> extraEntryFiles;
    for (int i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "--files") && i + 1 < argc) files = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--extra") && i + 1 < argc) {
            const std::string a = argv[++i];
            const size_t at = a.rfind('@');
            if (at == std::string::npos) {
                fprintf(stderr, "--extra wants <file>@<hex address>\n");
                return 2;
            }
            extras.emplace_back(a.substr(0, at), uint32_t(std::stoul(a.substr(at + 1), nullptr, 16)));
        } else if (!strcmp(argv[i], "--extra-entries") && i + 1 < argc) extraEntryFiles.push_back(argv[++i]);
    }

    const auto t0 = std::chrono::steady_clock::now();
    Image img;
    if (!img.load(argv[1])) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    fprintf(stderr, "r2recomp: read %s\n", argv[1]);
    Config cfg;
    if (!cfg.load(argv[2])) return 1;
    for (uint32_t at : cfg.embedded) {
        if (!img.addEmbedded(at)) {
            fprintf(stderr, "no embedded image at %08X\n", at);
            return 1;
        }
    }
    for (uint32_t at : cfg.embeddedZlib) {
        if (!img.addEmbedded(at, true)) {
            fprintf(stderr, "no compressed embedded image at %08X\n", at);
            return 1;
        }
    }
    for (auto& [path, at] : extras) {
        if (!img.addRaw(path, at)) {
            fprintf(stderr, "cannot read extra code %s\n", path.c_str());
            return 1;
        }
    }
    for (auto& path : extraEntryFiles) {
        std::ifstream in(path);
        if (!in) {
            fprintf(stderr, "cannot read %s\n", path.c_str());
            return 1;
        }
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ss(line);
            std::string a, name;
            ss >> a >> name;
            if (a.empty()) continue;
            const uint32_t addr = uint32_t(std::stoul(a, nullptr, 16));
            cfg.extraEntries.insert(addr);
            if (!name.empty() && !cfg.names.count(addr)) cfg.names[addr] = name;
        }
    }
    for (auto& s : img.sections)
        printf("  %-4s %08X-%08X %s\n", s.name.c_str(), s.addr, s.end(), s.text ? "text" : "data");
    Analysis an(img, cfg);
    an.run();

    std::filesystem::create_directories(argv[3]);
    Emitter em(img, an, cfg);
    if (!em.write(argv[3], files)) return 1;

    size_t insns = 0, tables = 0;
    for (auto& [e, f] : an.funcs) {
        insns += f.insns.size();
        tables += f.tables.size();
    }
    {
        std::ofstream lst(std::string(argv[3]) + "/functions.txt");
        for (auto& [e, f] : an.funcs) {
            char buf[128];
            snprintf(buf, sizeof buf, "%08X %6zu", e, f.insns.size());
            lst << buf;
            if (auto it = cfg.names.find(e); it != cfg.names.end()) lst << " " << it->second;
            lst << "\n";
        }
        for (auto& n : an.notes) lst << "# " << n << "\n";
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("r2recomp: %zu functions, %zu instructions (with overlap), %zu jump tables, %zu notes, %.1fs\n",
           an.funcs.size(), insns, tables, an.notes.size(), secs);
    for (auto& [s, end] : an.codeLimit) printf("  code %08X-%08X\n", s, end);
    return 0;
}

// A failure must say so in the build log: output is unbuffered (a crash would lose buffered
// lines), an exception is reported, and on Windows a crash prints its code and address instead
// of ending the process silently.
#ifdef _WIN32
static LONG WINAPI reportCrash(EXCEPTION_POINTERS* e) {
    fprintf(stderr, "r2recomp: crashed with exception 0x%08lX at %p\n", e->ExceptionRecord->ExceptionCode,
            e->ExceptionRecord->ExceptionAddress);
    if (e->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW) fprintf(stderr, "r2recomp: (stack overflow)\n");
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(reportCrash);
    ULONG guarantee = 64 * 1024;  // room to report a stack overflow
    SetThreadStackGuarantee(&guarantee);
#endif
    try {
        const int rc = run(argc, argv);
        if (rc) fprintf(stderr, "r2recomp: failed (exit code %d)\n", rc);
        return rc;
    } catch (const std::exception& e) {
        fprintf(stderr, "r2recomp: error: %s\n", e.what());
    } catch (...) {
        fprintf(stderr, "r2recomp: error: unknown exception\n");
    }
    return 3;
}
