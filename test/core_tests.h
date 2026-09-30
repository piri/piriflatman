#pragma once
#include "piriflatman/network.h"
#include <ArduinoJson.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <functional>
#include <vector>

namespace tests {
inline int checks = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << ": " << #expr << " failed\n"; std::exit(1); } } while (false)
struct Rig {
    pfm::MockOutput output;
    pfm::Panel panel{output};
    pfm::Info info{pfm::uniqueId(0x112233445566ULL), "mock", "offline", "", 8080, 32227};
    pfm::Alpaca api{panel, info};
    Rig() { CHECK(panel.begin()); }
    pfm::Response request(std::string method, std::string member, std::string parameters = "") {
        return api.handle({method, "/api/v1/covercalibrator/0/" + member,
            method == "GET" ? parameters : "", method == "PUT" ? parameters : "", "application/x-www-form-urlencoded"});
    }
    JsonDocument json(const pfm::Response& response, int error = 0) {
        CHECK(response.status == 200);
        CHECK(response.type == "application/json");
        JsonDocument doc;
        CHECK(!deserializeJson(doc, response.body));
        CHECK(doc["ErrorNumber"].as<int>() == error);
        CHECK(doc["ClientTransactionID"].is<uint32_t>());
        CHECK(doc["ServerTransactionID"].as<uint32_t>() > 0);
        CHECK((doc["ErrorMessage"].as<std::string>().empty()) == (error == 0));
        if (error) CHECK(doc["Value"].isNull());
        return doc;
    }
};
inline void panelTests() {
    Rig r;
    CHECK(!r.panel.connected()); CHECK(r.panel.brightness() == 0); CHECK(r.output.duty == 0);
    CHECK(r.panel.state() == pfm::CalibratorState::Off);
    CHECK(r.panel.on(500) == pfm::NotConnected);
    CHECK(r.panel.off() == pfm::NotConnected);
    CHECK(r.panel.connect(true, true) == pfm::Ok); CHECK(r.panel.connecting()); CHECK(!r.panel.connected());
    r.panel.tick(); CHECK(!r.panel.connecting()); CHECK(r.panel.connected());
    CHECK(r.panel.on(0) == pfm::Ok); CHECK(r.panel.state() == pfm::CalibratorState::Ready); CHECK(r.output.duty == 0);
    CHECK(r.panel.on(1000) == pfm::Ok); CHECK(r.output.duty == 4095);
    CHECK(r.panel.on(1001) == pfm::InvalidValue); CHECK(r.panel.on(-1) == pfm::InvalidValue);
    CHECK(r.panel.brightness() == 1000); CHECK(r.output.duty == 4095);
    CHECK(r.panel.connect(true) == pfm::Ok); CHECK(r.output.duty == 4095);
    CHECK(r.panel.off() == pfm::Ok); CHECK(r.panel.brightness() == 0); CHECK(r.panel.state() == pfm::CalibratorState::Off);
    CHECK(r.panel.on(700) == pfm::Ok);
    CHECK(r.panel.connect(false, true) == pfm::Ok); CHECK(r.output.duty == 0);
    CHECK(r.panel.on(100) == pfm::NotConnected);
    r.panel.tick(); CHECK(!r.panel.connected());
    r.panel.connect(true); r.panel.on(1000); r.panel.networkLost();
    CHECK(!r.panel.connected()); CHECK(!r.panel.connecting()); CHECK(r.panel.brightness() == 0); CHECK(r.output.duty == 0);
    r.panel.connect(true); CHECK(r.panel.state() == pfm::CalibratorState::Off);
    r.panel.on(999); CHECK(r.panel.begin()); CHECK(r.output.duty == 0); CHECK(!r.panel.connected());
    for (uint32_t maximum : {1023U, 4095U, 65535U}) {
        uint32_t prev = 0;
        for (int b = 0; b <= 1000; ++b) {
            const auto duty = pfm::Panel::mapDuty(b, maximum);
            CHECK(duty >= prev); CHECK(duty <= maximum);
            if (b > 0) CHECK(duty > prev);
            prev = duty;
        }
        CHECK(prev == maximum);
    }
}
inline void faultTests() {
    struct Fault : pfm::Output {
        bool init = true, fail = false; uint32_t duty = 123;
        bool begin() override { return init; }
        bool write(uint32_t d) override { if (fail) return false; duty = d; return true; }
        uint32_t maximum() const override { return 4095; }
    } o;
    pfm::Panel p(o);
    o.init = false; CHECK(!p.begin()); CHECK(p.state() == pfm::CalibratorState::Error); CHECK(p.connect(true) == pfm::DriverError);
    o.init = true; CHECK(p.begin()); CHECK(o.duty == 0); CHECK(p.connect(true) == 0);
    CHECK(p.on(50) == 0); o.fail = true;
    CHECK(p.on(100) == pfm::DriverError); CHECK(p.state() == pfm::CalibratorState::Error);
    CHECK(p.brightness() == 0); CHECK(p.off() == pfm::DriverError);
    CHECK(p.connect(false) == pfm::DriverError); CHECK(!p.connected());
}
inline void parameterTests() {
    pfm::Parameters p;
    CHECK(pfm::parseForm("BrIgHtNeSs=12&unknown=hello+world%21", p)); CHECK(p["brightness"] == "12"); CHECK(p["unknown"] == "hello world!");
    CHECK(pfm::parseForm("Parameters=&ActionName=a%26b%3Dc", p)); CHECK(p["actionname"] == "a&b=c");
    for (const auto& s : {"x=%", "x=%z0", "x=%00", "x=%0a", "x=1&X=2", "missing", "=value"}) CHECK(!pfm::parseForm(s, p));
    uint32_t u = 0; int32_t i = 0;
    CHECK(pfm::unsignedInteger("4294967295", u)); CHECK(u == UINT32_MAX);
    for (const auto& s : {"", "-1", "+1", "1.0", "1e2", "1junk", " 1", "4294967296", "999999999999999999999999"}) CHECK(!pfm::unsignedInteger(s, u));
    CHECK(pfm::signedInteger("-2147483648", i)); CHECK(i == INT32_MIN);
    CHECK(pfm::signedInteger("+2147483647", i)); CHECK(i == INT32_MAX);
    for (const auto& s : {"2147483648", "-2147483649", "--1", "-", "+", "0.0"}) CHECK(!pfm::signedInteger(s, i));
}
inline void apiTests() {
    Rig r;
    const std::vector<std::pair<std::string, std::string>> strings = {{"name","PiriFlatMan"},{"driverversion","0.1"}};
    for (const auto& item : strings) CHECK(r.json(r.request("GET", item.first))["Value"].as<std::string>() == item.second);
    for (const auto& m : {"description", "driverinfo"}) CHECK(!r.json(r.request("GET",m))["Value"].as<std::string>().empty());
    CHECK(r.json(r.request("GET","description"))["Value"].as<std::string>().size() <= 64);
    CHECK(r.json(r.request("GET","interfaceversion"))["Value"] == 2);
    CHECK(r.json(r.request("GET","supportedactions"))["Value"].as<JsonArray>().size() == 0);
    for (const auto& m : {"brightness","maxbrightness","calibratorstate","calibratorchanging","coverstate","covermoving","devicestate"}) r.json(r.request("GET",m), pfm::NotConnected);
    CHECK(r.request("PUT","connected","CoNnEcTeD=TrUe").status == 400);
    r.json(r.request("PUT","connected","Connected=TrUe&ClientID=4294967295&ClientTransactionID=4294967295"));
    CHECK(r.json(r.request("PUT","connected","Connected=true&clienttransactionid=12"))["ClientTransactionID"] == 0);
    CHECK(r.json(r.request("GET","brightness","ClientTransactionID=4294967295"))["ClientTransactionID"].as<uint32_t>() == UINT32_MAX);
    CHECK(r.json(r.request("GET","brightness"))["ClientTransactionID"] == 0);
    for (auto m : {"opencover","closecover","haltcover"}) r.json(r.request("PUT",m), pfm::NotImplemented);
    CHECK(r.json(r.request("GET","coverstate"))["Value"] == 0);
    CHECK(r.json(r.request("GET","covermoving"))["Value"].is<bool>());
    CHECK(!r.json(r.request("GET","covermoving"))["Value"].as<bool>());
    for (int b : {0, 1, 500, 1000}) {
        r.json(r.request("PUT","calibratoron","Brightness="+std::to_string(b)));
        CHECK(r.json(r.request("GET","brightness"))["Value"] == b);
        CHECK(r.json(r.request("GET","calibratorstate"))["Value"] == 3);
        CHECK(!r.json(r.request("GET","calibratorchanging"))["Value"].as<bool>());
    }
    for (auto b : {"-1","1001","2147483647","-2147483648"}) r.json(r.request("PUT","calibratoron",std::string("Brightness=")+b), pfm::InvalidValue);
    for (auto b : {"","1x","1.5","2147483648","%00"}) CHECK(r.request("PUT","calibratoron",std::string("Brightness=")+b).status == 400);
    CHECK(r.request("PUT","calibratoron").status == 400);
    CHECK(r.request("PUT","calibratoron","brightness=2").status == 400);
    CHECK(r.request("PUT","calibratoron","Brightness=1&brightness=2").status == 400);
    for (auto s : {"Connected=1","Connected=","Other=true"}) CHECK(r.request("PUT","connected",s).status == 400);
    r.json(r.request("PUT","calibratoron","Brightness=%2B25&future=value"));
    auto ds = r.json(r.request("GET","devicestate")); CHECK(ds["Value"].as<JsonArray>().size() == 5);
    r.info.timestamp = "2026-09-29T00:00:00Z";
    ds = r.json(r.request("GET","devicestate")); CHECK(ds["Value"].as<JsonArray>().size() == 6);
    CHECK(ds["Value"][5]["Name"] == "TimeStamp"); CHECK(ds["Value"][5]["Value"] == r.info.timestamp);
    r.json(r.request("PUT","action","ActionName=any&Parameters="), pfm::NotImplemented);
    for (auto m : {"commandblind","commandbool","commandstring"}) r.json(r.request("PUT",m,"Command=foo&Raw=FALSE"),pfm::NotImplemented);
    r.json(r.request("PUT","calibratoroff")); CHECK(r.json(r.request("GET","calibratorstate"))["Value"] == 1);
    CHECK(r.json(r.request("GET","brightness"))["Value"] == 0);
    r.json(r.request("PUT","disconnect")); r.panel.tick(); CHECK(!r.panel.connected());
    // IDs identify transactions; there are no per-client sessions.
    r.json(r.request("PUT","connected","Connected=true&ClientID=1"));
    r.json(r.request("PUT","calibratoron","Brightness=20&ClientID=2"));
    r.json(r.request("PUT","connected","Connected=false&ClientID=3"));
    CHECK(!r.panel.connected()); CHECK(r.output.duty == 0);
    for (auto path : {"/api/v2/covercalibrator/0/name","/api/v1/covercalibrator/1/name","/API/v1/covercalibrator/0/name","/api/v1/covercalibrator/0/Name","/api/v1/camera/0/name","/api/v1/covercalibrator/0/setupdialog"}) CHECK(r.api.handle({"GET",path,"","",""}).status == 400);
    for (auto path : {"/management/apiversions","/management/v1/description","/management/v1/configureddevices"}) r.json(r.api.handle({"GET",path,"","",""}));
    auto devices = r.json(r.api.handle({"GET","/management/v1/configureddevices","","",""}));
    CHECK(devices["Value"][0]["UniqueID"] == r.info.id);
    CHECK(devices["Value"].as<JsonArray>().size() == 1);
    for (auto path : {"/","/setup","/setup/v1/covercalibrator/0/setup"}) {
        auto response = r.api.handle({"GET",path,"","",""}); CHECK(response.status == 200); CHECK(response.type.find("text/html") == 0);
    }
    CHECK(r.api.handle({"GET","/setup/v1/covercalibrator/1/setup","","",""}).status == 403);
    CHECK(pfm::uniqueId(0x112233445566ULL) == r.info.id);
    CHECK(pfm::uniqueId(0x112233445567ULL) != r.info.id);
}
inline void httpTests() {
    const std::string wire = "PUT /api/v1/covercalibrator/0/calibratoron HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: 14\r\n\r\nBrightness=100";
    for (size_t split = 0; split <= wire.size(); ++split) {
        pfm::HttpRequest p;
        p.feed(wire.data(), split);
        CHECK(p.feed(wire.data()+split, wire.size()-split) == pfm::HttpRequest::State::Complete);
        CHECK(p.request().body == "Brightness=100");
        CHECK(p.request().method == "PUT");
    }
    for (auto data : {"PUT / HTTP/1.1\r\nContent-Length: -1\r\n\r\n", "PUT / HTTP/1.1\r\nContent-Length: 2049\r\n\r\n", "PUT / HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\n", "PUT / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", "GET / HTTP/7\r\n\r\n", "GET / HTTP/1.1\r\nBad header\r\n\r\n"}) {
        pfm::HttpRequest p; CHECK(p.feed(data, std::strlen(data)) == pfm::HttpRequest::State::Invalid);
    }
    pfm::HttpRequest huge; std::string h(4097, 'x'); CHECK(huge.feed(h.data(),h.size()) == pfm::HttpRequest::State::Invalid);
    pfm::HttpRequest expect;
    std::string header = "PUT / HTTP/1.1\r\nContent-Length: 3\r\nExpect: 100-continue\r\n\r\n";
    CHECK(expect.feed(header.data(),header.size()) == pfm::HttpRequest::State::Reading);
    CHECK(expect.takeContinue()); CHECK(!expect.takeContinue()); CHECK(expect.feed("a=b",3) == pfm::HttpRequest::State::Complete);
    CHECK(pfm::discoveryMessage("alpacadiscovery1")); CHECK(pfm::discoveryMessage("alpacadiscovery1reserved"));
    for (auto packet : {"","alpacadiscovery","alpacadiscovery2","Alpacadiscovery1","junkalpacadiscovery1"}) CHECK(!pfm::discoveryMessage(packet));
    CHECK(!pfm::discoveryMessage(std::string("alpacadiscovery1") + std::string(49,'x')));
}
inline void setupTests() {
    Rig r;
    r.panel.connect(true); r.panel.on(500);
    auto post = [&r](std::string body) {
        return r.api.handle({"POST", "/setup/ports", "", body, "application/x-www-form-urlencoded"});
    };
    for (auto body : {"", "httpport=0&discoveryport=32227", "httpport=65536&discoveryport=32227", "httpport=8080&discoveryport=garbage"}) CHECK(post(body).status == 400);
    CHECK(r.output.duty > 0); CHECK(!r.info.restartPending);
    r.info.savePorts = [](uint16_t, uint16_t) { return false; };
    CHECK(post("httpport=8081&discoveryport=32227").status == 500);
    CHECK(!r.info.restartPending);
    bool saved = false;
    r.info.savePorts = [&saved](uint16_t http, uint16_t discovery) {
        CHECK(http == 8081); CHECK(discovery == 32228); saved = true; return true;
    };
    CHECK(post("httpport=8081&discoveryport=32228").status == 200);
    CHECK(saved); CHECK(r.info.restartPending); CHECK(r.output.duty == 0); CHECK(!r.panel.connected());
    CHECK(r.request("PUT","connect").status == 400);
    CHECK(post("httpport=8081&discoveryport=32228").status == 400);
    CHECK(r.request("GET","name").status == 200);
}
inline int run() {
    for (const auto& test : std::vector<std::pair<const char*, std::function<void()>>>{{"panel and mapping",panelTests},{"output faults",faultTests},{"parameter parsing",parameterTests},{"Alpaca contract",apiTests},{"HTTP framing and discovery",httpTests},{"setup persistence and restart",setupTests}}) {
        test.second(); std::cout << "PASS " << test.first << '\n';
    }
    std::cout << "PASS " << checks << " assertions\n";
    return 0;
}
#undef CHECK
}
