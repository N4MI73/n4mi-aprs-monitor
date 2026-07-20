// N4MI Desktop Instrument Series - APRSMon
// config.h -- pin assignments and non-secret constants
//
// Display/encoder pins are IDENTICAL hardware to n4mi-propagation-monitor
// (same LilyGO T-Encoder Pro board) -- copied directly from that project's
// real, working config.h rather than re-derived, per project convention of
// verifying against the real source instead of reconstructing from memory.
// Originally confirmed there from LilyGO's official Xinyuan-LilyGO/
// T-Encoder-Pro repository PinOverview table.

#pragma once

// ---------------------------------------------------------------------
// Display (CO5300 AMOLED, QSPI bus)
// ---------------------------------------------------------------------
#define PIN_LCD_SDIO0   11
#define PIN_LCD_SDIO1   13
#define PIN_LCD_SDIO2   7
#define PIN_LCD_SDIO3   14
#define PIN_LCD_SCLK    12
#define PIN_LCD_RST     4
#define PIN_LCD_CS      10
#define PIN_LCD_VCI_EN  3

#define DISPLAY_WIDTH   390
#define DISPLAY_HEIGHT  390

// ---------------------------------------------------------------------
// Touch (CST816, I2C) -- not used in v1, same as the sibling project
// ---------------------------------------------------------------------
#define PIN_TOUCH_RST   8
#define PIN_TOUCH_INT   9
#define PIN_TOUCH_SDA   5
#define PIN_TOUCH_SCL   6

// ---------------------------------------------------------------------
// Rotary encoder + knob push-button
// ---------------------------------------------------------------------
#define PIN_ENCODER_A   1
#define PIN_ENCODER_B   2

// IMPORTANT: GPIO0 is also the ESP32-S3 boot-mode strap pin.
// Holding the knob while powering on forces bootloader mode.
#define PIN_ENCODER_KEY 0

// ---------------------------------------------------------------------
// Buzzer (not used in v1)
// ---------------------------------------------------------------------
#define PIN_BUZZER      17

// ---------------------------------------------------------------------
// Interaction timing -- same values as n4mi-propagation-monitor for
// consistency across the series, until there's a real reason to diverge
// ---------------------------------------------------------------------
#define IDLE_TIMEOUT_MS      10000
#define LONG_PRESS_MS        1500

// ---------------------------------------------------------------------
// Wi-Fi (temporary hardcoded credentials -- see wifi_credentials.h /
// wifi_credentials.h.example. Real captive-portal setup is a later,
// separate piece of work, same sequencing PropMon used.)
// ---------------------------------------------------------------------
#define WIFI_CONNECT_TIMEOUT_MS   15000

// ---------------------------------------------------------------------
// APRSMon weather backend (server/aprsmon_server.py)
// ---------------------------------------------------------------------
// Compile-time constant for now, same as PropMon's URL in the sibling
// project -- real config-portal-based configurability is a later
// enhancement, not a v1 requirement.
#define APRSMON_SERVER_URL "http://192.168.6.29:8079/api/instrument/weather"

// The backend itself only polls aprs.fi every 10 minutes (see
// server/aprsmon_server.py's POLL_INTERVAL_SECONDS) -- matched here
// intentionally, since fetching more often from the device would just
// re-read the same cached value.
#define WEATHER_FETCH_INTERVAL_MS (10UL * 60UL * 1000UL)

// How old data can get before the footer's "Updated Xs ago" text changes
// color to flag it as stale. Set to 1.5x the fetch interval above: long
// enough that normal operation (one fetch every 10 min) never trips it,
// but short enough to catch a single missed fetch cycle before a second
// one goes by too. (PropMon's own ratio is closer to 10x its fetch
// interval, but PropMon's fetch interval exists only to keep the footer
// feeling responsive, not because the underlying data refreshes that
// often -- here the fetch interval IS the real data refresh rate, so a
// tighter ratio makes more sense.)
#define STALE_DATA_THRESHOLD_MS (15UL * 60UL * 1000UL)

// Same footer-freezing bug PropMon found and fixed (2026-07-15) --
// building this in from day one rather than rediscovering it.
#define UI_TICK_INTERVAL_MS       5000
