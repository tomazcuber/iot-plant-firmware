#include "retained.h"

namespace plant {

WakeState retained_on_wake(main_retained &rs, bool deep_sleep_wake)
{
	WakeState w = {0, false, 0};

	/* Only a deep-sleep wake vouches for the magic: a reset or reflash can
	 * leave a complete record behind. */
	if (deep_sleep_wake && rs.magic == RETAINED_MAGIC) {
		/* Only the timer wakes the device so far: the armed length is
		 * the Sleep that was taken (FR-MA-06). A GPIO wake will use
		 * sleep_start_rtc_ms instead. */
		w.cycle_count = rs.cycle_count + 1;
		w.t_sleep_valid = true;
		w.t_sleep_ms = rs.scheduled_sleep_ms;
	}
	rs = {};
	rs.cycle_count = w.cycle_count;
	return w;
}

void retained_on_sleep(main_retained &rs, int64_t rtc_now_ms, uint32_t sleep_ms)
{
	rs.sleep_start_rtc_ms = rtc_now_ms;
	rs.scheduled_sleep_ms = sleep_ms;
	rs.magic = RETAINED_MAGIC;
}

} // namespace plant
