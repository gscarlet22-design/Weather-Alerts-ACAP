#ifndef ALERTS_H
#define ALERTS_H

#include "weather_api.h"

/*
 * Compare the active alert set against the configured alert types.
 * Fire VAPIX virtual input ports for newly-active alerts; clear ports for
 * alerts that have resolved since the last call.
 *
 * alert_types_csv : comma-separated list from params, e.g.
 *                   "Tornado Warning,Severe Thunderstorm Warning"
 * port_start      : first virtual input port number (default 20)
 * vapix_user/pass : credentials for localhost VAPIX Digest auth
 */
void alerts_process(const NWSAlertSet *alerts,
                    const char *alert_types_csv,
                    int port_start,
                    const char *vapix_user,
                    const char *vapix_pass);

/* Clear all managed virtual ports (call on shutdown). */
void alerts_clear_all(int port_start, int num_types,
                      const char *vapix_user, const char *vapix_pass);

#endif /* ALERTS_H */
