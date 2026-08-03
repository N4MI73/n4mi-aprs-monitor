// N4MI Desktop Instrument Series - APRSMon
// wifi_client.h -- Wi-Fi connection management + credential storage
//
// Wi-Fi setup Stage 1: added NVS-backed credential storage (via
// Preferences), ported from Propagation Monitor's own equivalent build
// of this exact feature. wifi_client_connect() now tries stored NVS
// credentials first; falls back to wifi_credentials.h's hardcoded
// values (seeding NVS with them on success) if nothing is stored yet --
// a one-time transparent migration, not a permanent reliance on the
// hardcoded file.
//
// Deliberately NOT ported yet: PropMon's wifi_client_try_credentials(),
// which exists specifically for its setup portal's own credential-
// validation flow. There's no portal here yet to call it -- that's
// Stage 2. Will be added then, along with the same WiFi.mode()
// discipline described below (which IS adopted now, even though only
// one caller needs it today, specifically to avoid the exact bug
// PropMon found on real hardware: forcing WIFI_STA unconditionally
// would silently drop a future setup portal's AP mid-request).

#pragma once

#include <Arduino.h>

// Attempts to connect to Wi-Fi. Tries stored NVS credentials first;
// falls back to wifi_credentials.h's hardcoded values (and seeds NVS
// with them on success) if nothing is stored yet. Sets WiFi.mode() to
// pure WIFI_STA -- only call this from the normal (non-setup-portal)
// boot/reconnect path. Blocks up to WIFI_CONNECT_TIMEOUT_MS (config.h).
// Returns true if connected by the time it returns, false on timeout.
bool wifi_client_connect();

// Returns true if currently associated to Wi-Fi.
bool wifi_client_is_connected();

// Human-readable IP for display on Config. Empty string if not connected.
String wifi_client_get_ip();

// Returns true if valid Wi-Fi credentials are currently stored in NVS
// (independent of the hardcoded wifi_credentials.h fallback).
bool wifi_client_has_stored_credentials();

// Attempts a connection using only stored NVS credentials -- does not
// fall back to wifi_credentials.h. Sets WiFi.mode() to pure WIFI_STA,
// same caveat as wifi_client_connect(). Returns false immediately (no
// connection attempt) if nothing is stored.
bool wifi_client_connect_stored();

// Saves credentials to NVS, overwriting any previous values. Does not
// attempt a connection itself. Not yet called from anywhere except
// wifi_client_connect()'s own hardcoded-fallback migration path --
// the setup portal (Stage 2) will call this directly once submitted
// credentials are validated.
void wifi_client_save_credentials(const char *ssid, const char *password);
