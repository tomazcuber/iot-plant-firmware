/* apps/main, reduced for the thesis comparison (issue #22). Each boot is one
 * Cycle: run the Active phase, then Sleep for CONFIG_APP_READ_INTERVAL_S or
 * until the soil-moisture comparator wakes the device (issue #26). There is no
 * application loop; a wake is a fresh boot, and the retained struct
 * (retained.h) is all that one Cycle hands the next (issue #24). */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>

#include "cycle.h"
#include "phase_markers.h"
#include "sleep.h"

LOG_MODULE_REGISTER(main_app, LOG_LEVEL_WRN);

int64_t plant::cycle_uptime_ms()
{
	return k_uptime_get();
}

namespace {

const plant::CycleConfig cycle_cfg = {
	CONFIG_APP_WIFI_SSID,
	CONFIG_APP_WIFI_PSK,
	CONFIG_APP_MQTT_BROKER,
	CONFIG_APP_DEVICE_NAME,
	10000, /* WiFi connect timeout (DD-MA-03) */
	3000,  /* SNTP timeout (DD-MA-11) */
	10000, /* MQTT connect timeout: not specified, matches baseline A */
	5000,  /* delivery confirmation timeout (ADR-0010) */
};

} // namespace

int main(void)
{
	/* Configure the marker pins first, so the timestamp and `01` are
	 * adjacent (FR-MA-18). Instrumentation only: a failed init leaves the
	 * markers silent and the Cycle unchanged. */
	(void)phase_markers_init();

	/* FR-MA-07 step 1: the first timestamp of the Active phase. */
	const int64_t t_active_start_ms = k_uptime_get();

	phase_markers_set(PHASE_MARKER_SENSORS);

	/* Steps 2 and 2a: the wake reason, this Cycle's number and the Sleep
	 * that preceded it. */
	main_retained &retained = *sleep_retained();
	const plant::WakeState wake = plant::retained_on_wake(
		retained, sleep_woke(), sleep_wake_reason(), sleep_rtc_ticks());

	LOG_WRN("main: device %s, read interval %d s, cycle %u, %s", CONFIG_APP_DEVICE_NAME,
		CONFIG_APP_READ_INTERVAL_S, wake.cycle_count,
		wake.t_sleep_valid ? "woke from Sleep" : "first boot");

	const plant::CycleResult r = plant::run_cycle(cycle_cfg, wake, t_active_start_ms);

	if (r.error != 0) {
		LOG_WRN("cycle not published: %d", r.error);
	}
	/* The published t_active_ms stops before the teardown (cycle.h); the
	 * time to Sleep entry includes it, for comparison with the trace. */
	LOG_WRN("t_sensor %u ms, t_wifi %u ms, t_active %u ms, %u ms to sleep",
		r.t_sensor_ms, r.t_wifi_ms, r.t_active_ms,
		(uint32_t)(k_uptime_get() - t_active_start_ms));

	/* The wake-loop rule (ADR-0009): whether the next Sleep arms GPIO6. */
	const bool soil_past = sleep_soil_past_threshold();
	const bool soil_wake = plant::soil_wake_next(wake, soil_past);

	LOG_WRN("wake %s, soil %s threshold, next Sleep %s",
		wake.wake_reason == PLANT_WAKE_SOIL_MOISTURE_ALERT ? "soil_moisture_alert"
								   : "timer",
		soil_past ? "past" : "within", soil_wake ? "timer + GPIO6" : "timer only");

	/* Step 19: the record the next Cycle reads, with the Sleep length and
	 * wake sources that are about to be armed. */
	const uint32_t sleep_s = CONFIG_APP_READ_INTERVAL_S;

	plant::retained_on_sleep(retained, sleep_rtc_ticks(), sleep_s * 1000U, soil_wake);

	/* Deferred logging: print everything before the power goes. */
	LOG_PANIC();
	sleep_for(sleep_s, soil_wake);
}
