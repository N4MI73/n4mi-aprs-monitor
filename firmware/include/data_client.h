#pragma once

#include <Arduino.h>

struct WeatherStation {
    char callsign[12] = "";
    float temp_f = 0;
    float humidity_pct = 0;
    float pressure_mbar = 0;
    float wind_mph = 0;
    float wind_gust_mph = 0;
    char wind_dir_compass[4] = "";
    float rain_1h_in = 0;
    char station_time[32] = "";  // ISO 8601 UTC, as served by the backend
    float distance_mi = 0;
    char bearing[4] = "";
    bool valid = false;  // secondary station can be legitimately absent
};

struct WeatherData {
    WeatherStation primary;
    WeatherStation secondary;
    char updated[32] = "";       // backend's own last-successful-fetch time, ISO 8601 UTC
    unsigned long fetched_at_millis = 0;  // local millis() when this struct was last populated
};

// Fetches from APRSMON_SERVER_URL and parses into a temporary struct first,
// only committing to `out` on full success -- same pattern
// n4mi-propagation-monitor's data_client_fetch_live() uses, so a partial or
// malformed response never corrupts previously-good data. Returns true on
// success. A 503 (backend hasn't completed its first poll yet) is treated
// as a normal, expected failure, not an error to alarm about.
bool data_client_fetch_weather(WeatherData &out);
