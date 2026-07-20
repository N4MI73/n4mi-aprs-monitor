#include "wifi_client.h"
#include <WiFi.h>
#include "config.h"
#include "wifi_credentials.h"

// Set to 1 for verbose Serial diagnostic output, 0 for production.
// Per-file convention, matching display_driver.cpp -- deliberately set
// to 1 during this proof-of-concept phase (real hardcoded-credentials
// testing), same as PropMon's own equivalent phase. Flip to 0 before
// this is considered done, not just before committing.
#define DEBUG_VERBOSE 1

bool wifi_client_connect() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

#if DEBUG_VERBOSE
    Serial.print("Connecting to Wi-Fi");
#endif

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > WIFI_CONNECT_TIMEOUT_MS) {
#if DEBUG_VERBOSE
            Serial.println();
            Serial.println("Wi-Fi connect timed out");
#endif
            return false;
        }
        delay(250);
#if DEBUG_VERBOSE
        Serial.print(".");
#endif
    }

#if DEBUG_VERBOSE
    Serial.println();
    Serial.print("Wi-Fi connected, IP: ");
    Serial.println(WiFi.localIP());
#endif

    return true;
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
