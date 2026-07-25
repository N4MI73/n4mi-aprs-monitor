"""
APRSMon Mobile Activity Service
=================================
Holds a persistent connection to APRS-IS (via aprslib), filtered to a 20-mile
radius around home QTH, and tracks which stations are genuinely MOVING (not
just present) so the Mobile Activity screen can show "how many stations have
been active in the last hour" and "who was heard most recently."

Architecture note: this is a fundamentally different shape from
aprsmon_server.py (Weather). Weather polls an external REST API on a timer.
This service holds one long-lived TCP connection and reacts to a continuous
stream of packets -- much closer to PropMon's own "background loop maintains
state, HTTP endpoint just reads it" pattern than to Weather's "fetch on a
timer" pattern. Deliberately run as a SEPARATE Docker service from Weather
(not combined) -- a persistent-socket failure mode (needing a reconnect) is
different from a periodic-poll failure mode, and there's no reason a hiccup
in one should have any chance of affecting the other.

*** THINGS THAT NEED REAL-WORLD VERIFICATION, NOT YET CONFIRMED ***
1. `speed` field unit -- assumed knots (standard APRS convention), not seen
   explicitly stated in aprslib's own docs. Verify against real logged
   packets once this is running.
2. Exact filter-setting API on aprslib.IS -- using `.set_filter()` before
   `.connect()`, which is believed correct but not confirmed from a primary
   source in this session. If filtering doesn't visibly work once deployed
   (i.e. packets from far outside 20mi are coming through), this is the
   first thing to check.
"""

import os
import time
import math
import logging
import threading
from datetime import datetime, timezone

import aprslib
from flask import Flask, jsonify

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

CALLSIGN = os.environ.get("APRS_CALLSIGN", "N4MI")
PASSCODE = "-1"  # receive-only, confirmed from aprs-is.net's own Connecting.aspx

HOME_LAT = 33.4493757
HOME_LON = -82.1824427
FILTER_RADIUS_KM = 32  # 20 miles, converted (aprs-is.net filters use km)

APRS_IS_HOST = "rotate.aprs2.net"
APRS_IS_PORT = 14580  # filtered port, NOT the 10152 full-feed firehose

ACTIVE_WINDOW_SECONDS = 60 * 60  # 1 hour headline count window
RECONNECT_DELAY_SECONDS = 30
PRUNE_INTERVAL_SECONDS = 60 * 30  # sweep stale entries every 30 min
PRUNE_AGE_SECONDS = 60 * 60 * 24  # drop anything not heard in 24h

# Movement-detection thresholds -- tunable, not yet validated against real
# traffic. MIN_SPEED_KNOTS filters GPS jitter reported as a tiny nonzero
# speed; MIN_MOVE_DISTANCE_MI is the position-delta fallback used when a
# packet has no speed field at all.
MIN_SPEED_KNOTS = 2.0
MIN_MOVE_DISTANCE_MI = 0.05  # roughly 265 feet

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("aprsmon-mobile")

# ---------------------------------------------------------------------------
# Distance/bearing helpers
# ---------------------------------------------------------------------------
# Intentionally duplicated from aprsmon_server.py rather than shared via a
# common module -- these two services are deployed independently, and a
# dozen lines of pure math is a small enough duplication to accept in
# exchange for not coupling their deployments together.

EARTH_RADIUS_MI = 3958.8
COMPASS_POINTS = [
    "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
    "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW",
]


def haversine_mi(lat1, lon1, lat2, lon2):
    phi1, phi2 = math.radians(lat1), math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlambda = math.radians(lon2 - lon1)
    a = math.sin(dphi / 2) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(dlambda / 2) ** 2
    c = 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))
    return EARTH_RADIUS_MI * c


def bearing_deg(lat1, lon1, lat2, lon2):
    phi1, phi2 = math.radians(lat1), math.radians(lat2)
    dlambda = math.radians(lon2 - lon1)
    x = math.sin(dlambda) * math.cos(phi2)
    y = math.cos(phi1) * math.sin(phi2) - math.sin(phi1) * math.cos(phi2) * math.cos(dlambda)
    theta = math.degrees(math.atan2(x, y))
    return (theta + 360) % 360


def compass_from_deg(deg):
    ix = round(deg / 22.5) % 16
    return COMPASS_POINTS[ix]


def distance_bearing_from_home(lat, lon):
    d = haversine_mi(HOME_LAT, HOME_LON, lat, lon)
    b = bearing_deg(HOME_LAT, HOME_LON, lat, lon)
    return round(d, 1), compass_from_deg(b)

# ---------------------------------------------------------------------------
# State
# ---------------------------------------------------------------------------

state_lock = threading.Lock()

# Every station's last-known position, updated on EVERY position packet
# regardless of whether it counted as "moving" -- purely so the next packet
# from that callsign has something to compare against.
last_position_seen = {}  # callsign -> (lat, lon, datetime)

# Only stations confirmed MOVING, per the thresholds above. This is what
# the JSON endpoint actually reads from.
mobile_state = {}  # callsign -> {distance_mi, bearing, last_heard}

# Connection diagnostics -- added because Portainer's log viewer isn't
# available in this deployment (Community Edition limitation), so the
# service needs to be self-diagnosable purely through its HTTP API.
connection_status = "starting"  # starting | connecting | connected | error
last_connect_error = None
connected_since = None
total_packets_seen = 0  # every position packet, moving or not -- answers
                          # "is anything coming through at all"


