# Dependency and reference review

Reviewed 2026-09-29. Official ASCOM specifications govern behavior; third-party examples and their test claims are not evidence of this device's conformity. No code was copied from either candidate project.

## ESP32AlpacaDevices2

[Repository](https://github.com/npeter/ESP32AlpacaDevices2), inspected commit [`bb88a94ea2630a39405d8b5c17bb433052088b21`](https://github.com/npeter/ESP32AlpacaDevices2/tree/bb88a94ea2630a39405d8b5c17bb433052088b21), latest inspected commit dated 2025-11-18. `library.json` reports 4.4.1; changelog labels the latest section 4.4.0. Root LICENSE: MIT, copyright Peter Neeser 2024.

- Arduino/espressif32 library; its core uses ESPAsyncWebServer. Version metadata declares ESPAsyncWebServer `^3.9.0`, ArduinoJson `^7.4.2`, and Git URL dependencies on ElegantOTA and SLog without commit pins.
- `AlpacaCoverCalibrator.cpp` and `AlpacaDevice.cpp` register the calibrator, common V2 connection, state and change-indicator members. The server includes discovery, management and setup. Coverage is substantial; the old V1 file comment alone does not describe its current coverage.
- Changelog reports an upstream NINA 3.2 test. That is an upstream statement, not an independently reproduced validation of this repository.
- Inspected connection handlers require positive ClientID and ClientTransactionID, use a fixed per-client table and strict form-name parsing in several PUT methods. The ID and session choices differ from this project's optional/zero-ID support and documented global connection policy. Strict PUT name casing is retained here based on the official protocol reference. The Action handler looks for `Action`, whereas the official API parameter is `ActionName`.
- The dependency graph brings OTA, logging and web assets beyond this milestone. Compatibility with the exact pinned Arduino 2.0.17 configuration here was not independently built; Arduino-family metadata is not a guarantee of compatibility with every core version.

**Decision:** do not adopt or adapt source. Use as a reference for coverage only. A small original contract layer makes parameter behavior, output isolation and host execution directly reviewable and avoids patching inherited connection policies.

## TeslaBoard

[Repository](https://github.com/StefanoTesla/TeslaBoard), inspected commit [`ffb3b93c7f7000a410570ebe4cfc3796cdc27973`](https://github.com/StefanoTesla/TeslaBoard/tree/ffb3b93c7f7000a410570ebe4cfc3796cdc27973), latest inspected commit dated 2024-11-19. Root LICENSE: MIT, copyright StefanoTesla 2021.

- PlatformIO Arduino application targeting `esp32doit-devkit-v1` with an unpinned `espressif32` platform. Dependencies include ESPAsyncWiFiManager 0.31.0, ElegantOTA 3.1.6 and ArduinoJson 7.2.0. Legacy `ledcRead(0)` and channel-oriented PWM calls tie the inspected source to the older Arduino LEDC API; no Arduino 3.x compatibility is established by this review.
- Includes CoverCalibrator, Dome, Switch, discovery, management and UI code. It is a complete controller application, not a minimal panel-state library.
- `src/CoverC/alpacaDevice.h` hard-codes CalibratorState and CoverState to numeric 3, reports MaxBrightness 8192 and reads brightness directly from LEDC. This does not express our absent-cover or off-state behavior. Brightness validation checks a field declared unsigned for values below zero. Its inspected CoverCalibrator route set does not provide all V2 members.
- The exact application was not compiled or electrically tested in this task. Its board and switching choices cannot identify or validate this project's unknown hardware.

**Decision:** reference only; do not adopt/adapt source. Independent state handling is necessary.

## Adopted build components

| Component | Pin | License / role |
| --- | --- | --- |
| PlatformIO Core | 6.1.18 | Apache-2.0; development/build tool |
| PlatformIO espressif32 | 6.12.0 | Apache-2.0; board/build integration |
| Arduino ESP32 | package 3.20017.0 = Arduino 2.0.17 | LGPL-2.1-or-later at package level; bundled components retain their own licenses |
| Xtensa compiler | 8.4.0+2021r2-patch5 | GCC/toolchain component licenses; build tool |
| esptool package | 2.40900.250804 = esptool 4.9.0 | GPL-2.0-or-later; host upload/build-image tool |
| ArduinoJson | 7.4.2 | MIT; JSON serialization, no network/hardware code |
| PlatformIO native | 1.2.1 | Apache-2.0; host build integration |

Framework Wi-Fi, sockets, LEDC and time APIs come from the pinned Arduino/ESP-IDF distribution. There are no AsyncTCP, external web server, OTA, filesystem UI or copied driver dependencies. The official [PlatformIO manifest](https://github.com/platformio/platform-espressif32/blob/v6.12.0/platform.json) maps this platform to Arduino 2.0.17; the resolved build package versions are recorded in validation. Relevant license sources: [Arduino ESP32](https://github.com/espressif/arduino-esp32/blob/2.0.17/LICENSE.md), [ArduinoJson](https://github.com/bblanchon/ArduinoJson/blob/v7.4.2/LICENSE.txt), [esptool](https://github.com/espressif/esptool/blob/v4.9.0/LICENSE).

`requirements-dev.txt` specifies the direct development tool; `requirements-dev.lock.txt` captures its resolved Python environment. Firmware inputs are exact pins, not floating Git branches. The native build uses the installed host compiler (recorded version in validation), and no claim of bit-for-bit cross-OS binary reproducibility is made. PlatformIO may install host utility Python dependencies into its own environment; those do not change the pinned firmware library sources.

Original-code MIT licensing was a proposal in the brief and remains an owner decision. No original-code LICENSE was invented. Dependency licensing does not confer a license on this project's original files; before public binary distribution, preserve the bundled notices and satisfy applicable source/relinking requirements for linked components.

## NINA source review

Inspected [AscomCoverCalibrator.cs](https://github.com/isbeorn/nina/blob/271420620874012b6373fd64043db1420243b636/NINA.Equipment/Equipment/MyFlatDevice/AscomCoverCalibrator.cs) and [AlpacaDirectCoverCalibrator.cs](https://github.com/isbeorn/nina/blob/271420620874012b6373fd64043db1420243b636/NINA.Equipment/Equipment/MyFlatDevice/AlpacaDirectCoverCalibrator.cs) at commit `271420620874012b6373fd64043db1420243b636` on 2026-09-29. The adapter sets MinBrightness to 0, reads MaxBrightness from the device, clamps requested values to that interval and calls CalibratorOn. It reports SupportsOpenClose from CoverState != NotPresent. The direct Alpaca adapter delegates to that implementation.

This supports choosing a stable 0–1000 API range with at least 10-bit PWM. It does not establish the behavior of the user's installed NINA release, Flat Wizard, discovery network or cameras. Those checks remain pending.
