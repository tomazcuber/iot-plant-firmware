/* Host tests for apps/main's Active phase (issue #22): the wake-cycle call
 * sequence (FR-MA-07), discard-on-failure (DD-MA-02), null fields
 * (FR-MA-08a, DD-MA-11) and the Phase durations in `status` (FR-MA-16).
 * The payloads come from the real plant_json encoder. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <plant/net.h>
#include <plant/sensors.h>
#include <plant/types.h>

#include "cycle.h"

static int failures;

#define CHECK(cond)                                                          \
	do {                                                                 \
		if (!(cond)) {                                               \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++;                                          \
		}                                                            \
	} while (0)

#define CHECK_STR(got, want)                                                  \
	do {                                                                  \
		if (strcmp((got), (want)) != 0) {                             \
			printf("FAIL %s:%d:\n  got  %s\n  want %s\n", __FILE__, \
			       __LINE__, (got), (want));                      \
			failures++;                                           \
		}                                                             \
	} while (0)

/* Fake clock and fake hardware (the seams are <plant/sensors.h> and
 * <plant/net.h>). Each fake advances the clock by its scripted duration and
 * appends a letter to `calls`: I sensors init, s sensors read, N net init,
 * W wifi connect, S sntp, M mqtt connect, P publish, R rssi, D mqtt
 * disconnect, X wifi disconnect. */
static int64_t now_ms;
static char calls[64];
static int wifi_ret, sntp_ret, mqtt_ret, publish_ret, rssi_ret;
static uint8_t sensors_valid;
static int32_t wifi_timeout, sntp_timeout, mqtt_timeout;
static const char *last_ssid, *last_psk, *last_broker, *last_client_id;

struct Published {
	char topic[96];
	char payload[256];
};
static Published published[4];
static int n_published;

static void log_call(char c, int64_t duration_ms)
{
	size_t n = strlen(calls);

	if (n + 1 < sizeof(calls)) {
		calls[n] = c;
		calls[n + 1] = '\0';
	}
	now_ms += duration_ms;
}

int64_t plant::cycle_uptime_ms()
{
	return now_ms;
}

extern "C" {

int plant_sensors_init(void)
{
	log_call('I', 2);
	return 0;
}

int plant_sensors_read(struct plant_sensor_values *out)
{
	log_call('s', 118);
	out->temperature_c = 23.4f;
	out->humidity_pct = 61.2f;
	out->bmp280_temperature_c = 23.1f;
	out->pressure_hpa = 1013.5f;
	out->lux = 450.0f;
	out->soil_moisture_pct = 65.0f;
	out->soil_moisture_raw_adc = 1820;
	out->valid = sensors_valid;
	return 0;
}

int plant_net_init(void)
{
	log_call('N', 0);
	return 0;
}

int plant_wifi_connect(const char *ssid, const char *psk, int32_t timeout_ms)
{
	log_call('W', 700);
	last_ssid = ssid;
	last_psk = psk;
	wifi_timeout = timeout_ms;
	return wifi_ret;
}

int plant_sntp_sync(int32_t timeout_ms, int64_t *epoch_ms)
{
	log_call('S', 60);
	sntp_timeout = timeout_ms;
	if (sntp_ret == 0) {
		*epoch_ms = 1712000000000LL; /* wall clock at return */
	}
	return sntp_ret;
}

int plant_mqtt_connect(const char *broker_host, const char *client_id, int32_t timeout_ms)
{
	log_call('M', 80);
	last_broker = broker_host;
	last_client_id = client_id;
	mqtt_timeout = timeout_ms;
	return mqtt_ret;
}

int plant_mqtt_publish(const char *topic, const char *payload, size_t len)
{
	log_call('P', 5);
	if (publish_ret == 0 && n_published < 4) {
		Published &p = published[n_published++];

		snprintf(p.topic, sizeof(p.topic), "%s", topic);
		snprintf(p.payload, sizeof(p.payload), "%.*s", (int)len, payload);
	}
	return publish_ret;
}

int plant_wifi_rssi(int16_t *rssi_dbm)
{
	log_call('R', 0);
	if (rssi_ret == 0) {
		*rssi_dbm = -55;
	}
	return rssi_ret;
}

int plant_mqtt_disconnect(void)
{
	log_call('D', 3);
	return 0;
}

int plant_wifi_disconnect(void)
{
	log_call('X', 20);
	return 0;
}

int plant_mqtt_poll(int32_t timeout_ms)
{
	(void)timeout_ms;
	log_call('?', 0);
	return 0;
}

} /* extern "C" */

