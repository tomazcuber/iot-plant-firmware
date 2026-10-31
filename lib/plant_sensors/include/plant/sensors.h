#ifndef PLANT_SENSORS_H
#define PLANT_SENSORS_H

#include <stdint.h>
#include <plant/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Check that all sensor devices and the ADC channel are ready, and configure
 * the ADC channel. Call once per boot, before the first read.
 * Returns 0, or -ENODEV if any device is not ready. A not-ready device does
 * not stop the app: its fields are simply never valid. */
int plant_sensors_init(void);

/* Run the sensor phase: BMP280 -> BH1750FVI -> HDC1080 -> soil moisture ADC.
 * Each sensor is attempted exactly once; no retries (FR-MA-08a).
 * Fills *out and sets a `valid` bit for each field that was read.
 * Returns the number of sensors that failed (0..4). Never returns early. */
int plant_sensors_read(struct plant_sensor_values *out);

/* Pure two-point calibration (DD-MA-12), no Zephyr dependency.
 * pct = 100 * (dry - raw) / (dry - wet), clamped to [0, 100].
 * Precondition: dry != wet (enforced at build time for the Kconfig values). */
float plant_soil_pct(uint16_t raw, uint16_t dry_adc, uint16_t wet_adc);

#ifdef __cplusplus
}
#endif
#endif
