// N4MI Desktop Instrument Series - APRSMon
// wifi_portal.h -- real captive-portal Wi-Fi setup
//
// Ported directly from Propagation Monitor's own real, hardware-proven
// build of this exact feature -- not reconstructed from scratch.
//
// Runs an open AP (WIFI_SETUP_AP_NAME, config.h), a DNS server that
// redirects every domain to the device's own IP (the standard captive-
// portal trick -- it's *why* phones auto-detect this as "needs sign
// in" and pop their browser automatically, not a hack specific to any
// one OS), and a small web server serving a network-scan-and-select
// form. All three are polled from loop() (see wifi_portal_process()),
// never blocking -- except the one deliberate exception described
// below.
//
// Deliberate design choice, carried over from PropMon: credential
// *validation* (the moment a submitted password is actually tried
// against the chosen network) is blocking, up to
// WIFI_CONNECT_TIMEOUT_MS. During that window the device's own screens
// and knob are unresponsive -- judged an acceptable trade-off since
// attention is on the phone during that specific action, not the
// device, unlike every other blocking-avoidance decision made
// elsewhere in this project.

#pragma once

#include <Arduino.h>

enum class WifiPortalStatus {
    SCANNING,           // async network scan still running
    WAITING,            // scan done, waiting for someone to submit the form
    CONNECTING,         // form submitted, mid-validation (blocking -- see above)
    CONNECTED,          // last attempt succeeded, credentials saved to NVS
    FAILED,             // last attempt failed, back to WAITING-equivalent state
};

// Starts the AP, DNS server, web server, and kicks off an async
// network scan. Safe to call again while already active (no-op).
void wifi_portal_start();

// Stops the AP, DNS server, and web server, and returns Wi-Fi to
// whatever state it was in before (does not itself reconnect to a
// normal network -- caller's job, e.g. via wifi_client_connect()).
// Safe to call even if not currently active (no-op).
void wifi_portal_stop();

// Must be called every loop() pass while the portal is active --
// services the DNS server and web server (both non-blocking when
// polled this way), and checks for a completed network scan. Safe to
// call when not active (returns immediately).
void wifi_portal_process();

// Returns true once wifi_portal_start() has been called and
// wifi_portal_stop() hasn't been called since.
bool wifi_portal_is_active();

// Returns true once a connection attempt has succeeded and
// credentials have been saved. Caller (main.cpp) should treat this as
// the signal to fetch live data, then call wifi_portal_stop() -- after
// waiting WIFI_SETUP_SUCCESS_GRACE_MS (config.h) so the phone's
// browser has time to fully receive the confirmation page first.
bool wifi_portal_is_complete();

// Current status, for the Setup screen to display.
WifiPortalStatus wifi_portal_get_status();

// Number of unique networks found by the last completed scan (0 while
// still scanning or if none found).
uint8_t wifi_portal_network_count();

// Number of phones/devices currently associated to the setup AP.
uint8_t wifi_portal_client_count();
