// Online multiplayer (experimental build): see netplay.h.
//
// One TCP connection, Nagle off. Messages: a 1-byte type, a 4-byte little-endian length, the
// payload. The one joining sends Hello; the host answers Setup (settings, input delay, memory
// card); then both send Input for every controller poll (the poll's number and that machine's
// port 1) and, every 600 polls, Hash (a checksum of main memory, to report a desync).
#include "netplay.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using sock_t = SOCKET;
#define closesock closesocket
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
using sock_t = int;
#define INVALID_SOCKET (-1)
#define closesock close
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "ctx.h"
#include "game.h"
#include "mods.h"
#include "settings.h"

namespace rt::netplay {
namespace {

constexpr int kDefaultPort = 27911;
constexpr u32 kProtocol = 1;
enum Msg : u8 { Hello = 1, Setup = 2, Input = 3, Hash = 4, Bye = 5 };

struct Wire {  // a controller's state on the wire (PadState)
    u8 connected, stickX, stickY, substickX, substickY, triggerL, triggerR, pad;
    u16 buttons;
};

bool s_active = false, s_host = false;
sock_t s_sock = INVALID_SOCKET;
int s_delay = 4;
u64 s_poll = 0;                    // controller polls so far (the frame number both machines share)
std::map<u64, Wire> s_remote;      // the other machine's inputs by poll
std::deque<std::pair<u64, Wire>> s_localQueue;  // ours, by the poll they apply to
std::map<u64, u64> s_remoteHash, s_localHash;
std::string s_status;
u64 s_waitTb = 0, s_waits = 0;
bool s_desyncReported = false;
std::vector<u8> s_rx;  // bytes received, not yet a whole message

bool sendAll(const void* p, size_t n) {
    const char* c = static_cast<const char*>(p);
    while (n) {
        const int r = send(s_sock, c, int(n), 0);
        if (r <= 0) return false;
        c += r;
        n -= size_t(r);
    }
    return true;
}

bool sendMsg(Msg type, const void* payload, u32 len) {
    u8 h[5] = {type, u8(len), u8(len >> 8), u8(len >> 16), u8(len >> 24)};
    return sendAll(h, 5) && (len == 0 || sendAll(payload, len));
}

// receives whole messages; blocking: waits for one (timeout ms; -1 forever)
bool recvMsg(Msg& type, std::vector<u8>& payload, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs < 0 ? 1 << 30 : timeoutMs);
    for (;;) {
        if (s_rx.size() >= 5) {
            const u32 len = u32(s_rx[1]) | u32(s_rx[2]) << 8 | u32(s_rx[3]) << 16 | u32(s_rx[4]) << 24;
            if (s_rx.size() >= 5 + size_t(len)) {
                type = Msg(s_rx[0]);
                payload.assign(s_rx.begin() + 5, s_rx.begin() + 5 + len);
                s_rx.erase(s_rx.begin(), s_rx.begin() + 5 + len);
                return true;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(s_sock, &rd);
        const long long left =
            now >= deadline ? 0 : std::chrono::duration_cast<std::chrono::microseconds>(deadline - now).count();
        const long long wait = std::min<long long>(left, 100000);
        timeval tv{long(wait / 1000000), long(wait % 1000000)};
        const int r = select(int(s_sock) + 1, &rd, nullptr, nullptr, &tv);
        if (r < 0) return false;
        if (r == 0) {
            if (now >= deadline) return false;
            continue;
        }
        char buf[4096];
        const int n = recv(s_sock, buf, sizeof buf, 0);
        if (n <= 0) return false;
        s_rx.insert(s_rx.end(), buf, buf + n);
    }
}

void noDelay() {
    int one = 1;
    setsockopt(s_sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
}

bool netInit() {
#ifdef _WIN32
    WSADATA w;
    return WSAStartup(MAKEWORD(2, 2), &w) == 0;
#else
    return true;
#endif
}

bool hostListen(int port) {
    sock_t l = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    bool v6 = l != INVALID_SOCKET;
    if (!v6) l = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (l == INVALID_SOCKET) return false;
    int one = 1, zero = 0;
    setsockopt(l, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof one);
    if (v6) setsockopt(l, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&zero), sizeof zero);  // IPv4 too
    sockaddr_storage ss{};
    socklen_t sl;
    if (v6) {
        auto* a = reinterpret_cast<sockaddr_in6*>(&ss);
        a->sin6_family = AF_INET6;
        a->sin6_addr = in6addr_any;
        a->sin6_port = htons(u16(port));
        sl = sizeof(sockaddr_in6);
    } else {
        auto* a = reinterpret_cast<sockaddr_in*>(&ss);
        a->sin_family = AF_INET;
        a->sin_addr.s_addr = htonl(INADDR_ANY);
        a->sin_port = htons(u16(port));
        sl = sizeof(sockaddr_in);
    }
    if (bind(l, reinterpret_cast<sockaddr*>(&ss), sl) != 0 || listen(l, 1) != 0) {
        logf("netplay", "cannot listen on port %d (in use? blocked?)", port);
        closesock(l);
        return false;
    }
    logf("netplay", "hosting on port %d: waiting for player 2 to join (forward TCP %d on your router for internet play)", port,
         port);
    s_status = "Waiting for player 2 on port " + std::to_string(port);
    s_sock = accept(l, nullptr, nullptr);
    closesock(l);
    if (s_sock == INVALID_SOCKET) return false;
    noDelay();
    return true;
}

bool joinConnect(const std::string& where) {
    std::string host = where;
    int port = kDefaultPort;
    // [v6]:port, host:port, host
    if (!host.empty() && host[0] == '[') {
        const auto e = host.find(']');
        if (e != std::string::npos) {
            if (e + 2 < host.size() && host[e + 1] == ':') port = atoi(host.c_str() + e + 2);
            host = host.substr(1, e - 1);
        }
    } else if (const auto c = host.rfind(':'); c != std::string::npos && host.find(':') == c) {
        port = atoi(host.c_str() + c + 1);
        host.resize(c);
    }
    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        logf("netplay", "cannot find %s", host.c_str());
        return false;
    }
    logf("netplay", "joining %s port %d", host.c_str(), port);
    s_status = "Joining " + host;
    for (int attempt = 0; attempt < 30 && s_sock == INVALID_SOCKET; attempt++) {  // (the host may start a moment later)
        for (addrinfo* a = res; a; a = a->ai_next) {
            sock_t s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            if (s == INVALID_SOCKET) continue;
            if (connect(s, a->ai_addr, socklen_t(a->ai_addrlen)) == 0) {
                s_sock = s;
                break;
            }
            closesock(s);
        }
        if (s_sock == INVALID_SOCKET) std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    freeaddrinfo(res);
    if (s_sock == INVALID_SOCKET) {
        logf("netplay", "could not connect to %s:%d", host.c_str(), port);
        return false;
    }
    noDelay();
    return true;
}

// --- what both machines must agree on -------------------------------------------------------
struct SetupInfo {
    u32 protocol, delay, fps;
    u8 fastDisc, skipLogos, skipCutscenes, invincible, infiniteLives, infiniteAmmo, perfectStats, allCraft;
    u8 speakers, pad[3];
    u32 cardSize;
};

std::string buildId() {
    return std::string(game::kGame.id) + " " + __DATE__ " " __TIME__;  // (the same build on both machines)
}

void putU32(std::vector<u8>& v, u32 x) {
    for (int i = 0; i < 4; i++) v.push_back(u8(x >> (8 * i)));
}

bool hostHandshake() {
    Msg t;
    std::vector<u8> p;
    if (!recvMsg(t, p, 30000) || t != Hello) {
        logf("netplay", "player 2 did not say hello");
        return false;
    }
    const std::string theirs(p.begin(), p.end()), ours = buildId();
    if (theirs != ours) {
        logf("netplay", "player 2 runs a different build (%s; this is %s): both need the same Red Three", theirs.c_str(),
             ours.c_str());
        const std::string why = "different build: host has " + ours;
        sendMsg(Bye, why.data(), u32(why.size()));
        return false;
    }
    settings::Settings& s = settings::get();
    std::vector<u8> card;
    {
        std::ifstream f(cardImagePath(0), std::ios::binary);
        card.assign(std::istreambuf_iterator<char>(f), {});
    }
    SetupInfo si{};
    si.protocol = kProtocol;
    si.delay = u32(s_delay);
    si.fps = u32(s.fps);
    si.fastDisc = s.fastDisc, si.skipLogos = s.skipLogos, si.skipCutscenes = s.skipCutscenes;
    si.invincible = s.invincible, si.infiniteLives = s.infiniteLives, si.infiniteAmmo = s.infiniteAmmo;
    si.perfectStats = s.perfectStats, si.allCraft = s.allCraft, si.speakers = u8(s.speakers);
    si.cardSize = u32(card.size());
    std::vector<u8> msg(sizeof si);
    memcpy(msg.data(), &si, sizeof si);
    msg.insert(msg.end(), card.begin(), card.end());
    if (!sendMsg(Setup, msg.data(), u32(msg.size()))) return false;
    logf("netplay", "player 2 joined; sent the settings and the memory card (%zu bytes); input delay %d frames", card.size(),
         s_delay);
    return true;
}

bool joinHandshake() {
    const std::string id = buildId();
    if (!sendMsg(Hello, id.data(), u32(id.size()))) return false;
    Msg t;
    std::vector<u8> p;
    if (!recvMsg(t, p, 60000)) {
        logf("netplay", "the host did not answer");
        return false;
    }
    if (t == Bye) {
        logf("netplay", "the host refused: %s", std::string(p.begin(), p.end()).c_str());
        return false;
    }
    if (t != Setup || p.size() < sizeof(SetupInfo)) return false;
    SetupInfo si;
    memcpy(&si, p.data(), sizeof si);
    if (si.protocol != kProtocol || p.size() < sizeof si + si.cardSize) return false;
    s_delay = int(si.delay);
    settings::Settings& s = settings::get();  // (for this session only: not saved)
    s.fps = int(si.fps);
    s.fastDisc = si.fastDisc, s.skipLogos = si.skipLogos, s.skipCutscenes = si.skipCutscenes;
    s.invincible = si.invincible, s.infiniteLives = si.infiniteLives, s.infiniteAmmo = si.infiniteAmmo;
    s.perfectStats = si.perfectStats, s.allCraft = si.allCraft, s.speakers = settings::Speakers(si.speakers);
    viSetRate(s.fps);
    // the host's memory card, as a card of its own (this machine's saves stay as they are)
    const std::string dir = mods::userDir() + "saves/netplay/";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string path = dir + game::kGame.id + "_A.raw";
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(p.data() + sizeof si), std::streamsize(si.cardSize));
    }
#ifdef _WIN32
    _putenv_s("R2_CARD_A", path.c_str());
#else
    setenv("R2_CARD_A", path.c_str(), 1);
#endif
    logf("netplay", "joined: the host's settings and memory card (%u bytes) for this session; input delay %d frames",
         si.cardSize, s_delay);
    return true;
}

Wire toWire(const PadState& p) {
    return {u8(p.connected), p.stickX, p.stickY, p.substickX, p.substickY, p.triggerL, p.triggerR, 0, p.buttons};
}
void fromWire(const Wire& w, PadState& p) {
    p.connected = w.connected != 0;
    p.buttons = w.buttons;
    p.stickX = w.stickX, p.stickY = w.stickY, p.substickX = w.substickX, p.substickY = w.substickY;
    p.triggerL = w.triggerL, p.triggerR = w.triggerR;
}

u64 memHash() {  // FNV-1a over main memory, 8 bytes at a time
    const u64* m = reinterpret_cast<const u64*>(g_base + 0x80000000u);
    u64 h = 1469598103934665603ull;
    for (u32 i = 0; i < kMem1Size / 8; i++) h = (h ^ m[i]) * 1099511628211ull;
    return h;
}

void handle(Msg t, const std::vector<u8>& p) {
    if (t == Input && p.size() >= 8 + sizeof(Wire)) {
        u64 poll;
        memcpy(&poll, p.data(), 8);
        Wire w;
        memcpy(&w, p.data() + 8, sizeof w);
        s_remote[poll] = w;
    } else if (t == Hash && p.size() >= 16) {
        u64 poll, h;
        memcpy(&poll, p.data(), 8);
        memcpy(&h, p.data() + 8, 8);
        s_remoteHash[poll] = h;
    } else if (t == Bye) {
        logf("netplay", "the other player left");
        s_status = "The other player left";
        s_active = false;
    }
}

void fail(const char* why) {
    logf("netplay", "connection lost (%s): playing on alone", why);
    s_status = std::string("Connection lost: ") + why;
    s_active = false;
    if (s_sock != INVALID_SOCKET) closesock(s_sock);
    s_sock = INVALID_SOCKET;
}

void checkHashes() {
    for (auto it = s_localHash.begin(); it != s_localHash.end();) {
        auto r = s_remoteHash.find(it->first);
        if (r == s_remoteHash.end()) {
            ++it;
            continue;
        }
        if (r->second != it->second && !s_desyncReported) {
            s_desyncReported = true;
            logf("netplay", "DESYNC at poll %llu: the two games differ (memory %016llX here, %016llX there)",
                 (unsigned long long)it->first, (unsigned long long)it->second, (unsigned long long)r->second);
            s_status = "Desynced: the games differ - restart the session";
        } else if (r->second == it->second && it->first % 3600 == 0) {
            logf("netplay", "in sync at poll %llu", (unsigned long long)it->first);
        }
        s_remoteHash.erase(r);
        it = s_localHash.erase(it);
    }
}

}  // namespace

