/* Sleep on Espressif (ESP32-C6): the RTC timer wakes the chip and
 * sys_poweroff() enters deep sleep, as in apps/static-sleep and
 * zephyr/samples/boards/espressif/deep_sleep. */
#include <zephyr/sys/poweroff.h>

#include <esp_private/esp_clk.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include "sleep.h"

/* LP SRAM (RTC fast memory), which stays powered in deep sleep (the HAL keeps
 * it on when its power-down option is AUTO). `.rtc_noinit` is NOLOAD in the
 * SoC linker script, so the boot code neither loads nor zeroes it. The
 * section is named directly: the HAL's RTC_NOINIT_ATTR expands to nothing,
 * silently, when the SoC capability macros aren't visible. */
static struct main_retained retained __attribute__((section(".rtc_noinit")));

bool sleep_ended(void)
{
	/* The reset reason, not the wakeup cause: a GPIO wake will end a
	 * Sleep too. */
	return esp_reset_reason() == ESP_RST_DEEPSLEEP;
}

struct main_retained *sleep_retained(void)
{
	return &retained;
}

int64_t sleep_rtc_ms(void)
{
	return (int64_t)(esp_clk_rtc_time() / 1000U);
}

FUNC_NORETURN void sleep_for(uint32_t seconds)
{
	/* 64-bit microseconds: a 32-bit product overflows after ~71 minutes. */
	(void)esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
	sys_poweroff();
}