static const plant::CycleConfig cfg = {
	"lab-ap", "password", "10.0.0.2", "balcony-pot-1", 10000, 3000, 10000,
};

/* Wake at uptime 50 ms (the boot prologue precedes step 1). */
static const int64_t T_ACTIVE_START = 50;

static void fresh_wake()
{
	now_ms = T_ACTIVE_START;
	calls[0] = '\0';
	wifi_ret = sntp_ret = mqtt_ret = publish_ret = rssi_ret = 0;
	sensors_valid = 0x7f;
	n_published = 0;
	memset(published, 0, sizeof(published));
}

/* Happy path: sensors -> WiFi -> SNTP -> MQTT -> raw -> RSSI -> status ->
 * MQTT disconnect -> WiFi disconnect, with the configured credentials. */
static void test_cycle_runs_the_wake_sequence_in_order()
{
	fresh_wake();

	plant::CycleResult r = plant::run_cycle(cfg, T_ACTIVE_START);

	CHECK(r.error == 0);
	CHECK_STR(calls, "IsNWSMPRPDX");
	CHECK_STR(last_ssid, "lab-ap");
	CHECK_STR(last_psk, "password");
	CHECK_STR(last_broker, "10.0.0.2");
	CHECK_STR(last_client_id, "balcony-pot-1");
	CHECK(wifi_timeout == 10000);
	CHECK(sntp_timeout == 3000);
	CHECK(mqtt_timeout == 10000);
}

/* Both payloads go to plants/{device-name}/... (FR-MA-09) in main's schema
 * (FR-MA-10). Scripted clock: wake at 50, sensors done at 170 (t_sensor 120),
 * SNTP returns at 930 with epoch 1712000000000, raw is encoded at 1010 after
 * MQTT connect, and status at 1015 after the raw publish and the RSSI sample
 * (t_wifi 845, t_active 965). Both payloads carry the wall clock of the raw
 * publish. cycle_count and t_sleep_ms are null: no retained state yet. */
static void test_cycle_publishes_raw_then_status_with_phase_durations()
{
	fresh_wake();

	plant::CycleResult r = plant::run_cycle(cfg, T_ACTIVE_START);

	CHECK(n_published == 2);
	CHECK_STR(published[0].topic, "plants/balcony-pot-1/sensors/raw");
	CHECK_STR(published[0].payload,
		  "{\"temperature_c\":23.4,\"bmp280_temperature_c\":23.1,"
		  "\"humidity_pct\":61.2,\"pressure_hpa\":1013.5,\"lux\":450.0,"
		  "\"soil_moisture_pct\":65.0,\"soil_moisture_raw_adc\":1820,"
		  "\"timestamp_ms\":1712000000080}");
	CHECK_STR(published[1].topic, "plants/balcony-pot-1/status");
	CHECK_STR(published[1].payload,
		  "{\"wake_reason\":\"timer\",\"t_active_ms\":965,\"t_sensor_ms\":120,"
		  "\"t_wifi_ms\":845,\"t_ble_ms\":0,\"t_sleep_ms\":null,"
		  "\"wifi_rssi_dbm\":-55,\"cycle_count\":null,"
		  "\"timestamp_ms\":1712000000080}");
	CHECK(r.t_sensor_ms == 120);
	CHECK(r.t_wifi_ms == 845);
	CHECK(r.t_active_ms == 965);
}

/* DD-MA-02: a failed WiFi connect discards the Cycle's data. No SNTP, MQTT or
 * publish; the interface is still brought down before Sleep. */
