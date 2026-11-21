/* Phase markers (FR-MA-18, issue #28): two GPIOs that tell the INA219
 * Reader which part of the Active phase the device is in, so the Current
 * trace can be cut into boot, sensors, WiFi and Teardown (ADR-0009).
 *
 * Code bit 1 is GPIO18 and bit 0 is GPIO19 (marker-gpios in app.overlay).
 * The Reader pulls both down, so they read `00` during boot and deep sleep.
 *
 * Zephyr-free so the host tests can fake it (tests/host/test_main_cycle.cpp);
 * C so it can move to a shared module unchanged. The Zephyr implementation is
 * phase_markers.c. */
#ifndef MAIN_PHASE_MARKERS_H
#define MAIN_PHASE_MARKERS_H

#ifdef __cplusplus
extern "C" {
#endif

enum phase_marker {
	PHASE_MARKER_NONE = 0,     /* 00: boot, deep sleep; set last before sys_poweroff() */
	PHASE_MARKER_SENSORS = 1,  /* 01: from t_active_start */
	PHASE_MARKER_WIFI = 2,     /* 10: from t_wifi_start */
	PHASE_MARKER_TEARDOWN = 3, /* 11: from where t_wifi_ms stops */
};

/* Configure both pins as outputs at `00`. Call once, before the first
 * phase_markers_set(). Returns 0 or a negative errno; on error
 * phase_markers_set() does nothing. */
int phase_markers_init(void);

/* Drive `code` on both pins with one register write, so the Reader never
 * samples a code in between. */
void phase_markers_set(enum phase_marker code);

#ifdef __cplusplus
}
#endif
#endif
