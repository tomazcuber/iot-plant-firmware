/* Bench smoke test for plant_net WiFi/SNTP (issue #7, ADR-0006 gate).
 * Associates over DHCP, prints RSSI and the SNTP epoch, then idles associated
 * so the INA219 trace can show the MIN_MODEM DTIM wake pattern. */
#include <errno.h>
#include <inttypes.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <plant/net.h>

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

	printk("idle, associated\n");
	while (1) {
		k_sleep(K_FOREVER);
	}
	return 0;
}
