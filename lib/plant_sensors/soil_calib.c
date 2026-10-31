#include <plant/sensors.h>

float plant_soil_pct(uint16_t raw, uint16_t dry_adc, uint16_t wet_adc)
{
	float pct = 100.0f * ((float)dry_adc - (float)raw) /
		    ((float)dry_adc - (float)wet_adc);

	if (pct < 0.0f) {
		return 0.0f;
	}
	if (pct > 100.0f) {
		return 100.0f;
	}
	return pct;
}
