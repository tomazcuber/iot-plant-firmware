#include <errno.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#include <plant/sensors.h>
#include "soil.h"

static const struct device *const bmp280 = DEVICE_DT_GET(DT_NODELABEL(bmp280));
static const struct device *const bh1750 = DEVICE_DT_GET(DT_NODELABEL(bh1750));
static const struct device *const hdc1080 = DEVICE_DT_GET(DT_NODELABEL(hdc1080));

static bool bmp280_ready, bh1750_ready, hdc1080_ready, soil_ready;

int plant_sensors_init(void)
{
	bmp280_ready = device_is_ready(bmp280);
	bh1750_ready = device_is_ready(bh1750);
	hdc1080_ready = device_is_ready(hdc1080);
	soil_ready = (plant_soil_init() == 0);

	if (!bmp280_ready || !bh1750_ready || !hdc1080_ready || !soil_ready) {
		return -ENODEV;
	}
	return 0;
}

/* Fetch one sample, then read one or two channels (b may be NULL). */
static int fetch_get(const struct device *dev, enum sensor_channel ch_a,
		     struct sensor_value *a, enum sensor_channel ch_b,
		     struct sensor_value *b)
{
	int err = sensor_sample_fetch(dev);

	if (err) {
		return err;
	}
	err = sensor_channel_get(dev, ch_a, a);
	if (err) {
		return err;
	}
	if (b != NULL) {
		err = sensor_channel_get(dev, ch_b, b);
	}
	return err;
}

static int read_bmp280(struct plant_sensor_values *out)
{
	struct sensor_value t, p;
	int err;

	if (!bmp280_ready) {
		return -ENODEV;
	}
	err = fetch_get(bmp280, SENSOR_CHAN_AMBIENT_TEMP, &t, SENSOR_CHAN_PRESS, &p);
	if (err) {
		return err;
	}
	out->bmp280_temperature_c = sensor_value_to_float(&t);
	out->pressure_hpa = sensor_value_to_float(&p) * 10.0f; /* kPa -> hPa */
	out->valid |= PLANT_VALID_BMP280_TEMPERATURE_C | PLANT_VALID_PRESSURE_HPA;
	return 0;
}

static int read_bh1750(struct plant_sensor_values *out)
{
	struct sensor_value l;
	int err;

	if (!bh1750_ready) {
		return -ENODEV;
	}
	err = fetch_get(bh1750, SENSOR_CHAN_LIGHT, &l, SENSOR_CHAN_LIGHT, NULL);
	if (err) {
		return err;
	}
	out->lux = sensor_value_to_float(&l);
	out->valid |= PLANT_VALID_LUX;
	return 0;
}

static int read_hdc1080(struct plant_sensor_values *out)
{
	struct sensor_value t, h;
	int err;

	if (!hdc1080_ready) {
		return -ENODEV;
	}
	err = fetch_get(hdc1080, SENSOR_CHAN_AMBIENT_TEMP, &t,
			SENSOR_CHAN_HUMIDITY, &h);
	if (err) {
		return err;
	}
	out->temperature_c = sensor_value_to_float(&t);
	out->humidity_pct = sensor_value_to_float(&h);
	out->valid |= PLANT_VALID_TEMPERATURE_C | PLANT_VALID_HUMIDITY_PCT;
	return 0;
}

static int read_soil(struct plant_sensor_values *out)
{
	uint16_t raw;
	float pct;
	int err;

	if (!soil_ready) {
		return -ENODEV;
	}
	err = plant_soil_read(&raw, &pct);
	if (err) {
		return err;
	}
	out->soil_moisture_raw_adc = raw;
	out->soil_moisture_pct = pct;
	out->valid |= PLANT_VALID_SOIL_MOISTURE_PCT | PLANT_VALID_SOIL_MOISTURE_RAW_ADC;
	return 0;
}

int plant_sensors_read(struct plant_sensor_values *out)
{
	int failed = 0;

	*out = (struct plant_sensor_values){0};

	/* Fixed order (FR-MA-07 step 3), one attempt each. A failed sensor
	 * leaves its bits cleared (they are only set on success) and the read
	 * moves on to the next sensor. */
	failed += read_bmp280(out) != 0;
	failed += read_bh1750(out) != 0;
	failed += read_hdc1080(out) != 0;
	failed += read_soil(out) != 0;

	return failed;
}
