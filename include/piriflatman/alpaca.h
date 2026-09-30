#pragma once
#include "panel.h"
#include <map>
#include <functional>
#include <string>
namespace pfm {
using Parameters = std::map<std::string, std::string>;
std::string lower(std::string value);
bool parseForm(const std::string& text, Parameters& result, bool caseInsensitive = true);
bool unsignedInteger(const std::string& text, uint32_t& value);
bool signedInteger(const std::string& text, int32_t& value);
std::string uniqueId(uint64_t factoryMac);
struct Request { std::string method, path, query, body, contentType; };
struct Response {
    int status = 200;
    std::string type = "application/json", body;
    std::string wire() const;
};
struct Info {
    std::string id, mode = "mock", address = "offline", timestamp;
    uint16_t httpPort = 8080, discoveryPort = 32227;
    std::function<bool(uint16_t, uint16_t)> savePorts{};
    bool restartPending = false;
};
class Alpaca {
public:
    Alpaca(Panel& panel, Info& info) : panel_(panel), info_(info) {}
    Response handle(const Request& request);
private:
    Response setup() const;
    Panel& panel_;
    Info& info_;
    uint32_t transaction_ = 0;
};
bool discoveryMessage(const std::string& packet);
}