bool start(int argc, char** argv) {
    std::string mode, where;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--netplay-host")) {
            mode = "host";
            if (i + 1 < argc && isdigit(u8(argv[i + 1][0]))) where = argv[++i];
        } else if (!strcmp(argv[i], "--netplay-join") && i + 1 < argc) {
            mode = "join";
            where = argv[++i];
        }
    }
    if (mode.empty())
        if (const char* e = getenv("R2_NETPLAY"); e && *e) {
            const std::string v = e;
            if (v.rfind("host", 0) == 0) mode = "host", where = v.size() > 5 ? v.substr(5) : "";
            else if (v.rfind("join:", 0) == 0) mode = "join", where = v.substr(5);
        }
    if (mode.empty()) return false;
    if (const char* d = getenv("R2_NETPLAY_DELAY")) s_delay = std::clamp(atoi(d), 1, 30);
    if (!netInit()) return false;
    s_host = mode == "host";
    const bool ok = s_host ? hostListen(where.empty() ? kDefaultPort : atoi(where.c_str())) && hostHandshake()
                           : joinConnect(where) && joinHandshake();
    if (!ok) {
        if (s_sock != INVALID_SOCKET) closesock(s_sock);
        s_sock = INVALID_SOCKET;
        s_status = "Netplay could not start (see the log)";
        return false;
    }
    // The first s_delay polls: neutral controllers on both machines (connected, as they will be)
    for (u64 i = 0; i < u64(s_delay); i++) {
        Wire neutral{1, 0x80, 0x80, 0x80, 0x80, 0, 0, 0, 0x0080};
        s_remote[i] = neutral;
        s_localQueue.push_back({i, neutral});
    }
    setDeterministic(true);
    mods::setCodesEnabled(false);  // (cheat codes would differ between the machines)
    s_active = true;
    s_status = s_host ? "Netplay: hosting (you are player 1)" : "Netplay: joined (you are player 2)";
    logf("netplay", "%s", s_status.c_str());
    return true;
}

