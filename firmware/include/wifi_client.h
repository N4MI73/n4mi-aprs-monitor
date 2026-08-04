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
// Wi-Fi setup Stage 2: added wifi_client_try_credentials(), for the
// real setup portal's (wifi_portal.cpp) own credential-validation
// flow, ported directly from PropMon's own build -- including the
// "explicitly disconnect first" fix already baked in, since PropMon's
// own hardware testing found that skipping it produces a false-positive
// "Connected!" result whenever the device happens to already be
// connected from before setup mode started. IMPORTANT: this function
// deliberately does NOT touch WiFi.mode() -- see wifi_client.cpp's
// header comment for why. The portal must already be in WIFI_AP_STA
// mode before calling this, or its own AP will drop out from under the
// phone mid-request.

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
// attempt a connection itself. Called from wifi_client_connect()'s
// hardcoded-fallback migration path, and directly by the setup portal
// once a submitted password is validated.
void wifi_client_save_credentials(const char *ssid, const char *password);

// Attempts a connection using the given credentials, blocking up to
// WIFI_CONNECT_TIMEOUT_MS. Does NOT save to NVS (caller decides,
// typically only on success) and does NOT touch WiFi.mode() -- the
// caller is responsible for setting the correct mode first. This is
// what lets the setup portal validate a submitted password while
// keeping its own AP interface alive throughout the attempt, so the
// phone's connection to the setup network doesn't drop mid-request.
bool wifi_client_try_credentials(const char *ssid, const char *password);

