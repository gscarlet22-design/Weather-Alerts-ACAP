#include "params.h"

#include <axparameter.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_NAME "weather_acap"

static AXParameter *g_axparam = NULL;

/* Compiled-in defaults used when a parameter has no stored value. */
static const struct { const char *name; const char *value; } DEFAULTS[] = {
    { "ZipCode",          ""                                                           },
    { "LatOverride",      ""                                                           },
    { "LonOverride",      ""                                                           },
    { "AlertTypes",       "Tornado Warning,Severe Thunderstorm Warning,Flash Flood Warning" },
    { "PollInterval",     "300"                                                        },
    { "VirtualPortStart", "20"                                                         },
    { "WeatherProvider",  "auto"                                                       },
    { "NWSUserAgent",     "WeatherACAP/2.0 (admin@example.com)"                        },
    { "OverlayEnabled",   "yes"                                                        },
    { "VapixUser",        "root"                                                       },
    { "VapixPass",        ""                                                           },
    { "MockMode",         "no"                                                         },
    { NULL,               NULL                                                         }
};

static const char *compiled_default(const char *name) {
    for (int i = 0; DEFAULTS[i].name; i++)
        if (strcmp(DEFAULTS[i].name, name) == 0)
            return DEFAULTS[i].value;
    return "";
}

gboolean params_init(GError **error) {
    g_axparam = axparameter_new(APP_NAME, error);
    return g_axparam != NULL;
}

void params_cleanup(void) {
    if (g_axparam) {
        axparameter_free(g_axparam);
        g_axparam = NULL;
    }
}

char *params_get(const char *name) {
    if (!g_axparam) return strdup(compiled_default(name));

    GError *err = NULL;
    gchar  *val = NULL;
    if (!axparameter_get(g_axparam, name, &val, &err) || !val) {
        if (err) g_error_free(err);
        return strdup(compiled_default(name));
    }
    char *result = strdup(val);
    g_free(val);
    return result;
}

gboolean params_set(const char *name, const char *value, GError **error) {
    if (!g_axparam) {
        if (error)
            *error = g_error_new(G_FILE_ERROR, G_FILE_ERROR_FAILED, "axparameter not initialized");
        return FALSE;
    }
    return axparameter_set(g_axparam, name, value, error);
}

int params_get_int(const char *name, int default_val) {
    char *s = params_get(name);
    int   v = default_val;
    if (s && *s) v = atoi(s);
    free(s);
    return v;
}
