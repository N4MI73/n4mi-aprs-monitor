// N4MI Desktop Instrument Series - APRSMon
// main.cpp -- navigation scaffolding milestone.
//
// GOAL OF THIS PASS: prove real navigation works across all three
// screens (Overview, Mobile Activity, Weather) plus a Config entry
// point, with always-fresh background data for both backends
// regardless of which screen is visible. NOT the goal of this pass:
// finishing Overview/Mobile/Config's actual content, or the two
// screen-dependent short-press behaviors (Weather station swap,
// Mobile Activity's Recent Stations sub-view) -- those are deliberately
// separate, later work, same "navigation first, screen behaviors after"
// sequencing Propagation Monitor itself used (its own Phase 3).
//
// Weather's render function is carried over EXACTLY as proven on real
// hardware (three rounds of photograph-and-fix) -- not touched here.
// Overview, Mobile Activity, and Config are honest placeholders,
// matching PropMon's own established precedent for screens that don't
// have real content yet.

#include <Arduino.h>
#include "config.h"
#include "display_driver.h"
#include "wifi_client.h"
#include "data_client.h"
#include "mobile_client.h"
#include "encoder.h"

// Set to 1 for verbose Serial diagnostic output, 0 for production.
// Per-file convention, matching display_driver.cpp.
#define DEBUG_VERBOSE 1

// ---------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------

enum class Screen {
    OVERVIEW,
    MOBILE,
    WEATHER,
    CONFIG,  // reached via long press from any of the above -- NOT part
             // of the normal rotation cycle, matching PropMon's Config
             // screen behavior exactly.
};

static Screen current_screen = Screen::OVERVIEW;
static Screen screen_before_config = Screen::OVERVIEW;

// Rotation order confirmed this session: Overview -> Mobile -> Weather
// -> back to Overview. next_screen()/prev_screen() define that LOGICAL
// order; physical CW was confirmed backwards on real hardware (same
// issue PropMon itself hit on this identical encoder/wiring) -- fixed
// by swapping which function each ROTATE_CW/CCW event calls in loop(),
// not by changing these two functions' own meaning.
static Screen next_screen(Screen s) {
    switch (s) {
        case Screen::OVERVIEW: return Screen::MOBILE;
        case Screen::MOBILE:   return Screen::WEATHER;
        case Screen::WEATHER:  return Screen::OVERVIEW;
        default:                return Screen::OVERVIEW;
    }
}
static Screen prev_screen(Screen s) {
    switch (s) {
        case Screen::OVERVIEW: return Screen::WEATHER;
        case Screen::MOBILE:   return Screen::OVERVIEW;
        case Screen::WEATHER:  return Screen::MOBILE;
        default:                return Screen::OVERVIEW;
    }
}

// ---------------------------------------------------------------------
// Colors (unchanged from the proven Weather-only build)
// ---------------------------------------------------------------------
static uint16_t COLOR_TEAL;
static uint16_t COLOR_LABEL;
static uint16_t COLOR_MUTED;
static uint16_t COLOR_STALE;
static uint16_t COLOR_FOOTER;
static uint16_t COLOR_RAIN;

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
    COLOR_RAIN   = gfx->color565(0x37, 0x8A, 0xDD);
}

// ---------------------------------------------------------------------
// Data state -- both sources fetched in the background regardless of
// which screen is currently visible, per this session's decision
// (mains-powered, no reason to let a screen show stale data just
// because you weren't looking at it).
// ---------------------------------------------------------------------
static WeatherData current_weather;
static bool have_weather_data = false;
static unsigned long last_weather_fetch_millis = 0;

static MobileData current_mobile;
static bool have_mobile_data = false;
static unsigned long last_mobile_fetch_millis = 0;

// Weather screen's short-press behavior: swap which station (primary/
// secondary) gets the large-value treatment. Purely a display-state
// toggle -- no new fetch needed, both stations' data are already in
// memory. Reset on any real navigation (not on ticks or the toggle
// itself) so re-arriving at Weather always starts predictable, rather
// than surprising with whatever was left over from a previous visit.
static bool weather_swapped = false;

// Mobile Activity's short-press behavior: toggle to the Recent
// Stations sub-view. Same reset-on-navigation discipline as
// weather_swapped -- always starts on the main view when arriving
// fresh, regardless of what was showing on a previous visit.
static bool mobile_show_recent = false;

#define FIRST_FETCH_RETRY_MS 5000

