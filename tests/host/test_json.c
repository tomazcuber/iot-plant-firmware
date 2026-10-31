#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <plant/json.h>

static int failures;

#define CHECK(cond)                                                          \
	do {                                                                 \
		if (!(cond)) {                                               \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++;                                          \
		}                                                            \
	} while (0)

#define CHECK_STR(got, want)                                                 \
	do {                                                                 \
		if (strcmp((got), (want)) != 0) {                            \
			printf("FAIL %s:%d:\n  got:  %s\n  want: %s\n",      \
			       __FILE__, __LINE__, (got), (want));           \
			failures++;                                          \
		}                                                            \
	} while (0)

#define ALL_VALID 0x7Fu

static struct plant_sensor_values sample_values(void)
{
	struct plant_sensor_values v = {
		.temperature_c = 23.4f,
		.humidity_pct = 61.2f,
		.bmp280_temperature_c = 23.1f,
		.pressure_hpa = 1013.5f,
		.lux = 450.0f,
		.soil_moisture_pct = 65.0f,
		.soil_moisture_raw_adc = 1820,
		.valid = ALL_VALID,
	};
	return v;
}

static struct plant_status sample_status(void)
{
	struct plant_status s = {
		.wake_reason = PLANT_WAKE_TIMER,
		.t_active_ms = 1240,
		.t_sensor_ms = 120,
		.t_wifi_ms = 860,
		.t_ble_ms = 260,
		.t_sleep_ms = 300000,
		.wifi_rssi_dbm = -62,
		.cycle_count = 42,
		.timestamp_ms = 1712000000000LL,
		.present = 0x1FF,
	};
	return s;
}

static const char RAW_EXAMPLE[] =
	"{\"temperature_c\":23.4,\"bmp280_temperature_c\":23.1,"
	"\"humidity_pct\":61.2,\"pressure_hpa\":1013.5,\"lux\":450.0,"
	"\"soil_moisture_pct\":65.0,\"soil_moisture_raw_adc\":1820,"
	"\"timestamp_ms\":1712000000000}";

static const char STATUS_EXAMPLE[] =
	"{\"wake_reason\":\"timer\",\"t_active_ms\":1240,\"t_sensor_ms\":120,"
	"\"t_wifi_ms\":860,\"t_ble_ms\":260,\"t_sleep_ms\":300000,"
	"\"wifi_rssi_dbm\":-62,\"cycle_count\":42,"
	"\"timestamp_ms\":1712000000000}";

static void test_raw_example(void)
{
	struct plant_sensor_values v = sample_values();
	int64_t ts = 1712000000000LL;
	char buf[PLANT_JSON_RAW_MAX];
	int n = plant_json_encode_raw(&v, &ts, buf, sizeof(buf));

	CHECK(n == (int)strlen(RAW_EXAMPLE));
	CHECK_STR(buf, RAW_EXAMPLE);
}

static void test_status_example(void)
{
	struct plant_status s = sample_status();
	char buf[PLANT_JSON_STATUS_MAX];
	int n = plant_json_encode_status(&s, buf, sizeof(buf));

	CHECK(n == (int)strlen(STATUS_EXAMPLE));
	CHECK_STR(buf, STATUS_EXAMPLE);
}

/* Replace the first occurrence of `from` with `to`; returns 0 on success. */
static int substitute(char *dst, size_t len, const char *src,
		      const char *from, const char *to)
{
	const char *at = strstr(src, from);

	if (at == NULL) {
		return -1;
	}
	snprintf(dst, len, "%.*s%s%s", (int)(at - src), src, to, at + strlen(from));
	return 0;
}

static void test_each_raw_bit_cleared(void)
{
	static const struct {
		unsigned int bit;
		const char *from;
		const char *to;
	} cases[] = {
		{ PLANT_VALID_TEMPERATURE_C, "\"temperature_c\":23.4", "\"temperature_c\":null" },
		{ PLANT_VALID_HUMIDITY_PCT, "\"humidity_pct\":61.2", "\"humidity_pct\":null" },
		{ PLANT_VALID_BMP280_TEMPERATURE_C, "\"bmp280_temperature_c\":23.1",
		  "\"bmp280_temperature_c\":null" },
		{ PLANT_VALID_PRESSURE_HPA, "\"pressure_hpa\":1013.5", "\"pressure_hpa\":null" },
		{ PLANT_VALID_LUX, "\"lux\":450.0", "\"lux\":null" },
		{ PLANT_VALID_SOIL_MOISTURE_PCT, "\"soil_moisture_pct\":65.0",
		  "\"soil_moisture_pct\":null" },
		{ PLANT_VALID_SOIL_MOISTURE_RAW_ADC, "\"soil_moisture_raw_adc\":1820",
		  "\"soil_moisture_raw_adc\":null" },
	};
	int64_t ts = 1712000000000LL;

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		struct plant_sensor_values v = sample_values();
		char buf[PLANT_JSON_RAW_MAX], want[PLANT_JSON_RAW_MAX];

		v.valid = (uint8_t)(ALL_VALID & ~cases[i].bit);
		CHECK(plant_json_encode_raw(&v, &ts, buf, sizeof(buf)) > 0);
		CHECK(substitute(want, sizeof(want), RAW_EXAMPLE, cases[i].from, cases[i].to) == 0);
		CHECK_STR(buf, want);
	}
}

