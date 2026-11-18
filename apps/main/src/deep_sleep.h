/* Sleep entry and wake cause: the only SoC-specific code of apps/main.
 * The Espressif implementation is deep_sleep_esp32.c. */
#ifndef MAIN_DEEP_SLEEP_H
#define MAIN_DEEP_SLEEP_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/* true when this boot is a wake from a deep-sleep timer, false on a cold boot
 * (power-on, reset, reflash). Logged only: both count as a "timer" wake until
 * retained state exists. */
bool deep_sleep_woke_from_timer(void);

/* Arm the RTC timer to wake after `seconds` and enter deep sleep. Never
 * returns: the wake is a fresh boot. */
FUNC_NORETURN void deep_sleep_for(uint32_t seconds);

#ifdef __cplusplus
}
#endif
#endif
