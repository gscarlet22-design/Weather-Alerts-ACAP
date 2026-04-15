/*
 * config_cgi — Web UI backend for Weather ACAP
 *
 * GET  /local/weather_acap/config_cgi  → JSON with current config + status
 * POST /local/weather_acap/config_cgi  → save config params, return JSON result
 *
 * Compiled as a native binary; executed by the device web server as CGI.
 * Uses axparameter directly — no CSRF token required.
 */
#include "params.h"
#include "cJSON.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STATUS_FILE "/tmp/weather_acap_status.json"

/* ── URL decode ─────────────────────────────────────────────────────────── */

static char *url_decode(const char *src) {
    if (!src) return strdup("");
    char *dst = (char *)malloc(strlen(src) + 1);
    char *p   = dst;
    while (*src) {
        if (*src == '+') { *p++ = ' '; src++; }
        else if (*src == '%' && src[1] && src[2]) {
            char h[3] = { src[1], src[2], 0 };
            *p++ = (char)strtol(h, NULL, 16);
            src += 3;
        } else { *p++ = *src++; }
    }
    *p = '\0';
    return dst;
}

/* ── POST body parser ───────────────────────────────────────────────────── */

typedef struct { char key[64]; char *value; } KV;

static int parse_form(const char *body, KV *out, int max) {
    char *copy = strdup(body);
    int   n    = 0;
    char *save = NULL;
    char *tok  = strtok_r(copy, "&", &save);
    while (tok && n < max) {
        char *eq = strchr(tok, '=');
        if (eq) {
            *eq = '\0';
            char *k = url_decode(tok);
            char *v = url_decode(eq + 1);
            snprintf(out[n].key, sizeof(out[n].key), "%s", k);
            out[n].value = v;
            free(k);
            n++;
        }
        tok = strtok_r(NULL, "&", &save);
    }
    free(copy);
    return n;
}

/* ── Output helpers ─────────────────────────────────────────────────────── */

static void json_header(void) {
    printf("Content-Type: application/json\r\n"
           "Cache-Control: no-cache\r\n"
           "\r\n");
}

static void html_header(void) {
    printf("Content-Type: text/html\r\n"
           "Cache-Control: no-cache\r\n"
           "\r\n");
}

/* ── Read status file ───────────────────────────────────────────────────── */

static char *read_status_file(void) {
    FILE *f = fopen(STATUS_FILE, "r");
    if (!f) return strdup("{}");
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return strdup("{}"); }
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

/* ── GET handler ─────────────────────────────────────────────────────────── */

static void handle_get(void) {
    json_header();

    char *status = read_status_file();

    /* Collect current params */
    char *zip      = params_get("ZipCode");
    char *lat_ov   = params_get("LatOverride");
    char *lon_ov   = params_get("LonOverride");
    char *atypes   = params_get("AlertTypes");
    char *interval = params_get("PollInterval");
    char *portstart = params_get("VirtualPortStart");
    char *provider = params_get("WeatherProvider");
    char *ua       = params_get("NWSUserAgent");
    char *overlay  = params_get("OverlayEnabled");
    char *vuser    = params_get("VapixUser");
    char *mock     = params_get("MockMode");

    printf("{\n"
           "  \"config\": {\n"
           "    \"zip\": \"%s\",\n"
           "    \"lat_override\": \"%s\",\n"
           "    \"lon_override\": \"%s\",\n"
           "    \"alert_types\": \"%s\",\n"
           "    \"poll_interval\": \"%s\",\n"
           "    \"virtual_port_start\": \"%s\",\n"
           "    \"weather_provider\": \"%s\",\n"
           "    \"nws_user_agent\": \"%s\",\n"
           "    \"overlay_enabled\": \"%s\",\n"
           "    \"vapix_user\": \"%s\",\n"
           "    \"mock_mode\": \"%s\"\n"
           "  },\n"
           "  \"status\": %s\n"
           "}\n",
           zip ? zip : "",
           lat_ov ? lat_ov : "",
           lon_ov ? lon_ov : "",
           atypes ? atypes : "",
           interval ? interval : "300",
           portstart ? portstart : "20",
           provider ? provider : "auto",
           ua ? ua : "",
           overlay ? overlay : "yes",
           vuser ? vuser : "root",
           mock ? mock : "no",
           status);

    free(zip); free(lat_ov); free(lon_ov); free(atypes);
    free(interval); free(portstart); free(provider); free(ua);
    free(overlay); free(vuser); free(mock); free(status);
}

/* ── POST handler ────────────────────────────────────────────────────────── */

static const char *SAVE_FIELDS[] = {
    "ZipCode", "LatOverride", "LonOverride", "AlertTypes",
    "PollInterval", "VirtualPortStart", "WeatherProvider",
    "NWSUserAgent", "OverlayEnabled", "VapixUser", "VapixPass", "MockMode",
    NULL
};

/* Mapping from HTML form field names to axparameter names */
static const char *FORM_TO_PARAM[][2] = {
    { "zip",               "ZipCode"          },
    { "lat_override",      "LatOverride"       },
    { "lon_override",      "LonOverride"       },
    { "alert_types",       "AlertTypes"        },
    { "poll_interval",     "PollInterval"      },
    { "virtual_port_start","VirtualPortStart"  },
    { "weather_provider",  "WeatherProvider"   },
    { "nws_user_agent",    "NWSUserAgent"      },
    { "overlay_enabled",   "OverlayEnabled"    },
    { "vapix_user",        "VapixUser"         },
    { "vapix_pass",        "VapixPass"         },
    { "mock_mode",         "MockMode"          },
    { NULL, NULL }
};

static void handle_post(void) {
    /* Read POST body */
    const char *cl_str = getenv("CONTENT_LENGTH");
    int cl = cl_str ? atoi(cl_str) : 0;
    if (cl <= 0 || cl > 8192) {
        json_header();
        printf("{\"ok\":false,\"error\":\"bad content length\"}\n");
        return;
    }

    char *body = (char *)malloc(cl + 1);
    if (!body) { json_header(); printf("{\"ok\":false,\"error\":\"OOM\"}\n"); return; }
    int r = fread(body, 1, cl, stdin);
    body[r] = '\0';

    KV fields[32];
    int nf = parse_form(body, fields, 32);
    free(body);

    int saved = 0, errors = 0;
    for (int i = 0; i < nf; i++) {
        /* Map form name → param name */
        const char *param_name = NULL;
        for (int m = 0; FORM_TO_PARAM[m][0]; m++) {
            if (strcmp(fields[i].key, FORM_TO_PARAM[m][0]) == 0) {
                param_name = FORM_TO_PARAM[m][1];
                break;
            }
        }
        if (!param_name) { free(fields[i].value); continue; }

        GError *err = NULL;
        if (params_set(param_name, fields[i].value, &err))
            saved++;
        else {
            errors++;
            if (err) g_error_free(err);
        }
        free(fields[i].value);
    }

    json_header();
    printf("{\"ok\":%s,\"saved\":%d,\"errors\":%d}\n",
           errors == 0 ? "true" : "false", saved, errors);
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(void) {
    GError *err = NULL;
    if (!params_init(&err)) {
        html_header();
        printf("<h1>Error: axparameter not available</h1>\n");
        if (err) g_error_free(err);
        return 1;
    }

    const char *method = getenv("REQUEST_METHOD");
    if (method && strcmp(method, "POST") == 0)
        handle_post();
    else
        handle_get();

    params_cleanup();
    return 0;
}
