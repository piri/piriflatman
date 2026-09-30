#include "piriflatman/network.h"
#include "../test/core_tests.h"
#include <chrono>
#include <csignal>
#include <ctime>
#include <iostream>
#include <thread>
namespace { volatile std::sig_atomic_t running = 1; void stop(int) { running = 0; } }
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--test") return tests::run();
    if (argc < 2 || std::string(argv[1]) != "--serve" || argc > 4) {
        std::cerr << "Usage: program --test | --serve [http-port=8080] [discovery-port=32227]\n"; return 2;
    }
    uint32_t http = 8080, udp = 32227;
    if ((argc > 2 && !pfm::unsignedInteger(argv[2],http)) || (argc > 3 && !pfm::unsignedInteger(argv[3],udp)) || !http || http > 65535 || !udp || udp > 65535) return 2;
    pfm::MockOutput output; pfm::Panel panel(output); panel.begin();
    // Host identity deliberately distinct from every physical unit.
    pfm::Info info{"piriflatman-host-mock-000000000001", "mock (host simulation)", "127.0.0.1", "", static_cast<uint16_t>(http), static_cast<uint16_t>(udp)};
    pfm::Alpaca api(panel,info); pfm::NetworkServer server(api);
    if (!server.begin(http, udp)) { std::cerr << "Cannot bind HTTP/UDP ports\n"; return 1; }
    std::signal(SIGINT,stop); std::signal(SIGTERM,stop);
    const auto start = std::chrono::steady_clock::now();
    std::cout << "Host mock listening on HTTP " << http << ", discovery UDP " << udp << std::endl;
    while (running) {
        panel.tick();
        time_t utc = time(nullptr); struct tm date{}; gmtime_r(&utc,&date);
        char text[32]; strftime(text,sizeof(text),"%Y-%m-%dT%H:%M:%SZ",&date); info.timestamp = text;
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
        server.tick(static_cast<uint32_t>(now));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    panel.networkLost();
    return 0;
}
