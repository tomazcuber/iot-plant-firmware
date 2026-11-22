/* Host tests for apps/main's retained state (issue #24): the magic check of
 * FR-MA-06, cycle_count (FR-MA-17, kept next to magic: ADR-0009, #23) and
 * t_sleep_ms on a timer wake. Each test drives the struct through the same
 * wake/sleep calls the firmware makes, Cycle by Cycle. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "retained.h"

static int failures;

#define CHECK(cond)                                                          \
	do {                                                                 \
		if (!(cond)) {                                               \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++;                                          \
		}                                                            \
	} while (0)

/* LP SRAM after power-on: arbitrary content. */
static main_retained power_on_garbage()
{
	main_retained rs;

	memset(&rs, 0xA5, sizeof(rs));
	return rs;
}

/* Fake RTC calibration (the seam is retained_ticks_to_ms(), retained.h). */
static uint64_t ticks_per_ms = 2;

extern "C" uint64_t retained_ticks_to_ms(uint64_t ticks)
{
	return ticks / ticks_per_ms;
}

/* The wake kinds the firmware reports. The RTC value is used only by a soil
 * alert, so the timer helpers pass 0. */
static plant::WakeState wake_after_reset(main_retained &rs)
{
	return plant::retained_on_wake(rs, false, PLANT_WAKE_TIMER, 0);
}

static plant::WakeState wake_by_timer(main_retained &rs)
{
	return plant::retained_on_wake(rs, true, PLANT_WAKE_TIMER, 0);
}

static plant::WakeState wake_by_soil_alert(main_retained &rs, uint64_t rtc_now_ticks)
{
	return plant::retained_on_wake(rs, true, PLANT_WAKE_SOIL_MOISTURE_ALERT, rtc_now_ticks);
}

/* First boot: whatever LP SRAM holds is not a Sleep record. The first status
 * has t_sleep_ms = null and cycle_count = 0. */
static void test_first_boot_starts_at_zero_with_null_t_sleep()
{
	main_retained rs = power_on_garbage();

	plant::WakeState w = wake_after_reset(rs);

	CHECK(w.cycle_count == 0);
	CHECK(!w.t_sleep_valid);
}

/* The bench check with the 60 s fragment: after the first boot, every
 * Cycle that ends in Sleep hands the next one cycle_count + 1, and each timer
 * wake reports the 60 s that was armed. */
static void test_timer_wakes_count_up_and_report_the_armed_sleep()
{
	main_retained rs = power_on_garbage();
	plant::WakeState w[3];

	w[0] = wake_after_reset(rs);
	plant::retained_on_sleep(rs, 5000, 60000, true);
	w[1] = wake_by_timer(rs);
	plant::retained_on_sleep(rs, 66200, 60000, true);
	w[2] = wake_by_timer(rs);

	CHECK(w[0].cycle_count == 0);
	CHECK(!w[0].t_sleep_valid);
	CHECK(w[1].cycle_count == 1);
	CHECK(w[1].t_sleep_valid);
	CHECK(w[1].t_sleep_ms == 60000);
	CHECK(w[2].cycle_count == 2);
	CHECK(w[2].t_sleep_valid);
	CHECK(w[2].t_sleep_ms == 60000);
}

/* t_sleep_ms is the Sleep the previous Cycle armed, not today's setting: a
 * 300 s Sleep reports 300000 even if the next one is shorter. */
static void test_t_sleep_is_the_previously_armed_length()
{
	main_retained rs = power_on_garbage();

	(void)wake_after_reset(rs);
	plant::retained_on_sleep(rs, 5000, 300000, true);
	plant::WakeState w = wake_by_timer(rs);
	plant::retained_on_sleep(rs, 306000, 60000, true);

	CHECK(w.t_sleep_valid);
	CHECK(w.t_sleep_ms == 300000);
}

/* A reflash or reset keeps LP SRAM powered, so a complete record, magic
 * included, can still be there. It was not followed by a Sleep, so the count
 * restarts and the first t_sleep_ms is null (bench: after a reflash). */
