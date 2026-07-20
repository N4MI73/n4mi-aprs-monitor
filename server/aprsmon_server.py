"""
APRSMon Weather Service
========================
Polls aprs.fi's REST API for 1-2 known local weather station callsigns,
converts units to imperial, and serves the result as flat JSON for the
APRSMon ESP32 firmware's Weather screen to poll.

Mirrors n4mi-propagation-monitor's PropMon architecture: the ESP32 never
talks to aprs.fi directly and never sees the API key. "Serve last
known-good data on fetch failure" -- same principle PropMon already
proved out in production.

Response shape CONFIRMED against a real live call (2026-07-20) for both
ENMVJD and KC5DDG: envelope is {"result": "ok", "found": N, "entries": [...]},
and every field this module parses (temp, pressure, humidity, wind_direction,
wind_speed, wind_gust, rain_1h, time) is present exactly as documented.
`time` is a raw Unix epoch string, not pre-formatted -- converted to ISO
8601 UTC below to match PropMon's own timestamp convention. `rain_24h` and
`rain_mn` are also present in the real response but deliberately unused,
per project scope decision (2026-07-19) -- this is a quick-glance device,
not a rain history tool.
"""

import os
import time
import logging
import threading
from datetime import datetime, timezone

import requests
from flask import Flask, jsonify

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

APRSFI_API_KEY = os.environ.get("APRSFI_API_KEY")
if not APRSFI_API_KEY:
    raise RuntimeError("APRSFI_API_KEY environment variable is required")

POLL_INTERVAL_SECONDS = 10 * 60  # 10 minutes, per project decision (2026-07-18)
APRSFI_WX_URL = "https://api.aprs.fi/api/get"
USER_AGENT = "N4MI-APRSMon/0.1 (https://github.com/N4MI73/n4mi-aprs-monitor)"
REQUEST_TIMEOUT_SECONDS = 10

# Static station config. Distance/bearing computed once (great-circle, from
# home QTH) -- not recalculated live. See project Joplin notes for the
# calculation (2026-07-19). Update these two entries if stations are ever
# swapped (e.g. if GW7732 proves reliable after a few weeks and is preferred
# for its closer distance).
STATIONS = {
    "primary": {
        "callsign": "ENMVJD",
        "distance_mi": 6.4,
        "bearing": "E",
    },
    "secondary": {
        "callsign": "KC5DDG",
        "distance_mi": 6.7,
        "bearing": "NE",
    },
}

CALLSIGNS = ",".join(s["callsign"] for s in STATIONS.values())

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("aprsmon-weather")

# ---------------------------------------------------------------------------
# Unit conversions
# ---------------------------------------------------------------------------

def c_to_f(c):
    return round(c * 9 / 5 + 32, 1)


def mps_to_mph(mps):
    return round(mps * 2.237, 1)


def mm_to_in(mm):
    return round(mm * 0.03937, 2)


COMPASS_POINTS = [
    "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
    "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW",
]


def degrees_to_compass(deg):
    ix = round(deg / 22.5) % 16
    return COMPASS_POINTS[ix]


def epoch_to_iso(epoch_str):
    """aprs.fi returns station report time as a raw Unix epoch string
    (confirmed live, 2026-07-20) -- convert to ISO 8601 UTC to match
    PropMon's own timestamp convention rather than passing raw epoch
    through unexamined."""
    if epoch_str in (None, ""):
        return None
    return datetime.fromtimestamp(int(epoch_str), tz=timezone.utc).isoformat()

# ---------------------------------------------------------------------------
# aprs.fi fetch + parse
# ---------------------------------------------------------------------------

def fetch_raw():
    """Single HTTP call to aprs.fi covering both stations at once."""
    params = {
        "name": CALLSIGNS,
        "what": "wx",
        "apikey": APRSFI_API_KEY,
        "format": "json",
    }
    headers = {"User-Agent": USER_AGENT}
    resp = requests.get(
        APRSFI_WX_URL, params=params, headers=headers, timeout=REQUEST_TIMEOUT_SECONDS
    )
    resp.raise_for_status()
    data = resp.json()

    # Envelope shape confirmed live 2026-07-20 -- see module docstring.
    if data.get("result") != "ok":
        raise RuntimeError(f"aprs.fi returned non-ok result: {data}")
    return data.get("entries", [])


