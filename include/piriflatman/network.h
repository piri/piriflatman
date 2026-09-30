#pragma once
#include "alpaca.h"
#include <array>
namespace pfm {
// Incremental HTTP/1.0 and HTTP/1.1 framing shared by ESP32 and host tests.
class HttpRequest {
public:
    enum class State { Reading, Complete, Invalid };
    State feed(const char* bytes, size_t length);
    const Request& request() const { return request_; }
    bool takeContinue();
private:
    std::string buffer_;
    Request request_;
    bool headers_ = false, continue_ = false;
    uint32_t contentLength_ = 0;
    State state_ = State::Reading;
};
class NetworkServer {
public:
    explicit NetworkServer(Alpaca& api) : api_(api) {}
    ~NetworkServer() { stop(); }
    bool begin(uint16_t httpPort, uint16_t discoveryPort);
    void stop();
    void tick(uint32_t now);
private:
    struct Client {
        int fd = -1;
        uint32_t opened = 0;
        HttpRequest parser;
        std::string output;
        size_t sent = 0;
        bool final = false;
    };
    void closeClient(Client& client);
    Alpaca& api_;
    int http_ = -1, udp_ = -1;
    uint16_t port_ = 0;
    std::array<Client, 4> clients_;
};
}
