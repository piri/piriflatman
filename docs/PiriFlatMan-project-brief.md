# PiriFlatMan — ESP32 Alpaca flat panel
Design brief and implementation handoff · 2026-09-29

## Goal
Build an open-source DIY flat-field panel for X38 Observatory, controlled by NINA over the local network. The ESP32 runs an ASCOM Alpaca device server and drives a 12 V COB LED strip through an external MOSFET. No motorized telescope cover is present.

This is a starting specification, not a claim of ASCOM conformity. The implementation must verify the current official specifications and report the exact interface version implemented. Earlier discussion of particular ASCOM/API versions or third-party compliance must not substitute for verification.

## Agreed architecture
NINA on Windows → LAN HTTP / Alpaca → ESP32 Wi-Fi → PWM GPIO → MOSFET → COB strip.

USB remains available for flashing, power and serial diagnostics. Initial development uses USB flashing; OTA is outside the first milestone.

Publish one device: CoverCalibrator, device number 0, name PiriFlatMan.
Implement calibrator functionality only. CoverState reports NotPresent; cover methods must follow the official behavior for an absent cover.

## Known hardware and open questions
| Item | Status |
| --- | --- |
| ESP32 development board with USB-C | Identified in earlier photos; exact board/module and USB bridge still to be verified from markings |
| Wi-Fi | ESP32 family proposed here uses 2.4 GHz; verify actual module before choosing build target |
| Light source | 12 V COB LED strip, approximately 5 m |
| Switching | External N-channel MOSFET; exact part/module unknown |
| Gate pull-down | 10 kΩ discussed; confirm actual wiring |
| Supply | 12 V supply proposed; final rating and measured panel current to confirm |
| Diffusers | First layer approximately 60 mm above LEDs; final second-layer spacing not fixed here |
| Cover motor / optical sensor | None |
| PWM GPIO, frequency and resolution | To be selected after board and MOSFET verification |
| Local button | Exists in physical design; final input wiring and interaction policy to confirm |

Do not infer the exact ESP32 module or CP2102 bridge solely from the previous conversational identification: the images were not available for inspection during this document preparation.

ESP32 GPIO must never receive 12 V. Use a switching stage that works with a 3.3 V control signal; verify the MOSFET's on-resistance at an appropriate gate voltage, not just its threshold voltage. Confirm common ground, gate resistor, pull-down, fuse and wiring before powering the LEDs. Determine whether the chosen board accepts the intended regulated supply at its actual power input. Do not feed 12 V into its USB/5 V pin.

## Firmware proposal
Use C++ with a reproducible PlatformIO build and a supported ESP32 framework. Pin the toolchain and dependencies after evaluating compatibility. Keep the panel state machine and parameter validation independent of HTTP and hardware so they can be tested on a host.

Evaluate ESP32AlpacaDevices2 as a potential dependency and TeslaBoard as a reference. Inspect licenses, maintenance, current ESP32 framework support, interface coverage, validation and error handling. Prefer official ASCOM requirements whenever a reference differs. Record whether the dependency is adopted, adapted or rejected.

Suggested repository layout:
- README.md — purpose, quick start, limitations and validation status
- platformio.ini — reproducible firmware target
- src/ and include/ — firmware
- test/ — host tests and firmware tests where useful
- tools/ — network smoke-test client
- docs/architecture.md — architecture and design decisions
- docs/hardware.md — pinout, wiring and confirmed components
- docs/alpaca-contract.md — endpoint and requirement matrix
- docs/validation.md — build, Conform Universal and NINA evidence
- config.example.* — public configuration example without secrets
- .gitignore — credentials, generated output and machine-local files

## Alpaca contract
Consult the official CoverCalibrator master interface and Alpaca API before implementation. Document every required endpoint and its behavior rather than implementing only the happy path.

Include:
- UDP discovery (standard discovery port 32227; verify exact packet format).
- Required Management endpoints and configured-device metadata.
- Stable device UniqueID across reboots and DHCP address changes; do not derive it from the current IP.
- Required common device members, interface version, supported actions and appropriate exceptions/errors for unsupported methods.
- Connected and, if required by the selected interface, asynchronous connection members.
- Brightness, MaxBrightness, CalibratorState, CalibratorOn and CalibratorOff.
- CoverState and absent-cover method behavior.
- Case-insensitive parameter handling where required, correct GET/PUT encoding, Alpaca JSON envelopes, transaction IDs and standard error numbers.
- Invalid device numbers, malformed/missing parameters, out-of-range brightness and disconnected operation.
- Explicit connection semantics for multiple clients, consistent with the standard. Do not assume that generic HTTP requests form sessions or that Device Hub behavior automatically defines this device's behavior.

