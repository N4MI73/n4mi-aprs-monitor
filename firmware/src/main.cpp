// N4MI Desktop Instrument Series - APRSMon
// main.cpp -- first firmware milestone: real Wi-Fi + real weather fetch +
// the approved Weather screen mockup, rendered with real data.
//
// DELIBERATELY NOT YET BUILT (see project Joplin notes / brief for the
// full plan): encoder input, multi-screen navigation, real captive-portal
// Wi-Fi setup. This is a single-screen proof of concept, same milestone
// shape as PropMon's own first live-fetch build before navigation and
// setup were added on top of it.
//
// NOT YET FLASHED OR PHOTOGRAPHED ON REAL HARDWARE. Pixel positions below
// are a direct translation of the approved SVG mockup, adjusted for two
// known hardware/library constraints carried over from n4mi-propagation-
// monitor's own hard-won lessons: textSize(1) is invisible on this
// display, so nothing here goes below size 2; and the degree symbol is
// deliberately omitted rather than risked, since extended/special glyphs
// have silently failed to render before on this exact display/library
// combination. Expect this to need real adjustment after the first actual
// flash -- per the project's own established lesson, a mockup is not a
// reliable preview of real hardware rendering.

#include <Arduino.h>
#include "config.h"
#include "display_driver.h"
#include "wifi_client.h"
#include "data_client.h"

// Set to 1 for verbose Serial diagnostic output, 0 for production.
// Per-file convention, matching display_driver.cpp. Flip to 0 once this
// is proven working on real hardware.
#define DEBUG_VERBOSE 1

// ---------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------
static uint16_t COLOR_TEAL;    // header
static uint16_t COLOR_LABEL;   // grid/station labels
static uint16_t COLOR_MUTED;   // humidity line, footer (fresh)
static uint16_t COLOR_STALE;   // footer, once data crosses the staleness threshold
static uint16_t COLOR_FOOTER;  // footer (fresh, darkest)

static void init_colors() {
    COLOR_TEAL   = gfx->color565(0x5D, 0xCA, 0xA5);
    COLOR_LABEL  = gfx->color565(0x88, 0x87, 0x80);
    COLOR_MUTED  = gfx->color565(0xB4, 0xB2, 0xA9);
    COLOR_STALE  = gfx->color565(0xFF, 0xB0, 0x00);  // placeholder amber --
                                                       // confirm against the
                                                       // series' real amber
                                                       // constant once
                                                       // ui_common is ported
    COLOR_FOOTER = gfx->color565(0x5F, 0x5E, 0x5A);
}

// ---------------------------------------------------------------------
// State
// ---------------------------------------------------------------------
static WeatherData current_data;
static bool have_data = false;
static unsigned long last_fetch_attempt_millis = 0;
static unsigned long last_ui_tick_millis = 0;

// Retry cadence while waiting for the very first successful fetch --
// deliberately much faster than the normal 10-minute cycle, since the
// backend may still be completing its own first aprs.fi poll (503) right
// after both services start up.
#define FIRST_FETCH_RETRY_MS 5000

// ---------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------

// Centers text horizontally around cx at baseline y. `bold` uses the same
// fake-bold double-draw trick documented in n4mi-propagation-monitor's
// ui_common (a 1px-offset second draw) -- not yet factored into a shared
// helper here, since ui_common hasn't been ported to this repo yet.
static void draw_centered_text(const char *text, int16_t cx, int16_t y,
                                uint8_t text_size, uint16_t color, bool bold = false) {
    gfx->setTextSize(text_size);
    gfx->setTextColor(color);

    int16_t x1, y1;
    uint16_t w, h;
    gfx->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    int16_t x = cx - w / 2;

    gfx->setCursor(x, y);
    gfx->print(text);
    if (bold) {
        gfx->setCursor(x + 1, y);
        gfx->print(text);
    }
}

static void format_age(unsigned long fetched_at_millis, char *out, size_t out_size) {
    if (fetched_at_millis == 0) {
        snprintf(out, out_size, "never");
        return;
    }
    unsigned long elapsed_s = (millis() - fetched_at_millis) / 1000;
    if (elapsed_s < 60) {
        snprintf(out, out_size, "%lus ago", elapsed_s);
    } else {
        snprintf(out, out_size, "%lum ago", elapsed_s / 60);
    }
}

static uint16_t staleness_color(unsigned long fetched_at_millis) {
    if (fetched_at_millis == 0) return COLOR_STALE;
    if (millis() - fetched_at_millis > STALE_DATA_THRESHOLD_MS) return COLOR_STALE;
    return COLOR_FOOTER;
}

// ---------------------------------------------------------------------
// Weather screen
// ---------------------------------------------------------------------

