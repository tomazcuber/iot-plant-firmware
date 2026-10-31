/*
 * Bench smoke app for the plant_sensors shield and module.
 * Prints one sensor phase as a plant_json sensors/raw payload every 5 s.
 * Re-run on the bench after any change to the shield.
 */
#include <stdio.h>

#include <zephyr/kernel.h>

#include <plant/json.h>
#include <plant/sensors.h>

int main(void)
{
	struct plant_sensor_values v;
	char buf[PLANT_JSON_RAW_MAX];
	int ret = plant_sensors_init();

	printf("plant_sensors_init: %d\n", ret);

	for (;;) {
		int failed = plant_sensors_read(&v);
		int len = plant_json_encode_raw(&v, NULL, buf, sizeof(buf));

		printf("failed=%d valid=0x%02x %s\n", failed, v.valid,
		       len >= 0 ? buf : "(encode error)");
		k_sleep(K_SECONDS(5));
	}
	return 0;
}
