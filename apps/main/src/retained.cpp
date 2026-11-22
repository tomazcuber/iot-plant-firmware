#include "retained.h"

enum plant_wake_reason main_wake_reason(enum main_wake_cause cause, bool soil_pin_latched)
{
	if (cause == MAIN_WAKE_CAUSE_EXT1 || (cause == MAIN_WAKE_CAUSE_NONE && soil_pin_latched)) {
		return PLANT_WAKE_SOIL_MOISTURE_ALERT;
	}
	return PLANT_WAKE_TIMER;
}

namespace plant {

WakeState retained_on_wake(main_retained &rs, bool woke_from_sleep, plant_wake_reason wake_reason,
			   uint64_t rtc_now_ticks)
{
	WakeState w = {0, false, 0, woke_from_sleep ? wake_reason : PLANT_WAKE_TIMER, false};

	/* Only a wake from Sleep vouches for the magic: a reset or reflash
	 * can leave a complete record behind. */
	if (woke_from_sleep && rs.magic == RETAINED_MAGIC) {
		w.cycle_count = rs.cycle_count + 1;
		w.soil_wake_armed = rs.soil_wake_armed != 0;
		if (wake_reason == PLANT_WAKE_TIMER) {
			/* The armed length is the Sleep that was taken (FR-MA-06). */
			w.t_sleep_valid = true;
			w.t_sleep_ms = rs.scheduled_sleep_ms;
		} else if (rtc_now_ticks >= rs.sleep_start_rtc_ticks) {
			/* An alert cuts the Sleep short: measure it. */
			const uint64_t elapsed_ms =
				retained_ticks_to_ms(rtc_now_ticks - rs.sleep_start_rtc_ticks);

			if (elapsed_ms <= UINT32_MAX) {
				w.t_sleep_valid = true;
				w.t_sleep_ms = (uint32_t)elapsed_ms;
			}
		}
	}
	rs = {};
	rs.cycle_count = w.cycle_count;
	return w;
}

bool soil_wake_next(const WakeState &wake, bool soil_past_threshold)
{
	if (wake.wake_reason == PLANT_WAKE_SOIL_MOISTURE_ALERT) {
		return false;
	}
	return wake.soil_wake_armed || !soil_past_threshold;
}

void retained_on_sleep(main_retained &rs, uint64_t rtc_now_ticks, uint32_t sleep_ms,
		       bool soil_wake_armed)
{
	rs.sleep_start_rtc_ticks = rtc_now_ticks;
	rs.scheduled_sleep_ms = sleep_ms;
	rs.soil_wake_armed = soil_wake_armed ? 1U : 0U;
	rs.magic = RETAINED_MAGIC;
}

} // namespace plant
