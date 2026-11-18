/* One Active phase of apps/main (FR-MA-07 steps 3-11 and 16-17).
 *
 * Zephyr-free so the host tests can run it (tests/host/test_main_cycle.cpp):
 * it reaches the hardware only through <plant/sensors.h> and <plant/net.h>,
 * and the clock only through cycle_uptime_ms(), which the app provides.
 * Waking up and going back to Sleep are the caller's job (deep_sleep.h). */
#ifndef MAIN_CYCLE_H
#define MAIN_CYCLE_H

#include <stdint.h>

namespace plant {

struct CycleConfig {
	const char *ssid;
	const char *psk;
	const char *broker;
	const char *device_name; /* MQTT topic segment and client id, < 64 bytes */
	int32_t wifi_timeout_ms;
	int32_t sntp_timeout_ms;
	int32_t mqtt_timeout_ms;
};

struct CycleResult {
	/* 0 when both payloads were published, otherwise the error of the step
	 * that failed. Publishing stops at the first failure (DD-MA-02). */
	int error;
	/* The Phase durations as published in `status` (FR-MA-16). Measured up
	 * to the moment `status` is encoded: the MQTT and WiFi teardown that
	 * follows is in no t_* term (this slice has no retained state to carry
	 * it to the next Cycle). t_ble_ms is always 0 (BLE is cut). */
	uint32_t t_sensor_ms;
	uint32_t t_wifi_ms;
	uint32_t t_active_ms;
};

/* Milliseconds since boot. Provided by the app (k_uptime_get()). */
int64_t cycle_uptime_ms();

/* Run one Active phase: read the sensors, connect WiFi, try SNTP once,
 * connect MQTT, publish sensors/raw, sample RSSI, publish status, then
 * disconnect MQTT and WiFi. t_active_start_ms is the uptime recorded at wake
 * entry (FR-MA-07 step 1). Never retries: a failed WiFi or MQTT step skips
 * the publishing, and the function still returns with the radio torn down,
 * ready for Sleep. wake_reason is always "timer" in this slice, and
 * cycle_count and t_sleep_ms are null (no retained state yet). */
CycleResult run_cycle(const CycleConfig &cfg, int64_t t_active_start_ms);

} // namespace plant

#endif
