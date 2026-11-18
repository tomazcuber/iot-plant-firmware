/* apps/main, reduced for the thesis comparison (issue #22): a timer-only
 * Cycle. Each boot is one Cycle: run the Active phase, then Sleep for
 * CONFIG_APP_READ_INTERVAL_S. There is no application loop; the RTC timer
 * wake is a fresh boot. */
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
};

} // namespace

int main(void)
{
	/* FR-MA-07 step 1: the first timestamp of the Active phase. */
	const int64_t t_active_start_ms = k_uptime_get();

	LOG_WRN("main: device %s, read interval %d s, %s", CONFIG_APP_DEVICE_NAME,
		CONFIG_APP_READ_INTERVAL_S,
		sleep_woke_from_timer() ? "timer wake" : "cold boot");

	const plant::CycleResult r = plant::run_cycle(cycle_cfg, t_active_start_ms);

	if (r.error != 0) {
		LOG_WRN("cycle not published: %d", r.error);
	}
	/* The published t_active_ms stops before the teardown (cycle.h); the
	 * time to Sleep entry includes it, for comparison with the trace. */
	LOG_WRN("t_sensor %u ms, t_wifi %u ms, t_active %u ms, %u ms to sleep",
		r.t_sensor_ms, r.t_wifi_ms, r.t_active_ms,
		(uint32_t)(k_uptime_get() - t_active_start_ms));

	/* Deferred logging: print everything before the power goes. */
	LOG_PANIC();
	sleep_for(CONFIG_APP_READ_INTERVAL_S);
}