static void test_each_status_bit_cleared(void)
{
	static const struct {
		unsigned int bit;
		const char *from;
		const char *to;
	} cases[] = {
		{ PLANT_STATUS_WAKE_REASON, "\"wake_reason\":\"timer\"", "\"wake_reason\":null" },
		{ PLANT_STATUS_T_ACTIVE_MS, "\"t_active_ms\":1240", "\"t_active_ms\":null" },
		{ PLANT_STATUS_T_SENSOR_MS, "\"t_sensor_ms\":120", "\"t_sensor_ms\":null" },
		{ PLANT_STATUS_T_WIFI_MS, "\"t_wifi_ms\":860", "\"t_wifi_ms\":null" },
		{ PLANT_STATUS_T_BLE_MS, "\"t_ble_ms\":260", "\"t_ble_ms\":null" },
		{ PLANT_STATUS_T_SLEEP_MS, "\"t_sleep_ms\":300000", "\"t_sleep_ms\":null" },
		{ PLANT_STATUS_WIFI_RSSI_DBM, "\"wifi_rssi_dbm\":-62", "\"wifi_rssi_dbm\":null" },
		{ PLANT_STATUS_CYCLE_COUNT, "\"cycle_count\":42", "\"cycle_count\":null" },
		{ PLANT_STATUS_TIMESTAMP_MS, "\"timestamp_ms\":1712000000000",
		  "\"timestamp_ms\":null" },
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		struct plant_status s = sample_status();
		char buf[PLANT_JSON_STATUS_MAX], want[PLANT_JSON_STATUS_MAX];

		s.present = (uint16_t)(0x1FFu & ~cases[i].bit);
		CHECK(plant_json_encode_status(&s, buf, sizeof(buf)) > 0);
		CHECK(substitute(want, sizeof(want), STATUS_EXAMPLE, cases[i].from, cases[i].to) == 0);
		CHECK_STR(buf, want);
	}
}

static void test_all_invalid(void)
{
	struct plant_sensor_values v = sample_values();
	struct plant_status s = sample_status();
	char buf[PLANT_JSON_RAW_MAX];

	v.valid = 0;
	CHECK(plant_json_encode_raw(&v, NULL, buf, sizeof(buf)) > 0);
	CHECK_STR(buf,
		  "{\"temperature_c\":null,\"bmp280_temperature_c\":null,"
		  "\"humidity_pct\":null,\"pressure_hpa\":null,\"lux\":null,"
		  "\"soil_moisture_pct\":null,\"soil_moisture_raw_adc\":null,"
		  "\"timestamp_ms\":null}");

	s.present = 0;
	CHECK(plant_json_encode_status(&s, buf, sizeof(buf)) > 0);
	CHECK_STR(buf,
		  "{\"wake_reason\":null,\"t_active_ms\":null,\"t_sensor_ms\":null,"
		  "\"t_wifi_ms\":null,\"t_ble_ms\":null,\"t_sleep_ms\":null,"
		  "\"wifi_rssi_dbm\":null,\"cycle_count\":null,\"timestamp_ms\":null}");
}

static void test_null_timestamp(void)
{
	struct plant_sensor_values v = sample_values();
	char buf[PLANT_JSON_RAW_MAX], want[PLANT_JSON_RAW_MAX];

	CHECK(plant_json_encode_raw(&v, NULL, buf, sizeof(buf)) > 0);
	CHECK(substitute(want, sizeof(want), RAW_EXAMPLE, "\"timestamp_ms\":1712000000000",
			 "\"timestamp_ms\":null") == 0);
	CHECK_STR(buf, want);
}

static void test_wake_reasons(void)
{
	struct plant_status s = sample_status();
	char buf[PLANT_JSON_STATUS_MAX];

	s.wake_reason = PLANT_WAKE_SOIL_MOISTURE_ALERT;
	CHECK(plant_json_encode_status(&s, buf, sizeof(buf)) > 0);
	CHECK(strstr(buf, "\"wake_reason\":\"soil_moisture_alert\",\"t_active_ms\"") != NULL);

	s.wake_reason = PLANT_WAKE_TIMER;
	CHECK(plant_json_encode_status(&s, buf, sizeof(buf)) > 0);
	CHECK(strstr(buf, "\"wake_reason\":\"timer\",\"t_active_ms\"") != NULL);
}

