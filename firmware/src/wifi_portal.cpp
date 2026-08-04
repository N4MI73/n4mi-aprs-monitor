// N4MI Desktop Instrument Series - APRSMon
// wifi_portal.cpp
//
// Ported directly from Propagation Monitor's own real, hardware-proven
// build -- including its scan-before-AP sequencing fix, adopted here
// from the start rather than rediscovered. PropMon found on real
// hardware that scanning while the AP is already running fails
// outright and repeatedly -- a known ESP32 limitation (the radio can't
// reliably hop channels for a scan while also holding a fixed channel
// for the AP's beacon) -- and that a naive retry-every-loop-pass made
// it much worse, hammering the radio into a confused state that broke
// later unrelated operations too. This version scans FIRST, in plain
// STA mode, before the AP is ever started -- same pattern WiFiManager
// and similar libraries use, for the same reason. Trade-off: the AP
// doesn't exist yet during the scan, so a phone won't see it in its
// Wi-Fi list until a few seconds after RESET_HOLD fires -- the
// device's own screen/knob stay responsive throughout regardless,
// since this is still an async, polled scan.

#include "wifi_portal.h"
#include "wifi_client.h"
#include "config.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>

// Per-file convention, matching every other file in this project.
#define DEBUG_VERBOSE 0

static const byte DNS_PORT = 53;
static DNSServer dnsServer;
static WebServer server(80);

static bool portal_active = false;
static bool portal_complete = false;
static bool ap_started = false; // true once the AP/DNS/web server are actually up (after the scan)
static WifiPortalStatus status = WifiPortalStatus::SCANNING;

// Scan retry backoff -- prevents hammering WiFi.scanNetworks() on
// every loop() pass if a scan fails for a genuinely transient reason.
// Gives up gracefully after SCAN_MAX_RETRIES rather than looping
// forever -- the AP still comes up with an empty network list, which
// at least lets the setup timeout eventually clean things up instead
// of leaving the device stuck.
#define SCAN_RETRY_BACKOFF_MS 500
#define SCAN_MAX_RETRIES      6
static uint8_t scan_retry_count = 0;
static uint32_t last_scan_attempt_ms = 0;

// Deduplicated network list, rebuilt once per completed scan. Capped
// at a reasonable size -- more than enough for any real household.
#define MAX_UNIQUE_NETWORKS 24
static String unique_ssids[MAX_UNIQUE_NETWORKS];
static bool unique_locked[MAX_UNIQUE_NETWORKS];
static uint8_t unique_count = 0;
static bool scan_results_ready = false;

static void rebuild_unique_network_list() {
    int n = WiFi.scanComplete();
    unique_count = 0;
    if (n <= 0) return;

    // Networks are deduplicated by SSID, keeping the first (strongest,
    // since scan results come back sorted by RSSI) occurrence -- this
    // matters concretely for any mesh Wi-Fi system (multiple access
    // points broadcasting the same network name), not just as a
    // theoretical edge case.
    for (int i = 0; i < n && unique_count < MAX_UNIQUE_NETWORKS; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue; // hidden network, skip

        bool dup = false;
        for (uint8_t j = 0; j < unique_count; j++) {
            if (unique_ssids[j] == ssid) { dup = true; break; }
        }
        if (dup) continue;

        unique_ssids[unique_count] = ssid;
        unique_locked[unique_count] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        unique_count++;
    }

#if DEBUG_VERBOSE
    if (Serial) Serial.printf("[portal] scan complete: %d unique networks\n", unique_count);
#endif
}

static void send_setup_page(const char *error_msg) {
    String html;
    html.reserve(2200);
    html += "<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>N4MI APRSMon Setup</title><style>"
            "body{font-family:sans-serif;background:#111;color:#eee;padding:20px;max-width:420px;margin:0 auto;}"
            "h1{color:#5DCAA5;font-size:20px;}"
            "label{display:block;margin-top:14px;font-size:13px;color:#999;}"
            "select,input[type=password]{width:100%;padding:10px;margin-top:4px;font-size:16px;"
            "background:#222;color:#eee;border:1px solid #444;border-radius:4px;box-sizing:border-box;}"
            "button{width:100%;padding:12px;font-size:16px;background:#5DCAA5;color:#000;"
            "border:none;border-radius:4px;font-weight:bold;margin-top:18px;}"
            ".err{color:#E24B4A;margin:10px 0;padding:10px;background:#2a1414;border-radius:4px;}"
            "</style></head><body>";
    html += "<h1>N4MI APRSMon Wi-Fi Setup</h1>";

    if (error_msg) {
        html += "<div class='err'>" + String(error_msg) + "</div>";
    }

    if (unique_count == 0) {
        html += "<p>No networks found. Move the device closer to your router, "
                "then reconnect to this network and reload this page.</p>";
    } else {
        html += "<form method='POST' action='/connect'>";
        html += "<label>Network</label><select name='ssid'>";
        for (uint8_t i = 0; i < unique_count; i++) {
            html += "<option value='" + unique_ssids[i] + "'>" + unique_ssids[i];
            if (unique_locked[i]) html += " &#128274;";
            html += "</option>";
        }
        html += "</select>";
        html += "<label>Password</label>"
                "<input type='password' name='pass' autocomplete='off'>";
        html += "<button type='submit'>Connect</button>";
        html += "</form>";
    }
    html += "</body></html>";

    server.send(200, "text/html", html);
}

static void handle_root() {
    send_setup_page(nullptr);
}

