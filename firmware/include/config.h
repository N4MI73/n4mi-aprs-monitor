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

// Wi-Fi setup Stage 2 -- real captive portal.
#define WIFI_SETUP_AP_NAME           "N4MI-APRSMon-Setup"
#define WIFI_SETUP_SUCCESS_GRACE_MS  3000UL    // time for the phone's browser to receive the confirmation page before the AP drops
#define WIFI_SETUP_ABANDON_TIMEOUT_MS (5UL * 60UL * 1000UL) // give up if nobody finishes

// Required by the reused encoder.cpp (identical hardware to PropMon) --
// fires RESET_HOLD if the same physical hold continues past this point,
// after LONG_PRESS already fired at LONG_PRESS_MS. PropMon uses this to
// enter its real Wi-Fi setup portal; APRSMon doesn't have that portal
// yet (still on hardcoded credentials, deliberately deferred), so
// RESET_HOLD is currently just received and ignored in main.cpp. Kept
// here, matching PropMon's real value, so the encoder driver compiles
// unmodified and the gesture is ready to wire up once that portal work
// actually happens.
#define KNOB_RESET_HOLD_MS   3000

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

// ---------------------------------------------------------------------
// APRSMon mobile activity backend (server/aprsmon_mobile.py)
// ---------------------------------------------------------------------
#define MOBILE_SERVER_URL "http://192.168.6.29:8081/api/instrument/mobile"

// How often the FIRMWARE asks the mobile backend for a fresh snapshot.
// Deliberately much more aggressive than Weather's 10-minute interval,
// since this is a self-hosted service with no external rate limit to
// respect (unlike aprs.fi), and the whole point of this data is
// feeling current. Note this is the firmware's poll interval, not the
// backend's own update cadence -- the backend itself updates
// continuously as real APRS-IS packets arrive.
#define MOBILE_FETCH_INTERVAL_MS (60UL * 1000UL)

// ---------------------------------------------------------------------
// Alerts screen thresholds
// ---------------------------------------------------------------------
// Starting guesses, not researched values -- worth revisiting once real
// data shows how often these actually fire. Checked against both
// weather stations independently; the worst active alert wins.
#define GUST_CAUTION_MPH    25.0f
#define GUST_WARNING_MPH    40.0f
#define RAIN_CAUTION_IN_HR  0.25f
#define RAIN_WARNING_IN_HR  0.50f

// N4MI-13 silence alert -- beacon interval confirmed at 10 minutes, so
// CAUTION allows for one missed beacon (APRS is inherently a bit
// lossy), WARNING means several in a row have been missed, a real
// pattern rather than noise.
#define N4MI13_SILENCE_CAUTION_MIN  30
#define N4MI13_SILENCE_WARNING_MIN  60

// Ambient alert banner -- how long the full-width overlay shows after
// a new/worsened alert, matching PropMon's own proven value for this
// exact feature.
#define ALERT_BANNER_DURATION_MS  9000UL
