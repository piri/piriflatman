# Hardware notes

## Confirmed information versus open decisions

| Item | Known | Still required |
| --- | --- | --- |
| Development board | ESP32-family board with USB-C | Exact module/chip, pinout, flash size, USB bridge and correct PlatformIO board |
| Switching module | Q1/Q2 transistor markings, power terminals and J1 control pads | Transistor part numbers, topology, terminal/pad functions, polarity, 3.3 V compatibility, gate drive and protection |
| Light source | 12 V COB LED strip, approximately 5 m per brief | Actual load current, power, segmentation and thermal behavior |
| Power supply | 12 V proposed | Rated capacity, regulation, fuse, wiring/current limits and correct ESP32 regulated input |
| Pull-down | 10 kΩ discussed | Presence, location and suitability for actual module |
| Diffusion | First layer approximately 60 mm above LEDs | Second-layer spacing, gradients and usable panel size |
| Button | Physical design includes one | Pin, electrical circuit and interaction policy; not implemented |
| PWM | Firmware provides configurable active-high LEDC | GPIO, frequency, resolution, acceptable dimming range and exposure banding |

**No J1 wiring is specified.** Two transistors do not identify the topology or input requirements. USB-C does not establish the ESP32 variant. A successful generic ESP32 compile does not resolve either question.

## First test boundary

USB power and mock-output firmware only. Keep the switching module (including all J1 controls), 12 V supply and LEDs disconnected. Confirm the actual chip before flashing. The provisional `esp32dev` profile builds for classic ESP32, 4 MB flash; it is not proof that this board is that model.

MockOutput records virtual duty in RAM and never configures or writes a panel GPIO. Normal system functions such as serial USB/UART and Wi-Fi still operate. No firmware was uploaded and no hardware was energized during repository implementation.

## Before enabling the hardware profile

Identify both transistors and all terminals/control pads from trustworthy part/module documentation and measurements. Establish that the control input accepts 3.3 V and determine its polarity and ground reference. For a directly driven MOSFET, inspect specified on-resistance at the intended gate voltage; threshold voltage alone is insufficient.

Confirm common ground where required, gate resistor, passive pull-down, fuse, conductor current rating and a current-limited first power test. Establish how the ESP32 receives a regulated supply. Never connect 12 V to a GPIO, USB/5 V pin or an unverified board input.

The implemented adapter assumes active-high PWM with zero duty safely off. Do not select it for an active-low or otherwise incompatible module. Firmware cannot hold an output low during ROM boot, reset, bootloader execution or loss of power; verify passive off behavior on the actual assembly.

After electrical review, supply explicit local GPIO, frequency and resolution values plus `PFM_HARDWARE_VERIFIED=1`. A conservative classic-ESP32 GPIO allow-list excludes flash pins, strap pins, UART0 and input-only pins. It cannot know whether an allowed pin is used by PSRAM or another feature on the actual module. LEDC rejects an unsupported frequency/resolution combination and prevents logical connection. Resolution is limited to 10–16 bits for the chosen API mapping.

The synthetic adapter compilation uses arbitrary compiler-fixture values in an isolated temporary directory. Those values are not recommendations and are not copied to `config.example.h`.

## Physical validation still pending

Observe boot/reset/disconnect/radio-loss off behavior with a meter or oscilloscope before connecting the full LED load. Measure input compatibility, gate signal, minimum/maximum output, PWM frequency/duty, switching losses, temperature and current. Test flat-field uniformity, repeatability and banding using actual camera exposures. Nominal PWM frequency alone cannot demonstrate flicker-free flats.