def parse_wx_entry(entry):
    """Convert one raw aprs.fi wx entry to our own units/shape.

    Fields confirmed present in aprs.fi's documented API: temp, pressure,
    humidity, wind_direction, wind_speed, wind_gust, rain_1h, time. All are
    returned as strings by the API -- cast explicitly. Any field aprs.fi
    omits (station didn't report it) is treated as None rather than
    assumed to always be present, since aprs.fi's own docs say responses
    only contain keys for data actually known.
    """
    def get_float(key):
        val = entry.get(key)
        return float(val) if val not in (None, "") else None

    temp_c = get_float("temp")
    wind_mps = get_float("wind_speed")
    gust_mps = get_float("wind_gust")
    rain_mm = get_float("rain_1h")
    wind_dir = get_float("wind_direction")

    return {
        "callsign": entry.get("name"),
        "temp_f": c_to_f(temp_c) if temp_c is not None else None,
        "humidity_pct": get_float("humidity"),
        "pressure_mbar": get_float("pressure"),
        "wind_mph": mps_to_mph(wind_mps) if wind_mps is not None else None,
        "wind_gust_mph": mps_to_mph(gust_mps) if gust_mps is not None else None,
        "wind_dir_compass": degrees_to_compass(wind_dir) if wind_dir is not None else None,
        "rain_1h_in": mm_to_in(rain_mm) if rain_mm is not None else None,
        "station_time": epoch_to_iso(entry.get("time")),
    }

# ---------------------------------------------------------------------------
# Cache -- "serve last known-good data on fetch failure", same principle
# PropMon already proved out in production
# ---------------------------------------------------------------------------

_cache_lock = threading.Lock()
_cache = {
    "updated": None,
    "primary": None,
    "secondary": None,
}


def poll_loop():
    while True:
        try:
            entries = fetch_raw()
            by_callsign = {e.get("name"): e for e in entries}

            primary_raw = by_callsign.get(STATIONS["primary"]["callsign"])
            secondary_raw = by_callsign.get(STATIONS["secondary"]["callsign"])

            if primary_raw is None:
                raise RuntimeError(
                    f"Primary station {STATIONS['primary']['callsign']} "
                    f"missing from aprs.fi response"
                )

            new_primary = {**parse_wx_entry(primary_raw), **STATIONS["primary"]}
            new_secondary = (
                {**parse_wx_entry(secondary_raw), **STATIONS["secondary"]}
                if secondary_raw is not None
                else None
            )

            with _cache_lock:
                _cache["primary"] = new_primary
                _cache["secondary"] = new_secondary
                _cache["updated"] = datetime.now(timezone.utc).isoformat()

            log.info(
                "Fetch succeeded: primary=%s secondary=%s",
                new_primary.get("callsign"),
                new_secondary.get("callsign") if new_secondary else "missing",
            )

        except Exception as exc:
            # Deliberately keep serving whatever is already cached -- same
            # "last known-good" principle PropMon uses. Only log, never
            # clear the cache on failure.
            log.warning("Fetch failed, keeping last known-good data: %s", exc)

        time.sleep(POLL_INTERVAL_SECONDS)

# ---------------------------------------------------------------------------
# Flask app
# ---------------------------------------------------------------------------

app = Flask(__name__)


@app.route("/healthz")
def healthz():
    return jsonify({"status": "ok"})


@app.route("/api/instrument/weather")
def weather():
    with _cache_lock:
        if _cache["updated"] is None:
            # Nothing fetched yet (e.g. right after startup, before the
            # first poll completes) -- return 503 rather than fabricate data
            return jsonify({"error": "no data yet"}), 503
        return jsonify(dict(_cache))


if __name__ == "__main__":
    poll_thread = threading.Thread(target=poll_loop, daemon=True)
    poll_thread.start()
    app.run(host="0.0.0.0", port=8079)
