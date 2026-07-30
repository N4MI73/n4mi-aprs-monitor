# N4MI APRS Monitor (APRSMon)

> ⚠️ **This repo contains both firmware and a backend server.** The PlatformIO
> project is in [`firmware/`](./firmware/) — **open that subfolder in VS Code,
> not the repo root**, or the PlatformIO extension will not find
> `firmware/platformio.ini` and its sidebar will not appear. The backend
> services live in [`server/`](./server/) and are deployed separately (Docker/
> Portainer).

A small desk instrument showing local APRS activity relevant to a home ham radio
station — part of the **N4MI Desktop Instrument Series**, the sibling project to
[n4mi-propagation-monitor](https://github.com/N4MI73/n4mi-propagation-monitor).
Same hardware platform (LilyGO T-Encoder Pro), same one-glance design philosophy,
different question: not "can I make the contact," but "what's happening in my
local operating area right now."

Unlike Propagation Monitor, this instrument's firmware and backend servers live
in the same repo.

*Screenshots coming soon.*

---

## Status

- **Overview screen: done.** The front door — a condensed glance at both other
  screens: primary station temp/humidity (plus a rain callout when it's
  actually raining), and the most recently heard mobile station.
- **Weather screen: done.** Queries the [aprs.fi API](https://aprs.fi/page/api)
  for 2 fixed, known local weather station callsigns — not a proximity search,
  since aprs.fi's API doesn't support one by design. Short press swaps which
  station is shown large.
- **Mobile Activity screen: done.** Genuine local proximity data ("what's
  moving nearby"), via a direct, persistent connection to APRS-IS (the amateur
  radio community's own network), filtered to a 20-mile radius. Shows a
  1-hour activity count and the most recently heard station; short press
  switches to a "Recent Stations" list (last 3 heard).
- **Navigation: done.** Rotate to cycle Overview → Mobile → Weather. Long
  press opens Config from any screen. 10-second idle timeout returns to
  Overview. Both backends fetch continuously in the background regardless of
  which screen is visible.
- **All of the above confirmed on real hardware, including real APRS-IS
  traffic** — not just mock/quiet-state testing.
- **Config screen: not yet built.** Will show Wi-Fi status, IP, and data
  source info. Wi-Fi is currently hardcoded credentials; porting Propagation
  Monitor's real captive-portal setup flow is separate, later work.
- **Alerts screen: not started.**

## Repo layout

```
n4mi-aprs-monitor/
├── firmware/                    <- PlatformIO project (open THIS folder in VS Code)
│   ├── platformio.ini
│   ├── include/
│   │   ├── config.h
│   │   ├── display_driver.h
│   │   ├── encoder.h
│   │   ├── wifi_client.h
│   │   ├── data_client.h        <- Weather
│   │   └── mobile_client.h      <- Mobile Activity
│   └── src/
│       ├── main.cpp             <- screen state machine, all rendering
│       ├── display_driver.cpp
│       ├── encoder.cpp
│       ├── wifi_client.cpp
│       ├── data_client.cpp
│       └── mobile_client.cpp
└── server/                      <- Backend services (each its own Docker deployment)
    ├── aprsmon_server.py        <- Weather: polls aprs.fi
    ├── Dockerfile
    ├── requirements.txt
    ├── docker-compose.yml
    ├── aprsmon_mobile.py        <- Mobile Activity: persistent APRS-IS listener
    ├── Dockerfile.mobile
    ├── requirements-mobile.txt
    └── docker-compose-mobile.yml
```

## Hardware

Same as Propagation Monitor: LilyGO T-Encoder Pro, ESP32-S3 (8MB PSRAM), 390×390
round AMOLED (`Arduino_CO5300` driver), rotary encoder with push button.

## Data sources & credit

- **Weather data:** [aprs.fi](https://aprs.fi), via their public API. Used per
  their API terms — specific, known station queries only, not proximity search.
- **Mobile Activity data:** [APRS-IS](https://www.aprs-is.net), the amateur
  radio community's own volunteer-run network, via a direct, receive-only
  connection filtered to a 20-mile radius around the operator's home location.

## License

*(to fill in)*
