# PiriFlatMan

ESP32 DIY flat-field panel, controlled over Wi-Fi by native ASCOM Alpaca. Publishes one **CoverCalibrator, device 0, PiriFlatMan**, implementing **ICoverCalibratorV2** through Alpaca API **v1**.

The first milestone is USB-flashable **mock-output firmware**: discovery, management, status/setup and calibrator control work without connecting the power module. Physical NINA, Conform Universal and optical validation remain pending. This repository does not claim ASCOM conformity or verified hardware compatibility.

## Build and test

Requirements: Python 3.10+, a C++17 host compiler, Internet access for the first dependency install. Commands below use Linux/macOS shell syntax; the recorded host validation used Ubuntu 22.04 and GCC 11.4.

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-dev.lock.txt
.venv/bin/pio run -e native
python3 tools/validate_host.py
.venv/bin/pio run -e esp32-mock
```

The default `pio run` also builds `esp32-mock`. Platform, ESP32 compiler, Arduino framework and ArduinoJson are pinned in [platformio.ini](platformio.ini). The firmware image is `.pio/build/esp32-mock/firmware.bin`; PlatformIO upload uses this with the generated bootloader and partition table.

No credentials are needed to compile. Without local configuration, Wi-Fi stays disabled and output stays off. To prepare a network-enabled image:

```sh
cp -n config.example.h config.local.h
chmod 600 config.local.h
# Edit config.local.h locally: PFM_WIFI_SSID and PFM_WIFI_PASSWORD.
.venv/bin/pio run -e esp32-mock
```

`config.local.h` is ignored. Credentials are embedded in the built image, so do not publish locally configured firmware binaries. Firmware diagnostics never print credentials. Default ports, optional static addressing and optional local NTP are configured in the same file. The setup page also lets you save HTTP/discovery ports; these persist in NVS and override build defaults. Saving turns output off and restarts.

## First physical test: USB power, mock output

**First confirm the module and pinout.** `esp32dev` is a provisional target for a classic ESP32 with 4 MB flash. USB-C does not identify the chip; this image is not a confirmed fit for the board in the brief. If it is an S2/S3/C3 or another variant, select and validate the correct board before upload.

Leave the MOSFET module, J1 controls, COB strip and 12 V supply disconnected. Once the classic ESP32 target is confirmed, replace the example port and IP below:

```sh
.venv/bin/pio device list
.venv/bin/pio run -e esp32-mock -t upload --upload-port /dev/ttyUSB0
.venv/bin/pio device monitor -b 115200 --port /dev/ttyUSB0
# In another terminal, use the IP printed by the firmware:
python3 tools/smoke_test.py --host 192.0.2.10 --port 8080 --exercise
```

`192.0.2.10` is a documentation placeholder. Open `http://DEVICE_IP:8080/setup`. The page must show **mock (no panel GPIO)**. The smoke test performs discovery and brightness commands, then turns off and disconnects; it refuses illumination commands on a non-mock profile unless explicitly allowed. `python3 tools/smoke_test.py` discovers devices and performs read-only checks by default.

On the same LAN, select the discovered PiriFlatMan CoverCalibrator in NINA's flat-device/Alpaca selection. Where the installed version supports a direct Alpaca address, use the device IP, port 8080 and device number 0. Confirm the actual NINA version and follow [physical validation](docs/validation.md); no NINA compatibility result has been recorded yet.

## Behavior

- No cover: `CoverState=NotPresent`, `CoverMoving=false`; cover operations return NotImplemented.
- Brightness is an integer **0–1000**. `CalibratorOn(0)` gives Ready with duty zero; `CalibratorOff` gives Off with brightness zero.
- Ready means the commanded electrical state has been applied. There is no photodiode or measured optical settling time.
- Boot/reset, explicit disconnect and detected Wi-Fi loss clear brightness. Reconnection never restores light automatically.
- Connection state is shared across clients. Any client can disconnect the device and turn it off. ClientID is for transaction identification, not ownership.
- No HTTP inactivity light timer, open access point, OTA, physical button or browser light controls in this milestone.
- HTTP/UDP serve the local IPv4 network without authentication. Use on a trusted LAN.

## Hardware output

The `esp32-hardware` profile requires explicit `PFM_HARDWARE_VERIFIED=1`, `PFM_PWM_GPIO`, `PFM_PWM_HZ` and `PFM_PWM_BITS` in the local configuration. There are **no approved hardware settings yet**. Building this profile without those definitions intentionally fails. The adapter uses active-high LEDC PWM; its suitability for J1 is unknown.

`python3 tools/check_hardware_build.py` verifies the compile gate and compiles the adapter in a temporary synthetic fixture. It does not upload, retain that image, or approve its fixture values for wiring.

## Documentation

- [Original project brief — preserved](docs/PiriFlatMan-project-brief.md)
- [Official Alpaca contract and requirements matrix](docs/alpaca-contract.md)
- [Architecture and reliability policies](docs/architecture.md)
- [Dependency evaluation and licenses](docs/dependencies.md)
- [Hardware facts and unresolved decisions](docs/hardware.md)
- [Validation results and physical test procedure](docs/validation.md)

The owner's license choice for original code remains open; the brief proposed MIT but did not approve it. No license for original code has been assigned here. Third-party component licenses are recorded separately. No commit or push has been made by this implementation task.
