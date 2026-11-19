/* Sleep on Espressif (ESP32-C6): the RTC timer wakes the chip and
 * sys_poweroff() enters deep sleep, as in apps/static-sleep and
 * zephyr/samples/boards/espressif/deep_sleep. */
#include <zephyr/sys/poweroff.h>

#include <esp_sleep.h>

#include "sleep.h"

bool sleep_woke_from_timer(void)
{
	return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
}

FUNC_NORETURN void sleep_for(uint32_t seconds)
{
	/* 64-bit microseconds: a 32-bit product overflows after ~71 minutes. */
	(void)esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
	sys_poweroff();
}