def is_moving(callsign, lat, lon, speed_knots):
    if speed_knots is not None and speed_knots > MIN_SPEED_KNOTS:
        return True

    prior = last_position_seen.get(callsign)
    if prior is not None:
        prior_lat, prior_lon, _ = prior
        if haversine_mi(prior_lat, prior_lon, lat, lon) > MIN_MOVE_DISTANCE_MI:
            return True

    # First-ever sighting with no speed data and nothing to compare against
    # -- deliberately NOT counted as moving. Conservative default: a single
    # ambiguous packet shouldn't flag a possibly-stationary station.
    return False

# ---------------------------------------------------------------------------
# Packet handling
# ---------------------------------------------------------------------------

def on_packet(packet):
    global total_packets_seen
    try:
        if "latitude" not in packet or "longitude" not in packet:
            return  # not a position report -- status/telemetry/etc, ignore

        callsign = packet.get("from")
        if not callsign:
            return

        total_packets_seen += 1

        lat = packet["latitude"]
        lon = packet["longitude"]
        speed = packet.get("speed")  # knots, assumed -- see module docstring

        now = datetime.now(timezone.utc)
        moving = is_moving(callsign, lat, lon, speed)

        with state_lock:
            last_position_seen[callsign] = (lat, lon, now)
            if moving:
                d, b = distance_bearing_from_home(lat, lon)
                mobile_state[callsign] = {
                    "distance_mi": d,
                    "bearing": b,
                    "last_heard": now,
                }
                log.info("Moving: %s at %.1f mi %s", callsign, d, b)

    except Exception as exc:
        # A single malformed/unexpected packet should never take down the
        # whole listener -- log and move on, same resilience philosophy as
        # every other service in this project.
        log.warning("Error processing packet: %s", exc)

# ---------------------------------------------------------------------------
# APRS-IS connection loop
# ---------------------------------------------------------------------------

def aprs_is_loop():
    global connection_status, last_connect_error, connected_since
    filter_str = f"r/{HOME_LAT}/{HOME_LON}/{FILTER_RADIUS_KM}"
    while True:
        try:
            connection_status = "connecting"
            log.info("Connecting to APRS-IS as %s (receive-only)", CALLSIGN)
            ais = aprslib.IS(CALLSIGN, passcode=PASSCODE, host=APRS_IS_HOST, port=APRS_IS_PORT)
            ais.set_filter(filter_str)  # UNVERIFIED API -- see module docstring
            ais.connect()
            connection_status = "connected"
            connected_since = datetime.now(timezone.utc)
            last_connect_error = None
            log.info("Connected. Filter: %s", filter_str)
            ais.consumer(on_packet, raw=False, immortal=True)
        except Exception as exc:
            connection_status = "error"
            last_connect_error = str(exc)
            connected_since = None
            log.warning("APRS-IS connection error, reconnecting in %ss: %s",
                        RECONNECT_DELAY_SECONDS, exc)
            time.sleep(RECONNECT_DELAY_SECONDS)


def prune_loop():
    while True:
        time.sleep(PRUNE_INTERVAL_SECONDS)
        cutoff = datetime.now(timezone.utc).timestamp() - PRUNE_AGE_SECONDS
        total_pruned = 0

        with state_lock:
            stale_positions = [
                c for c, (_, _, ts) in last_position_seen.items()
                if ts.timestamp() < cutoff
            ]
            for c in stale_positions:
                del last_position_seen[c]

            stale_mobile = [
                c for c, s in mobile_state.items()
                if s["last_heard"].timestamp() < cutoff
            ]
            for c in stale_mobile:
                del mobile_state[c]

            total_pruned = len(stale_positions) + len(stale_mobile)

        if total_pruned:
            log.info("Pruned %d stale entries (%d positions, %d mobile)",
                     total_pruned, len(stale_positions), len(stale_mobile))

# ---------------------------------------------------------------------------
# JSON snapshot
# ---------------------------------------------------------------------------

def compute_snapshot():
    now = datetime.now(timezone.utc)
    with state_lock:
        active = {
            c: s for c, s in mobile_state.items()
            if (now - s["last_heard"]).total_seconds() <= ACTIVE_WINDOW_SECONDS
        }

    count = len(active)
    if count == 0:
        last_active = None
    else:
        latest_call = max(active, key=lambda c: active[c]["last_heard"])
        s = active[latest_call]
        last_active = {
            "callsign": latest_call,
            "minutes_ago": int((now - s["last_heard"]).total_seconds() // 60),
            "distance_mi": s["distance_mi"],
            "bearing": s["bearing"],
        }

    return {
        "mobile_count_1h": count,
        "last_active": last_active,
        "updated": now.isoformat(),
    }

# ---------------------------------------------------------------------------
# Flask app
# ---------------------------------------------------------------------------

app = Flask(__name__)


@app.route("/healthz")
def healthz():
    return jsonify({"status": "ok"})


@app.route("/api/instrument/mobile")
def mobile():
    return jsonify(compute_snapshot())


@app.route("/debug")
def debug():
    """Connection diagnostics -- exists specifically because Portainer's log
    viewer isn't available in this deployment. Answers: is the APRS-IS
    connection actually up, and is anything coming through at all."""
    with state_lock:
        tracked_positions = len(last_position_seen)
        tracked_mobile = len(mobile_state)
    return jsonify({
        "connection_status": connection_status,
        "last_connect_error": last_connect_error,
        "connected_since": connected_since.isoformat() if connected_since else None,
        "total_packets_seen": total_packets_seen,
        "tracked_positions": tracked_positions,
        "tracked_mobile_stations": tracked_mobile,
        "filter": f"r/{HOME_LAT}/{HOME_LON}/{FILTER_RADIUS_KM}",
    })


if __name__ == "__main__":
    threading.Thread(target=aprs_is_loop, daemon=True).start()
    threading.Thread(target=prune_loop, daemon=True).start()
    app.run(host="0.0.0.0", port=8081)
