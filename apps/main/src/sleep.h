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

/* true when this boot is a wake from Sleep, by either wake source; false on
 * any other boot (power-on, reset, reflash). */
bool sleep_woke(void);

/* What ended the Sleep: PLANT_WAKE_SOIL_MOISTURE_ALERT when the GPIO6 wake
 * fired, otherwise PLANT_WAKE_TIMER (also on a boot that is no wake). */
enum plant_wake_reason sleep_wake_reason(void);

/* The struct that survives Sleep (retained.h), in LP SRAM. Its content is
 * whatever the previous boot left, or garbage after power-on. */
struct main_retained *sleep_retained(void);

/* The raw RTC counter. Keeps counting through Sleep; restarts on power-on.
 * retained_ticks_to_ms() (retained.h) converts a difference of two values. */
uint64_t sleep_rtc_ticks(void);

/* Read the LM393 output now: true when the soil is past the threshold. A pin
 * that can't be read counts as past it, so the wake is not armed on an unknown
 * level (a wake loop if the level is asserted). Not a pure query: it releases
 * the pad hold the last Sleep left on GPIO6 and configures the pin as input. */
bool sleep_soil_past_threshold(void);

/* Arm the RTC timer to wake after `seconds`, and the GPIO6 wake if soil_wake
 * (soil_wake_next(), retained.h), release the phase markers to `00`
 * (phase_markers.h) and enter Sleep. Never returns: the wake is a fresh boot.
 * Call it after every retained-RAM write and log. */
FUNC_NORETURN void sleep_for(uint32_t seconds, bool soil_wake);

#ifdef __cplusplus
}
#endif
#endif
