/* Comparison baseline A: the always-connected pattern from the literature
 * (spec 2026-04-05-active-wifi-firmware-design.md, ADR-0006).
 *
 * Connects once, then reads, publishes and waits on a fixed grid forever,
 * never sleeping (CONFIG_PM=n). Same sensor read, encoder and network stack
 * as apps/main; only the orchestration differs. One application thread.
 *
 * On a dropped connection it reconnects at once and skips the slots that fall
 * due meanwhile, still counting them (FR-AW-04): see link.h and slot.h. */
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <plant/json.h>
#include <plant/net.h>
#include <plant/sensors.h>
#include <plant/types.h>

#include "link.h"
#include "slot.h"

LOG_MODULE_REGISTER(active_wifi, LOG_LEVEL_WRN);

#define DEVICE_NAME "baseline-a"
#define TOPIC_RAW "plants/" DEVICE_NAME "/sensors/raw"
#define TOPIC_STATUS "plants/" DEVICE_NAME "/status"

#define INTERVAL_MS ((int64_t)CONFIG_APP_PUBLISH_INTERVAL_S * 1000)

static const struct link_config link_cfg = {
	.ssid = CONFIG_APP_WIFI_SSID,
	.psk = CONFIG_APP_WIFI_PSK,
	.broker = CONFIG_APP_MQTT_BROKER,
	.client_id = DEVICE_NAME,
	.wifi_timeout_ms = 30000,
	.mqtt_timeout_ms = 10000,
	.sntp_timeout_ms = 10000,
};

static struct link net_link;

int64_t link_uptime_ms(void)
{
	return k_uptime_get();
}

/* Retry at once until MQTT is up: no backoff, no sleep (FR-AW-04). A failing
 * attempt is logged only when its error changes, to keep the serial quiet
 * during an outage (NFR-AW-02). */
static void connect_until_up(void)
{
	int last = 0;
	int ret;

	while ((ret = link_try_up(&net_link)) < 0) {
		if (ret != last) {
			LOG_WRN("reconnect failed: %d (retrying)", ret);
			last = ret;
		}
	}
}

/* Encode and publish one payload. Only a failed publish is an error: a
 * failed encode loses that payload but says nothing about the connection. */
static int publish(const char *topic, const char *payload, int len)
{
	int ret;

	if (len < 0) {
		LOG_WRN("encode %s failed: %d", topic, len);
		return 0;
	}
	ret = plant_mqtt_publish(topic, payload, (size_t)len);
	if (ret < 0) {
		LOG_WRN("publish %s failed: %d", topic, ret);
	}
	return ret;
}

/* One grid slot (FR-AW-01.4, FR-AW-02). cycle_count is the slot index.
 * Returns the publish error if the connection failed mid-slot. */
static int publish_slot(uint32_t slot)
{
	struct plant_sensor_values vals = {0};
	struct plant_status status = {0};
	char payload[MAX(PLANT_JSON_RAW_MAX, PLANT_JSON_STATUS_MAX)];
	int64_t ts_ms;
	bool have_ts = link_wall_clock_ms(&net_link, k_uptime_get(), &ts_ms);
	int16_t rssi;
	int ret;

	(void)plant_sensors_read(&vals);
	ret = publish(TOPIC_RAW, payload,
		      plant_json_encode_raw(&vals, have_ts ? &ts_ms : NULL, payload,
					    sizeof(payload)));
	if (ret < 0) {
		return ret;
	}

	status.cycle_count = slot;
	status.present = PLANT_STATUS_CYCLE_COUNT;
	if (plant_wifi_rssi(&rssi) == 0) {
		status.wifi_rssi_dbm = rssi;
		status.present |= PLANT_STATUS_WIFI_RSSI_DBM;
	}
	if (have_ts) {
		status.timestamp_ms = ts_ms;
		status.present |= PLANT_STATUS_TIMESTAMP_MS;
	}
	/* wake_reason and every t_*_ms stay null: they don't apply to baseline A. */
	return publish(TOPIC_STATUS, payload,
		       plant_json_encode_status(&status, payload, sizeof(payload)));
}

/* Keep the connection serviced until `due` (FR-AW-01.5). Returns -ENOTCONN as
 * soon as plant_mqtt_poll reports a dropped connection. */
static int wait_until(int64_t due)
{
	int64_t now;

	while ((now = k_uptime_get()) < due) {
		if (plant_mqtt_poll((int32_t)MIN(due - now, INT32_MAX)) == -ENOTCONN) {
			return -ENOTCONN;
		}
	}
	return 0;
}

int main(void)
{
	uint32_t slot = 0;
	int64_t t0;
	int64_t now_wall_ms;
	int ret;

	LOG_WRN("baseline-a: publish interval %d s", CONFIG_APP_PUBLISH_INTERVAL_S);

	ret = plant_sensors_init();
	if (ret < 0) {
		LOG_WRN("sensors init: %d (failed sensors publish null)", ret);
	}
	ret = plant_net_init();
	if (ret < 0) {
		LOG_ERR("plant_net_init failed: %d", ret);
		return ret;
	}

	link_init(&net_link, &link_cfg);
	connect_until_up();
	if (!link_wall_clock_ms(&net_link, k_uptime_get(), &now_wall_ms)) {
		LOG_WRN("sntp failed: timestamp_ms is null until reboot");
	}

	t0 = k_uptime_get();
	for (;;) {
		uint32_t resume_from = slot;
		bool dropped = true;

		/* A drop while waiting keeps this slot, which may still be on time.
		 * Once publishing starts the slot is used up, even if it fails. */
		if (wait_until(t0 + (int64_t)slot * INTERVAL_MS) == 0) {
			dropped = (publish_slot(slot) < 0);
			resume_from = slot + 1;
		}
		if (dropped) {
			LOG_WRN("connection dropped at slot %u", slot);
			connect_until_up();
		}
		/* The first slot not yet due: never late, never a burst. */
		slot = slot_next(t0, INTERVAL_MS, resume_from, k_uptime_get());
	}
	return 0;
}
