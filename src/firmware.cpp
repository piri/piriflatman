#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_system.h>
#include <ctime>
#include "piriflatman/network.h"
#if __has_include("config.local.h")
#include "config.local.h"
#else
#include "config.example.h"
#endif

#if PFM_HARDWARE_OUTPUT
#if !defined(PFM_HARDWARE_VERIFIED) || PFM_HARDWARE_VERIFIED != 1 || !defined(PFM_PWM_GPIO) || !defined(PFM_PWM_HZ) || !defined(PFM_PWM_BITS)
#error "Hardware profile requires verified GPIO, frequency, resolution and PFM_HARDWARE_VERIFIED=1 in config.local.h"
#endif
#include <driver/gpio.h>
#include <driver/ledc.h>
class PwmOutput final : public pfm::Output {
public:
    bool begin() override {
        static_assert(PFM_PWM_BITS >= 10 && PFM_PWM_BITS <= 16, "Resolution must be 10..16 bits");
        static_assert(PFM_PWM_HZ > 0, "PWM frequency must be positive");
        // Classic ESP32 conservative allow-list excludes flash, UART0, straps and input-only pins.
        static_assert(PFM_PWM_GPIO == 4 || PFM_PWM_GPIO == 13 || PFM_PWM_GPIO == 14 ||
                      (PFM_PWM_GPIO >= 16 && PFM_PWM_GPIO <= 19) || PFM_PWM_GPIO == 21 ||
                      PFM_PWM_GPIO == 22 || PFM_PWM_GPIO == 23 || (PFM_PWM_GPIO >= 25 && PFM_PWM_GPIO <= 27) ||
                      PFM_PWM_GPIO == 32 || PFM_PWM_GPIO == 33, "GPIO excluded for provisional classic ESP32 target");
        const auto pin = static_cast<gpio_num_t>(PFM_PWM_GPIO);
        gpio_set_level(pin, 0);
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        ledc_timer_config_t timer{};
        timer.speed_mode = LEDC_LOW_SPEED_MODE;
        timer.duty_resolution = static_cast<ledc_timer_bit_t>(PFM_PWM_BITS);
        timer.timer_num = LEDC_TIMER_0;
        timer.freq_hz = PFM_PWM_HZ;
        timer.clk_cfg = LEDC_AUTO_CLK;
        if (ledc_timer_config(&timer) != ESP_OK) return false;
        ledc_channel_config_t channel{};
        channel.gpio_num = PFM_PWM_GPIO;
        channel.speed_mode = LEDC_LOW_SPEED_MODE;
        channel.channel = LEDC_CHANNEL_0;
        channel.timer_sel = LEDC_TIMER_0;
        channel.duty = 0;
        ready_ = ledc_channel_config(&channel) == ESP_OK;
        return ready_;
    }
    bool write(uint32_t duty) override {
        if (!ready_ || duty > maximum()) return false;
        if (ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty) == ESP_OK &&
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) == ESP_OK) return true;
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ready_ = false;
        return false;
    }
    uint32_t maximum() const override { return (1UL << PFM_PWM_BITS) - 1; }
private:
    bool ready_ = false;
};
PwmOutput output;
#else
pfm::MockOutput output;
#endif
pfm::Panel panel(output);
pfm::Info info;
pfm::Alpaca api(panel, info);
pfm::NetworkServer server(api);
bool configured = false, wasOnline = false, listening = false;
uint32_t lastAttempt = 0, lastListenAttempt = 0;
uint32_t restartAt = 0;

