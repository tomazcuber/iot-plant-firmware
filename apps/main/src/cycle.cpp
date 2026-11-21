#include <stdio.h>

#include <plant/json.h>
#include <plant/net.h>
#include <plant/sensors.h>
#include <plant/types.h>

#include "cycle.h"
#include "phase_markers.h"

namespace plant {

namespace {

/* The WiFi phase's inputs from the sensor phase, and the result it fills. */
struct WifiPhase {
	const CycleConfig &cfg;
	const WakeState &wake;
	const plant_sensor_values &values;
	int64_t t_wifi_start_ms;
	CycleResult &result;
};

/* Close the WiFi phase at the current uptime (FR-MA-16, cut early: see
 * CycleResult). t_active_ms = t_sensor_ms + t_wifi_ms + t_ble_ms, t_ble_ms = 0.
 * The Teardown starts here, on every path (FR-MA-18): each one calls this
 * exactly once. */
void stop_clock(WifiPhase &p)
{
	p.result.t_wifi_ms = (uint32_t)(cycle_uptime_ms() - p.t_wifi_start_ms);
	phase_markers_set(PHASE_MARKER_TEARDOWN);
	p.result.t_active_ms = p.result.t_sensor_ms + p.result.t_wifi_ms;
}

/* len is the encoder's result; an encode error fails the publish. */
int publish(const char *device_name, const char *subtopic, const char *payload, int len)
{
	char topic[96];

	if (len < 0) {
		return len;
	}
	snprintf(topic, sizeof(topic), "plants/%s/%s", device_name, subtopic);
	return plant_mqtt_publish(topic, payload, (size_t)len);
}

/* Steps 6-10, with WiFi associated: one SNTP attempt, MQTT connect, the two
 * publishes and the delivery confirmation (ADR-0010). Returns 0, or the error
 * of the first step that failed. */
int publish_cycle(WifiPhase &p)
{
	const CycleConfig &cfg = p.cfg;
	plant_status status = {};
	char payload[PLANT_JSON_RAW_MAX > PLANT_JSON_STATUS_MAX ? PLANT_JSON_RAW_MAX
								: PLANT_JSON_STATUS_MAX];
	int64_t epoch_ms;
	int64_t boot_epoch_ms = 0;
	bool have_epoch = false;
	int64_t timestamp_ms = 0;
	int16_t rssi;
	int len;
	int ret;

	if (plant_sntp_sync(cfg.sntp_timeout_ms, &epoch_ms) == 0) {
		boot_epoch_ms = epoch_ms - cycle_uptime_ms();
		have_epoch = true;
	}
	/* A failed connect releases its own socket: nothing to disconnect. */
	ret = plant_mqtt_connect(cfg.broker, cfg.device_name, cfg.mqtt_timeout_ms);
	if (ret < 0) {
		stop_clock(p);
		return ret;
	}

	if (have_epoch) {
		timestamp_ms = boot_epoch_ms + cycle_uptime_ms();
	}
	len = plant_json_encode_raw(&p.values, have_epoch ? &timestamp_ms : nullptr, payload,
				    sizeof(payload));
	ret = publish(cfg.device_name, "sensors/raw", payload, len);
	if (ret < 0) {
		stop_clock(p);
		/* A failed publish can leave the client marked connected. */
		(void)plant_mqtt_disconnect();
		return ret;
	}

	status.wake_reason = p.wake.wake_reason;
	status.present = PLANT_STATUS_WAKE_REASON | PLANT_STATUS_T_ACTIVE_MS |
			 PLANT_STATUS_T_SENSOR_MS | PLANT_STATUS_T_WIFI_MS | PLANT_STATUS_T_BLE_MS |
			 PLANT_STATUS_CYCLE_COUNT;
	status.cycle_count = p.wake.cycle_count;
	if (p.wake.t_sleep_valid) {
		status.t_sleep_ms = p.wake.t_sleep_ms;
		status.present |= PLANT_STATUS_T_SLEEP_MS;
	}
	if (plant_wifi_rssi(&rssi) == 0) {
		status.wifi_rssi_dbm = rssi;
		status.present |= PLANT_STATUS_WIFI_RSSI_DBM;
	}
	if (have_epoch) {
		status.timestamp_ms = timestamp_ms;
		status.present |= PLANT_STATUS_TIMESTAMP_MS;
	}
	stop_clock(p);
	status.t_sensor_ms = p.result.t_sensor_ms;
	status.t_wifi_ms = p.result.t_wifi_ms;
	status.t_ble_ms = 0;
	status.t_active_ms = p.result.t_active_ms;
	len = plant_json_encode_status(&status, payload, sizeof(payload));
	ret = publish(cfg.device_name, "status", payload, len);
	if (ret == 0) {
		/* QoS 0 only hands the payloads to the TCP send buffer. Before
		 * the radio goes down, confirm the broker has both (ADR-0010). */
		ret = plant_mqtt_flush(cfg.flush_timeout_ms);
	}

	(void)plant_mqtt_disconnect();
	return ret;
}

} // namespace

CycleResult run_cycle(const CycleConfig &cfg, const WakeState &wake, int64_t t_active_start_ms)
{
	CycleResult result = {};
	plant_sensor_values values = {};
	int ret;

	/* Sensor phase (steps 3-4): a failed sensor is a cleared valid bit,
	 * published as null (FR-MA-08a); it never stops the Cycle. */
	(void)plant_sensors_init();
	(void)plant_sensors_read(&values);

	WifiPhase p = {cfg, wake, values, cycle_uptime_ms(), result};

	phase_markers_set(PHASE_MARKER_WIFI); /* at t_wifi_start (FR-MA-18) */
	result.t_sensor_ms = (uint32_t)(p.t_wifi_start_ms - t_active_start_ms);

	/* WiFi phase (steps 4a-11). */
	ret = plant_net_init();
	if (ret == 0) {
		ret = plant_wifi_connect(cfg.ssid, cfg.psk, cfg.wifi_timeout_ms);
	}
	if (ret == 0) {
		ret = publish_cycle(p);
	} else {
		stop_clock(p);
	}
	result.error = ret;
	(void)plant_wifi_disconnect();
	return result;
}

} // namespace plant
