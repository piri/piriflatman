#include "piriflatman/alpaca.h"
#include <ArduinoJson.h>
#include <cstdio>
#include <limits>
#include <set>
#include <vector>
namespace pfm {
std::string lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}
static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static bool decode(const std::string& s, std::string& out) {
    out.clear();
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '+') c = ' ';
        else if (c == '%') {
            if (i + 2 >= s.size() || hex(s[i+1]) < 0 || hex(s[i+2]) < 0) return false;
            c = static_cast<char>(hex(s[i+1]) * 16 + hex(s[i+2]));
            i += 2;
        }
        if (static_cast<unsigned char>(c) < 32 || c == 127) return false;
        out += c;
    }
    return true;
}
bool parseForm(const std::string& text, Parameters& result, bool caseInsensitive) {
    result.clear();
    std::set<std::string> seen;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('&', start);
        if (end == std::string::npos) end = text.size();
        const auto pair = text.substr(start, end - start);
        const auto equals = pair.find('=');
        std::string key, value;
        if (equals == std::string::npos || !decode(pair.substr(0, equals), key) ||
            !decode(pair.substr(equals+1), value) || key.empty()) return false;
        if (!seen.insert(lower(key)).second) return false;
        if (caseInsensitive) key = lower(key);
        if (!result.emplace(key, value).second) return false;
        start = end + 1;
    }
    return true;
}
bool unsignedInteger(const std::string& s, uint32_t& value) {
    if (s.empty()) return false;
    uint64_t n = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        n = n * 10 + c - '0';
        if (n > UINT32_MAX) return false;
    }
    value = static_cast<uint32_t>(n);
    return true;
}
bool signedInteger(const std::string& s, int32_t& value) {
    if (s.empty()) return false;
    const bool negative = s[0] == '-';
    const size_t start = (negative || s[0] == '+') ? 1 : 0;
    uint32_t magnitude;
    if (!unsignedInteger(s.substr(start), magnitude)) return false;
    if (magnitude > (negative ? 2147483648U : 2147483647U)) return false;
    value = static_cast<int32_t>(negative ? -static_cast<int64_t>(magnitude) : magnitude);
    return true;
}
std::string uniqueId(uint64_t factoryMac) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "piriflatman-covercalibrator-%012llx",
                  static_cast<unsigned long long>(factoryMac & 0xffffffffffffULL));
    return buf;
}
static Response bad(const char* message, int status = 400) { return {status, "text/plain", message}; }
static const char* errorMessage(Error error) {
    switch (error) {
    case Ok: return "";
    case NotImplemented: return "Member is not implemented by this device";
    case InvalidValue: return "Brightness must be in the range 0 to 1000";
    case NotConnected: return "Device is not connected";
    default: return "Output initialization or write failed";
    }
}
Response Alpaca::handle(const Request& req) {
    if (++transaction_ == 0) ++transaction_;
    if (req.method == "POST" && req.path == "/setup/ports") {
        if (info_.restartPending) return bad("Restart is pending");
        Parameters form;
        uint32_t http = 0, discovery = 0;
        if (lower(req.contentType.substr(0, req.contentType.find(';'))) != "application/x-www-form-urlencoded" ||
            !parseForm(req.body, form) || !unsignedInteger(form["httpport"], http) ||
            !unsignedInteger(form["discoveryport"], discovery) || !http || http > 65535 || !discovery || discovery > 65535)
            return bad("Ports must be integers from 1 to 65535");
        if (!info_.savePorts || !info_.savePorts(http, discovery)) return bad("Could not persist network settings", 500);
        panel_.networkLost();
        info_.restartPending = true;
        return {200, "text/html; charset=utf-8", "<!doctype html><html lang=en><title>PiriFlatMan setup</title><h1>Settings saved</h1><p>Light off. Restarting; reopen http://" + info_.address + ":" + std::to_string(http) + "/setup after reconnecting.</p></html>"};
    }
    if (req.method == "GET" && (req.path == "/" || req.path == "/setup" ||
        req.path == "/setup/v1/covercalibrator/0/setup")) return setup();
    if (req.path.compare(0, 6, "/setup") == 0) return bad("Unknown setup path or method", 403);
    if (req.method != "GET" && req.method != "PUT") return bad("Expected GET or PUT");
    if (info_.restartPending && req.method == "PUT") return bad("Restart is pending; output is off");
    if (req.method == "PUT" && !req.body.empty() &&
        lower(req.contentType.substr(0, req.contentType.find(';'))) != "application/x-www-form-urlencoded")
        return bad("PUT body must use application/x-www-form-urlencoded");
    Parameters p;
    if (!parseForm(req.method == "GET" ? req.query : req.body, p, req.method == "GET")) return bad("Malformed or duplicate parameters");
    if (req.method == "PUT") {
        // API Reference V12 section 2.2.3: form names have canonical casing.
        // Unknown parameters (including incorrectly cased optional IDs) are ignored.
        Parameters canonical;
        for (const char* name : {"ClientID", "ClientTransactionID", "Connected", "Brightness", "ActionName", "Parameters", "Command", "Raw"}) {
            auto it = p.find(name);
            if (it != p.end()) canonical.emplace(lower(name), it->second);
        }
        p = std::move(canonical);
    }
    uint32_t clientTransaction = 0, client = 0;
    if ((p.count("clientid") && !unsignedInteger(p["clientid"], client)) ||
        (p.count("clienttransactionid") && !unsignedInteger(p["clienttransactionid"], clientTransaction)))
        return bad("Client IDs must be unsigned 32-bit integers");
    JsonDocument doc;
    doc["ClientTransactionID"] = clientTransaction;
    doc["ServerTransactionID"] = transaction_;
    Error error = Ok;
    if (req.path.compare(0, 12, "/management/") == 0) {
        if (req.method != "GET") return bad("Management requires GET");
        if (req.path == "/management/apiversions") doc["Value"].to<JsonArray>().add(1);
        else if (req.path == "/management/v1/description") {
            auto v = doc["Value"].to<JsonObject>();
            v["ServerName"] = "PiriFlatMan"; v["Manufacturer"] = "PiriFlatMan DIY";
            v["ManufacturerVersion"] = FirmwareVersion; v["Location"] = "Local network";
        } else if (req.path == "/management/v1/configureddevices") {
            auto v = doc["Value"].to<JsonArray>().add<JsonObject>();
            v["DeviceName"] = "PiriFlatMan"; v["DeviceType"] = "CoverCalibrator";
            v["DeviceNumber"] = 0; v["UniqueID"] = info_.id;
        } else return bad("Unknown management path");
    } else {
        const std::string prefix = "/api/v1/covercalibrator/0/";
        if (req.path.compare(0, prefix.size(), prefix) != 0) return bad("Unknown API, device type or device number");
        const auto member = req.path.substr(prefix.size());
        if (req.method == "GET") {
            if (member == "name") doc["Value"] = "PiriFlatMan";
            else if (member == "description") doc["Value"] = "DIY flat-field calibrator without a motorized cover";
            else if (member == "driverinfo") doc["Value"] = "PiriFlatMan 0.1.0; native Alpaca ICoverCalibratorV2; " + info_.mode;
            else if (member == "driverversion") doc["Value"] = "0.1";
            else if (member == "interfaceversion") doc["Value"] = 2;
            else if (member == "supportedactions") doc["Value"].to<JsonArray>();
            else if (member == "connected") doc["Value"] = panel_.connected();
            else if (member == "connecting") doc["Value"] = panel_.connecting();
            else if (member == "brightness" || member == "maxbrightness" || member == "calibratorstate" ||
                     member == "calibratorchanging" || member == "coverstate" || member == "covermoving" || member == "devicestate") {
                if (!panel_.connected()) error = NotConnected;
                else if (member == "brightness") doc["Value"] = panel_.brightness();
                else if (member == "maxbrightness") doc["Value"] = MaxBrightness;
                else if (member == "calibratorstate") doc["Value"] = static_cast<int>(panel_.state());
                else if (member == "coverstate") doc["Value"] = 0;
                else if (member == "devicestate") {
                    auto values = doc["Value"].to<JsonArray>();
                    auto add = [&values](const char* name, auto value) {
                        auto v = values.add<JsonObject>(); v["Name"] = name; v["Value"] = value;
                    };
                    add("Brightness", panel_.brightness()); add("CalibratorChanging", false);
                    add("CalibratorState", static_cast<int>(panel_.state()));
                    add("CoverMoving", false); add("CoverState", 0);
                    if (!info_.timestamp.empty()) add("TimeStamp", info_.timestamp);
                } else doc["Value"] = false;
            } else return bad("Unknown GET member");
        } else {
            if (member == "connected") {
                if (!p.count("connected")) return bad("Missing Connected");
                const auto b = lower(p["connected"]);
                if (b != "true" && b != "false") return bad("Connected must be true or false");
                error = panel_.connect(b == "true");
            } else if (member == "connect" || member == "disconnect") error = panel_.connect(member == "connect", true);
            else if (member == "calibratoron") {
                int32_t brightness;
                if (!p.count("brightness") || !signedInteger(p["brightness"], brightness)) return bad("Brightness must be a signed 32-bit integer");
                error = panel_.on(brightness);
            } else if (member == "calibratoroff") error = panel_.off();
            else if (member == "opencover" || member == "closecover" || member == "haltcover") error = NotImplemented;
            else if (member == "action") {
                if (!p.count("actionname") || !p.count("parameters")) return bad("Missing ActionName or Parameters");
                error = NotImplemented;
            } else if (member == "commandblind" || member == "commandbool" || member == "commandstring") {
                if (!p.count("command") || !p.count("raw")) return bad("Missing Command or Raw");
                if (lower(p["raw"]) != "true" && lower(p["raw"]) != "false") return bad("Raw must be true or false");
                error = NotImplemented;
            } else return bad("Unknown PUT member");
        }
    }
    doc["ErrorNumber"] = static_cast<int>(error);
    doc["ErrorMessage"] = errorMessage(error);
    if (error != Ok) doc.remove("Value");
    Response response;
    serializeJson(doc, response.body);
    return response;
}
Response Alpaca::setup() const {
    // All interpolated values are generated by this firmware, never credential/request text.
    std::string html = "<!doctype html><html lang=en><meta charset=utf-8><meta name=viewport content='width=device-width'><title>PiriFlatMan</title>"
        "<style>body{font:18px system-ui;max-width:48em;margin:3em auto;padding:1em}dt{font-weight:bold}dd{margin-bottom:1em}</style>"
        "<h1>PiriFlatMan</h1><p>DIY flat-field panel. No motorized cover.</p><dl><dt>Manufacturer</dt><dd>PiriFlatMan DIY</dd><dt>Firmware</dt><dd>";
    html += FirmwareVersion;
    html += "</dd><dt>Output profile</dt><dd>" + info_.mode + "</dd><dt>Device identity</dt><dd>" + info_.id;
    html += "</dd><dt>Network address</dt><dd>" + info_.address + ":" + std::to_string(info_.httpPort);
    html += "</dd><dt>Discovery UDP port</dt><dd>" + std::to_string(info_.discoveryPort);
    html += "</dd><dt>Connected</dt><dd>" + std::string(panel_.connected() ? "Yes" : "No");
    html += "</dd><dt>Calibrator state (1=Off, 3=Ready, 5=Error)</dt><dd>" + std::to_string(static_cast<int>(panel_.state()));
    html += "</dd><dt>Requested brightness / maximum</dt><dd>" + std::to_string(panel_.brightness()) + " / 1000";
    html += "</dd><dt>PWM duty</dt><dd>" + std::to_string(panel_.duty()) + "</dd></dl>"
        "<h2>Setup</h2><p>Configure Wi-Fi and optional static IP in config.local.h using config.example.h. Rebuild and USB flash to apply. DHCP is the default; a router reservation also fixes the IP.</p>";
    html += "<form method=post action=/setup/ports><label>HTTP port <input type=number name=httpport min=1 max=65535 required value=" + std::to_string(info_.httpPort) + "></label> "
        "<label>Discovery UDP port <input type=number name=discoveryport min=1 max=65535 required value=" + std::to_string(info_.discoveryPort) + "></label> "
        "<button>Save ports, turn off and restart</button></form><p>Saved ports override build defaults and survive normal firmware updates. Keep discovery at 32227 unless your network requires another port.</p>"
        "<p>There are no manual light controls here. Use Alpaca for control. Refresh to update status.</p>"
        "<p>Ready reports the commanded electrical state. Optical stabilization has not been measured. Physical Conform Universal, NINA and optical tests are pending.</p></html>";
    return {200, "text/html; charset=utf-8", html};
}
std::string Response::wire() const {
    const char* reason = status == 200 ? "OK" : status == 403 ? "Forbidden" : status == 500 ? "Internal Server Error" : "Bad Request";
    return "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\nContent-Type: " + type +
        "\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n" + body;
}
bool discoveryMessage(const std::string& packet) {
    // Reserved bytes may be appended by future clients; only v1 is implemented.
    return packet.size() >= 16 && packet.size() <= 64 && packet.compare(0, 16, "alpacadiscovery1") == 0;
}
}
