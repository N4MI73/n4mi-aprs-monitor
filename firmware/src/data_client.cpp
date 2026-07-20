#include "data_client.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"

// Set to 1 for verbose Serial diagnostic output, 0 for production.
// Per-file convention, matching display_driver.cpp. Flip to 0 once this
// is proven working, not just before committing.
#define DEBUG_VERBOSE 1

static bool parse_station(JsonObject obj, WeatherStation &station) {
    if (obj.isNull()) {
        station.valid = false;
        return true;  // absent secondary station is not a parse failure
    }

    strlcpy(station.callsign, obj["callsign"] | "", sizeof(station.callsign));
    station.temp_f = obj["temp_f"] | 0.0f;
    station.humidity_pct = obj["humidity_pct"] | 0.0f;
    station.pressure_mbar = obj["pressure_mbar"] | 0.0f;
    station.wind_mph = obj["wind_mph"] | 0.0f;
    station.wind_gust_mph = obj["wind_gust_mph"] | 0.0f;
    strlcpy(station.wind_dir_compass, obj["wind_dir_compass"] | "", sizeof(station.wind_dir_compass));
    station.rain_1h_in = obj["rain_1h_in"] | 0.0f;
    strlcpy(station.station_time, obj["station_time"] | "", sizeof(station.station_time));
    station.distance_mi = obj["distance_mi"] | 0.0f;
    strlcpy(station.bearing, obj["bearing"] | "", sizeof(station.bearing));
    station.valid = true;

    return true;
}

bool data_client_fetch_weather(WeatherData &out) {
    HTTPClient http;
    http.begin(APRSMON_SERVER_URL);
    http.setTimeout(10000);

    int httpCode = http.GET();

    if (httpCode == 503) {
        // Backend hasn't completed its first poll yet -- expected right
        // after the service starts, not a real error.
#if DEBUG_VERBOSE
        Serial.println("Weather fetch: backend has no data yet (503)");
#endif
        http.end();
        return false;
    }

    if (httpCode != 200) {
#if DEBUG_VERBOSE
        Serial.print("Weather fetch failed, HTTP code: ");
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
        Serial.print("Weather JSON parse failed: ");
        Serial.println(err.c_str());
#endif
        return false;
    }

    // Parse into a temporary struct first -- only commit to `out` on full
    // success, so a partial/malformed response never corrupts existing
    // good data. Same principle as PropMon's data_client_fetch_live().
    WeatherData temp;

    if (!parse_station(doc["primary"].as<JsonObject>(), temp.primary)) {
        return false;
    }
    if (!temp.primary.valid) {
        // A missing primary station is a real failure -- unlike secondary,
        // this should never legitimately be absent.
#if DEBUG_VERBOSE
        Serial.println("Weather fetch: primary station missing from response");
#endif
        return false;
    }

    parse_station(doc["secondary"].as<JsonObject>(), temp.secondary);

    strlcpy(temp.updated, doc["updated"] | "", sizeof(temp.updated));
    temp.fetched_at_millis = millis();

    out = temp;

#if DEBUG_VERBOSE
    Serial.print("Weather fetch succeeded: primary=");
    Serial.print(out.primary.callsign);
    Serial.print(" secondary=");
    Serial.println(out.secondary.valid ? out.secondary.callsign : "(none)");
#endif

    return true;
}