static void test_reflash_with_intact_record_restarts_the_count()
{
	main_retained rs = power_on_garbage();

	(void)wake_after_reset(rs);
	plant::retained_on_sleep(rs, 5000, 60000, true);
	(void)wake_by_timer(rs);
	plant::retained_on_sleep(rs, 66000, 60000, true); /* reflashed during this Sleep */
	plant::WakeState w = wake_after_reset(rs);

	CHECK(w.cycle_count == 0);
	CHECK(!w.t_sleep_valid);
}

/* A wake from Sleep whose record was never completed (the magic is
 * missing) is treated as a first boot too. */
static void test_wake_from_sleep_without_magic_restarts_the_count()
{
	main_retained rs = power_on_garbage();

	plant::WakeState w = wake_by_timer(rs);

	CHECK(w.cycle_count == 0);
	CHECK(!w.t_sleep_valid);
}

/* After a restart the count runs on from 0 again. */
static void test_count_runs_on_after_a_restart()
{
	main_retained rs = power_on_garbage();

	(void)wake_after_reset(rs);
	plant::retained_on_sleep(rs, 5000, 60000, true);
	(void)wake_by_timer(rs);
	plant::retained_on_sleep(rs, 66000, 60000, true);
	(void)wake_after_reset(rs); /* reset */
	plant::retained_on_sleep(rs, 5000, 60000, true);
	plant::WakeState w = wake_by_timer(rs);

	CHECK(w.cycle_count == 1);
	CHECK(w.t_sleep_valid);
	CHECK(w.t_sleep_ms == 60000);
}

/* A soil alert ends the Sleep early, so t_sleep_ms is the RTC time since Sleep
 * entry (FR-MA-06), not the 60 s that was armed: 12250 ticks at 2 ticks/ms. */
static void test_soil_alert_reports_the_measured_sleep()
{
	main_retained rs = power_on_garbage();

	ticks_per_ms = 2;
	(void)wake_after_reset(rs);
	plant::retained_on_sleep(rs, 5000, 60000, true);
	plant::WakeState w = wake_by_soil_alert(rs, 17250);

	CHECK(w.wake_reason == PLANT_WAKE_SOIL_MOISTURE_ALERT);
	CHECK(w.cycle_count == 1);
	CHECK(w.t_sleep_valid);
	CHECK(w.t_sleep_ms == 6125);
}

/* Every boot recalibrates the RTC clock. Only the ticks of this Sleep are
 * converted, so the time since power-on adds no error: 24000 ticks at the
 * current 3 ticks/ms are 8 s, however large the counter is. */
static void test_soil_alert_converts_only_the_ticks_of_this_sleep()
{
	main_retained rs = power_on_garbage();
	const uint64_t a_day_of_ticks = 12960000000ULL;

	ticks_per_ms = 2;
	(void)wake_after_reset(rs);
	plant::retained_on_sleep(rs, a_day_of_ticks, 60000, true);
	ticks_per_ms = 3;
	plant::WakeState w = wake_by_soil_alert(rs, a_day_of_ticks + 24000);
	ticks_per_ms = 2;

	CHECK(w.t_sleep_valid);
	CHECK(w.t_sleep_ms == 8000);
}

/* The cause is still reported when the record is unusable; only t_sleep_ms
 * becomes null (issue #26). */
static void test_soil_alert_without_a_record_has_null_t_sleep()
{
	main_retained rs = power_on_garbage();

	plant::WakeState w = wake_by_soil_alert(rs, 17250);

	CHECK(w.wake_reason == PLANT_WAKE_SOIL_MOISTURE_ALERT);
	CHECK(w.cycle_count == 0);
	CHECK(!w.t_sleep_valid);
}

/* An RTC reading before Sleep entry can't be a duration: null, not a wrapped
 * 32-bit value in the dataset. */
