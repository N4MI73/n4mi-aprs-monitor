// N4MI Desktop Instrument Series - APRSMon
// wifi_client.cpp
//
// Wi-Fi setup Stage 1 -- see wifi_client.h for the full picture. NVS
// storage logic ported directly from Propagation Monitor's own real
// build of this exact feature, not reconstructed from scratch.

#include "wifi_client.h"
#include "wifi_credentials.h"
#include "config.h"
#include <WiFi.h>
#include <Preferences.h>

// Per-file convention, matching every other file in this project.
#define DEBUG_VERBOSE 0

static const char *NVS_NAMESPACE = "wifi";

// Deliberately does not touch WiFi.mode() -- callers are responsible
// for setting the correct mode first. Not load-bearing yet (Stage 1
// only has one caller, which always wants plain WIFI_STA), but
// structured this way now to avoid a known future problem: Stage 2's
// setup portal needs to stay in dual AP+STA mode while validating a
// submitted password, and forcing WIFI_STA unconditionally here would
// silently drop that AP -- and the phone's connection to it -- mid
// request. See wifi_client.h for the full explanation.
static bool connect_with_credentials(const char *ssid, const char *password) {
    if (WiFi.status() == WL_CONNECTED) return true;

    WiFi.begin(ssid, password);

#if DEBUG_VERBOSE
    if (Serial) Serial.printf("[wifi] connecting to \"%s\"...\n", ssid);
#endif

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
        delay(250);
    }

    bool connected = (WiFi.status() == WL_CONNECTED);

    if (!connected) {
        // Explicitly abort a failed attempt rather than leaving it
        // dangling -- PropMon found on real hardware that skipping
        // this left the Wi-Fi driver stuck silently retrying in the
        // background, blocking later unrelated operations.
        WiFi.disconnect();
    }

#if DEBUG_VERBOSE
    if (Serial) {
        if (connected) {
            Serial.printf("[wifi] connected, IP=%s\n", WiFi.localIP().toString().c_str());
        } else {
            Serial.printf("[wifi] connect FAILED after %lums, status=%d -- disconnected to free the radio\n",
                (unsigned long)(millis() - start), (int)WiFi.status());
        }
    }
#endif
    return connected;
}

bool wifi_client_has_stored_credentials() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true); // read-only
    bool has = prefs.isKey("ssid");
    prefs.end();
    return has;
}

void wifi_client_save_credentials(const char *ssid, const char *password) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false); // read-write
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
    prefs.end();
#if DEBUG_VERBOSE
    if (Serial) Serial.printf("[wifi] credentials saved to NVS for \"%s\"\n", ssid);
#endif
}

bool wifi_client_connect_stored() {
    if (!wifi_client_has_stored_credentials()) return false;

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    prefs.end();

    if (ssid.length() == 0) return false;
    return connect_with_credentials(ssid.c_str(), pass.c_str());
}

bool wifi_client_connect() {
    if (WiFi.status() == WL_CONNECTED) return true;

    // Normal boot/reconnect path -- never runs alongside a setup
    // portal's AP, so pure STA mode is always correct here.
    WiFi.mode(WIFI_STA);

    if (wifi_client_has_stored_credentials()) {
#if DEBUG_VERBOSE
        if (Serial) Serial.println("[wifi] using stored NVS credentials");
#endif
        return wifi_client_connect_stored();
    }

    // No stored credentials yet -- fall back to the temporary hardcoded
    // values (wifi_credentials.h) and seed NVS with them on success.
    // This is the one-time migration: once this succeeds, every future
    // boot uses the stored copy instead.
#if DEBUG_VERBOSE
    if (Serial) Serial.println("[wifi] no stored credentials, falling back to wifi_credentials.h");
#endif
    bool connected = connect_with_credentials(WIFI_SSID, WIFI_PASSWORD);
    if (connected) {
        wifi_client_save_credentials(WIFI_SSID, WIFI_PASSWORD);
    }
    return connected;
}

bool wifi_client_is_connected() {
    return WiFi.status() == WL_CONNECTED;
}

String wifi_client_get_ip() {
    if (!wifi_client_is_connected()) {
        return "";
    }
    return WiFi.localIP().toString();
}
