#pragma once

// Empty by default. The .gitignore excludes a real secrets.h
// in this directory; the user copies this file to secrets.h
// and fills in their own WiFi credentials.
//
// secrets.h is only used in dev; production flashes have
// credentials provisioned via the serial CLI instead.

#ifndef DEFAULT_WIFI_SSID_VALUE
#define DEFAULT_WIFI_SSID_VALUE ""
#endif

#ifndef DEFAULT_WIFI_PASS_VALUE
#define DEFAULT_WIFI_PASS_VALUE ""
#endif

#define DEFAULT_WIFI_SSID DEFAULT_WIFI_SSID_VALUE
#define DEFAULT_WIFI_PASS DEFAULT_WIFI_PASS_VALUE
