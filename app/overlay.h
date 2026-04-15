#ifndef OVERLAY_H
#define OVERLAY_H

#include "weather_api.h"

/*
 * Update (or create) the VAPIX dynamic text overlay.
 * Format: "[ALERT: TYPE | ...] Temp: 72°F | Partly Cloudy | Wind: 5mph SW | Humidity: 65%"
 * Silently no-ops if the VAPIX overlay API is unavailable (speaker/intercom).
 */
void overlay_update(const WeatherSnapshot *snap,
                    const char *vapix_user,
                    const char *vapix_pass);

/* Remove the overlay (call on shutdown). */
void overlay_delete(const char *vapix_user, const char *vapix_pass);

#endif /* OVERLAY_H */
