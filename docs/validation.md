# Validation record and instructions

## Recorded results — 2026-09-29

No device was flashed, no physical GPIO was exercised and no LED/power module was energized. The original project brief remains unchanged. No commit or push was made.

| Check | Exact result | Scope |
| --- | --- | --- |
| ESP32 mock build | Success: `.pio/build/esp32-mock/firmware.bin`; RAM 50,148 / 327,680 bytes (15.3%); flash 997,361 / 1,310,720 bytes (76.1%) | Provisional classic ESP32 compile, empty public Wi-Fi configuration |
| Native build | Success with GCC 11.4.0, C++17, `-Wall -Wextra -Werror` | Ubuntu 22.04 host |
| C++ host tests | 10,116 assertions passed across six groups | State, monotonic mapping, output faults, parsing, contract, HTTP/discovery, setup persistence/restart policy |
| HTTP/UDP smoke | 98 HTTP checks passed, discovery response verified | Actual loopback sockets running shared C++ transport/API/state with MockOutput |
| Additional socket checks | Passed malformed discovery rejection, fragmented body, 100-continue, concurrent slow client and 5-second expiry | Host transport |
| AddressSanitizer + UndefinedBehaviorSanitizer | Same 10,116 assertions passed; no sanitizer diagnostics | Host C++ test executable |
| Hardware compile gate | Unconfigured hardware profile rejected as expected | Compile-only safety gate |
| Hardware adapter fixture | Configured synthetic fixture compiled: RAM 50,252 bytes, flash 1,002,889 bytes; temporary image deleted | Build-only, arbitrary fixture settings; no electrical approval |
| Conform Universal 4.5.0, build 53834.49ab847 | Host interface test exit 0: 0 errors, 0 issues, 0 configuration alerts, 0 timing issues | Official tool against host MockOutput only |
| Conform Universal protocol check | Final exit 0: no errors, issues or information alerts | Official protocol tool against host only |
| Physical Conform Universal | **Pending** | Real ESP32/lwIP/Wi-Fi/device reset behavior |
| NINA and Flat Wizard | **Pending** | Actual X38 installation and LAN |
| Electrical, thermal and optical | **Pending** | Actual board, J1/Q1/Q2 module, COB, diffuser and cameras |

Mock firmware SHA-256 for the build above (before adding local credentials):

```text
a820eb67d0b312d873d7381dbf76365b3af824fd4ea1c7a9638d0e1503d7f2ef
```

The official tool's own log includes a generic “passes” message. That result applies only to the host mock under the recorded settings. It is not a declaration of ASCOM conformity, hardware compatibility or NINA integration for the physical product.

### Official tool findings and evidence