bool active() { return s_active; }

void exchange() {
    if (!s_active) return;
    const u64 poll = s_poll++;
    // ours for poll + delay
    const Wire mine = toWire(padState(0));
    {
        std::vector<u8> m(8 + sizeof(Wire));
        const u64 at = poll + u64(s_delay);
        memcpy(m.data(), &at, 8);
        memcpy(m.data() + 8, &mine, sizeof mine);
        if (!sendMsg(Input, m.data(), u32(m.size()))) return fail("send");
        s_localQueue.push_back({at, mine});
    }
    // theirs for this poll (wait for it)
    Msg t;
    std::vector<u8> p;
    const u64 w0 = tbHostNow();
    while (s_active && !s_remote.count(poll)) {
        if (!recvMsg(t, p, 10000)) return fail("no input from the other player for 10 s");
        handle(t, p);
    }
    if (!s_active) return;
    const u64 waited = tbHostNow() - w0;
    if (waited > kTbClock / 1000) s_waits++, s_waitTb += waited;
    // drain whatever else has arrived (without waiting)
    while (recvMsg(t, p, 0)) handle(t, p);
    Wire local{};
    while (!s_localQueue.empty() && s_localQueue.front().first <= poll) {
        if (s_localQueue.front().first == poll) local = s_localQueue.front().second;
        s_localQueue.pop_front();
    }
    const Wire remote = s_remote[poll];
    s_remote.erase(poll);
    // the host is player 1 (port 1), the one joining player 2 (port 2); ports 3-4 empty
    fromWire(s_host ? local : remote, padState(0));
    fromWire(s_host ? remote : local, padState(1));
    padState(0).connected = true;
    for (int i = 2; i < 4; i++) padState(i) = PadState{};
    if (poll % 600 == 0 && poll > 0) {
        const u64 h = memHash();
        s_localHash[poll] = h;
        // R2_NETPLAY_DUMP=<folder>: main memory at each check, to compare the two machines' (desync hunting)
        if (const char* d = getenv("R2_NETPLAY_DUMP"); d && *d) {
            const std::string path = std::string(d) + "/" + (s_host ? "host_" : "join_") + std::to_string(poll) + ".bin";
            if (FILE* f = fopen(path.c_str(), "wb")) {
                fwrite(g_base + 0x80000000u, 1, kMem1Size, f);
                fclose(f);
            }
        }
        std::vector<u8> m(16);
        memcpy(m.data(), &poll, 8);
        memcpy(m.data() + 8, &h, 8);
        if (!sendMsg(Hash, m.data(), 16)) return fail("send");
        checkHashes();
    }
    if (poll % 3600 == 0 && poll > 0 && s_waits) {
        logf("netplay", "waited for the other player %llu times in the last minute (%.0f ms in all)",
             (unsigned long long)s_waits, double(s_waitTb) * 1000.0 / double(kTbClock));
        s_waits = s_waitTb = 0;
    }
}

std::string status() { return s_status; }

}  // namespace rt::netplay
