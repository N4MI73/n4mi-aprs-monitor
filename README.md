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

---

## Status

- **Weather screen: done.** Queries the [aprs.fi API](https://aprs.fi/page/api)
  for 2 fixed, known local weather station callsigns — not a proximity search,
  since aprs.fi's API doesn't support one by design. Confirmed working on real
  hardware.
- **Mobile Activity screen: backend done, firmware not yet built.** Unlike
  Weather, this needs genuine local proximity data ("what's moving nearby"),
  which aprs.fi's API can't provide — instead uses a direct, persistent
  connection to APRS-IS (the amateur radio community's own network), filtered
  to a 20-mile radius. The listener service is deployed and confirmed working
  against real traffic; the on-device screen itself is the next piece.
- **Overview / Alerts screens: not started.**

## Repo layout

```
n4mi-aprs-monitor/
├── firmware/                    <- PlatformIO project (open THIS folder in VS Code)
│   ├── platformio.ini
│   ├── include/
│   └── src/
│       └── main.cpp             <- currently renders the Weather screen
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