static void test_soil_alert_with_rtc_before_sleep_entry_has_null_t_sleep()
{
	main_retained rs = power_on_garbage();

	(void)wake_after_reset(rs);
	plant::retained_on_sleep(rs, 5000, 60000, true);
	plant::WakeState w = wake_by_soil_alert(rs, 4000);

	CHECK(w.wake_reason == PLANT_WAKE_SOIL_MOISTURE_ALERT);
	CHECK(w.cycle_count == 1);
	CHECK(!w.t_sleep_valid);
}

/* The wake-loop rule (ADR-0009, issue #26). `past` is the comparator output
 * read in this Cycle: true while the soil is past the threshold. */
static const bool PAST = true;
static const bool NORMAL = false;

/* A record left by a Sleep with the GPIO6 wake armed or not. */
static main_retained record_of_sleep(bool soil_wake_armed)
{
	main_retained rs = power_on_garbage();

	(void)wake_after_reset(rs);
	plant::retained_on_sleep(rs, 5000, 60000, soil_wake_armed);
	return rs;
}

/* After an alert the next Sleep is timer-only, whatever DO reads now. */
static void test_alert_sleeps_with_only_the_timer()
{
	main_retained past = record_of_sleep(true);
	main_retained normal = record_of_sleep(true);

	CHECK(!plant::soil_wake_next(wake_by_soil_alert(past, 17250), PAST));
	CHECK(!plant::soil_wake_next(wake_by_soil_alert(normal, 17250), NORMAL));
}

/* Timer-only, DO still past the threshold: stay timer-only. */
static void test_timer_wake_past_threshold_stays_timer_only()
{
	main_retained rs = record_of_sleep(false);

	CHECK(!plant::soil_wake_next(wake_by_timer(rs), PAST));
}

/* Timer-only, DO back on the normal side: re-arm the GPIO6 wake. */
static void test_timer_wake_back_to_normal_rearms()
{
	main_retained rs = record_of_sleep(false);

	CHECK(plant::soil_wake_next(wake_by_timer(rs), NORMAL));
}

/* A timer wake with the GPIO6 wake armed changes nothing, whatever DO reads:
 * a crossing during the Active phase then wakes the device as an alert. */
static void test_timer_wake_with_the_wake_armed_changes_nothing()
{
	main_retained past = record_of_sleep(true);
	main_retained normal = record_of_sleep(true);

	CHECK(plant::soil_wake_next(wake_by_timer(past), PAST));
	CHECK(plant::soil_wake_next(wake_by_timer(normal), NORMAL));
}

/* First boot follows DO: a probe already past the threshold at the start of a
 * Run stays timer-only instead of waking at once. */
static void test_first_boot_arms_only_on_the_normal_side()
{
	main_retained past = power_on_garbage();
	main_retained normal = power_on_garbage();

	CHECK(!plant::soil_wake_next(wake_after_reset(past), PAST));
	CHECK(plant::soil_wake_next(wake_after_reset(normal), NORMAL));
}

/* An armed flag in a record that doesn't count is not trusted: a reset, or a
 * wake without the magic, follows DO like a first boot. */
static void test_unusable_record_does_not_count_as_armed()
{
	main_retained after_reset = record_of_sleep(true);
	main_retained garbage = power_on_garbage();

	CHECK(!plant::soil_wake_next(wake_after_reset(after_reset), PAST));
	CHECK(!plant::soil_wake_next(wake_by_timer(garbage), PAST));
}

/* One whole dry spell through the retained struct: one alert, timer-only while
 * the soil stays past the threshold, and a new alert after it recovers. */
