/* Bench smoke test for plant_net (issues #7 and #8, ADR-0006 gate).
 * Associates over DHCP, prints RSSI and the SNTP epoch, connects to the MQTT
 * broker, publishes one status and one sensors/raw payload at QoS 0, polls for
 * CONFIG_SMOKE_KEEPALIVE_WAIT_S seconds (keepalive check), disconnects, then
 * idles associated so the INA219 trace can show the MIN_MODEM DTIM wake
 * pattern. If the poll returns -ENOTCONN (e.g. broker stopped) it says so. */
#include <errno.h>
#include <inttypes.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <stdio.h>

#include <plant/json.h>
#include <plant/net.h>
#include <plant/types.h>

static void smoke_mqtt(int16_t rssi, int64_t epoch_ms)
{
	static const struct plant_sensor_values vals = {
		.temperature_c = 21.5f,
		.humidity_pct = 55.0f,
		.bmp280_temperature_c = 21.0f,
		.pressure_hpa = 1013.2f,
		.lux = 120.0f,
		.soil_moisture_pct = 40.0f,
		.soil_moisture_raw_adc = 1500,
		.valid = 0x7f,
	};
	struct plant_status status = {
		.wake_reason = PLANT_WAKE_TIMER,
		.wifi_rssi_dbm = rssi,
		.cycle_count = 1,
		.timestamp_ms = epoch_ms,
		.present = PLANT_STATUS_WAKE_REASON | PLANT_STATUS_WIFI_RSSI_DBM |
			   PLANT_STATUS_CYCLE_COUNT | PLANT_STATUS_TIMESTAMP_MS,
	};
	char topic[96];
	char payload[PLANT_JSON_RAW_MAX];
	int len;
	int ret;

	ret = plant_mqtt_connect(CONFIG_SMOKE_BROKER_HOST, CONFIG_SMOKE_DEVICE_NAME,
				 CONFIG_SMOKE_CONNECT_TIMEOUT_MS);
	if (ret < 0) {
		printk("plant_mqtt_connect failed: %d\n", ret);
		return;
	}
	printk("MQTT connected\n");

	len = plant_json_encode_status(&status, payload, sizeof(payload));
	snprintf(topic, sizeof(topic), "plants/%s/status", CONFIG_SMOKE_DEVICE_NAME);
	if (len > 0) {
		ret = plant_mqtt_publish(topic, payload, len);
		printk("publish %s: %d\n", topic, ret);
	}

	len = plant_json_encode_raw(&vals, &epoch_ms, payload, sizeof(payload));
	snprintf(topic, sizeof(topic), "plants/%s/sensors/raw", CONFIG_SMOKE_DEVICE_NAME);
	if (len > 0) {
		ret = plant_mqtt_publish(topic, payload, len);
		printk("publish %s: %d\n", topic, ret);
	}

	for (int i = 0; i < CONFIG_SMOKE_KEEPALIVE_WAIT_S; i++) {
		ret = plant_mqtt_poll(1000);
		if (ret == -ENOTCONN) {
			printk("plant_mqtt_poll: -ENOTCONN after %d s\n", i);
			return;
		}
	}
	printk("keepalive held for %d s\n", CONFIG_SMOKE_KEEPALIVE_WAIT_S);

	ret = plant_mqtt_disconnect();
	printk("plant_mqtt_disconnect: %d\n", ret);
}

int main(void)
{
	int16_t rssi;
	int64_t epoch_ms;
	int ret;

	ret = plant_net_init();
	if (ret < 0) {
		printk("plant_net_init failed: %d\n", ret);
		return ret;
	}

	ret = plant_wifi_connect(CONFIG_SMOKE_WIFI_SSID, CONFIG_SMOKE_WIFI_PSK,
				 CONFIG_SMOKE_CONNECT_TIMEOUT_MS);
	if (ret == -ETIMEDOUT) {
		printk("plant_wifi_connect: timeout\n");
		return ret;
	} else if (ret < 0) {
		printk("plant_wifi_connect failed: %d\n", ret);
		return ret;
	}
	printk("associated, DHCP lease obtained\n");

	if (plant_wifi_rssi(&rssi) == 0) {
		printk("RSSI: %d dBm\n", rssi);
	} else {
		printk("RSSI unavailable\n");
	}

	ret = plant_sntp_sync(10000, &epoch_ms);
	if (ret == 0) {
		printk("SNTP epoch: %" PRId64 " ms\n", epoch_ms);
	} else {
		printk("plant_sntp_sync failed: %d\n", ret);
	}

	smoke_mqtt(rssi, epoch_ms);

	printk("idle, associated\n");
	while (1) {
		k_sleep(K_FOREVER);
	}
	return 0;
}
