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
    char updated[32] = "";       // backend's own last-update time, ISO 8601 UTC
    unsigned long fetched_at_millis = 0;  // local millis() when last populated
};

// Fetches from MOBILE_SERVER_URL and parses into a temporary struct
// first, only committing to `out` on full success -- same pattern as
// data_client_fetch_weather(). A quiet state (mobile_count_1h == 0,
// last_active null) is a normal, successful response, not a failure.
bool mobile_client_fetch(MobileData &out);
