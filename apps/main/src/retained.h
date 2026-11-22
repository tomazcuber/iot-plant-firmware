/* State that survives Sleep (FR-MA-06 retained struct, FR-MA-17 cycle_count;
 * scope cut: ADR-0009, #23). Each Cycle is a fresh boot, so this is the only
 * memory one Cycle leaves the next.
 *
 * The struct lives in LP SRAM, which stays powered in deep sleep; the
 * SoC-specific placement is in sleep_esp32.c. This header and retained.cpp
 * are Zephyr-free so the host tests can run them
 * (tests/host/test_main_retained.cpp). */
#ifndef MAIN_RETAINED_H
#define MAIN_RETAINED_H

#include <stdbool.h>
#include <stdint.h>

#include <plant/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Not initialised by the boot code: after power-on it holds whatever LP SRAM
 * powers up with. Only retained_on_wake() and retained_on_sleep() write it. */
struct main_retained {
	uint32_t magic;              /* RETAINED_MAGIC: a complete Sleep record */
	uint32_t cycle_count;        /* the Cycle that wrote the record */
	/* RTC counter at Sleep entry (for soil alerts). Raw ticks, not ms: each
	 * boot recalibrates the RTC clock, so ms from two boots don't subtract. */
	uint64_t sleep_start_rtc_ticks;
	uint32_t scheduled_sleep_ms; /* the Sleep length that was armed */
	uint32_t soil_wake_armed;    /* non-zero: that Sleep had the GPIO6 wake */
};

/* RTC ticks to ms with this boot's calibration. Provided by the SoC file
 * (sleep_esp32.c). */
uint64_t retained_ticks_to_ms(uint64_t ticks);

/* What the SoC's wake-cause register recorded for the Sleep that just ended. */
enum main_wake_cause {
	MAIN_WAKE_CAUSE_NONE, /* no source recorded */
	MAIN_WAKE_CAUSE_TIMER,
	MAIN_WAKE_CAUSE_EXT1,
};

/* The Wake reason of a Sleep (ADR-0012). The register can stay empty after a
 * GPIO6 wake, so soil_pin_latched (GPIO6 both selected and latched in the EXT1
 * block) decides that case:
 *
 *   recorded cause   soil_pin_latched   Wake reason
 *   EXT1             any                soil alert
 *   timer            any                timer
 *   none             yes                soil alert
 *   none             no                 timer */
enum plant_wake_reason main_wake_reason(enum main_wake_cause cause, bool soil_pin_latched);

#ifdef __cplusplus
}

namespace plant {

constexpr uint32_t RETAINED_MAGIC = 0xDEADB33Fu;

/* What this Cycle publishes from the retained state. */
struct WakeState {
	uint32_t cycle_count;
	bool t_sleep_valid; /* false: t_sleep_ms is published as null */
	uint32_t t_sleep_ms;
	plant_wake_reason wake_reason;
	bool soil_wake_armed; /* the Sleep just ended had the GPIO6 wake */
};

/* FR-MA-07 steps 2 and 2a. Read the record the previous Cycle left, if any,
 * and claim this Cycle's number. The record is valid only when this boot is a
 * wake from Sleep (woke_from_sleep) *and* the magic is intact: a reset or a
 * reflash can leave LP SRAM, magic included, untouched. With a valid record
 * cycle_count is the previous one plus 1, and t_sleep_ms is the armed length
 * on a timer wake or the RTC ticks since Sleep entry, in ms, on a soil alert
 * (null if that is not a duration). Otherwise (first boot, reset, reflash,
 * power loss) the count restarts at 0, as baseline A's first slot, and
 * t_sleep_ms is null. wake_reason is ignored unless woke_from_sleep; a boot
 * that is no wake reports "timer". */
WakeState retained_on_wake(main_retained &rs, bool woke_from_sleep, plant_wake_reason wake_reason,
			   uint64_t rtc_now_ticks);

/* The wake-loop rule (ADR-0009): should the next Sleep arm the GPIO6 wake? The
 * wake is level-triggered, so arming it while DO is past the threshold would
 * wake the device at once.
 *
 *   this wake                  DO past threshold   next Sleep
 *   soil alert                 any                 timer only
 *   timer, GPIO6 wake armed    any                 timer + GPIO6
 *   timer, timer only          yes                 timer only
 *   timer, timer only          no                  timer + GPIO6
 *
 * A boot with no usable record counts as "timer, timer only". */
bool soil_wake_next(const WakeState &wake, bool soil_past_threshold);

/* FR-MA-07 step 19, just before Sleep: record the RTC counter, the Sleep length
 * and whether the GPIO6 wake is about to be armed, and mark the record valid. */
void retained_on_sleep(main_retained &rs, uint64_t rtc_now_ticks, uint32_t sleep_ms,
		       bool soil_wake_armed);

} // namespace plant

#endif
#endif
