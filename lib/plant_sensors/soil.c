#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>

#include <plant/sensors.h>
#include "soil.h"

BUILD_ASSERT(CONFIG_PLANT_SOIL_DRY_ADC != CONFIG_PLANT_SOIL_WET_ADC,
	     "soil calibration: dry and wet ADC values must differ");

static const struct adc_dt_spec soil_adc = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

int plant_soil_init(void)
{
	if (!adc_is_ready_dt(&soil_adc)) {
		return -ENODEV;
	}
	return adc_channel_setup_dt(&soil_adc);
}

int plant_soil_read(uint16_t *raw, float *pct)
{
	int16_t sample = 0;
	struct adc_sequence seq = {
		.buffer = &sample,
		.buffer_size = sizeof(sample),
	};
	int err = adc_sequence_init_dt(&soil_adc, &seq);

	if (err) {
		return err;
	}
	err = adc_read_dt(&soil_adc, &seq);
	if (err) {
		return err;
	}

	/* The raw value is reported unclamped; only the percentage is clamped. */
	*raw = sample < 0 ? 0 : (uint16_t)sample;
	*pct = plant_soil_pct(*raw, CONFIG_PLANT_SOIL_DRY_ADC,
			      CONFIG_PLANT_SOIL_WET_ADC);
	return 0;
}