static void render_weather_screen() {
    display_clear();

    const WeatherStation &p = current_data.primary;

    // Header
    draw_centered_text("WEATHER", 195, 30, 2, COLOR_TEAL, true);

    // Primary station label
    char station_line[40];
    snprintf(station_line, sizeof(station_line), "%s - %.1f mi %s",
              p.callsign, p.distance_mi, p.bearing);
    draw_centered_text(station_line, 195, 56, 2, COLOR_LABEL);

    // Big temperature -- degree symbol deliberately omitted, see file header
    char temp_str[16];
    snprintf(temp_str, sizeof(temp_str), "%.0fF", p.temp_f);
    draw_centered_text(temp_str, 195, 100, 7, RGB565_WHITE, true);

    char humidity_str[24];
    snprintf(humidity_str, sizeof(humidity_str), "Humidity %.0f%%", p.humidity_pct);
    draw_centered_text(humidity_str, 195, 168, 2, COLOR_MUTED);

    gfx->drawLine(40, 190, 350, 190, COLOR_LABEL);

    // 2x2 stat grid: WIND | GUST / PRESSURE | RAIN 1H
    char wind_str[20], gust_str[16], pressure_str[16], rain_str[16];
    snprintf(wind_str, sizeof(wind_str), "%s %.0f mph", p.wind_dir_compass, p.wind_mph);
    snprintf(gust_str, sizeof(gust_str), "%.0f mph", p.wind_gust_mph);
    snprintf(pressure_str, sizeof(pressure_str), "%.1f", p.pressure_mbar);
    snprintf(rain_str, sizeof(rain_str), "%.2f in", p.rain_1h_in);

    draw_centered_text("WIND", 100, 218, 2, COLOR_LABEL);
    draw_centered_text(wind_str, 100, 244, 3, RGB565_WHITE, true);

    draw_centered_text("GUST", 290, 218, 2, COLOR_LABEL);
    draw_centered_text(gust_str, 290, 244, 3, RGB565_WHITE, true);

    draw_centered_text("PRESSURE", 115, 282, 2, COLOR_LABEL);
    draw_centered_text(pressure_str, 115, 308, 3, RGB565_WHITE, true);

    draw_centered_text("RAIN 1H", 275, 282, 2, COLOR_LABEL);
    draw_centered_text(rain_str, 275, 308, 3, RGB565_WHITE, true);

    gfx->drawLine(60, 330, 330, 330, COLOR_LABEL);

    // Secondary station -- compact comparison line, matching the series'
    // established "secondary = compact, not full parity" visual grammar.
    // Deliberately SHORT: this display is round, and the usable width at
    // this y-position (far from vertical center) is only ~230px, not the
    // full 390px -- confirmed by real-hardware testing 2026-07-20, where
    // the original longer version (including distance/bearing) got
    // clipped off both ends. Distance/bearing are already established by
    // the primary station; this line only needs to answer "how does the
    // second-closest station compare."
    if (current_data.secondary.valid) {
        const WeatherStation &s = current_data.secondary;
        char secondary_line[32];
        snprintf(secondary_line, sizeof(secondary_line), "%s %.0fF - %.0f%% RH",
                  s.callsign, s.temp_f, s.humidity_pct);
        draw_centered_text(secondary_line, 195, 340, 2, COLOR_LABEL);
    }

    // Footer -- also affected by the round display's narrowing width at
    // this y-position. Real hardware testing 2026-07-20 showed even the
    // shortened "Updated Xs ago" form still clipping at both ends here --
    // dropped the "Updated " prefix entirely; the age text alone is well
    // within the available width, and the color (fresh vs. stale) already
    // carries the meaning "Updated" was adding.
    char footer_str[24];
    format_age(current_data.fetched_at_millis, footer_str, sizeof(footer_str));
    draw_centered_text(footer_str, 195, 366, 2, staleness_color(current_data.fetched_at_millis));
}

// ---------------------------------------------------------------------
// Fetch
// ---------------------------------------------------------------------

static void do_fetch() {
    if (data_client_fetch_weather(current_data)) {
        have_data = true;
    }
    // On failure, current_data is left exactly as it was -- data_client_
    // fetch_weather() only writes to `out` on full success -- so this
    // naturally preserves last-known-good data with no extra logic here,
    // same "serve last known-good on failure" principle PropMon proved out.
}

// ---------------------------------------------------------------------
// setup() / loop()
// ---------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
#if DEBUG_VERBOSE
    unsigned long serial_wait_start = millis();
    while (!Serial && millis() - serial_wait_start < 3000) {
        delay(10);
    }
#endif

    display_init();
    init_colors();
    display_show_boot_message("APRSMon", "Connecting to Wi-Fi...");

    if (!wifi_client_connect()) {
        display_show_boot_message("Wi-Fi failed", "Check credentials, reboot");
        // Deliberately halt here -- no reconnect/retry logic yet. Same
        // simplification PropMon's own first live-fetch build accepted
        // before real reconnect handling was built later.
        while (true) {
            delay(1000);
        }
    }

    display_show_boot_message("APRSMon", "Fetching weather...");
    last_fetch_attempt_millis = millis();
}

void loop() {
    unsigned long now = millis();

    if (!have_data) {
        if (now - last_fetch_attempt_millis >= FIRST_FETCH_RETRY_MS) {
            do_fetch();
            last_fetch_attempt_millis = now;
            if (have_data) {
                last_ui_tick_millis = now;
                render_weather_screen();
            }
        }
        delay(50);
        return;
    }

    if (now - last_fetch_attempt_millis >= WEATHER_FETCH_INTERVAL_MS) {
        do_fetch();
        last_fetch_attempt_millis = now;
        render_weather_screen();
    } else if (now - last_ui_tick_millis >= UI_TICK_INTERVAL_MS) {
        render_weather_screen();
        last_ui_tick_millis = now;
    }

    delay(50);
}
