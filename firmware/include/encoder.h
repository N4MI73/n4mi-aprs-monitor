// N4MI Desktop Instrument Series - APRSMon
// encoder.h -- rotary encoder rotation + knob button reading
//
// Identical hardware to n4mi-propagation-monitor -- reused directly from
// that project's verified-working file, not rewritten. Rotation decoding
// is interrupt-driven (see encoder.cpp) so that quadrature transitions
// are never missed while loop() is blocked on something else (e.g. a
// display redraw). Button press/release is still polled from loop() via
// encoder_poll() -- press timing doesn't need interrupt precision the
// way rotation does.

#pragma once

#include <Arduino.h>

enum class EncoderEvent {
    NONE,
    ROTATE_CW,
    ROTATE_CCW,
    SHORT_PRESS,
    LONG_PRESS,

    // Fires once if the same physical hold continues past
    // KNOB_RESET_HOLD_MS (config.h) -- always preceded by a LONG_PRESS
    // event for the same hold, fired earlier at LONG_PRESS_MS. In
    // PropMon this enters the real Wi-Fi setup portal; APRSMon doesn't
    // have that yet, so main.cpp currently just receives and ignores
    // this event. Kept so the encoder driver compiles unmodified.
    RESET_HOLD,
};

void encoder_init();
EncoderEvent encoder_poll();

// Returns milliseconds the button has been continuously held, or 0 if
// not currently pressed. Read-only -- does not affect the press/
// release event state tracked internally by encoder_poll(). Lets UI
// code show hold-progress feedback while a long-press is still
// building -- encoder_poll() itself only reports SHORT_PRESS/
// LONG_PRESS/RESET_HOLD as completed, discrete events after the fact.
uint32_t encoder_get_hold_ms();
