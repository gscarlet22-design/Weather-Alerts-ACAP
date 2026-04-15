/*
 * weather_acap — Native ACAP v4 main daemon
 *
 * Runs a GLib event-loop with a configurable poll timer.
 * Each tick: fetch weather → update virtual inputs → update overlay → heartbeat.
 */
#include "params.h"
#include "weather_api.h"
#include "alerts.h"
#include "overlay.h"

#include <curl/curl.h>
#include <glib.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

#define HEARTBEAT_FILE "/tmp/weather_acap_heartbeat"
#define STATUS_FILE    "/tmp/weather_acap_status.json"
#define MIN_POLL_SEC   60

static GMainLoop *g_loop = NULL;
static guint      g_timer_id = 0;

/* ── Signal handler ─────────────────────────────────────────────────────── */

static void on_signal(int sig) {
    (void)sig;
    if (g_loop) g_main_loop_quit(g_loop);
}

/* ── Status JSON ─────────────────────────────────────────────────────────── */

static void write_status(const WeatherSnapshot *snap, const char *overlay_text) {
    FILE *f = fopen(STATUS_FILE, "w");
    if (!f) return;

    time_t now = time(NULL);
    char   ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));

    fprintf(f, "{\n"
               "  \"last_poll\": \"%s\",\n"
               "  \"lat\": %.6f,\n"
               "  \"lon\": %.6f,\n"
               "  \"conditions\": {\n"
               "    \"temp_f\": %.1f,\n"
               "    \"description\": \"%s\",\n"
               "    \"wind_speed_mph\": %.1f,\n"
               "    \"wind_dir_deg\": %d,\n"
               "    \"wind_dir_str\": \"%s\",\n"
               "    \"humidity_pct\": %d,\n"
               "    \"provider\": \"%s\",\n"
               "    \"valid\": %s\n"
               "  },\n"
               "  \"alert_count\": %d,\n"
               "  \"overlay_text\": \"%s\"\n"
               "}\n",
               ts,
               snap->lat, snap->lon,
               snap->conditions.temp_f,
               snap->conditions.description,
               snap->conditions.wind_speed_mph,
               snap->conditions.wind_dir_deg,
               weather_wind_dir_str(snap->conditions.wind_dir_deg),
               snap->conditions.humidity_pct,
               snap->conditions.provider,
               snap->conditions.valid ? "true" : "false",
               snap->alerts.count,
               overlay_text ? overlay_text : "");
    fclose(f);
}

/* ── Poll callback ───────────────────────────────────────────────────────── */