Do not hard-code CalibratorOn(0) as an invalid request or silently map it to a different command: verify its exact standard semantics. Separate logical requested brightness from physical PWM.

Ready means the output has reached the commanded electrical state. With no photodiode, it does not mean measured optical stabilization or verified illumination.

## Brightness and reliability
Choose a stable public brightness range after studying NINA behavior and usable PWM resolution. A provisional 0–1000 logical range is a design option, not a requirement. Start with a monotonic mapping; leave provision for a later measured lookup table. Do not add a nonzero minimum duty that prevents sufficiently faint flats without hardware measurements.

Select frequency and resolution together using the actual ESP32 LEDC capabilities and MOSFET switching performance. Test exposure uniformity and banding at relevant camera exposure times. Do not promise flicker-free operation solely from a nominal PWM frequency.

Boot and reset with the light off; never restore a previously illuminated state automatically. Keep PWM off during initialization and failed configuration. Recover Wi-Fi without blocking the state machine. Define and document behavior on Wi-Fi loss, client disconnect and reset. A proposed initial policy is off on confirmed Wi-Fi loss, no automatic reillumination after recovery; validate implications for an interrupted exposure. Avoid a short HTTP-inactivity timeout that could turn the panel off during a legitimate long flat exposure.

Keep credentials out of logs and the public repository. Use local ignored configuration for the first build. A later explicit provisioning mode may offer configuration; do not launch an open control access point automatically during a capture session.

## Setup and diagnostics
Provide a simple status/setup page showing firmware version, network status, device identity, light state and requested brightness. Avoid exposing Wi-Fi passwords. Separate optional manual-control functions from standardized Alpaca methods. Document whether physical button control is implemented, and how it interacts with NINA.

## Delivery stages
1. Verify board, dependency licenses and official contract; create build and design-decision record.
2. USB-powered prototype with LEDs disconnected: Wi-Fi, discovery, management, CoverCalibrator and mock PWM.
3. Host tests plus live network smoke test; retain exact build results.
4. Verify wiring and use a current-limited/appropriately fused hardware setup; test off, minimum and maximum output.
5. Run official Conform Universal against the physical device. Record version, options and complete failures; fix applicable findings.
6. Connect from the actual X38 NINA installation; test discovery, reconnect, brightness, Flat Wizard, abort/off and restart.
7. Capture real flats with the X38 cameras. Evaluate brightness repeatability, gradients, banding, exposure range and thermal behavior.

Hardware-dependent validation must remain explicitly pending until performed. Passing mock tests is not evidence of NINA compatibility or full conformity.

## Initial Codex prompt
Work in this repository to implement PiriFlatMan, a DIY ESP32 flat panel with native ASCOM Alpaca CoverCalibrator support.

Read this document first. Begin by consulting the official ASCOM CoverCalibrator master interface, Alpaca API/Discovery/Management specification and Conform Universal documentation. Inspect ESP32AlpacaDevices2 and TeslaBoard as optional third-party references, including their licenses. Report the chosen framework, pinned dependencies, supported interface version, and a requirements matrix.

Implement a reproducible PlatformIO project, separated panel state machine, real HTTP/UDP Alpaca server, mockable PWM adapter, stable identity, safe boot-off behavior, local secret configuration, concise setup/status page, host tests and a network smoke-test tool. Include all required common and CoverCalibrator members with standard errors and parameter handling. There is no motorized cover.

Do not guess hardware pinout or exact module. Until confirmed, provide an explicitly documented provisional build target and disabled/mock output profile. Do not energize hardware or claim electrical verification. Keep hardware constants configurable and document unresolved components.

Complete meaningful build/tests available in your environment. Document remaining physical Conform Universal, NINA and optical checks separately. Never label an untested integration compliant. Keep public files free of credentials and private network details. Use English for code, README and technical documents. Do not publish or flash hardware as an incidental side effect.

## Public repository setup
Suggested name: piriflatman
Suggested description: Open-source ESP32 ASCOM Alpaca flat-field panel for astrophotography.
Initialize with README and a C++ .gitignore. MIT is a proposed license for original code; confirm the owner's choice and compatibility before including third-party code. This document can become docs/project-brief.md.

## References to verify during implementation
- https://ascom-standards.org/newdocs/covercalibrator.html
- https://ascom-standards.org/AlpacaDeveloper/ASCOMAlpacaAPIReference.html
- https://github.com/ASCOMInitiative
- https://github.com/npeter/ESP32AlpacaDevices2
- https://github.com/StefanoTesla/TeslaBoard
- https://github.com/isbeorn/nina

The official web pages could not be retrieved during preparation of this brief. Detailed contract claims, current library support and interface versions are therefore implementation verification tasks.

