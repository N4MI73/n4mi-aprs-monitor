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
#include <WiFi.h>
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
    ALERTS,
    CONFIG,  // reached via long press from any of the above -- NOT part
             // of the normal rotation cycle, matching PropMon's Config
             // screen behavior exactly.
    SETUP,   // reached by holding PAST the point Config triggers (the
             // RESET_HOLD event) -- "hold longer to go further", same
             // gesture PropMon itself used for this same purpose.
             // Wi-Fi setup Stage 1: placeholder only, no real portal yet.
};

static Screen current_screen = Screen::OVERVIEW;

// Hold-progress bar: 0 means no bar currently drawn on screen. Tracked
// separately from encoder_get_hold_ms() itself so we know when a
// release needs to trigger a redraw to erase a partially-grown bar.
static uint32_t last_hold_bar_width = 0;

// Reused for both Config and Setup -- both are reached via the same
// physical long-press gesture (Setup only differs by the hold
// continuing further), so this correctly captures "the screen active
// before this whole hold began" regardless of which of the two it's
// returning from.
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
        case Screen::WEATHER:  return Screen::ALERTS;
        case Screen::ALERTS:   return Screen::OVERVIEW;
        default:                return Screen::OVERVIEW;
    }
}
static Screen prev_screen(Screen s) {
    switch (s) {
        case Screen::OVERVIEW: return Screen::ALERTS;
        case Screen::MOBILE:   return Screen::OVERVIEW;
        case Screen::WEATHER:  return Screen::MOBILE;
        case Screen::ALERTS:   return Screen::WEATHER;
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
static uint16_t COLOR_RED;

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
    COLOR_RED    = gfx->color565(0xE2, 0x4B, 0x4A);
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
// Long-press hold-progress bar. PropMon's own real history here: a
// first design (a ring of ~90 small drawLine() calls) never rendered
// on real hardware despite correct underlying logic -- root-caused to
// a genuine quirk of this display/library combination, where single
// primitive calls of any size render reliably but many small
// primitives fired in a loop do not. Fixed there with a single growing
// fillRect() bar; reused directly here rather than rediscovering the
// same failure.
//
// Deliberately single-phase, matching what PropMon actually shipped:
// fills 0->100% across LONG_PRESS_MS (the Config threshold), then just
// sits full during the extended hold toward RESET_HOLD_MS (Setup) --
// PropMon's own second-phase fill for that extended window was a
// documented, deferred backlog item there too, never actually built.
// ---------------------------------------------------------------------

#define HOLD_BAR_MAX_WIDTH 100
#define HOLD_BAR_HEIGHT    6
#define HOLD_BAR_Y         10

static void draw_hold_progress_bar(uint32_t width_px) {
    int x0 = 195 - HOLD_BAR_MAX_WIDTH / 2;
    gfx->fillRect(x0, HOLD_BAR_Y, (int)width_px, HOLD_BAR_HEIGHT, COLOR_TEAL);
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
// Alerts -- evaluates all known alert sources against thresholds.
// Reuses PropMon's exact severity tiers rather than inventing new
// ones. Structured so future sources (e.g. KC4GS-13's early-warning
// checks, still pending) slot in as one more candidate here, not a
// restructuring -- already proven once by adding the N4MI-13 silence
// check below without touching the weather-threshold logic.
// ---------------------------------------------------------------------

enum class AlertLevel { NONE, CAUTION, WARNING, CRITICAL };
enum class AlertCategory { WEATHER, SYSTEM };

// Max candidates any single find_worst_alert() call can hold. Bump
// this if a future source could add more than one candidate at once
// (weather currently contributes up to 4: gust+rain x2 stations).
#define ALERT_MAX_CANDIDATES 5

struct Alert {
    AlertLevel level = AlertLevel::NONE;
    AlertCategory category = AlertCategory::WEATHER;
    char message[24] = "";
    char detail_line[28] = "";
};

static AlertLevel gust_level(float mph) {
    if (mph >= GUST_WARNING_MPH) return AlertLevel::WARNING;
    if (mph >= GUST_CAUTION_MPH) return AlertLevel::CAUTION;
    return AlertLevel::NONE;
}

static AlertLevel rain_level(float in_hr) {
    if (in_hr >= RAIN_WARNING_IN_HR) return AlertLevel::WARNING;
    if (in_hr >= RAIN_CAUTION_IN_HR) return AlertLevel::CAUTION;
    return AlertLevel::NONE;
}

// -1 (never heard yet) deliberately does NOT alert -- that's "no
// baseline established," not "gone silent," same distinction this
// project draws everywhere else between waiting-for-first-data and an
// actual problem.
static AlertLevel silence_level(int minutes_since_heard) {
    if (minutes_since_heard < 0) return AlertLevel::NONE;
    if (minutes_since_heard >= N4MI13_SILENCE_WARNING_MIN) return AlertLevel::WARNING;
    if (minutes_since_heard >= N4MI13_SILENCE_CAUTION_MIN) return AlertLevel::CAUTION;
    return AlertLevel::NONE;
}

static void add_weather_candidate_if_alert(AlertLevel level, const char *msg_fmt, float value,
                                             const WeatherStation &station,
                                             Alert *candidates, int &n) {
    if (level == AlertLevel::NONE || n >= ALERT_MAX_CANDIDATES) return;
    Alert &c = candidates[n++];
    c.level = level;
    c.category = AlertCategory::WEATHER;
    snprintf(c.message, sizeof(c.message), msg_fmt, value);
    snprintf(c.detail_line, sizeof(c.detail_line), "%s - %.1f mi %s",
              station.callsign, station.distance_mi, station.bearing);
}

// Finds the single worst active alert across all sources and how many
// total are active. `out` is untouched (stays AlertLevel::NONE) if
// nothing is active -- callers should check total_count, not just
// out.level, since a zero-count call leaves out in its default state
// either way.
static void find_worst_alert(Alert &out, int &total_count) {
    Alert candidates[ALERT_MAX_CANDIDATES];
    int n = 0;

    if (have_weather_data) {
        const WeatherStation &p = current_weather.primary;
        add_weather_candidate_if_alert(gust_level(p.wind_gust_mph), "Gust %.0f mph", p.wind_gust_mph, p, candidates, n);
        add_weather_candidate_if_alert(rain_level(p.rain_1h_in), "Rain %.2f in/hr", p.rain_1h_in, p, candidates, n);

        if (current_weather.secondary.valid) {
            const WeatherStation &s = current_weather.secondary;
            add_weather_candidate_if_alert(gust_level(s.wind_gust_mph), "Gust %.0f mph", s.wind_gust_mph, s, candidates, n);
            add_weather_candidate_if_alert(rain_level(s.rain_1h_in), "Rain %.2f in/hr", s.rain_1h_in, s, candidates, n);
        }
    }

    if (have_mobile_data && n < ALERT_MAX_CANDIDATES) {
        AlertLevel lvl = silence_level(current_mobile.home_station_minutes_ago);
        if (lvl != AlertLevel::NONE) {
            Alert &c = candidates[n++];
            c.level = lvl;
            c.category = AlertCategory::SYSTEM;
            snprintf(c.message, sizeof(c.message), "N4MI-13 silent");
            snprintf(c.detail_line, sizeof(c.detail_line), "%dm since last heard",
                      current_mobile.home_station_minutes_ago);
        }
    }

    total_count = n;
    for (int i = 0; i < n; i++) {
        if ((int)candidates[i].level > (int)out.level) {
            out = candidates[i];
        }
    }
}

static void render_alerts_screen() {
    display_clear();

    draw_centered_text("ALERTS", 195, 34, 3, COLOR_TEAL, true);
    gfx->drawLine(60, 64, 330, 64, COLOR_LABEL);

    Alert worst;
    int count = 0;
    find_worst_alert(worst, count);

    if (count == 0) {
        // Legitimate calm state -- same "quiet is okay" principle used
        // everywhere else in this series.
        draw_centered_text("ALL CLEAR", 195, 200, 4, COLOR_TEAL, true);
        draw_centered_text("No active alerts", 195, 240, 2, COLOR_LABEL);
    } else {
        uint16_t level_color = (worst.level == AlertLevel::CAUTION) ? COLOR_STALE : COLOR_RED;
        const char *level_word =
            (worst.level == AlertLevel::CAUTION)  ? "CAUTION" :
            (worst.level == AlertLevel::WARNING)  ? "WARNING" : "CRITICAL";
        const char *category_label =
            (worst.category == AlertCategory::WEATHER) ? "WEATHER" : "SYSTEM";

        draw_centered_text(category_label, 195, 108, 2, COLOR_LABEL);
        draw_centered_text(level_word, 195, 138, 3, level_color, true);
        draw_centered_text(worst.message, 195, 170, 2, COLOR_MUTED);
        draw_centered_text(worst.detail_line, 195, 192, 2, COLOR_LABEL);

        if (count > 1) {
            char more_line[20];
            snprintf(more_line, sizeof(more_line), "+%d more alert%s",
                      count - 1, (count - 1 == 1) ? "" : "s");
            draw_centered_text(more_line, 195, 228, 2, COLOR_FOOTER);
        }
    }

    gfx->drawLine(60, 330, 330, 330, COLOR_LABEL);
    draw_centered_text("Rotate to cycle", 195, 356, 2, COLOR_FOOTER);
}

// ---------------------------------------------------------------------
// Overview, Mobile Activity, Config -- honest placeholders. Real
// content and behavior for each is separate, later work; this pass
// only needs navigation TO them to work correctly.
// ---------------------------------------------------------------------

static void render_overview_screen() {
    display_clear();

    // APRSMON is now the screen's main title, replacing the separate
    // small-APRSMON + OVERVIEW pair from the previous pass -- matches
    // PropMon's own pattern of a consistent app-identity header.
    draw_centered_text("APRSMON", 195, 32, 3, COLOR_TEAL, true);

    // -- Weather section --
    draw_centered_text("WEATHER", 195, 68, 3, COLOR_LABEL, true);

    if (have_weather_data) {
        const WeatherStation &p = current_weather.primary;

        char temp_str[16];
        snprintf(temp_str, sizeof(temp_str), "%.0fF", p.temp_f);
        draw_centered_text(temp_str, 195, 100, 5, RGB565_WHITE, true);

        char humidity_str[24];
        snprintf(humidity_str, sizeof(humidity_str), "Humidity %.0f%%", p.humidity_pct);
        draw_centered_text(humidity_str, 195, 156, 3, COLOR_MUTED);

        if (p.rain_1h_in > 0.0f) {
            char rain_str[24];
            snprintf(rain_str, sizeof(rain_str), "Rain %.2f in/hr", p.rain_1h_in);
            draw_centered_text(rain_str, 195, 186, 2, COLOR_RAIN);
        }
    } else {
        draw_centered_text("Waiting for data...", 195, 108, 2, COLOR_LABEL);
    }

    gfx->drawLine(60, 214, 330, 214, COLOR_LABEL);

    // -- Mobile section --
    draw_centered_text("MOBILE", 195, 240, 3, COLOR_LABEL, true);

    if (have_mobile_data) {
        if (current_mobile.last_active.valid) {
            const LastActiveStation &s = current_mobile.last_active;
            draw_centered_text(s.callsign, 195, 270, 3, RGB565_WHITE, true);

            char sub_line[40];
            snprintf(sub_line, sizeof(sub_line), "%.1f mi %s - %dm ago",
                      s.distance_mi, s.bearing, s.minutes_ago);
            draw_centered_text(sub_line, 195, 300, 2, COLOR_MUTED);
        } else {
            draw_centered_text("No activity in last hour", 195, 270, 2, COLOR_MUTED);
        }
    } else {
        draw_centered_text("Waiting for data...", 195, 270, 2, COLOR_LABEL);
    }

    gfx->drawLine(60, 330, 330, 330, COLOR_LABEL);

    draw_centered_text("Rotate to cycle", 195, 352, 2, COLOR_FOOTER);
}

static void render_mobile_main_screen() {
    display_clear();

    draw_centered_text("MOBILE", 195, 32, 3, COLOR_TEAL, true);
    draw_centered_text("within 20 mi", 195, 68, 2, COLOR_LABEL);

    if (!have_mobile_data) {
        display_show_boot_message("Mobile", "Waiting for data...");
        return;
    }

    char count_str[8];
    snprintf(count_str, sizeof(count_str), "%d", current_mobile.mobile_count_1h);
    draw_centered_text(count_str, 195, 112, 7, RGB565_WHITE, true);

    // 68px clearance below the size-7 count -- Weather's proven gap.
    draw_centered_text("active last hour", 195, 180, 3, COLOR_MUTED, true);

    gfx->drawLine(60, 214, 330, 214, COLOR_LABEL);

    if (current_mobile.last_active.valid) {
        const LastActiveStation &s = current_mobile.last_active;
        draw_centered_text("LAST HEARD", 195, 240, 2, COLOR_LABEL, true);
        draw_centered_text(s.callsign, 195, 266, 3, RGB565_WHITE, true);

        char sub_line[40];
        snprintf(sub_line, sizeof(sub_line), "%.1f mi %s - %dm ago",
                  s.distance_mi, s.bearing, s.minutes_ago);
        draw_centered_text(sub_line, 195, 296, 2, COLOR_MUTED);
    } else {
        draw_centered_text("All quiet", 195, 252, 3, COLOR_TEAL, true);
        draw_centered_text("No activity in last hour", 195, 282, 2, COLOR_LABEL);
    }

    gfx->drawLine(60, 326, 330, 326, COLOR_LABEL);

    char footer_str[24];
    format_age(current_mobile.fetched_at_millis, footer_str, sizeof(footer_str));
    draw_centered_text(footer_str, 195, 350, 2, staleness_color(current_mobile.fetched_at_millis));
}

static void render_mobile_recent_screen() {
    display_clear();

    draw_centered_text("RECENT", 195, 30, 3, COLOR_TEAL, true);
    draw_centered_text("within 20 mi", 195, 62, 2, COLOR_LABEL);

    gfx->drawLine(55, 90, 335, 90, COLOR_LABEL);

    if (!have_mobile_data) {
        display_show_boot_message("Recent", "Waiting for data...");
        return;
    }

    if (current_mobile.recent_count == 0) {
        draw_centered_text("No recent activity", 195, 200, 2, COLOR_MUTED);
    } else {
        // Three evenly-spaced rows, all kept close to vertical center
        // deliberately -- learned from Weather's own round-display
        // fixes, applied proactively here rather than rediscovered.
        int row_y[3] = {126, 198, 270};
        int div_y[2] = {162, 234};

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

    gfx->drawLine(55, 316, 335, 316, COLOR_LABEL);
    draw_centered_text("press to return", 195, 340, 2, COLOR_FOOTER);
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

    draw_centered_text("CONFIG", 195, 30, 3, COLOR_TEAL, true);
    gfx->drawLine(60, 60, 330, 60, COLOR_LABEL);

    // -- Wi-Fi --
    draw_centered_text("WI-FI", 195, 82, 2, COLOR_LABEL);
    if (WiFi.status() == WL_CONNECTED) {
        draw_centered_text("Connected", 195, 104, 3, COLOR_TEAL, true);
        draw_centered_text(WiFi.localIP().toString().c_str(), 195, 134, 2, COLOR_MUTED);
    } else {
        draw_centered_text("Disconnected", 195, 104, 3, COLOR_STALE, true);
    }

    gfx->drawLine(60, 156, 330, 156, COLOR_LABEL);

    // -- Weather --
    draw_centered_text("WEATHER", 195, 176, 2, COLOR_LABEL);
    if (have_weather_data) {
        char line[24];
        char age[16];
        format_age(current_weather.fetched_at_millis, age, sizeof(age));
        snprintf(line, sizeof(line), "LIVE - %s", age);
        draw_centered_text(line, 195, 198, 2, COLOR_TEAL, true);
    } else {
        draw_centered_text("Waiting...", 195, 198, 2, COLOR_STALE, true);
    }

    // -- Mobile --
    draw_centered_text("MOBILE", 195, 220, 2, COLOR_LABEL);
    if (have_mobile_data) {
        char line[24];
        char age[16];
        format_age(current_mobile.fetched_at_millis, age, sizeof(age));
        snprintf(line, sizeof(line), "LIVE - %s", age);
        draw_centered_text(line, 195, 242, 2, COLOR_TEAL, true);
    } else {
        draw_centered_text("Waiting...", 195, 242, 2, COLOR_STALE, true);
    }

    // -- Home weather station liveness (N4MI-13, separate weewx pipeline) --
    // Only meaningful once Mobile's own backend has actually returned
    // data at least once; -1 in that data means "never heard yet."
    draw_centered_text("HOME WX", 195, 264, 2, COLOR_LABEL);
    if (have_mobile_data && current_mobile.home_station_minutes_ago >= 0) {
        char line[24];
        snprintf(line, sizeof(line), "LIVE - %dm ago", current_mobile.home_station_minutes_ago);
        draw_centered_text(line, 195, 286, 2, COLOR_TEAL, true);
    } else {
        draw_centered_text("Not heard yet", 195, 286, 2, COLOR_STALE, true);
    }

    gfx->drawLine(60, 308, 330, 308, COLOR_LABEL);
    draw_centered_text("Long press to return", 195, 330, 2, COLOR_FOOTER);
}

// Wi-Fi setup Stage 1 -- honest placeholder, matching PropMon's own
// precedent for this exact screen. No real portal exists yet; that's
// Stage 2. Reached by continuing a long press past the point Config
// normally triggers (the RESET_HOLD event).
static void render_setup_screen() {
    display_clear();

    draw_centered_text("SETUP", 195, 34, 3, COLOR_TEAL, true);
    gfx->drawLine(60, 60, 330, 60, COLOR_LABEL);

    draw_centered_text("Wi-Fi setup portal", 195, 130, 2, COLOR_MUTED);
    draw_centered_text("not yet built", 195, 154, 2, COLOR_MUTED);

    gfx->drawLine(60, 200, 330, 200, COLOR_LABEL);

    draw_centered_text("Currently using", 195, 230, 2, COLOR_LABEL);
    draw_centered_text("stored or hardcoded", 195, 254, 2, COLOR_LABEL);
    draw_centered_text("credentials", 195, 278, 2, COLOR_LABEL);

    draw_centered_text("Rotate or long press", 195, 316, 2, COLOR_FOOTER);
    draw_centered_text("to return", 195, 338, 2, COLOR_FOOTER);
}

static void render_current_screen() {
    switch (current_screen) {
        case Screen::OVERVIEW: render_overview_screen(); break;
        case Screen::MOBILE:   render_mobile_screen();   break;
        case Screen::WEATHER:  render_weather_screen();  break;
        case Screen::ALERTS:   render_alerts_screen();   break;
        case Screen::CONFIG:   render_config_screen();   break;
        case Screen::SETUP:    render_setup_screen();    break;
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

    // Hold-progress bar -- independent of encoder_poll()'s discrete
    // events, since it needs to update WHILE a press is still ongoing,
    // not just after it resolves. encoder_get_hold_ms() is read-only
    // and doesn't affect the press/release state machine at all.
    uint32_t hold_ms = encoder_get_hold_ms();
    if (hold_ms > 0) {
        uint32_t capped_ms = (hold_ms > (uint32_t)LONG_PRESS_MS) ? (uint32_t)LONG_PRESS_MS : hold_ms;
        uint32_t bar_width = (capped_ms * HOLD_BAR_MAX_WIDTH) / (uint32_t)LONG_PRESS_MS;
        if (bar_width != last_hold_bar_width) {
            draw_hold_progress_bar(bar_width);
            last_hold_bar_width = bar_width;
        }
    } else if (last_hold_bar_width != 0) {
        // Button was just released. Most release paths already trigger
        // a full redraw below (LONG_PRESS/RESET_HOLD change screens;
        // Weather/Mobile's SHORT_PRESS already redraws) -- but a short
        // press on a screen with no short-press behavior (Overview,
        // Alerts, Config, Setup) wouldn't otherwise redraw, which would
        // leave a partially-grown bar stuck on screen until the next
        // tick. Force it here so every release path is covered.
        needs_render = true;
        last_hold_bar_width = 0;
    }

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
            if (current_screen == Screen::CONFIG || current_screen == Screen::SETUP) {
                current_screen = screen_before_config;
            } else {
                current_screen = prev_screen(current_screen);
            }
            needs_render = true;
            break;

        case EncoderEvent::ROTATE_CCW:
            if (current_screen == Screen::CONFIG || current_screen == Screen::SETUP) {
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
            if (current_screen == Screen::CONFIG || current_screen == Screen::SETUP) {
                current_screen = screen_before_config;
            } else {
                screen_before_config = current_screen;
                current_screen = Screen::CONFIG;
            }
            needs_render = true;
            break;

        case EncoderEvent::RESET_HOLD:
            // "Hold longer to go further" -- overrides whatever
            // LONG_PRESS just did for this same physical hold (which
            // already tentatively entered Config, capturing
            // screen_before_config as the real pre-hold screen just
            // before doing so). Reuse that same captured value as
            // where Setup should return to -- no separate tracking
            // variable needed.
            current_screen = Screen::SETUP;
            needs_render = true;
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
