// N4MI Desktop Instrument Series - APRSMon
// display_driver.h -- AMOLED display setup (CO5300, QSPI)
//
// Identical hardware to n4mi-propagation-monitor -- reused directly from
// that project's verified-working file rather than rewritten. See that
// project's history for the full CO5300-vs-SH8601 story. display_init()
// is called from main.cpp's setup().

#pragma once

#include <Arduino_GFX_Library.h>
#include "config.h"

extern Arduino_GFX *gfx;

bool display_init();
void display_clear();
void display_show_boot_message(const char *line1, const char *line2 = nullptr);
