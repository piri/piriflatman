# Alpaca contract and requirements matrix

## Authority and exact versions

Verified on **2026-09-29** against the live official specifications:

1. [ASCOM Master Interfaces: ICoverCalibratorV2](https://ascom-standards.org/newdocs/covercalibrator.html), live HTML documentation (site release 1.0.27 when researched). Implemented `InterfaceVersion = 2`.
2. [ASCOM Alpaca API Reference, Version 12, 23 February 2026](https://ascom-standards.org/AlpacaDeveloper/ASCOMAlpacaAPIReference.html). Especially §§1.7, 2–5: robustness, HTTP, management, discovery and identity.
3. [Official device OpenAPI definition](https://ascom-standards.org/api/AlpacaDeviceAPI_v1.yaml): Alpaca device API **v1**, OAS 3.1.1 in the retrieved document. This version is independent of CoverCalibrator interface version 2.
4. [Official management OpenAPI definition](https://ascom-standards.org/api/AlpacaManagementAPI_v1.yaml): management **v1**, OAS 3.1.1.
5. [Current TimeStamp FAQ](https://ascom-standards.org/newdocs/timestamp-faq.html): timestamp is optional when time is known. Older frozen PDFs contain different wording; the current HTML governs.
6. [Conform Universal](https://github.com/ASCOMInitiative/ConformU): official test tool, not a substitute for the interface definitions. Host-only test evidence is in [validation](validation.md).

Retrieved primary document hashes are in [specification-sources.json](specification-sources.json). The brief is not an authority for version numbers, and library claims were not used as normative requirements.

### Specification inconsistencies handled explicitly

The master Connect/Disconnect Important paragraphs have reversed connected/disconnected wording. This implementation follows the method definitions and Connecting property: Connect requests connected=true, Disconnect requests connected=false; Connecting indicates a pending transition. The CalibratorState notes also use “Changing”, but the actual enum names/value and method definitions use **NotReady (2)**. This implementation completes its immediate electrical writes before returning and therefore does not expose a pending calibrator transition.

API Reference V12 §2.2.3 asks clients to send PUT form names in the specified casing, while the live OpenAPI introduction describes parameter names as case-insensitive. The implementation follows the more specific API Reference V12 rule and the official Conform Universal 4.5.0 protocol tests: GET names are case-insensitive; PUT names require canonical casing. Incorrectly cased optional IDs are treated as unknown and ignored; incorrectly cased required names result in a missing-parameter HTTP 400. URI paths remain case-sensitive lowercase.

## Device members

Base: `/api/v1/covercalibrator/0/`. Exactly one device is published, named **PiriFlatMan**. All rows are implemented and covered by host tests. Physical behavior, conformity and NINA integration are still pending.

“Connected required” means disconnected reads/operations return HTTP 200 with `ErrorNumber=1031`. Members that are inherently unimplemented return 1024 even while disconnected. Malformed requests are rejected before device execution.

| Member | Verb / parameter | Implemented behavior | Connection |
| --- | --- | --- | --- |
| `name` | GET | `PiriFlatMan` | Always readable |
| `description` | GET | Device description, <=64 ASCII characters | Always readable |
| `driverinfo` | GET | Firmware, interface and output-profile information | Always readable |
| `driverversion` | GET | `0.1` (major.minor); full firmware version `0.1.0` in metadata | Always readable |
| `interfaceversion` | GET | Integer 2 | Always readable |
| `supportedactions` | GET | Empty array | Always readable |
| `action` | PUT `ActionName`, `Parameters` | 1024 NotImplemented: no custom actions at all | Inherently unsupported |
| `commandblind` | PUT `Command`, `Raw` | 1024 NotImplemented | Inherently unsupported |
| `commandbool` | PUT `Command`, `Raw` | 1024 NotImplemented, no Value | Inherently unsupported |
| `commandstring` | PUT `Command`, `Raw` | 1024 NotImplemented, no Value | Inherently unsupported |
| `connected` | GET | Shared logical connection boolean | Always readable |
| `connected` | PUT `Connected` | Synchronous connect/disconnect; disconnect turns light off | Always callable |
| `connect` | PUT | Starts connection, completion next loop; does not illuminate | Always callable |
| `disconnect` | PUT | Turns light off immediately, connection completion next loop | Always callable |
| `connecting` | GET | True while connect/disconnect completion is pending | Always readable |
| `devicestate` | GET | Array of Name/Value items: Brightness, CalibratorChanging, CalibratorState, CoverMoving, CoverState; optional TimeStamp when UTC known | Required |
| `brightness` | GET | Last successfully applied logical brightness; zero when Off | Required |
| `maxbrightness` | GET | Integer 1000 | Required |
| `calibratorstate` | GET | Off=1, Ready=3, Error=5 | Required |
| `calibratorchanging` | GET | False: electrical changes complete during call | Required |
| `calibratoron` | PUT `Brightness` | Inclusive 0–1000; Ready after successful write, including zero; 1025 outside range | Required |
| `calibratoroff` | PUT | Off and brightness zero | Required |
| `coverstate` | GET | NotPresent=0 | Required |
| `covermoving` | GET | False | Required |
| `opencover` | PUT | 1024 NotImplemented: no cover | Inherently unsupported |
| `closecover` | PUT | 1024 NotImplemented: no cover | Inherently unsupported |
| `haltcover` | PUT | 1024 NotImplemented: no cover | Inherently unsupported |
| COM `SetupDialog` | No Alpaca method | Deliberately no device API route; browser endpoints below replace it | Not applicable |

ActionNotImplemented (1036) applies to an unsupported action in a driver that implements custom actions. The master explicitly permits MethodNotImplemented (1024) when none are implemented; that is this device's policy. Commands are deprecated but their routes remain present with the appropriate unsupported-method response.

## Management, discovery and setup

| Requirement | Endpoint or behavior | Implementation / validation |
| --- | --- | --- |
| API version enumeration | GET `/management/apiversions` | `Value: [1]`; host HTTP checked |
| Server description | GET `/management/v1/description` | ServerName, Manufacturer, ManufacturerVersion, Location; host HTTP checked |
| Configured devices | GET `/management/v1/configureddevices` | One object: DeviceName=PiriFlatMan, DeviceType=CoverCalibrator, DeviceNumber=0, UniqueID; host HTTP checked |
| Stable globally unique identity | UniqueID across power/network changes | Namespaced immutable factory 48-bit eFuse MAC; no IP/SSID/random-at-boot input. Actual unit persistence test pending |
| Main setup | GET `/setup` (also `/`) | HTML identity, manufacturer, version, network, profile, state and setup instructions |
| Device setup | GET `/setup/v1/covercalibrator/0/setup` | Same status/setup page; calibrator electrical settings configured at build time |
| Configuration access | IP/port configuration | Browser form saves HTTP/discovery ports to NVS and restarts with output off. Wi-Fi/static IP remain in ignored `config.local.h`; DHCP reservations are an alternative |
| Invalid setup path | `/setup/...` unknown device/path/verb | HTTP 403 text |
| IPv4 discovery | UDP 32227 by default, locally configurable | Accept `alpacadiscovery1`, 16 ASCII bytes, optional reserved tail through 64 bytes; reject other version/prefix/oversize |
| Discovery response | Unicast to sender IP and source port | `{"AlpacaPort":8080}` with numeric configured port; no Alpaca device envelope |
| Discovery responsiveness | Nonblocking socket | Shared loop; one packet per pass; tested over host loopback |
| Port sharing | Socket options | SO_REUSEADDR, plus SO_REUSEPORT for UDP where platform defines it |
| IPv6 | Optional in API Reference §5.8.3 | Not implemented; IPv4 only |

The setup page provides a form to change the cross-device HTTP and discovery ports through POST `/setup/ports` (an application endpoint, not an Alpaca member). Valid values are 1–65535. Successful NVS persistence forces output off, rejects subsequent control writes and restarts after one second. Wi-Fi credentials and optional static addressing remain compile-time local configuration for this milestone. Host tests inject the persistence callback; actual ESP32 NVS/restart behavior remains pending physical testing.

## HTTP and parameter rules

| Requirement | Policy / evidence |
| --- | --- |
| Paths | Lowercase, case-sensitive; only device 0, covercalibrator, API v1. Invalid path/member/verb/type/version/number: HTTP 400 plain text |
| GET encoding | URL query string; percent decoding and `+` as space |
| PUT encoding | `application/x-www-form-urlencoded` body, media-type casing tolerated and optional charset accepted. Query parameters do not satisfy PUT arguments |
| Name casing | GET names accepted in any casing; PUT requires canonical names; JSON output keys use exact canonical casing |
| Boolean values | `true`/`false` case-insensitive; numeric booleans rejected as malformed |
| Integer values | Full-string decimal parsing, no trailing junk, overflow, floating point or exponent notation. Signed int32 Brightness; unsigned uint32 IDs |
| Required arguments | Missing or malformed required arguments: HTTP 400 text before execution |
| Range violations | Well-formed int32 Brightness outside 0–1000: HTTP 200 / 1025 InvalidValue; existing output unchanged |
| Optional IDs | ClientID and ClientTransactionID optional, zero accepted, max 4294967295 accepted. Absent ClientTransactionID echoed as 0 |
| Server transactions | Global uint32 counter across requests; increments, skips zero after wrap; reset on reboot. Management and device JSON responses include it |
| JSON envelope | ClientTransactionID, ServerTransactionID, ErrorNumber, ErrorMessage; Value only on successful value-returning calls |
| Standard errors | 0 success; 1024 NotImplemented; 1025 InvalidValue; 1031 NotConnected; 1280 (0x500) driver/output failure |
| HTTP semantics | Understood operations, including ASCOM errors, return 200 JSON. Transport mistakes return 400 text; no made-up ASCOM error for an unknown path |
| Duplicate arguments | Case-insensitive duplicate keys rejected with HTTP 400, avoiding ambiguous command execution |
| Unknown parameters | Ignored when syntactically well formed; do not add device actions |
| JSON safety | ArduinoJson serialization; arbitrary incoming strings are never concatenated into JSON/HTML responses |
| HTTP framing | Content-Length, connection close; incremental parsing, bounded memory, 100-continue; malformed/oversize/duplicate-length/chunked requests rejected |

ClientID is validated but not used as a session identifier. See [architecture](architecture.md) for shared connection state and why one client's disconnect affects others. Error precedence is parsing/path validation first, then the member behavior in the matrix; range and connection errors do not modify illumination. No request/password logging occurs.

## Examples

```sh
curl 'http://DEVICE_IP:8080/management/v1/configureddevices'
curl -X PUT -d 'Connected=true&ClientID=1&ClientTransactionID=1' \
  'http://DEVICE_IP:8080/api/v1/covercalibrator/0/connected'
curl -X PUT -d 'Brightness=0&ClientID=1&ClientTransactionID=2' \
  'http://DEVICE_IP:8080/api/v1/covercalibrator/0/calibratoron'
curl -X PUT -d 'ClientID=1&ClientTransactionID=3' \
  'http://DEVICE_IP:8080/api/v1/covercalibrator/0/calibratoroff'
```

Use the examples against mock output for the first test. Passing these calls, host tests or a host-only official tool run does not validate electrical behavior, physical conformity, NINA integration or optical quality.
