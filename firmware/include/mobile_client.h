#pragma once

#include <Arduino.h>

struct LastActiveStation {
    char callsign[12] = "";
    int minutes_ago = 0;
    float distance_mi = 0;
    char bearing[4] = "";
    bool valid = false;  // false when the backend's last_active is null
                          // (legitimate quiet state, not a fetch failure)
};

struct MobileData {
    int mobile_count_1h = 0;
    LastActiveStation last_active;

    // Up to 3 most-recently-heard stations, newest first -- same shape
    // as last_active, just a fixed-size array instead of one entry.
    // recent_count tells you how many of the 3 slots are actually
    // populated (0-3); fewer than 3 recent stations is a normal,
    // legitimate state, not something to pad with placeholders.
    static const int RECENT_MAX = 3;
    LastActiveStation recent[RECENT_MAX];
    int recent_count = 0;

    char updated[32] = "";       // backend's own last-update time, ISO 8601 UTC
    unsigned long fetched_at_millis = 0;  // local millis() when last populated

    // N4MI-13 (home Tempest weather station, relayed via APRS-IS) --
    // -1 means never heard this run. Purely a liveness check for a
    // separate weewx pipeline; not weather data (PropMon already gets
    // Tempest data more directly via WeatherFlow's own API).
    int home_station_minutes_ago = -1;
};

// Fetches from MOBILE_SERVER_URL and parses into a temporary struct
// first, only committing to `out` on full success -- same pattern as
// data_client_fetch_weather(). A quiet state (mobile_count_1h == 0,
// last_active null) is a normal, successful response, not a failure.
bool mobile_client_fetch(MobileData &out);
