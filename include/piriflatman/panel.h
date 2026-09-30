#pragma once
#include <cstdint>

namespace pfm {
constexpr int MaxBrightness = 1000;
constexpr const char* FirmwareVersion = "0.1.0";
enum class CalibratorState { Off = 1, Ready = 3, Error = 5 };
enum Error { Ok = 0, NotImplemented = 0x400, InvalidValue = 0x401,
             NotConnected = 0x407, DriverError = 0x500 };
class Output {
public:
    virtual ~Output() = default;
    virtual bool begin() = 0;
    virtual bool write(uint32_t duty) = 0;
    virtual uint32_t maximum() const = 0;
};
class MockOutput final : public Output {
public:
    bool begin() override { duty = 0; return true; }
    bool write(uint32_t value) override { duty = value; return true; }
    uint32_t maximum() const override { return 4095; }
    uint32_t duty = 0;
};
class Panel {
public:
    explicit Panel(Output& output) : output_(output) {}
    bool begin();
    Error connect(bool value, bool asynchronous = false);
    void tick();
    void networkLost();
    Error on(int brightness);
    Error off();
    bool connected() const { return connected_; }
    bool connecting() const { return pending_; }
    int brightness() const { return brightness_; }
    CalibratorState state() const { return state_; }
    uint32_t duty() const { return duty_; }
    static uint32_t mapDuty(int brightness, uint32_t maximum);
private:
    bool forceOff();
    Output& output_;
    bool initialized_ = false, connected_ = false, pending_ = false, target_ = false;
    int brightness_ = 0;
    uint32_t duty_ = 0;
    CalibratorState state_ = CalibratorState::Off;
};
}
