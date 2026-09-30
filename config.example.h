#pragma once
// Copy to config.local.h (ignored). Empty credentials compile but leave Wi-Fi disabled.
#define PFM_WIFI_SSID ""
#define PFM_WIFI_PASSWORD ""
#define PFM_HTTP_PORT 8080
#define PFM_DISCOVERY_PORT 32227
// Empty means DHCP. To use static addressing, supply all four values.
#define PFM_STATIC_IP ""
#define PFM_GATEWAY ""
#define PFM_SUBNET ""
#define PFM_DNS ""
// Optional local NTP server. No Internet time service is contacted by default.
#define PFM_NTP_SERVER ""
// Hardware output requires ALL of these after electrical and board verification.
// No GPIO, PWM frequency or resolution is selected on your behalf.
// #define PFM_HARDWARE_VERIFIED 1
// #define PFM_PWM_GPIO <verified output-capable GPIO>
// #define PFM_PWM_HZ <verified frequency>
// #define PFM_PWM_BITS <resolution from 10 to 16>
