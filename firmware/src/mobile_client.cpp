#include "mobile_client.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"

// Set to 1 for verbose Serial diagnostic output, 0 for production.
// Per-file convention, matching display_driver.cpp.
#define DEBUG_VERBOSE 1

bool mobile_client_fetch(MobileData &out) {
    HTTPClient http;
    http.begin(MOBILE_SERVER_URL);
    http.setTimeout(10000);

    int httpCode = http.GET();

    if (httpCode != 200) {
#if DEBUG_VERBOSE
        Serial.print("Mobile fetch failed, HTTP code: ");
        Serial.println(httpCode);
#endif
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
#if DEBUG_VERBOSE
        Serial.print("Mobile JSON parse failed: ");
        Serial.println(err.c_str());
#endif
        return false;
    }

    MobileData temp;
    temp.mobile_count_1h = doc["mobile_count_1h"] | 0;

    // last_active is legitimately null in the quiet state -- that's a
    // successful, normal response, not a parse failure.
    JsonVariant la = doc["last_active"];
    if (!la.isNull()) {
        JsonObject obj = la.as<JsonObject>();
        strlcpy(temp.last_active.callsign, obj["callsign"] | "",
                sizeof(temp.last_active.callsign));
        temp.last_active.minutes_ago = obj["minutes_ago"] | 0;
        temp.last_active.distance_mi = obj["distance_mi"] | 0.0f;
        strlcpy(temp.last_active.bearing, obj["bearing"] | "",
                sizeof(temp.last_active.bearing));
        temp.last_active.valid = true;
    } else {
        temp.last_active.valid = false;
    }

    strlcpy(temp.updated, doc["updated"] | "", sizeof(temp.updated));
    temp.fetched_at_millis = millis();

    out = temp;

#if DEBUG_VERBOSE
    Serial.print("Mobile fetch succeeded: count_1h=");
    Serial.print(out.mobile_count_1h);
    Serial.print(" last_active=");
    Serial.println(out.last_active.valid ? out.last_active.callsign : "(none)");
#endif

    return true;
}