static void handle_connect() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");

    if (ssid.length() == 0) {
        send_setup_page("Please choose a network.");
        return;
    }

    status = WifiPortalStatus::CONNECTING;
#if DEBUG_VERBOSE
    if (Serial) Serial.printf("[portal] validating credentials for \"%s\"\n", ssid.c_str());
#endif

    // Deliberately blocking, up to WIFI_CONNECT_TIMEOUT_MS -- see
    // wifi_portal.h's header comment. wifi_client_try_credentials()
    // does NOT touch WiFi.mode(), so the AP interface (and this
    // phone's connection to it) stays alive throughout the attempt.
    bool ok = wifi_client_try_credentials(ssid.c_str(), pass.c_str());

    if (ok) {
        wifi_client_save_credentials(ssid.c_str(), pass.c_str());
        status = WifiPortalStatus::CONNECTED;
        portal_complete = true;
#if DEBUG_VERBOSE
        if (Serial) Serial.println("[portal] connected, credentials saved");
#endif
        server.send(200, "text/html",
            "<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'></head>"
            "<body style='font-family:sans-serif;background:#111;color:#eee;padding:20px;'>"
            "<h1 style='color:#5DCAA5;'>Connected!</h1>"
            "<p>You can close this page. The instrument will start showing live data shortly.</p>"
            "</body></html>");
    } else {
        status = WifiPortalStatus::FAILED;
#if DEBUG_VERBOSE
        if (Serial) Serial.println("[portal] connection FAILED");
#endif
        send_setup_page("Could not connect -- check the password and try again.");
    }
}

static void start_ap_and_server() {
    if (ap_started) return;

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(WIFI_SETUP_AP_NAME); // open network, no password

    IPAddress apIP = WiFi.softAPIP();
    dnsServer.start(DNS_PORT, "*", apIP);

    server.on("/", HTTP_GET, handle_root);
    server.on("/connect", HTTP_POST, handle_connect);
    server.onNotFound(handle_root); // catches every OS's captive-portal probe URL
    server.begin();

    ap_started = true;
    status = WifiPortalStatus::WAITING;

#if DEBUG_VERBOSE
    if (Serial) Serial.printf("[portal] AP started (post-scan), AP=\"%s\" IP=%s\n",
        WIFI_SETUP_AP_NAME, apIP.toString().c_str());
#endif
}

void wifi_portal_start() {
    if (portal_active) return;

    portal_active = true;
    portal_complete = false;
    ap_started = false;
    status = WifiPortalStatus::SCANNING;
    scan_results_ready = false;
    unique_count = 0;
    scan_retry_count = 0;

    // Scan BEFORE the AP exists -- see file header for why. Plain STA
    // mode here (not AP_STA yet) avoids the channel conflict entirely.
    WiFi.mode(WIFI_STA);
    WiFi.scanNetworks(true);
    last_scan_attempt_ms = millis();

#if DEBUG_VERBOSE
    if (Serial) Serial.println("[portal] scanning (AP not started yet)");
#endif
}

void wifi_portal_stop() {
    if (!portal_active) return;

    if (ap_started) {
        server.stop();
        dnsServer.stop();
        WiFi.softAPdisconnect(true);
    }

    // Explicitly drop back to pure STA mode, whether or not the AP
    // ever actually started this time.
    WiFi.mode(WIFI_STA);

    portal_active = false;
    portal_complete = false;
    ap_started = false;
    status = WifiPortalStatus::SCANNING;

#if DEBUG_VERBOSE
    if (Serial) Serial.println("[portal] stopped");
#endif
}

void wifi_portal_process() {
    if (!portal_active) return;

    if (!scan_results_ready) {
        int n = WiFi.scanComplete();
        if (n >= 0) {
            rebuild_unique_network_list();
            scan_results_ready = true;
            start_ap_and_server();
        } else if (n == WIFI_SCAN_FAILED) {
            if (scan_retry_count >= SCAN_MAX_RETRIES) {
                // Give up gracefully rather than looping forever --
                // bring the AP up anyway with an empty network list.
                // The person can still reload the page later (unlikely
                // to help without a real fix, but doesn't hang), and
                // the setup abandon-timeout eventually cleans up if
                // nothing else does.
#if DEBUG_VERBOSE
                if (Serial) Serial.println("[portal] scan retries exhausted, starting AP anyway");
#endif
                scan_results_ready = true;
                unique_count = 0;
                start_ap_and_server();
            } else if ((millis() - last_scan_attempt_ms) >= SCAN_RETRY_BACKOFF_MS) {
                scan_retry_count++;
#if DEBUG_VERBOSE
                if (Serial) Serial.printf("[portal] scan failed, retry %d/%d\n",
                    scan_retry_count, SCAN_MAX_RETRIES);
#endif
                WiFi.scanNetworks(true);
                last_scan_attempt_ms = millis();
            }
            // else: still within backoff window, do nothing this pass
        }
        return; // AP/DNS/web server don't exist yet during the scan phase
    }

    dnsServer.processNextRequest();
    server.handleClient();
}

bool wifi_portal_is_active() {
    return portal_active;
}

bool wifi_portal_is_complete() {
    return portal_complete;
}

WifiPortalStatus wifi_portal_get_status() {
    return status;
}

uint8_t wifi_portal_network_count() {
    return unique_count;
}

uint8_t wifi_portal_client_count() {
    if (!ap_started) return 0;
    return WiFi.softAPgetStationNum();
}
