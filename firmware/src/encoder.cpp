// N4MI Desktop Instrument Series - APRSMon
// encoder.cpp
//
// Identical hardware to n4mi-propagation-monitor -- reused directly from
// that project's verified-working file, not rewritten. That project's
// own history: rotation was originally decoded by polling A/B pins once
// per loop() iteration, which silently lost fast turns whenever loop()
// was blocked elsewhere (e.g. a full-frame QSPI display redraw). Fixed
// there by moving quadrature decoding into a GPIO change interrupt,
// which fires the instant a pin transitions regardless of what loop()
// is doing -- loop() just drains a shared counter whenever it gets a
// chance to run.
//
// Threshold of 2 (not 4) transitions per detent, and the CW/CCW
// transition table itself, both confirmed correct via real hardware
// testing on the identical board -- not re-derived here.

#include "encoder.h"
#include "config.h"

// Shared between the ISR and loop()/encoder_poll() -- must only be
// touched inside a critical section, since an ISR can fire on either
// CPU core while encoder_poll() is running on the other.
static portMUX_TYPE encoder_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t last_encoded = 0;
static volatile int32_t encoder_delta_accum = 0;

static bool button_was_pressed = false;
static unsigned long button_press_started_ms = 0;
static bool long_press_already_fired = false;
static bool reset_hold_already_fired = false;

// IRAM_ATTR: keeps this handler resident in internal RAM rather than
// flash, so an interrupt firing during a flash write (e.g. NVS) can't
// crash the device by reaching for flash-resident code that's briefly
// unreachable. Not yet load-bearing in APRSMon (no NVS writes yet,
// still on hardcoded credentials) but kept for consistency and because
// it costs nothing.
static void IRAM_ATTR encoder_isr() {
    uint8_t a = digitalRead(PIN_ENCODER_A);
    uint8_t b = digitalRead(PIN_ENCODER_B);
    uint8_t encoded = (a << 1) | b;
    uint8_t sum = (last_encoded << 2) | encoded;

    int8_t step = 0;
    switch (sum) {
        case 0b0001: case 0b0111: case 0b1110: case 0b1000: step =  1; break;
        case 0b0010: case 0b1011: case 0b1101: case 0b0100: step = -1; break;
        default: step = 0; break;
    }
    last_encoded = encoded;

    if (step != 0) {
        portENTER_CRITICAL_ISR(&encoder_mux);
        encoder_delta_accum += step;
        portEXIT_CRITICAL_ISR(&encoder_mux);
    }
}

void encoder_init() {
    pinMode(PIN_ENCODER_A, INPUT_PULLUP);
    pinMode(PIN_ENCODER_B, INPUT_PULLUP);
    pinMode(PIN_ENCODER_KEY, INPUT_PULLUP);

    uint8_t a = digitalRead(PIN_ENCODER_A);
    uint8_t b = digitalRead(PIN_ENCODER_B);
    last_encoded = (a << 1) | b;

    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A), encoder_isr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_B), encoder_isr, CHANGE);
}

EncoderEvent encoder_poll() {
    portENTER_CRITICAL(&encoder_mux);
    int32_t accum = encoder_delta_accum;
    portEXIT_CRITICAL(&encoder_mux);

    if (accum >= 2) {
        portENTER_CRITICAL(&encoder_mux);
        encoder_delta_accum -= 2;
        portEXIT_CRITICAL(&encoder_mux);
        return EncoderEvent::ROTATE_CW;
    }
    if (accum <= -2) {
        portENTER_CRITICAL(&encoder_mux);
        encoder_delta_accum += 2;
        portEXIT_CRITICAL(&encoder_mux);
        return EncoderEvent::ROTATE_CCW;
    }

    // Button press/release detection unchanged -- still polled, since
    // it doesn't need interrupt-level precision the way rotation did.
    bool is_pressed = (digitalRead(PIN_ENCODER_KEY) == LOW);
    if (is_pressed && !button_was_pressed) {
        button_was_pressed = true;
        button_press_started_ms = millis();
        long_press_already_fired = false;
        reset_hold_already_fired = false;
    } else if (is_pressed && button_was_pressed) {
        unsigned long held = millis() - button_press_started_ms;
        if (!long_press_already_fired && held >= LONG_PRESS_MS) {
            long_press_already_fired = true;
            return EncoderEvent::LONG_PRESS;
        }
        if (long_press_already_fired && !reset_hold_already_fired &&
            held >= KNOB_RESET_HOLD_MS) {
            reset_hold_already_fired = true;
            return EncoderEvent::RESET_HOLD;
        }
    } else if (!is_pressed && button_was_pressed) {
        button_was_pressed = false;
        if (!long_press_already_fired &&
            (millis() - button_press_started_ms) < LONG_PRESS_MS) {
            return EncoderEvent::SHORT_PRESS;
        }
    }
    return EncoderEvent::NONE;
}

uint32_t encoder_get_hold_ms() {
    if (!button_was_pressed) return 0;
    return (uint32_t)(millis() - button_press_started_ms);
}
