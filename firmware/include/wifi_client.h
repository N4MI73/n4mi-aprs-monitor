#pragma once

#include <Arduino.h>

// Blocking connect using credentials from wifi_credentials.h, with a
// timeout. Returns true on success. Temporary hardcoded-credentials
// approach -- real captive portal setup is a later, separate piece of work
// (see n4mi-propagation-monitor's own history for the same sequencing).
bool wifi_client_connect();

bool wifi_client_is_connected();

// Human-readable IP for display on a future Config screen. Empty string
// if not connected.
String wifi_client_get_ip();