static void maybe_fetch_weather(unsigned long now) {
    unsigned long interval = have_weather_data ? WEATHER_FETCH_INTERVAL_MS : FIRST_FETCH_RETRY_MS;
    if (now - last_weather_fetch_millis < interval) return;
    last_weather_fetch_millis = now;
    if (data_client_fetch_weather(current_weather)) {
        have_weather_data = true;
    }
    // On failure, current_weather is left untouched (fetch function only
    // writes on full success) -- last known-good is preserved naturally.
}

static void maybe_fetch_mobile(unsigned long now) {
    unsigned long interval = have_mobile_data ? MOBILE_FETCH_INTERVAL_MS : FIRST_FETCH_RETRY_MS;
    if (now - last_mobile_fetch_millis < interval) return;
    last_mobile_fetch_millis = now;
    if (mobile_client_fetch(current_mobile)) {
        have_mobile_data = true;
    }
}

// ---------------------------------------------------------------------
// Drawing helpers (unchanged from the proven Weather-only build)
// ---------------------------------------------------------------------

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
// Weather screen -- reused EXACTLY as confirmed working on real
// hardware after three rounds of photograph-and-fix. Not touched in
// this pass.
// ---------------------------------------------------------------------

static void render_weather_screen() {
    display_clear();

    if (!have_weather_data) {
        display_show_boot_message("Weather", "Waiting for data...");
        return;
    }

    const WeatherStation &p = (weather_swapped && current_weather.secondary.valid)
                                ? current_weather.secondary : current_weather.primary;
    const WeatherStation &s = (weather_swapped && current_weather.secondary.valid)
                                ? current_weather.primary : current_weather.secondary;

    draw_centered_text("WEATHER", 195, 30, 2, COLOR_TEAL, true);

    char station_line[40];
    snprintf(station_line, sizeof(station_line), "%s - %.1f mi %s",
              p.callsign, p.distance_mi, p.bearing);
    draw_centered_text(station_line, 195, 56, 2, COLOR_LABEL);

    char temp_str[16];
    snprintf(temp_str, sizeof(temp_str), "%.0fF", p.temp_f);
    draw_centered_text(temp_str, 195, 100, 7, RGB565_WHITE, true);

    char humidity_str[24];
    snprintf(humidity_str, sizeof(humidity_str), "Humidity %.0f%%", p.humidity_pct);
    draw_centered_text(humidity_str, 195, 168, 2, COLOR_MUTED);

    gfx->drawLine(40, 190, 350, 190, COLOR_LABEL);

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

    if (s.valid) {
        char secondary_line[32];
        snprintf(secondary_line, sizeof(secondary_line), "%s %.0fF - %.0f%% RH",
                  s.callsign, s.temp_f, s.humidity_pct);
        draw_centered_text(secondary_line, 195, 340, 2, COLOR_LABEL);
    }

    char footer_str[24];
    format_age(current_weather.fetched_at_millis, footer_str, sizeof(footer_str));
    draw_centered_text(footer_str, 195, 366, 2, staleness_color(current_weather.fetched_at_millis));
}

// ---------------------------------------------------------------------
// Overview, Mobile Activity, Config -- honest placeholders. Real
// content and behavior for each is separate, later work; this pass
// only needs navigation TO them to work correctly.
// ---------------------------------------------------------------------

static void render_overview_screen() {
    display_clear();

    draw_centered_text("OVERVIEW", 195, 30, 2, COLOR_TEAL, true);

    // -- Weather section --
    draw_centered_text("WEATHER", 195, 58, 2, COLOR_LABEL);

    if (have_weather_data) {
        const WeatherStation &p = current_weather.primary;

        char temp_str[16];
        snprintf(temp_str, sizeof(temp_str), "%.0fF", p.temp_f);
        draw_centered_text(temp_str, 195, 104, 5, RGB565_WHITE, true);

        char humidity_str[24];
        snprintf(humidity_str, sizeof(humidity_str), "Humidity %.0f%%", p.humidity_pct);
        draw_centered_text(humidity_str, 195, 130, 2, COLOR_MUTED);

        // Rain callout only appears when actually raining -- dry
        // conditions show nothing extra, matching the approved mockup.
        if (p.rain_1h_in > 0.0f) {
            char rain_str[24];
            snprintf(rain_str, sizeof(rain_str), "Rain %.2f in/hr", p.rain_1h_in);
            draw_centered_text(rain_str, 195, 152, 2, COLOR_RAIN);
        }
    } else {
        draw_centered_text("Waiting for data...", 195, 110, 2, COLOR_LABEL);
    }

    gfx->drawLine(60, 172, 330, 172, COLOR_LABEL);

    // -- Mobile section --
    draw_centered_text("MOBILE", 195, 198, 2, COLOR_LABEL);

    if (have_mobile_data) {
        if (current_mobile.last_active.valid) {
            const LastActiveStation &s = current_mobile.last_active;
            draw_centered_text(s.callsign, 195, 228, 3, RGB565_WHITE, true);

            char sub_line[40];
            snprintf(sub_line, sizeof(sub_line), "%.1f mi %s - %dm ago",
                      s.distance_mi, s.bearing, s.minutes_ago);
            draw_centered_text(sub_line, 195, 254, 2, COLOR_MUTED);
        } else {
            // Quiet state -- legitimate and calm, not an error, same
            // "quiet is okay" principle as PropMon's ALL CLEAR state.
            draw_centered_text("No activity in the last hour", 195, 232, 2, COLOR_MUTED);
        }
    } else {
        draw_centered_text("Waiting for data...", 195, 232, 2, COLOR_LABEL);
    }

    gfx->drawLine(60, 274, 330, 274, COLOR_LABEL);

    draw_centered_text("Rotate to cycle", 195, 300, 2, COLOR_FOOTER);
}