static void test_one_alert_per_dry_spell()
{
	main_retained rs = power_on_garbage();
	plant::WakeState w = wake_after_reset(rs);
	bool armed = plant::soil_wake_next(w, NORMAL);

	CHECK(armed);
	plant::retained_on_sleep(rs, 5000, 60000, armed);

	w = wake_by_soil_alert(rs, 20000); /* the soil dries out */
	CHECK(w.wake_reason == PLANT_WAKE_SOIL_MOISTURE_ALERT);
	armed = plant::soil_wake_next(w, PAST);
	CHECK(!armed);
	plant::retained_on_sleep(rs, 21000, 60000, armed);

	w = wake_by_timer(rs); /* still dry */
	CHECK(w.wake_reason == PLANT_WAKE_TIMER);
	armed = plant::soil_wake_next(w, PAST);
	CHECK(!armed);
	plant::retained_on_sleep(rs, 82000, 60000, armed);

	w = wake_by_timer(rs); /* watered: re-armed */
	armed = plant::soil_wake_next(w, NORMAL);
	CHECK(armed);
	plant::retained_on_sleep(rs, 143000, 60000, armed);

	w = wake_by_soil_alert(rs, 150000); /* the next dry spell */
	CHECK(w.wake_reason == PLANT_WAKE_SOIL_MOISTURE_ALERT);
	CHECK(w.t_sleep_ms == 3500); /* 7000 ticks at 2 ticks/ms */
	CHECK(!plant::soil_wake_next(w, PAST));
}

/* ADR-0012, bench row 1646: GPIO6 woke the chip after 17.64 s, the wake-cause
 * register stayed empty, and DO was back to normal when the Cycle read it. The
 * Cycle is still an alert, with the measured Sleep, followed by timer-only. */
static void test_empty_wake_cause_with_latched_pin_is_a_soil_alert()
{
	main_retained rs = record_of_sleep(true);

	ticks_per_ms = 2;
	plant::WakeState w = plant::retained_on_wake(
		rs, true, main_wake_reason(MAIN_WAKE_CAUSE_NONE, true), 5000 + 35280);

	CHECK(w.wake_reason == PLANT_WAKE_SOIL_MOISTURE_ALERT);
	CHECK(w.t_sleep_valid);
	CHECK(w.t_sleep_ms == 17640);
	CHECK(!plant::soil_wake_next(w, NORMAL));
}

/* The fallback applies only to an empty cause. A recorded timer stays a timer
 * wake even with GPIO6 latched (bench row 1651: both fired after a full Sleep),
 * and an empty cause with nothing latched is not an alert. */
static void test_wake_reason_table()
{
	CHECK(main_wake_reason(MAIN_WAKE_CAUSE_EXT1, true) == PLANT_WAKE_SOIL_MOISTURE_ALERT);
	CHECK(main_wake_reason(MAIN_WAKE_CAUSE_EXT1, false) == PLANT_WAKE_SOIL_MOISTURE_ALERT);
	CHECK(main_wake_reason(MAIN_WAKE_CAUSE_TIMER, true) == PLANT_WAKE_TIMER);
	CHECK(main_wake_reason(MAIN_WAKE_CAUSE_TIMER, false) == PLANT_WAKE_TIMER);
	CHECK(main_wake_reason(MAIN_WAKE_CAUSE_NONE, false) == PLANT_WAKE_TIMER);
}

int main()
{
	test_first_boot_starts_at_zero_with_null_t_sleep();
	test_timer_wakes_count_up_and_report_the_armed_sleep();
	test_t_sleep_is_the_previously_armed_length();
	test_reflash_with_intact_record_restarts_the_count();
	test_wake_from_sleep_without_magic_restarts_the_count();
	test_count_runs_on_after_a_restart();
	test_soil_alert_reports_the_measured_sleep();
	test_soil_alert_converts_only_the_ticks_of_this_sleep();
	test_soil_alert_without_a_record_has_null_t_sleep();
	test_soil_alert_with_rtc_before_sleep_entry_has_null_t_sleep();
	test_alert_sleeps_with_only_the_timer();
	test_timer_wake_past_threshold_stays_timer_only();
	test_timer_wake_back_to_normal_rearms();
	test_timer_wake_with_the_wake_armed_changes_nothing();
	test_first_boot_arms_only_on_the_normal_side();
	test_unusable_record_does_not_count_as_armed();
	test_one_alert_per_dry_spell();
	test_empty_wake_cause_with_latched_pin_is_a_soil_alert();
	test_wake_reason_table();

	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("test_main_retained: all passed\n");
	return 0;
}
