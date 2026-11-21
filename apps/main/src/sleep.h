/* Sleep entry, wake cause and retained memory: the only SoC-specific code of
 * apps/main. Sleep is the SoC's deep sleep (HP domain off), entered with
 * sys_poweroff(), not the light sleep CONFIG_PM picks when idle. The
 * Espressif implementation is sleep_esp32.c. */
#ifndef MAIN_SLEEP_H
#define MAIN_SLEEP_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/toolchain.h>

#include "retained.h"

#ifdef __cplusplus
extern "C" {
#endif

/* true when this boot is a wake from Sleep, false on any other boot
 * (power-on, reset, reflash). Only the RTC timer ends a Sleep so far. */
bool sleep_woke(void);

/* The struct that survives Sleep (retained.h), in LP SRAM. Its content is
 * whatever the previous boot left, or garbage after power-on. */
struct main_retained *sleep_retained(void);

/* RTC time in ms. Keeps counting through Sleep; restarts on power-on. */
int64_t sleep_rtc_ms(void);

/* Arm the RTC timer to wake after `seconds`, release the phase markers to
 * `00` (phase_markers.h) and enter Sleep. Never returns: the wake is a fresh
 * boot. Call it after every retained-RAM write and log. */
FUNC_NORETURN void sleep_for(uint32_t seconds);

#ifdef __cplusplus
}
#endif
#endif