static gboolean do_poll(gpointer user_data) {
    (void)user_data;

    char *zip      = params_get("ZipCode");
    char *lat_ov   = params_get("LatOverride");
    char *lon_ov   = params_get("LonOverride");
    char *provider = params_get("WeatherProvider");
    char *ua       = params_get("NWSUserAgent");
    char *atypes   = params_get("AlertTypes");
    char *vuser    = params_get("VapixUser");
    char *vpass    = params_get("VapixPass");
    char *mock     = params_get("MockMode");
    int   port_start = params_get_int("VirtualPortStart", 20);
    char *ov_enabled = params_get("OverlayEnabled");

    int is_mock = (mock && strcasecmp(mock, "yes") == 0);
    if (is_mock)
        syslog(LOG_INFO, "weather_acap: [MOCK] poll tick");
    else
        syslog(LOG_INFO, "weather_acap: poll tick (provider=%s)", provider ? provider : "auto");

    WeatherSnapshot snap;
    int ok;

    if (is_mock) {
        /* Mock data — useful for testing without NWS connectivity */
        memset(&snap, 0, sizeof(snap));
        snap.conditions.temp_f        = 72.0;
        snap.conditions.wind_speed_mph = 8.0;
        snap.conditions.wind_dir_deg  = 225;
        snap.conditions.humidity_pct  = 65;
        snap.conditions.valid         = 1;
        snprintf(snap.conditions.description, sizeof(snap.conditions.description),
                 "Mostly Cloudy");
        snprintf(snap.conditions.provider, sizeof(snap.conditions.provider), "mock");
        snprintf(snap.alerts.alerts[0].event, sizeof(snap.alerts.alerts[0].event),
                 "Tornado Warning");
        snap.alerts.count = 1;
        ok = 1;
    } else {
        ok = weather_api_fetch(provider ? provider : "auto",
                               zip, lat_ov, lon_ov,
                               ua ? ua : "WeatherACAP/2.0",
                               &snap);
    }

    if (!ok) {
        syslog(LOG_WARNING, "weather_acap: weather fetch failed — skipping this cycle");
    } else {
        /* Virtual inputs */
        alerts_process(&snap.alerts, atypes, port_start, vuser, vpass);

        /* Overlay */
        char overlay_text[256] = "";
        int overlay_on = ov_enabled && strcasecmp(ov_enabled, "yes") == 0;
        if (overlay_on) {
            /* Build preview text for status file */
            if (snap.alerts.count > 0) {
                snprintf(overlay_text, sizeof(overlay_text), "[ALERT: %s] ",
                         snap.alerts.alerts[0].event);
            }
            char cond_part[200];
            snprintf(cond_part, sizeof(cond_part),
                     "Temp: %.0f\xC2\xB0""F | %s | Wind: %.0fmph %s | Humidity: %d%%",
                     snap.conditions.temp_f,
                     snap.conditions.description,
                     snap.conditions.wind_speed_mph,
                     weather_wind_dir_str(snap.conditions.wind_dir_deg),
                     snap.conditions.humidity_pct);
            strncat(overlay_text, cond_part, sizeof(overlay_text) - strlen(overlay_text) - 1);

            overlay_update(&snap, vuser, vpass);
        }

        /* Status file */
        write_status(&snap, overlay_text);

        syslog(LOG_INFO,
               "weather_acap: %.0f°F %s | wind %.0fmph | alerts:%d",
               snap.conditions.temp_f,
               snap.conditions.description,
               snap.conditions.wind_speed_mph,
               snap.alerts.count);
    }

    /* Heartbeat */
    FILE *hb = fopen(HEARTBEAT_FILE, "w");
    if (hb) { fprintf(hb, "%ld\n", (long)time(NULL)); fclose(hb); }

    free(zip); free(lat_ov); free(lon_ov); free(provider);
    free(ua);  free(atypes); free(vuser);  free(vpass);
    free(mock); free(ov_enabled);

    return G_SOURCE_CONTINUE;
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

int main(void) {
    openlog("weather_acap", LOG_PID | LOG_CONS, LOG_USER);
    syslog(LOG_INFO, "weather_acap: starting up (native ACAP v4)");

    signal(SIGTERM, on_signal);
    signal(SIGINT,  on_signal);

    /* Initialize axparameter */
    GError *err = NULL;
    if (!params_init(&err)) {
        syslog(LOG_ERR, "weather_acap: axparameter init failed: %s",
               err ? err->message : "unknown");
        if (err) g_error_free(err);
        return 1;
    }

    /* Initialize libcurl (once per process) */
    curl_global_init(CURL_GLOBAL_DEFAULT);

    /* Run an initial poll immediately, then start the timer */
    do_poll(NULL);

    int interval = params_get_int("PollInterval", 300);
    if (interval < MIN_POLL_SEC) interval = MIN_POLL_SEC;
    syslog(LOG_INFO, "weather_acap: poll interval %d seconds", interval);

    g_loop    = g_main_loop_new(NULL, FALSE);
    g_timer_id = g_timeout_add_seconds((guint)interval, do_poll, NULL);

    g_main_loop_run(g_loop);

    /* Cleanup on shutdown */
    syslog(LOG_INFO, "weather_acap: shutting down");

    if (g_timer_id) g_source_remove(g_timer_id);

    char *vuser = params_get("VapixUser");
    char *vpass = params_get("VapixPass");
    int   port_start = params_get_int("VirtualPortStart", 20);
    char *atypes = params_get("AlertTypes");
    /* Count configured alert types to know how many ports to clear */
    int num_types = 0;
    if (atypes) {
        for (const char *p = atypes; *p; p++) if (*p == ',') num_types++;
        if (*atypes) num_types++;
    }
    alerts_clear_all(port_start, num_types, vuser, vpass);
    overlay_delete(vuser, vpass);
    free(vuser); free(vpass); free(atypes);

    params_cleanup();
    curl_global_cleanup();
    g_main_loop_unref(g_loop);
    closelog();
    return 0;
}
