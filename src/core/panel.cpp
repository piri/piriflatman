#include "piriflatman/panel.h"
namespace pfm {
bool Panel::begin() {
    connected_ = pending_ = false;
    brightness_ = 0;
    duty_ = 0;
    initialized_ = output_.begin();
    if (initialized_) initialized_ = output_.write(0);
    state_ = initialized_ ? CalibratorState::Off : CalibratorState::Error;
    return initialized_;
}
uint32_t Panel::mapDuty(int brightness, uint32_t maximum) {
    return static_cast<uint32_t>((static_cast<uint64_t>(brightness) * maximum + MaxBrightness / 2) / MaxBrightness);
}
bool Panel::forceOff() {
    brightness_ = 0;
    duty_ = 0;
    const bool ok = initialized_ && output_.write(0);
    state_ = ok ? CalibratorState::Off : CalibratorState::Error;
    return ok;
}
Error Panel::connect(bool value, bool asynchronous) {
    if (value && !initialized_) return DriverError;
    const bool offOk = value || forceOff();
    target_ = value;
    pending_ = asynchronous && connected_ != value;
    if (!pending_) connected_ = value;
    return offOk ? Ok : DriverError;
}
void Panel::tick() {
    if (pending_) { connected_ = target_; pending_ = false; }
}
void Panel::networkLost() {
    forceOff();
    connected_ = pending_ = target_ = false;
}
Error Panel::on(int brightness) {
    if (!connected_ || (pending_ && !target_)) return NotConnected;
    if (brightness < 0 || brightness > MaxBrightness) return InvalidValue;
    const auto duty = mapDuty(brightness, output_.maximum());
    if (!output_.write(duty)) {
        forceOff();
        state_ = CalibratorState::Error;
        return DriverError;
    }
    brightness_ = brightness;
    duty_ = duty;
    state_ = CalibratorState::Ready;
    return Ok;
}
Error Panel::off() {
    if (!connected_) return NotConnected;
    return forceOff() ? Ok : DriverError;
}
}