static void test_wifi_failure_skips_publishing_and_tears_down()
{
	fresh_wake();
	wifi_ret = -ETIMEDOUT;

	plant::CycleResult r = plant::run_cycle(cfg, T_ACTIVE_START);

	CHECK(r.error == -ETIMEDOUT);
	CHECK_STR(calls, "IsNWX");
	CHECK(n_published == 0);
}

/* DD-MA-02: a failed MQTT connect skips both publishes. plant_mqtt_connect
 * releases its own socket on failure, so only WiFi is torn down. */
static void test_mqtt_failure_skips_publishing_and_disconnects_wifi()
{
	fresh_wake();
	mqtt_ret = -ECONNREFUSED;

	plant::CycleResult r = plant::run_cycle(cfg, T_ACTIVE_START);

	CHECK(r.error == -ECONNREFUSED);
	CHECK_STR(calls, "IsNWSMX");
	CHECK(n_published == 0);
}

/* DD-MA-02: publishing stops at the first failed publish (QoS 0, no retry).
 * The client may still be marked connected, so MQTT is disconnected too. */
static void test_failed_raw_publish_skips_status_and_tears_down()
{
	fresh_wake();
	publish_ret = -ENOTCONN;

	plant::CycleResult r = plant::run_cycle(cfg, T_ACTIVE_START);

	CHECK(r.error == -ENOTCONN);
	CHECK_STR(calls, "IsNWSMPDX");
}

/* DD-MA-11: one SNTP attempt; on failure timestamp_ms is null in both payloads
 * and the Cycle carries on. */
static void test_sntp_failure_publishes_null_timestamps()
{
	fresh_wake();
	sntp_ret = -ETIMEDOUT;

	plant::CycleResult r = plant::run_cycle(cfg, T_ACTIVE_START);

	CHECK(r.error == 0);
	CHECK_STR(calls, "IsNWSMPRPDX");
	CHECK(n_published == 2);
	CHECK(strstr(published[0].payload, "\"timestamp_ms\":null}") != nullptr);
	CHECK(strstr(published[1].payload, "\"timestamp_ms\":null}") != nullptr);
}

/* FR-MA-08a: failed sensors (cleared valid bits) are published as null; the
 * rest of the Cycle is unaffected. Here the HDC1080 and the soil probe fail. */
static void test_failed_sensors_publish_null_fields()
{
	fresh_wake();
	sensors_valid = PLANT_VALID_BMP280_TEMPERATURE_C | PLANT_VALID_PRESSURE_HPA |
			PLANT_VALID_LUX;

	plant::CycleResult r = plant::run_cycle(cfg, T_ACTIVE_START);

	CHECK(r.error == 0);
	CHECK(n_published == 2);
	CHECK_STR(published[0].payload,
		  "{\"temperature_c\":null,\"bmp280_temperature_c\":23.1,"
		  "\"humidity_pct\":null,\"pressure_hpa\":1013.5,\"lux\":450.0,"
		  "\"soil_moisture_pct\":null,\"soil_moisture_raw_adc\":null,"
		  "\"timestamp_ms\":1712000000080}");
}

/* An RSSI sample that fails publishes wifi_rssi_dbm as null. */
static void test_rssi_failure_publishes_null_rssi()
{
	fresh_wake();
	rssi_ret = -EIO;

	plant::CycleResult r = plant::run_cycle(cfg, T_ACTIVE_START);

	CHECK(r.error == 0);
	CHECK(n_published == 2);
	CHECK(strstr(published[1].payload, "\"wifi_rssi_dbm\":null,") != nullptr);
}

int main()
{
	test_cycle_runs_the_wake_sequence_in_order();
	test_cycle_publishes_raw_then_status_with_phase_durations();
	test_wifi_failure_skips_publishing_and_tears_down();
	test_mqtt_failure_skips_publishing_and_disconnects_wifi();
	test_failed_raw_publish_skips_status_and_tears_down();
	test_sntp_failure_publishes_null_timestamps();
	test_failed_sensors_publish_null_fields();
	test_rssi_failure_publishes_null_rssi();

	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("test_main_cycle: all passed\n");
	return 0;
}
