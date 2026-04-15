#include "overlay.h"
#include "weather_api.h"
#include "cJSON.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

static char g_overlay_id[64] = { 0 }; /* VAPIX overlay ID, empty = not created */
static int  g_has_video      = -1;    /* -1=unknown, 0=no, 1=yes */

typedef struct { char *data; size_t size; } Buf;

static size_t write_cb(void *ptr, size_t sz, size_t nmemb, void *ud) {
    Buf *b   = (Buf *)ud;
    size_t n = sz * nmemb;
    char  *p = realloc(b->data, b->size + n + 1);
    if (!p) return 0;
    b->data = p;
    memcpy(b->data + b->size, ptr, n);
    b->size += n;
    b->data[b->size] = '\0';
    return n;
}

static CURL *make_curl(const char *user, const char *pass, Buf *buf) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    char userpwd[256];
    snprintf(userpwd, sizeof(userpwd), "%s:%s", user ? user : "", pass ? pass : "");
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH,      CURLAUTH_DIGEST);
    curl_easy_setopt(curl, CURLOPT_USERPWD,       userpwd);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       10L);
    if (buf) {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA,     buf);
    } else {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    }
    return curl;
}

/* Probe for video capability (cached after first call). */
static int has_video(const char *user, const char *pass) {
    if (g_has_video >= 0) return g_has_video;

    CURL *curl = make_curl(user, pass, NULL);
    if (!curl) { g_has_video = 0; return 0; }

    curl_easy_setopt(curl, CURLOPT_URL,
        "http://localhost/axis-cgi/param.cgi?action=list&group=Properties.Image");
    curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);

    Buf buf = { NULL, 0 };
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

    curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);

    g_has_video = (code == 200 && buf.data && strstr(buf.data, "Properties.Image")) ? 1 : 0;
    free(buf.data);
    syslog(LOG_INFO, "weather_acap: video capability: %s", g_has_video ? "yes" : "no");
    return g_has_video;
}

/* Build overlay text ≤ 128 chars. */
static void build_overlay_text(const WeatherSnapshot *snap, char *out, size_t outlen) {
    char alerts_part[128] = { 0 };
    if (snap->alerts.count > 0) {
        char tmp[128] = "[ALERT: ";
        for (int i = 0; i < snap->alerts.count && i < 3; i++) {
            if (i > 0) strncat(tmp, " | ", sizeof(tmp) - strlen(tmp) - 1);
            strncat(tmp, snap->alerts.alerts[i].event, sizeof(tmp) - strlen(tmp) - 1);
        }
        strncat(tmp, "] ", sizeof(tmp) - strlen(tmp) - 1);
        snprintf(alerts_part, sizeof(alerts_part), "%s", tmp);
    }

    const char *dir = weather_wind_dir_str(snap->conditions.wind_dir_deg);
    snprintf(out, outlen,
             "%sTemp: %.0f\xC2\xB0""F | %s | Wind: %.0fmph %s | Humidity: %d%%",
             alerts_part,
             snap->conditions.temp_f,
             snap->conditions.description,
             snap->conditions.wind_speed_mph,
             dir,
             snap->conditions.humidity_pct);
}

/* POST or PUT the overlay via VAPIX REST API. */
void overlay_update(const WeatherSnapshot *snap,
                    const char *vapix_user,
                    const char *vapix_pass) {
    if (!snap->conditions.valid) return;
    if (!has_video(vapix_user, vapix_pass)) return;

    char text[200];
    build_overlay_text(snap, text, sizeof(text));

    /* Build JSON body */
    char body[512];
    snprintf(body, sizeof(body),
             "{\"text\":\"%s\",\"position\":\"topLeft\",\"visible\":true}", text);

    CURL *curl = curl_easy_init();
    if (!curl) return;

    char userpwd[256];
    snprintf(userpwd, sizeof(userpwd), "%s:%s",
             vapix_user ? vapix_user : "", vapix_pass ? vapix_pass : "");

    Buf buf = { NULL, 0 };
    struct curl_slist *hdrs = curl_slist_append(NULL, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_HTTPAUTH,      CURLAUTH_DIGEST);
    curl_easy_setopt(curl, CURLOPT_USERPWD,       userpwd);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,    hdrs);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       10L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &buf);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    body);

    if (g_overlay_id[0]) {
        /* PUT to update existing overlay */
        char url[256];
        snprintf(url, sizeof(url), "http://localhost/vapix/overlays/text/%s", g_overlay_id);
        curl_easy_setopt(curl, CURLOPT_URL,        url);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    } else {
        /* POST to create overlay */
        curl_easy_setopt(curl, CURLOPT_URL, "http://localhost/vapix/overlays/text");
    }

    curl_easy_perform(curl);

    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    /* On first creation (201), parse the ID from the response */
    if ((code == 200 || code == 201) && buf.data && !g_overlay_id[0]) {
        cJSON *root = cJSON_Parse(buf.data);
        cJSON *id   = root ? cJSON_GetObjectItem(root, "id") : NULL;
        if (cJSON_IsString(id))
            snprintf(g_overlay_id, sizeof(g_overlay_id), "%s", id->valuestring);
        else if (cJSON_IsNumber(id))
            snprintf(g_overlay_id, sizeof(g_overlay_id), "%.0f", id->valuedouble);
        cJSON_Delete(root);
    }

    if (code != 200 && code != 201 && code != 204)
        syslog(LOG_WARNING, "weather_acap: overlay update HTTP %ld", code);

    free(buf.data);
}

void overlay_delete(const char *vapix_user, const char *vapix_pass) {
    if (!g_overlay_id[0]) return;

    char url[256];
    snprintf(url, sizeof(url), "http://localhost/vapix/overlays/text/%s", g_overlay_id);

    CURL *curl = curl_easy_init();
    if (!curl) return;

    char userpwd[256];
    snprintf(userpwd, sizeof(userpwd), "%s:%s",
             vapix_user ? vapix_user : "", vapix_pass ? vapix_pass : "");

    curl_easy_setopt(curl, CURLOPT_URL,           url);
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH,      CURLAUTH_DIGEST);
    curl_easy_setopt(curl, CURLOPT_USERPWD,       userpwd);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       5L);
    curl_easy_setopt(curl, CURLOPT_NOBODY,        1L);

    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    g_overlay_id[0] = '\0';
}