static void render_mobile_main_screen() {
    display_clear();

    draw_centered_text("MOBILE", 195, 30, 2, COLOR_TEAL, true);
    draw_centered_text("within 20 mi", 195, 56, 2, COLOR_LABEL);

    if (!have_mobile_data) {
        display_show_boot_message("Mobile", "Waiting for data...");
        return;
    }

    char count_str[8];
    snprintf(count_str, sizeof(count_str), "%d", current_mobile.mobile_count_1h);
    draw_centered_text(count_str, 195, 122, 7, RGB565_WHITE, true);
    draw_centered_text("active last hour", 195, 146, 2, COLOR_MUTED);

    gfx->drawLine(60, 172, 330, 172, COLOR_LABEL);

    if (current_mobile.last_active.valid) {
        const LastActiveStation &s = current_mobile.last_active;
        draw_centered_text("LAST HEARD", 195, 202, 2, COLOR_LABEL);
        draw_centered_text(s.callsign, 195, 230, 3, RGB565_WHITE, true);

        char sub_line[24];
        snprintf(sub_line, sizeof(sub_line), "%dm ago", s.minutes_ago);
        draw_centered_text(sub_line, 195, 254, 2, COLOR_MUTED);

        char loc_line[16];
        snprintf(loc_line, sizeof(loc_line), "%.1f mi %s", s.distance_mi, s.bearing);
        draw_centered_text(loc_line, 195, 276, 2, COLOR_LABEL);
    } else {
        // Quiet state -- legitimate and calm, not an error.
        draw_centered_text("All quiet", 195, 232, 2, COLOR_TEAL);
        draw_centered_text("No activity in the last hour", 195, 256, 2, COLOR_LABEL);
    }

    gfx->drawLine(60, 292, 330, 292, COLOR_LABEL);

    char footer_str[24];
    format_age(current_mobile.fetched_at_millis, footer_str, sizeof(footer_str));
    draw_centered_text(footer_str, 195, 318, 2, staleness_color(current_mobile.fetched_at_millis));
}

static void render_mobile_recent_screen() {
    display_clear();

    draw_centered_text("RECENT", 195, 30, 2, COLOR_TEAL, true);
    draw_centered_text("within 20 mi", 195, 56, 2, COLOR_LABEL);

    gfx->drawLine(55, 74, 335, 74, COLOR_LABEL);

    if (!have_mobile_data) {
        display_show_boot_message("Recent", "Waiting for data...");
        return;
    }

    if (current_mobile.recent_count == 0) {
        draw_centered_text("No recent activity", 195, 190, 2, COLOR_MUTED);
    } else {
        // Three evenly-spaced rows, all kept close to vertical center
        // deliberately -- learned from Weather's own round-display
        // fixes, applied proactively here rather than rediscovered.
        int row_y[3] = {110, 182, 254};
        int div_y[2] = {146, 218};

        for (int i = 0; i < current_mobile.recent_count; i++) {
            const LastActiveStation &s = current_mobile.recent[i];
            draw_centered_text(s.callsign, 195, row_y[i], 3, RGB565_WHITE, true);

            char sub_line[32];
            snprintf(sub_line, sizeof(sub_line), "%.1f mi %s - %dm ago",
                      s.distance_mi, s.bearing, s.minutes_ago);
            draw_centered_text(sub_line, 195, row_y[i] + 22, 2, COLOR_MUTED);

            if (i < current_mobile.recent_count - 1 && i < 2) {
                gfx->drawLine(80, div_y[i], 310, div_y[i], COLOR_FOOTER);
            }
        }
    }

    gfx->drawLine(55, 300, 335, 300, COLOR_LABEL);
    draw_centered_text("press to return", 195, 324, 2, COLOR_FOOTER);
}

