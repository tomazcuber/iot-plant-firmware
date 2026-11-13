/* Comparison baseline A: the always-connected pattern from the literature
 * (spec 2026-04-05-active-wifi-firmware-design.md, ADR-0006).
 *
 * Connects once, then reads, publishes and waits on a fixed grid forever,
 * never sleeping (CONFIG_PM=n). Same sensor read, encoder and network stack
 * as apps/main; only the orchestration differs. One application thread. */
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <plant/json.h>
#include <plant/net.h>
#include <plant/sensors.h>
#include <plant/types.h>

LOG_MODULE_REGISTER(active_wifi, LOG_LEVEL_WRN);

#define DEVICE_NAME "baseline-a"
#define TOPIC_RAW "plants/" DEVICE_NAME "/sensors/raw"
#define TOPIC_STATUS "plants/" DEVICE_NAME "/status"

#define WIFI_TIMEOUT_MS 30000
#define MQTT_TIMEOUT_MS 10000
#define SNTP_TIMEOUT_MS 10000
/* Guard against a busy spin when a connect call fails without waiting. */
#define RETRY_GUARD_MS 1000

#define INTERVAL_MS ((int64_t)CONFIG_APP_PUBLISH_INTERVAL_S * 1000)

/* Wall clock = boot epoch + k_uptime_get(); valid only if the boot sync worked. */
static bool have_epoch;
static int64_t boot_epoch_ms;
static bool sntp_attempted;

static void retry_guard(int64_t started_ms)
{
	int64_t spent = k_uptime_get() - started_ms;

	if (spent < RETRY_GUARD_MS) {
		k_msleep((int32_t)(RETRY_GUARD_MS - spent));
	}
}

static void wifi_connect_retrying(void)
{
	for (;;) {
		int64_t start = k_uptime_get();
		int ret = plant_wifi_connect(CONFIG_APP_WIFI_SSID, CONFIG_APP_WIFI_PSK,
					     WIFI_TIMEOUT_MS);

		if (ret == 0) {
			return;
		}
		LOG_WRN("wifi connect failed: %d", ret);
		retry_guard(start);
	}
}

/* One SNTP attempt per boot, right after the first association (FR-AW-01.3). */
static void sntp_once(void)
{
	int64_t epoch_ms;
	int ret;

	if (sntp_attempted) {
		return;
	}
	sntp_attempted = true;

	ret = plant_sntp_sync(SNTP_TIMEOUT_MS, &epoch_ms);
	if (ret == 0) {
		boot_epoch_ms = epoch_ms - k_uptime_get();
		have_epoch = true;
	} else {
		LOG_WRN("sntp failed: %d, timestamp_ms is null until reboot", ret);
	}
}

static void mqtt_connect_retrying(void)
{
	for (;;) {
		int64_t start = k_uptime_get();
		int ret = plant_mqtt_connect(CONFIG_APP_MQTT_BROKER, DEVICE_NAME,
					     MQTT_TIMEOUT_MS);

		if (ret == 0) {
			return;
		}
		LOG_WRN("mqtt connect failed: %d", ret);
		retry_guard(start);
	}
}

static void publish(const char *topic, const char *payload, int len)
{
	int ret;

	if (len < 0) {
		LOG_WRN("encode %s failed: %d", topic, len);
		return;
	}
	ret = plant_mqtt_publish(topic, payload, (size_t)len);
	if (ret < 0) {
		LOG_WRN("publish %s failed: %d", topic, ret);
	}
}

/* One grid slot (FR-AW-01.4, FR-AW-02). cycle_count is the slot index. */
static void publish_slot(uint32_t slot)
{
	struct plant_sensor_values vals = {0};
	struct plant_status status = {0};
	char payload[MAX(PLANT_JSON_RAW_MAX, PLANT_JSON_STATUS_MAX)];
	int64_t ts_ms = boot_epoch_ms + k_uptime_get();
	const int64_t *ts = have_epoch ? &ts_ms : NULL;
	int16_t rssi;

	(void)plant_sensors_read(&vals);
	publish(TOPIC_RAW, payload, plant_json_encode_raw(&vals, ts, payload, sizeof(payload)));

	status.cycle_count = slot;
	status.present = PLANT_STATUS_CYCLE_COUNT;
	if (plant_wifi_rssi(&rssi) == 0) {
		status.wifi_rssi_dbm = rssi;
		status.present |= PLANT_STATUS_WIFI_RSSI_DBM;
	}
	if (have_epoch) {
		status.timestamp_ms = ts_ms;
		status.present |= PLANT_STATUS_TIMESTAMP_MS;
	}
	/* wake_reason and every t_*_ms stay null: they don't apply to baseline A. */
	publish(TOPIC_STATUS, payload, plant_json_encode_status(&status, payload, sizeof(payload)));
}

/* First grid slot after `prev` whose due time has not passed yet. A slot whose
 * due time passed (overrun or outage) is skipped but still counted, so the
 * firmware never publishes late and the gap shows in cycle_count. */
static uint32_t next_slot(int64_t t0, uint32_t prev, int64_t now)
{
	uint32_t next = prev + 1;

	if (now > t0 + (int64_t)next * INTERVAL_MS) {
		next = (uint32_t)((now - t0) / INTERVAL_MS) + 1;
	}
	return next;
}

/* Keep the connection serviced until `due` (FR-AW-01.5). */
static void wait_until(int64_t due)
{
	int64_t now;

	while ((now = k_uptime_get()) < due) {
		int ret = plant_mqtt_poll((int32_t)MIN(due - now, INT32_MAX));

		if (ret == -ENOTCONN) {
			LOG_WRN("mqtt connection dropped");
			/* Reconnect is #20 (FR-AW-04); until then just keep the grid. */
			k_msleep((int32_t)CLAMP(due - k_uptime_get(), 0, INT32_MAX));
		}
	}
}

int main(void)
{
	uint32_t slot = 0;
	int64_t t0;
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

	wifi_connect_retrying();
	sntp_once();
	mqtt_connect_retrying();

	t0 = k_uptime_get();
	for (;;) {
		wait_until(t0 + (int64_t)slot * INTERVAL_MS);
		publish_slot(slot);
		slot = next_slot(t0, slot, k_uptime_get());
	}
	return 0;
}
