/* Publication grid of comparison baseline A (FR-AW-03, FR-AW-04).
 * Zephyr-free so the host tests can run it (tests/host/test_active_wifi.c). */
#ifndef ACTIVE_WIFI_SLOT_H
#define ACTIVE_WIFI_SLOT_H

#include <stdint.h>

/* Slot k is due at t0_ms + k * interval_ms, and its cycle_count is k.
 * Returns the first slot >= min_slot that is not late at now_ms (due >= now).
 * A slot that fell due during an overrun or an outage is skipped but still
 * counted, so the firmware never publishes late and the gap in cycle_count
 * equals the outage in intervals. now_ms must not be before t0_ms. */
static inline uint32_t slot_next(int64_t t0_ms, int64_t interval_ms, uint32_t min_slot,
				 int64_t now_ms)
{
	uint32_t first_on_time = (uint32_t)((now_ms - t0_ms + interval_ms - 1) / interval_ms);

	return (first_on_time > min_slot) ? first_on_time : min_slot;
}

#endif
