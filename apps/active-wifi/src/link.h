/* Connection recovery of comparison baseline A (FR-AW-01.2-3, FR-AW-04).
 *
 * Zephyr-free so the host tests can run it (tests/host/test_active_wifi.c):
 * it reaches the network only through <plant/net.h>, and the clock only
 * through link_uptime_ms(), which the app provides. */
#ifndef ACTIVE_WIFI_LINK_H
#define ACTIVE_WIFI_LINK_H

#include <stdbool.h>
#include <stdint.h>

struct link_config {
	const char *ssid;
	const char *psk;
	const char *broker;
	const char *client_id;
	int32_t wifi_timeout_ms;
	int32_t mqtt_timeout_ms;
	int32_t sntp_timeout_ms;
};

struct link {
	const struct link_config *cfg;
	bool associated_once;
	bool sntp_attempted;
	bool have_epoch;
	int64_t boot_epoch_ms;
};

/* Milliseconds since boot. Provided by the app (k_uptime_get()). */
int64_t link_uptime_ms(void);

void link_init(struct link *l, const struct link_config *cfg);

/* One attempt to bring the MQTT connection up, used at boot and after every
 * drop. Connects WiFi first until it has associated once; after that,
 * reassociates first only when plant_wifi_rssi() reports -ENOTCONN, and
 * otherwise reconnects MQTT alone. The single SNTP attempt per boot happens
 * right after the first successful association. Returns 0 when MQTT is
 * connected, or the error of the step that failed. The caller retries at
 * once: no backoff, no sleep. */
int link_try_up(struct link *l);

/* Wall-clock time (ms since the Unix epoch) at the given uptime, from the
 * boot's SNTP sync. false while no sync has succeeded (timestamp_ms null). */
bool link_wall_clock_ms(const struct link *l, int64_t uptime_ms, int64_t *wall_ms);

#endif
