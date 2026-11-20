/* State that survives Sleep (FR-MA-06 retained struct, FR-MA-17 cycle_count;
 * scope cut: ADR-0009). Each Cycle is a fresh boot, so this is the only
 * memory one Cycle leaves the next.
 *
 * The struct lives in LP SRAM, which stays powered in deep sleep; the
 * SoC-specific placement is in sleep_esp32.c. This header and retained.cpp
 * are Zephyr-free so the host tests can run them
 * (tests/host/test_main_retained.cpp). */
#ifndef MAIN_RETAINED_H
#define MAIN_RETAINED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Not initialised by the boot code: after power-on it holds whatever LP SRAM
 * powers up with. Only retained_on_wake() and retained_on_sleep() write it. */
struct main_retained {
	uint32_t magic;              /* RETAINED_MAGIC: a complete Sleep record */
	uint32_t cycle_count;        /* the Cycle that wrote the record */
	int64_t sleep_start_rtc_ms;  /* RTC time at Sleep entry (for GPIO wakes) */
	uint32_t scheduled_sleep_ms; /* the Sleep length that was armed */
};

#ifdef __cplusplus
}

namespace plant {

constexpr uint32_t RETAINED_MAGIC = 0xDEADB33Fu;

/* What this Cycle publishes from the retained state. */
struct WakeState {
	uint32_t cycle_count;
	bool t_sleep_valid; /* false: t_sleep_ms is published as null */
	uint32_t t_sleep_ms;
};

/* FR-MA-07 step 2a. Read the record the previous Cycle left, if any, and
 * claim this Cycle's number. The record is valid only when this boot is the
 * end of a Sleep (deep_sleep_wake) *and* the magic is intact: a reset or a
 * reflash can leave LP SRAM, magic included, untouched. On a valid timer wake
 * cycle_count is the previous one plus 1 and t_sleep_ms is the Sleep length
 * that was armed. Otherwise (first boot, reset, reflash, power loss) the
 * count restarts at 0, as baseline A's first slot, and t_sleep_ms is null. */
WakeState retained_on_wake(main_retained &rs, bool deep_sleep_wake);

/* FR-MA-07 step 19, just before Sleep: record the RTC time and the Sleep
 * length about to be armed, and mark the record valid. */
void retained_on_sleep(main_retained &rs, int64_t rtc_now_ms, uint32_t sleep_ms);

} // namespace plant

#endif
#endif
