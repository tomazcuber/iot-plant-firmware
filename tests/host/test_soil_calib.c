#include <stdio.h>

#include <plant/sensors.h>

static int failures;

#define CHECK_NEAR(got, want)                                                \
	do {                                                                 \
		float g_ = (got), w_ = (want);                               \
		if (g_ < w_ - 0.001f || g_ > w_ + 0.001f) {                  \
			printf("FAIL %s:%d: got %f want %f\n", __FILE__,     \
			       __LINE__, (double)g_, (double)w_);            \
			failures++;                                          \
		}                                                            \
	} while (0)

#define DRY 3000
#define WET 1500

static void test_endpoints(void)
{
	CHECK_NEAR(plant_soil_pct(DRY, DRY, WET), 0.0f);
	CHECK_NEAR(plant_soil_pct(WET, DRY, WET), 100.0f);
}

static void test_midpoint(void)
{
	CHECK_NEAR(plant_soil_pct(2250, DRY, WET), 50.0f);
	CHECK_NEAR(plant_soil_pct(2625, DRY, WET), 25.0f);
}

static void test_clamped(void)
{
	CHECK_NEAR(plant_soil_pct(4095, DRY, WET), 0.0f);   /* drier than dry */
	CHECK_NEAR(plant_soil_pct(0, DRY, WET), 100.0f);    /* wetter than wet */
}

/* Inverted probe: reads higher when wet. */
static void test_inverted(void)
{
	CHECK_NEAR(plant_soil_pct(1500, 1500, 3000), 0.0f);
	CHECK_NEAR(plant_soil_pct(3000, 1500, 3000), 100.0f);
	CHECK_NEAR(plant_soil_pct(2250, 1500, 3000), 50.0f);
	CHECK_NEAR(plant_soil_pct(0, 1500, 3000), 0.0f);
	CHECK_NEAR(plant_soil_pct(4095, 1500, 3000), 100.0f);
}

int main(void)
{
	test_endpoints();
	test_midpoint();
	test_clamped();
	test_inverted();

	if (failures) {
		printf("test_soil_calib: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_soil_calib: all passed\n");
	return 0;
}
