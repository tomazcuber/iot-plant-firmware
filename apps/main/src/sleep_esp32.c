/* Sleep on Espressif (ESP32-C6): the RTC timer and the LM393 output on GPIO6
 * wake the chip and sys_poweroff() enters deep sleep, as in apps/static-sleep
 * and zephyr/samples/boards/espressif/deep_sleep.
 *
 * The GPIO6 wake uses EXT1, not the HAL's "GPIO" deep-sleep wake: that one
 * forces the RTC_PERIPH power domain on during Sleep (sleep_modes.c), which
 * apps/static-sleep's I_sleep doesn't include. */
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/poweroff.h>

#include <esp_private/esp_clk.h>
#include <esp_private/sleep_gpio.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <hal/lp_aon_ll.h>
#include <soc/rtc.h>
#include <soc/soc_caps.h>

#include "phase_markers.h"
#include "sleep.h"

#define USER_NODE DT_PATH(zephyr_user)

BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(USER_NODE, soil_alert_gpios), DT_NODELABEL(gpio0)),
	     "the soil-alert wake pin must be on gpio0");
BUILD_ASSERT(((1ULL << DT_GPIO_PIN(USER_NODE, soil_alert_gpios)) &
	      SOC_GPIO_DEEP_SLEEP_WAKE_VALID_GPIO_MASK) != 0,
	     "the soil-alert wake pin must be an LP GPIO (GPIO0-7)");

static const struct gpio_dt_spec soil_alert = GPIO_DT_SPEC_GET(USER_NODE, soil_alert_gpios);

/* LP SRAM (RTC fast memory), which stays powered in deep sleep (the HAL keeps
 * it on when its power-down option is AUTO). `.rtc_noinit` is NOLOAD in the
 * SoC linker script, so the boot code neither loads nor zeroes it. The
 * section is named directly: the HAL's RTC_NOINIT_ATTR expands to nothing,
 * silently, when the SoC capability macros aren't visible. */
static struct main_retained retained __attribute__((section(".rtc_noinit")));

bool sleep_woke(void)
{
	/* The reset reason, not the wakeup cause: both wake sources end a
	 * Sleep, and a reset or reflash is no wake whatever the cause. */
	return esp_reset_reason() == ESP_RST_DEEPSLEEP;
}

enum plant_wake_reason sleep_wake_reason(void)
{
	/* GPIO6 is the only EXT1 source. If the timer fired too, the HAL
	 * reports the timer; the wake stays armed and fires on the next Sleep
	 * if DO is still asserted. */
	const esp_sleep_wakeup_cause_t hal_cause = esp_sleep_get_wakeup_cause();
	enum main_wake_cause cause = MAIN_WAKE_CAUSE_NONE;

	if (hal_cause == ESP_SLEEP_WAKEUP_TIMER) {
		cause = MAIN_WAKE_CAUSE_TIMER;
	} else if (hal_cause == ESP_SLEEP_WAKEUP_EXT1) {
		cause = MAIN_WAKE_CAUSE_EXT1;
	}

	/* The cause register can stay empty after a GPIO6 wake (ADR-0012). The
	 * latch alone is stale after a timer-only Sleep, so the pin must also
	 * still be selected; the pad-hold release in
	 * sleep_soil_past_threshold() clears that selection. LP IO numbers
	 * equal GPIO numbers on this SoC. */
	const uint32_t latched = lp_aon_ll_ext1_get_wakeup_pins() &
				 lp_aon_ll_ext1_get_wakeup_status();

	return main_wake_reason(cause, (latched & BIT(soil_alert.pin)) != 0);
}

struct main_retained *sleep_retained(void)
{
	return &retained;
}

uint64_t sleep_rtc_ticks(void)
{
	/* Not esp_rtc_get_time_us(): its accumulator is linked into HP SRAM
	 * here (CONFIG_RESERVE_RTC_MEM=0), so each boot rescales every tick
	 * since power-on with that boot's calibration, and values from two
	 * boots differ by a drift that grows with uptime. */
	return rtc_time_get();
}

uint64_t retained_ticks_to_ms(uint64_t ticks)
{
	return rtc_time_slowclk_to_us(ticks, esp_clk_slowclk_cal_get()) / 1000U;
}

bool sleep_soil_past_threshold(void)
{
	int level;

	/* EXT1 entry latched GPIO6 with a pad hold that survives the wake.
	 * ESP-IDF releases it in cpu_start.c, which Zephyr doesn't build;
	 * while held, the pad stays routed to the LP domain and the read below
	 * can't see DO change. */
	esp_deep_sleep_wakeup_io_reset();

	if (!gpio_is_ready_dt(&soil_alert) ||
	    gpio_pin_configure_dt(&soil_alert, GPIO_INPUT) != 0) {
		return true;
	}
	level = gpio_pin_get_dt(&soil_alert);
	return level != 0; /* 1 = active = past the threshold; < 0 = unknown */
}

FUNC_NORETURN void sleep_for(uint32_t seconds, bool soil_wake)
{
	/* 64-bit microseconds: a 32-bit product overflows after ~71 minutes. */
	(void)esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
	if (soil_wake) {
		/* The wake level is the devicetree's active level. */
		(void)esp_sleep_enable_ext1_wakeup_io(1ULL << soil_alert.pin,
						      (soil_alert.dt_flags & GPIO_ACTIVE_LOW)
							      ? ESP_EXT1_WAKEUP_ANY_LOW
							      : ESP_EXT1_WAKEUP_ANY_HIGH);
	}
	/* Releasing the markers to `00` ends the Teardown (FR-MA-18, ADR-0009):
	 * it is the last step before power-off. */
	phase_markers_set(PHASE_MARKER_NONE);
	sys_poweroff();
}
