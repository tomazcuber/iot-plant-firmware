#include <errno.h>

#include <plant/net.h>

#include "link.h"

void link_init(struct link *l, const struct link_config *cfg)
{
	*l = (struct link){.cfg = cfg};
}

static void sntp_once(struct link *l)
{
	int64_t epoch_ms;

	if (l->sntp_attempted) {
		return;
	}
	l->sntp_attempted = true;
	if (plant_sntp_sync(l->cfg->sntp_timeout_ms, &epoch_ms) == 0) {
		l->boot_epoch_ms = epoch_ms - link_uptime_ms();
		l->have_epoch = true;
	}
}

bool link_wall_clock_ms(const struct link *l, int64_t uptime_ms, int64_t *wall_ms)
{
	if (!l->have_epoch) {
		return false;
	}
	*wall_ms = l->boot_epoch_ms + uptime_ms;
	return true;
}

int link_try_up(struct link *l)
{
	const struct link_config *cfg = l->cfg;
	int16_t rssi;
	int ret;

	if (!l->associated_once || plant_wifi_rssi(&rssi) == -ENOTCONN) {
		ret = plant_wifi_connect(cfg->ssid, cfg->psk, cfg->wifi_timeout_ms);
		if (ret < 0) {
			return ret;
		}
		l->associated_once = true;
		sntp_once(l);
	}
	/* A failed publish can leave plant_net's client marked connected, and
	 * plant_mqtt_connect() would then answer -EALREADY on every retry.
	 * Releasing it first is harmless when it is already closed. */
	(void)plant_mqtt_disconnect();
	return plant_mqtt_connect(cfg->broker, cfg->client_id, cfg->mqtt_timeout_ms);
}