Downloaded the official [ConformU 4.5.0 Linux x64 release](https://github.com/ASCOMInitiative/ConformU/releases/tag/v4.5.0), published 2026-08-06. Ran one cycle with the command-line defaults and all interface tests selected by the `conformance` command; the protocol command suppresses routine OK messages. The tool's interface test does not exercise every optional method, so the additional unit/smoke checks remain relevant.

The first protocol run returned exit 1, **0 errors, 1 issue, 7 information messages**. It expected HTTP 400 for incorrect PUT Brightness casing; the implementation initially accepted it. Optional PUT ClientTransactionID casing generated the seven informational messages. The implementation now follows API Reference V12 §2.2.3: strict PUT names, case-insensitive GET names, and ignored unknown optional PUT names. Regression tests cover that distinction. The final runs passed.

Retained evidence:

- [Initial protocol findings, complete](validation-evidence/host-protocol-initial.txt)
- [Final interface log](validation-evidence/host-conformance-final.log)
- [Final interface machine-readable result](validation-evidence/host-conformance-final.json)
- [Final protocol output](validation-evidence/host-protocol-final.txt)

The protocol command did not create the requested JSON results file in this release; its complete console output and local `.log` are retained instead. Generated local artifacts live under ignored `artifacts/`; curated host evidence above contains only localhost endpoints and mock identity.

During initial project creation, an early dependency-install/build attempt linked before firmware entry points existed and failed with missing setup/loop. The first native compile found two test harness compile errors (an incomplete aggregate initializer and a missing `<cstring>` include); both were fixed. Final reported builds/tests include those fixes. There are no outstanding build/tool-install blockers.

## Reproduce without hardware

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-dev.lock.txt
.venv/bin/pio run -e native -e esp32-mock
python3 tools/validate_host.py
python3 tools/check_hardware_build.py
```

Core tests can also run alone:

```sh
.pio/build/native/program --test
```

Interactive host mock:

```sh
.pio/build/native/program --serve 8080 32227
# A second terminal:
python3 tools/smoke_test.py --host 127.0.0.1 --port 8080 --exercise
```

The host binds IPv4 interfaces for testing, uses a host-only identity and never drives hardware. Its setup page is rendered by the real contract layer; NVS persistence and reboot are simulated via an injected callback in unit tests, not implemented as actual host settings changes.

Sanitizer reproduction:

```sh
mkdir -p artifacts
g++ -std=c++17 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer -g \
  -Iinclude -I.pio/libdeps/native/ArduinoJson/src \
  src/core/*.cpp src/host.cpp -o artifacts/host-sanitized
artifacts/host-sanitized --test
```

Optional official tool reproduction after downloading/extracting its Linux x64 release:

```sh
python3 tools/run_conform_host.py --conformu /path/to/conformu
```

The runner starts a temporary host server, runs `conformance` and `alpacaprotocol` sequentially against `http://127.0.0.1:PORT/api/v1/covercalibrator/0`, saves logs/results to `artifacts/conform-host/`, and shuts the server down. It does not contact a physical device.

## First physical test: mock profile only

1. Identify the exact ESP32 module, flash size and board. Confirm the provisional `esp32dev` target fits; change and compile a correct target first if it does not. Do not use USB-C shape as identification.
2. Disconnect every MOSFET/J1/12 V/COB connection. Power only the development board through USB.
3. Copy the public configuration to the ignored local file, enter credentials locally, then rebuild:

   ```sh
   cp -n config.example.h config.local.h
   chmod 600 config.local.h
   # Edit PFM_WIFI_SSID and PFM_WIFI_PASSWORD in config.local.h.
   .venv/bin/pio run -e esp32-mock
   .venv/bin/pio device list
   ```

4. Only after confirming the target and USB port, upload and monitor (replace `/dev/ttyUSB0` with the actual port):

   ```sh
   .venv/bin/pio run -e esp32-mock -t upload --upload-port /dev/ttyUSB0
   .venv/bin/pio device monitor -b 115200 --port /dev/ttyUSB0
   ```

5. Open the printed `/setup` address and confirm the **mock** profile, identity, Off state and brightness zero. In another terminal, replace `DEVICE_IP` with the printed address:

   ```sh
   python3 tools/smoke_test.py --host DEVICE_IP --port 8080 --exercise
   ```

6. Record UniqueID. Command nonzero mock brightness, reset the board and reconnect; brightness must stay zero/Off until a new command. Repeat across USB power cycling, firmware upload, DHCP reassignment and NVS erase if performed intentionally later. Identity must remain the same on the same chip.
7. With nonzero mock brightness active, interrupt the ESP32's access-point association. Restore it and confirm automatic network recovery, Connected=false and output Off. A stopped NINA client with Wi-Fi still associated should not trigger the radio-loss policy. Run one long simulated exposure interval with no polling; illumination state should persist while Wi-Fi remains healthy.
8. Test port setup: change HTTP/discovery ports through the page, verify off/restart, rediscover using the selected UDP port, confirm persistence after reset, then return discovery to 32227. Wi-Fi password must never appear in serial output, HTML or HTTP responses.

## Physical Conform Universal and NINA — pending

Keep mock output and the power module disconnected. Record ESP32/module revision, firmware hash/profile, local network topology and test-tool versions.

Using the official installed ConformU executable, with your real device address:

```sh
conformu conformance http://DEVICE_IP:8080/api/v1/covercalibrator/0 \
  --logfilename physical-conformance.log --resultsfile physical-conformance.json
conformu alpacaprotocol http://DEVICE_IP:8080/api/v1/covercalibrator/0 \
  --logfilename physical-protocol.log --resultsfile physical-protocol.json
```

Also use ConformU's Discovery Map/discovery tests on the actual LAN. Retain complete logs, options, versions and every failure; resolve applicable findings before claiming any conformity. Do not assume that a localhost discovery response validates LAN broadcasts or firewall settings.

For NINA, record the installed version. On the same reachable LAN, use its Alpaca flat-device discovery/selection and select PiriFlatMan. If direct address selection is available, use the device IP, TCP port and device 0. Test connect/disconnect/reconnect, zero/minimum/mid/maximum brightness, reported no-cover capability, Flat Wizard's command flow, abort/off, NINA restart, firmware reset and Wi-Fi loss. A second client disconnect is intentionally global. While LEDs remain disconnected, Flat Wizard's **optical exposure solving cannot be validated**; only its device command flow can be observed.

## Electrical and optical validation — pending

Complete [hardware identification and review](hardware.md) before creating a hardware profile. Then use a verified current-limited/fused assembly to measure reset-off behavior, logic levels, PWM and current before full power. Record frequency/resolution/GPIO only after verification. Run physical Conform/NINA again with that profile, then real flats covering the intended exposure range, filters and cameras. Measure gradients, repeatability, banding, minimum useful output, thermal equilibrium and settling time. Preserve results separately from mock evidence.