void setup() {
    // Output initialization precedes serial and networking; no saved brightness exists.
    const bool outputOk = panel.begin();
    Serial.begin(115200);
    info.id = pfm::uniqueId(ESP.getEfuseMac());
    info.mode = PFM_HARDWARE_OUTPUT ? "hardware (electrical verification required)" : "mock (no panel GPIO)";
    info.httpPort = PFM_HTTP_PORT;
    info.discoveryPort = PFM_DISCOVERY_PORT;
    Preferences preferences;
    if (preferences.begin("piriflatman", true)) {
        const uint32_t ports = preferences.getUInt("ports", 0);
        if ((ports >> 16) && (ports & 0xffff)) {
            info.httpPort = ports >> 16;
            info.discoveryPort = ports & 0xffff;
        }
        preferences.end();
    }
    info.savePorts = [](uint16_t http, uint16_t discovery) {
        Preferences settings;
        if (!settings.begin("piriflatman", false)) return false;
        const bool saved = settings.putUInt("ports", (static_cast<uint32_t>(http) << 16) | discovery) == sizeof(uint32_t);
        settings.end();
        if (saved) restartAt = millis();
        return saved;
    };
    static_assert(PFM_HTTP_PORT > 0 && PFM_HTTP_PORT <= 65535, "Invalid HTTP port");
    static_assert(PFM_DISCOVERY_PORT > 0 && PFM_DISCOVERY_PORT <= 65535, "Invalid discovery port");
    Serial.printf("PiriFlatMan %s; %s; identity %s; output init %s\n", pfm::FirmwareVersion, info.mode.c_str(), info.id.c_str(), outputOk ? "OK" : "FAILED");
    if (strlen(PFM_WIFI_SSID) == 0) {
        Serial.println("Wi-Fi disabled: configure ignored config.local.h and rebuild.");
        return;
    }
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    WiFi.setHostname("piriflatman");
    if (strlen(PFM_STATIC_IP) || strlen(PFM_GATEWAY) || strlen(PFM_SUBNET) || strlen(PFM_DNS)) {
        IPAddress ip, gateway, subnet, dns;
        if (!ip.fromString(PFM_STATIC_IP) || !gateway.fromString(PFM_GATEWAY) ||
            !subnet.fromString(PFM_SUBNET) || !dns.fromString(PFM_DNS) || !WiFi.config(ip, gateway, subnet, dns)) {
            Serial.println("Invalid static network configuration; Wi-Fi disabled.");
            WiFi.mode(WIFI_OFF);
            return;
        }
    }
    configured = true;
    WiFi.begin(PFM_WIFI_SSID, PFM_WIFI_PASSWORD);
    lastAttempt = millis();
}
void loop() {
    if (info.restartPending && static_cast<uint32_t>(millis() - restartAt) >= 1000) ESP.restart();
    panel.tick();
    const uint32_t now = millis();
    const bool online = configured && WiFi.status() == WL_CONNECTED;
    if (!online && wasOnline) {
        panel.networkLost();
        server.stop(); listening = false;
        info.address = "offline";
        Serial.println("Wi-Fi lost: light off and connection cleared.");
    }
    if (online && !wasOnline) {
        info.address = WiFi.localIP().toString().c_str();
        Serial.printf("Wi-Fi connected: http://%s:%u/setup\n", info.address.c_str(), info.httpPort);
        if (strlen(PFM_NTP_SERVER)) configTime(0, 0, PFM_NTP_SERVER);
        lastListenAttempt = now - 5000;
    }
    if (online && !listening && static_cast<uint32_t>(now - lastListenAttempt) >= 5000) {
        listening = server.begin(info.httpPort, info.discoveryPort);
        lastListenAttempt = now;
        if (!listening) { panel.networkLost(); Serial.println("Network listener failed; retrying with output off."); }
    }
    wasOnline = online;
    if (configured && !online && static_cast<uint32_t>(now - lastAttempt) >= 15000) {
        WiFi.disconnect(false, false);
        WiFi.begin(PFM_WIFI_SSID, PFM_WIFI_PASSWORD);
        lastAttempt = now;
    }
    if (listening) {
        time_t utc = time(nullptr);
        info.timestamp.clear();
        if (utc > 1700000000) {
            struct tm date{}; gmtime_r(&utc, &date);
            char text[32]; strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &date);
            info.timestamp = text;
        }
        server.tick(now);
    }
    delay(1); // Yield to Wi-Fi/RTOS; no wait-for-connection loop.
}