static void test_enomem_one_byte_short(void)
{
	struct plant_sensor_values v = sample_values();
	struct plant_status s = sample_status();
	int64_t ts = 1712000000000LL;
	char buf[PLANT_JSON_RAW_MAX];
	size_t raw_len = strlen(RAW_EXAMPLE);
	size_t status_len = strlen(STATUS_EXAMPLE);

	/* exact fit: payload plus NUL */
	CHECK(plant_json_encode_raw(&v, &ts, buf, raw_len + 1) == (int)raw_len);
	CHECK(plant_json_encode_raw(&v, &ts, buf, raw_len) == -ENOMEM);
	CHECK(plant_json_encode_raw(&v, &ts, buf, 0) == -ENOMEM);

	CHECK(plant_json_encode_status(&s, buf, status_len + 1) == (int)status_len);
	CHECK(plant_json_encode_status(&s, buf, status_len) == -ENOMEM);
	CHECK(plant_json_encode_status(&s, buf, 0) == -ENOMEM);
}

static void test_negative_rssi(void)
{
	struct plant_status s = sample_status();
	char buf[PLANT_JSON_STATUS_MAX];

	s.wifi_rssi_dbm = -100;
	CHECK(plant_json_encode_status(&s, buf, sizeof(buf)) > 0);
	CHECK(strstr(buf, "\"wifi_rssi_dbm\":-100,") != NULL);

	s.wifi_rssi_dbm = INT16_MIN;
	CHECK(plant_json_encode_status(&s, buf, sizeof(buf)) > 0);
	CHECK(strstr(buf, "\"wifi_rssi_dbm\":-32768,") != NULL);
}

static void test_uint32_max(void)
{
	struct plant_status s = sample_status();
	char buf[PLANT_JSON_STATUS_MAX];

	s.t_active_ms = s.t_sensor_ms = s.t_wifi_ms = UINT32_MAX;
	s.t_ble_ms = s.t_sleep_ms = s.cycle_count = UINT32_MAX;
	s.timestamp_ms = INT64_MAX;
	CHECK(plant_json_encode_status(&s, buf, sizeof(buf)) > 0);
	CHECK_STR(buf,
		  "{\"wake_reason\":\"timer\",\"t_active_ms\":4294967295,"
		  "\"t_sensor_ms\":4294967295,\"t_wifi_ms\":4294967295,"
		  "\"t_ble_ms\":4294967295,\"t_sleep_ms\":4294967295,"
		  "\"wifi_rssi_dbm\":-62,\"cycle_count\":4294967295,"
		  "\"timestamp_ms\":9223372036854775807}");
	CHECK(strlen(buf) < PLANT_JSON_STATUS_MAX);
}

static void test_raw_max_fits(void)
{
	struct plant_sensor_values v = sample_values();
	int64_t ts = INT64_MAX;
	char buf[PLANT_JSON_RAW_MAX];

	v.temperature_c = v.humidity_pct = v.bmp280_temperature_c = -99999.9f;
	v.pressure_hpa = v.lux = v.soil_moisture_pct = 99999.9f;
	v.soil_moisture_raw_adc = UINT16_MAX;
	CHECK(plant_json_encode_raw(&v, &ts, buf, sizeof(buf)) > 0);
}

static void test_never_nan_or_inf(void)
{
	struct plant_sensor_values v = sample_values();
	int64_t ts = 1;
	char buf[PLANT_JSON_RAW_MAX];

	v.temperature_c = NAN;
	v.lux = INFINITY;
	CHECK(plant_json_encode_raw(&v, &ts, buf, sizeof(buf)) > 0);
	CHECK(strstr(buf, "\"temperature_c\":null") != NULL);
	CHECK(strstr(buf, "\"lux\":null") != NULL);
	CHECK(strstr(buf, "nan") == NULL && strstr(buf, "inf") == NULL);
}

int main(void)
{
	test_raw_example();
	test_status_example();
	test_each_raw_bit_cleared();
	test_each_status_bit_cleared();
	test_all_invalid();
	test_null_timestamp();
	test_wake_reasons();
	test_enomem_one_byte_short();
	test_negative_rssi();
	test_uint32_max();
	test_raw_max_fits();
	test_never_nan_or_inf();

	if (failures) {
		printf("test_json: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_json: all passed\n");
	return 0;
}
