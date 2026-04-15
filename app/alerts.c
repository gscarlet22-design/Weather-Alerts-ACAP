#include "alerts.h"
#include "nws.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#define MAX_TYPES 32

/* State: which ports were active after the previous poll. */
static int g_prev_active[MAX_TYPES] = { 0 };
static int g_num_types              = 0;

/* ── VAPIX virtual port control ─────────────────────────────────────────── */

static void vapix_set_port(int port, int activate,
                            const char *user, const char *pass) {
    /* action=11 → activate (high), action=10 → clear (low) */
    char url[256];
    snprintf(url, sizeof(url),
        "http://localhost/axis-cgi/io/virtualport.cgi"
        "?schemaversion=1&action=%d&port=%d",
        activate ? 11 : 10, port);

    CURL *curl = curl_easy_init();
    if (!curl) return;

    char userpwd[256];
    snprintf(userpwd, sizeof(userpwd), "%s:%s", user ? user : "", pass ? pass : "");

    curl_easy_setopt(curl, CURLOPT_URL,       url);
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH,  CURLAUTH_DIGEST);
    curl_easy_setopt(curl, CURLOPT_USERPWD,   userpwd);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,   5L);
    curl_easy_setopt(curl, CURLOPT_NOBODY,    1L); /* HEAD-style — we only need status */

    CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK || http_code != 200) {
        syslog(LOG_WARNING,
               "weather_acap: virtual port %d %s failed (curl=%d http=%ld)",
               port, activate ? "activate" : "clear", rc, http_code);
    } else {
        syslog(LOG_INFO,
               "weather_acap: virtual port %d %s OK",
               port, activate ? "ACTIVATED" : "cleared");
    }
}

/* ── CSV alert-type parser ──────────────────────────────────────────────── */

/* Parse csv into types[]; return count. Modifies a copy of csv. */
static int parse_types(const char *csv, char types[][128], int max) {
    char buf[2048];
    snprintf(buf, sizeof(buf), "%s", csv ? csv : "");

    int n = 0;
    char *save = NULL;
    char *tok  = strtok_r(buf, ",", &save);
    while (tok && n < max) {
        /* trim leading/trailing whitespace */
        while (*tok == ' ') tok++;
        char *end = tok + strlen(tok) - 1;
        while (end > tok && *end == ' ') *end-- = '\0';
        if (*tok) {
            snprintf(types[n], 128, "%s", tok);
            n++;
        }
        tok = strtok_r(NULL, ",", &save);
    }
    return n;
}

/* ── public API ─────────────────────────────────────────────────────────── */

void alerts_process(const NWSAlertSet *alerts,
                    const char *alert_types_csv,
                    int port_start,
                    const char *vapix_user,
                    const char *vapix_pass) {
    char types[MAX_TYPES][128];
    int  num = parse_types(alert_types_csv, types, MAX_TYPES);
    g_num_types = num;

    /* Build "currently active" bitmap */
    int now_active[MAX_TYPES] = { 0 };

    for (int t = 0; t < num; t++) {
        for (int a = 0; a < alerts->count; a++) {
            if (strcasecmp(alerts->alerts[a].event, types[t]) == 0) {
                now_active[t] = 1;
                break;
            }
        }
    }

    /* Fire/clear changed ports */
    for (int t = 0; t < num; t++) {
        int port = port_start + t;
        if (now_active[t] && !g_prev_active[t]) {
            syslog(LOG_WARNING, "weather_acap: ALERT active: %s → port %d", types[t], port);
            vapix_set_port(port, 1, vapix_user, vapix_pass);
        } else if (!now_active[t] && g_prev_active[t]) {
            syslog(LOG_INFO, "weather_acap: alert cleared: %s → port %d", types[t], port);
            vapix_set_port(port, 0, vapix_user, vapix_pass);
        }
        g_prev_active[t] = now_active[t];
    }
}

void alerts_clear_all(int port_start, int num_types,
                      const char *vapix_user, const char *vapix_pass) {
    for (int t = 0; t < num_types && t < MAX_TYPES; t++) {
        if (g_prev_active[t])
            vapix_set_port(port_start + t, 0, vapix_user, vapix_pass);
    }
    memset(g_prev_active, 0, sizeof(g_prev_active));
}
