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

/* First boot: whatever LP SRAM holds is not a Sleep record. The first status
 * has t_sleep_ms = null and cycle_count = 0. */
static void test_first_boot_starts_at_zero_with_null_t_sleep()
{
	main_retained rs = power_on_garbage();

	plant::WakeState w = plant::retained_on_wake(rs, false);

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

	w[0] = plant::retained_on_wake(rs, false);
	plant::retained_on_sleep(rs, 5000, 60000);
	w[1] = plant::retained_on_wake(rs, true);
	plant::retained_on_sleep(rs, 66200, 60000);
	w[2] = plant::retained_on_wake(rs, true);

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

	(void)plant::retained_on_wake(rs, false);
	plant::retained_on_sleep(rs, 5000, 300000);
	plant::WakeState w = plant::retained_on_wake(rs, true);
	plant::retained_on_sleep(rs, 306000, 60000);

	CHECK(w.t_sleep_valid);
	CHECK(w.t_sleep_ms == 300000);
}

/* A reflash or reset keeps LP SRAM powered, so a complete record, magic
 * included, can still be there. It was not followed by a Sleep, so the count
 * restarts and the first t_sleep_ms is null (bench: after a reflash). */
static void test_reflash_with_intact_record_restarts_the_count()
{
	main_retained rs = power_on_garbage();

	(void)plant::retained_on_wake(rs, false);
	plant::retained_on_sleep(rs, 5000, 60000);
	(void)plant::retained_on_wake(rs, true);
	plant::retained_on_sleep(rs, 66000, 60000); /* reflashed during this Sleep */
	plant::WakeState w = plant::retained_on_wake(rs, false);

	CHECK(w.cycle_count == 0);
	CHECK(!w.t_sleep_valid);
}

/* A wake from Sleep whose record was never completed (the magic is
 * missing) is treated as a first boot too. */
static void test_wake_from_sleep_without_magic_restarts_the_count()
{
	main_retained rs = power_on_garbage();

	plant::WakeState w = plant::retained_on_wake(rs, true);

	CHECK(w.cycle_count == 0);
	CHECK(!w.t_sleep_valid);
}

/* After a restart the count runs on from 0 again. */
static void test_count_runs_on_after_a_restart()
{
	main_retained rs = power_on_garbage();

	(void)plant::retained_on_wake(rs, false);
	plant::retained_on_sleep(rs, 5000, 60000);
	(void)plant::retained_on_wake(rs, true);
	plant::retained_on_sleep(rs, 66000, 60000);
	(void)plant::retained_on_wake(rs, false); /* reset */
	plant::retained_on_sleep(rs, 5000, 60000);
	plant::WakeState w = plant::retained_on_wake(rs, true);

	CHECK(w.cycle_count == 1);
	CHECK(w.t_sleep_valid);
	CHECK(w.t_sleep_ms == 60000);
}

int main()
{
	test_first_boot_starts_at_zero_with_null_t_sleep();
	test_timer_wakes_count_up_and_report_the_armed_sleep();
	test_t_sleep_is_the_previously_armed_length();
	test_reflash_with_intact_record_restarts_the_count();
	test_wake_from_sleep_without_magic_restarts_the_count();
	test_count_runs_on_after_a_restart();

	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("test_main_retained: all passed\n");
	return 0;
}
