/* apps/main, reduced for the thesis comparison (issue #22): a timer-only
 * Cycle. Each boot is one Cycle: run the Active phase, then Sleep for
 * CONFIG_APP_READ_INTERVAL_S. There is no application loop; the RTC timer
 * wake is a fresh boot, and the retained struct (retained.h) is all that one
 * Cycle hands the next (issue #24). */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>

#include "cycle.h"
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
	/* FR-MA-07 step 1: the first timestamp of the Active phase. */
	const int64_t t_active_start_ms = k_uptime_get();

	/* Step 2a: this Cycle's number and the Sleep that preceded it. */
	main_retained &retained = *sleep_retained();
	const plant::WakeState wake = plant::retained_on_wake(retained, sleep_ended());

	if (wake.t_sleep_valid) {
		LOG_WRN("main: device %s, read interval %d s, cycle %u after %u ms Sleep",
			CONFIG_APP_DEVICE_NAME, CONFIG_APP_READ_INTERVAL_S, wake.cycle_count,
			wake.t_sleep_ms);
	} else {
		LOG_WRN("main: device %s, read interval %d s, cycle %u, first boot",
			CONFIG_APP_DEVICE_NAME, CONFIG_APP_READ_INTERVAL_S, wake.cycle_count);
	}

	const plant::CycleResult r = plant::run_cycle(cycle_cfg, wake, t_active_start_ms);

	if (r.error != 0) {
		LOG_WRN("cycle not published: %d", r.error);
	}
	/* The published t_active_ms stops before the teardown (cycle.h); the
	 * time to Sleep entry includes it, for comparison with the trace. */
	LOG_WRN("t_sensor %u ms, t_wifi %u ms, t_active %u ms, %u ms to sleep",
		r.t_sensor_ms, r.t_wifi_ms, r.t_active_ms,
		(uint32_t)(k_uptime_get() - t_active_start_ms));

	/* Step 19: the record the next Cycle reads, with the Sleep length
	 * that is about to be armed. */
	const uint32_t sleep_s = CONFIG_APP_READ_INTERVAL_S;

	plant::retained_on_sleep(retained, sleep_rtc_ms(), sleep_s * 1000U);

	/* Deferred logging: print everything before the power goes. */
	LOG_PANIC();
	sleep_for(sleep_s);
}
