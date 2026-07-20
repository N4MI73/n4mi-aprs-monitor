// N4MI Desktop Instrument Series - APRSMon
// display_driver.cpp
//
// Identical hardware to n4mi-propagation-monitor -- this file is reused
// directly from that project's verified-working code, not rewritten.
// CO5300 driver confirmed working there via live red-fill test and text
// rendering; if a future unit in this project turns out to have the
// original SH8601 panel instead, swap Arduino_CO5300 for Arduino_SH8601
// below -- same constructor signature, drop-in swap.
//
// All Serial output guarded by DEBUG_VERBOSE (compile-time, not just a
// runtime `if (Serial)` check) -- confirmed necessary on this ESP32-S3
// native USB-CDC setup, since even a runtime-guarded print costs
// measurable time when nothing is connected to read it.

#include "display_driver.h"

// Set to 1 for verbose Serial diagnostic output, 0 (default) to compile
// it out entirely. Per-file convention.
#define DEBUG_VERBOSE 0

Arduino_GFX *gfx = nullptr;
static Arduino_OLED *oled = nullptr;
static Arduino_DataBus *bus = nullptr;

bool display_init() {
#if DEBUG_VERBOSE
    if (Serial) Serial.println("[display] display_init() starting");
#endif

    bus = new Arduino_ESP32QSPI(
        PIN_LCD_CS, PIN_LCD_SCLK,
        PIN_LCD_SDIO0, PIN_LCD_SDIO1, PIN_LCD_SDIO2, PIN_LCD_SDIO3);

    pinMode(PIN_LCD_VCI_EN, OUTPUT);
    digitalWrite(PIN_LCD_VCI_EN, HIGH);
    delay(20);

    // Arduino_CO5300 constructor signature: (bus, rst, r, w, h,
    // col_offset1-4) -- no ips parameter.
    gfx = new Arduino_CO5300(bus, PIN_LCD_RST, 0 /* rotation */,
                              DISPLAY_WIDTH, DISPLAY_HEIGHT);
    oled = static_cast<Arduino_OLED *>(gfx);

    if (!gfx->begin()) {
#if DEBUG_VERBOSE
        if (Serial) Serial.println("[display] gfx->begin() FAILED");
#endif
        return false;
    }

    gfx->fillScreen(RGB565_BLACK);

    // Brightness ramp -- 0 to 200 (not full 255) to avoid a startling
    // full-brightness flash on a dark desk.
    for (uint8_t i = 0; i < 200; i += 4) {
        oled->setBrightness(i);
        delay(2);
    }

#if DEBUG_VERBOSE
    if (Serial) Serial.println("[display] display_init() complete");
#endif
    return true;
}

void display_clear() {
    if (gfx) gfx->fillScreen(RGB565_BLACK);
}

void display_show_boot_message(const char *line1, const char *line2) {
    if (!gfx) return;
    display_clear();
    gfx->setTextColor(RGB565_WHITE);
    gfx->setTextSize(2);
    gfx->setCursor(40, 180);
    gfx->println(line1);
    if (line2) {
        gfx->setCursor(40, 210);
        gfx->println(line2);
    }
}
