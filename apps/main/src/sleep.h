/* Sleep entry and wake cause: the only SoC-specific code of apps/main.
 * Sleep is the SoC's deep sleep (HP domain off), entered with sys_poweroff(),
 * not the light sleep CONFIG_PM picks when idle. The Espressif implementation
 * is sleep_esp32.c. */
#ifndef MAIN_SLEEP_H
#define MAIN_SLEEP_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/* true when this boot is the end of a Sleep (RTC timer wake), false on a cold
 * boot (power-on, reset, reflash). Logged only: both count as a "timer" wake
 * until retained state exists. */
bool sleep_woke_from_timer(void);

/* Arm the RTC timer to wake after `seconds` and enter Sleep. Never returns:
 * the wake is a fresh boot. */
FUNC_NORETURN void sleep_for(uint32_t seconds);

#ifdef __cplusplus
}
#endif
#endif
