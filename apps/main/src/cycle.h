/* One Active phase of apps/main (FR-MA-07 steps 3-11 and 16-17).
 *
 * Zephyr-free so the host tests can run it (tests/host/test_main_cycle.cpp):
 * it reaches the hardware only through <plant/sensors.h> and <plant/net.h>,
 * and the clock only through cycle_uptime_ms(), which the app provides.
 * Waking up and going back to Sleep are the caller's job (sleep.h). */
#ifndef MAIN_CYCLE_H
#define MAIN_CYCLE_H

#include <stdint.h>

#include "retained.h"

namespace plant {

struct CycleConfig {
	const char *ssid;
	const char *psk;
	const char *broker;
	const char *device_name; /* MQTT topic segment and client id, < 64 bytes */
	int32_t wifi_timeout_ms;
	int32_t sntp_timeout_ms;
	int32_t mqtt_timeout_ms;
	int32_t flush_timeout_ms; /* delivery confirmation (ADR-0010) */
};

struct CycleResult {
	/* 0 when the broker confirmed both payloads, otherwise the error of the
	 * step that failed. Publishing stops at the first failure (DD-MA-02). */
	int error;
	/* The Phase durations as published in `status` (FR-MA-16). Measured up
	 * to the moment `status` is encoded: the MQTT and WiFi teardown that
	 * follows is in no t_* term. t_ble_ms is always 0 (BLE is cut). */
	uint32_t t_sensor_ms;
	uint32_t t_wifi_ms;
	uint32_t t_active_ms;
};

/* Milliseconds since boot. Provided by the app (k_uptime_get()). */
int64_t cycle_uptime_ms();

/* Run one Active phase: read the sensors, connect WiFi, try SNTP once,
 * connect MQTT, publish sensors/raw, sample RSSI, publish status, confirm
 * that the broker received both (ADR-0010), then disconnect MQTT and WiFi.
 * An unconfirmed delivery fails the Cycle like a failed publish.
 * t_active_start_ms is the uptime recorded at wake entry (FR-MA-07 step 1).
 * Never retries: a failed WiFi or MQTT step skips the publishing, and the
 * function still returns with the radio torn down, ready for Sleep.
 * wake_reason, cycle_count and t_sleep_ms come from `wake`
 * (retained_on_wake()); both wake reasons run the same sequence.
 * Sets the phase markers `10` at t_wifi_start and `11` where t_wifi_ms stops
 * (FR-MA-18), on every path; the caller sets `01` before and `00` after. */
CycleResult run_cycle(const CycleConfig &cfg, const WakeState &wake, int64_t t_active_start_ms);

} // namespace plant

#endif
