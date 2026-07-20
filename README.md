# N4MI APRS Monitor (APRSMon)

> ⚠️ **This repo contains both firmware and a backend server.** The PlatformIO
> project is in [`firmware/`](./firmware/) — **open that subfolder in VS Code,
> not the repo root**, or the PlatformIO extension will not find
> `firmware/platformio.ini` and its sidebar will not appear. The backend
> service lives in [`server/`](./server/) and is deployed separately (Docker/
> Portainer).

A small desk instrument showing local APRS activity relevant to a home ham radio
station — part of the **N4MI Desktop Instrument Series**, the sibling project to
[n4mi-propagation-monitor](https://github.com/N4MI73/n4mi-propagation-monitor).
Same hardware platform (LilyGO T-Encoder Pro), same one-glance design philosophy,
different question: not "can I make the contact," but "what's happening in my
local operating area right now."

Unlike Propagation Monitor, this instrument's firmware and backend server live
in the same repo.

---

## Status

Early development. The first screen (Weather) queries the
[aprs.fi API](https://aprs.fi/page/api) for 1-2 fixed, known local weather
station callsigns — not a proximity search, since aprs.fi's API doesn't support
one by design. Later screens (Overview, Mobile Activity) will need genuine
local proximity data, which will come from a direct APRS-IS connection instead.

## Repo layout

```
n4mi-aprs-monitor/
├── firmware/          <- PlatformIO project (open THIS folder in VS Code)
│   ├── platformio.ini
│   ├── include/
│   └── src/
└── server/             <- Backend service (aprs.fi polling now, APRS-IS listener later)
    ├── aprsmon_server.py
    ├── Dockerfile
    ├── requirements.txt
    └── docker-compose.yml
```

## Hardware

Same as Propagation Monitor: LilyGO T-Encoder Pro, ESP32-S3 (8MB PSRAM), 390×390
round AMOLED (`Arduino_CO5300` driver), rotary encoder with push button.

## Data sources & credit

- **Weather data:** [aprs.fi](https://aprs.fi), via their public API. Used per
  their API terms — specific, known station queries only, not proximity search.
- **Future proximity screens:** APRS-IS directly (the amateur radio community's
  own network), via a self-hosted listener service in `server/`.

## License

*(to fill in)*