static void render_mobile_screen() {
    if (mobile_show_recent) {
        render_mobile_recent_screen();
    } else {
        render_mobile_main_screen();
    }
}

static void render_config_screen() {
    display_clear();
    draw_centered_text("CONFIG", 195, 100, 2, COLOR_TEAL, true);
    draw_centered_text("Not yet built", 195, 140, 2, COLOR_LABEL);
    draw_centered_text("Long press to return", 195, 190, 2, COLOR_MUTED);
}

static void render_current_screen() {
    switch (current_screen) {
        case Screen::OVERVIEW: render_overview_screen(); break;
        case Screen::MOBILE:   render_mobile_screen();   break;
        case Screen::WEATHER:  render_weather_screen();  break;
        case Screen::CONFIG:   render_config_screen();   break;
    }
}

// ---------------------------------------------------------------------
// setup() / loop()
// ---------------------------------------------------------------------

static unsigned long last_interaction_millis = 0;
static unsigned long last_ui_tick_millis = 0;

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
    encoder_init();

    display_show_boot_message("APRSMon", "Connecting to Wi-Fi...");

    if (!wifi_client_connect()) {
        display_show_boot_message("Wi-Fi failed", "Check credentials, reboot");
        while (true) {
            delay(1000);
        }
    }

    display_show_boot_message("APRSMon", "Fetching data...");

    last_interaction_millis = millis();
    render_current_screen();
}

void loop() {
    unsigned long now = millis();

    // Background fetches, independent of which screen is visible.
    maybe_fetch_weather(now);
    maybe_fetch_mobile(now);

    // At most one render per loop pass -- coalescing redraws, same
    // lesson PropMon learned the hard way (its own Phase 2 lag bug #1).
    bool needs_render = false;

    EncoderEvent evt = encoder_poll();
    if (evt != EncoderEvent::NONE) {
        last_interaction_millis = now;
    }

    // Weather's swap state only persists within one continuous visit --
    // any real navigation resets it, so arriving at Weather is always
    // predictable rather than showing whatever was left over.
    if (evt == EncoderEvent::ROTATE_CW || evt == EncoderEvent::ROTATE_CCW ||
        evt == EncoderEvent::LONG_PRESS) {
        weather_swapped = false;
        mobile_show_recent = false;
    }

    switch (evt) {
        case EncoderEvent::ROTATE_CW:
            if (current_screen == Screen::CONFIG) {
                current_screen = screen_before_config;
            } else {
                current_screen = prev_screen(current_screen);
            }
            needs_render = true;
            break;

        case EncoderEvent::ROTATE_CCW:
            if (current_screen == Screen::CONFIG) {
                current_screen = screen_before_config;
            } else {
                current_screen = next_screen(current_screen);
            }
            needs_render = true;
            break;

        case EncoderEvent::SHORT_PRESS:
            if (current_screen == Screen::WEATHER && current_weather.secondary.valid) {
                weather_swapped = !weather_swapped;
                needs_render = true;
            } else if (current_screen == Screen::MOBILE) {
                mobile_show_recent = !mobile_show_recent;
                needs_render = true;
            }
            break;

        case EncoderEvent::LONG_PRESS:
            if (current_screen == Screen::CONFIG) {
                current_screen = screen_before_config;
            } else {
                screen_before_config = current_screen;
                current_screen = Screen::CONFIG;
            }
            needs_render = true;
            break;

        case EncoderEvent::RESET_HOLD:
            // Real Wi-Fi setup portal not yet built -- deliberately a
            // no-op. See config.h's KNOB_RESET_HOLD_MS comment.
            break;

        default:
            break;
    }

    // Idle timeout: return to Overview from anywhere, matching
    // PropMon's own established behavior.
    if (current_screen != Screen::OVERVIEW &&
        now - last_interaction_millis >= IDLE_TIMEOUT_MS) {
        current_screen = Screen::OVERVIEW;
        needs_render = true;
    }

    if (needs_render) {
        render_current_screen();
        last_ui_tick_millis = now;
    } else if (now - last_ui_tick_millis >= UI_TICK_INTERVAL_MS) {
        render_current_screen();
        last_ui_tick_millis = now;
    }

    delay(50);
}
