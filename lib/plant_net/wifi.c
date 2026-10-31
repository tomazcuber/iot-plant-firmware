#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/logging/log.h>

/* The only Espressif-specific code in the shared modules (SM-16). */
#include "esp_wifi.h"

#include <plant/net.h>

#include "net_util.h"

LOG_MODULE_REGISTER(plant_net, LOG_LEVEL_INF);

static struct net_mgmt_event_callback ip_cb;
static K_SEM_DEFINE(ip_sem, 0, 1);
static bool initialised;

static void ip_event_handler(struct net_mgmt_event_callback *cb, uint64_t event,
			     struct net_if *iface)
{
	ARG_UNUSED(cb);
	ARG_UNUSED(iface);

	if (event == NET_EVENT_IPV4_ADDR_ADD) {
		k_sem_give(&ip_sem);
	}
}

static int apply_power_save(void)
{
#if defined(CONFIG_PLANT_NET_WIFI_PS_NONE)
	const wifi_ps_type_t mode = WIFI_PS_NONE;
#elif defined(CONFIG_PLANT_NET_WIFI_PS_MAX_MODEM)
	const wifi_ps_type_t mode = WIFI_PS_MAX_MODEM;
#else
	const wifi_ps_type_t mode = WIFI_PS_MIN_MODEM;
#endif
	esp_err_t err = esp_wifi_set_ps(mode);

	if (err != ESP_OK) {
		LOG_ERR("esp_wifi_set_ps failed (%d)", (int)err);
		return -EIO;
	}
	return 0;
}

int plant_net_init(void)
{
	if (initialised) {
		return 0;
	}
	net_mgmt_init_event_callback(&ip_cb, ip_event_handler, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ip_cb);
	initialised = true;
	return 0;
}

int plant_wifi_connect(const char *ssid, const char *psk, int32_t timeout_ms)
{
	struct net_if *iface = net_if_get_first_wifi();
	struct wifi_connect_req_params params = {0};
	int ret;

	if (!initialised) {
		return -EINVAL;
	}
	ret = plant_wifi_args_check(ssid, psk);
	if (ret < 0) {
		return ret;
	}
	if (timeout_ms <= 0) {
		return -EINVAL;
	}
	if (iface == NULL) {
		return -ENODEV;
	}

	ret = net_if_up(iface);
	if (ret < 0 && ret != -EALREADY) {
		return ret;
	}

	k_sem_reset(&ip_sem);

	params.ssid = (const uint8_t *)ssid;
	params.ssid_length = strlen(ssid);
	params.psk = (const uint8_t *)psk;
	params.psk_length = strlen(psk);
	params.security = WIFI_SECURITY_TYPE_PSK;
	params.band = WIFI_FREQ_BAND_UNKNOWN;
	params.channel = WIFI_CHANNEL_ANY;
	params.mfp = WIFI_MFP_OPTIONAL;
	params.timeout = SYS_FOREVER_MS;

	ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
	if (ret < 0) {
		LOG_ERR("connect request failed (%d)", ret);
		return ret;
	}

	/* The ESP32 driver starts DHCP itself on association. */
	if (k_sem_take(&ip_sem, K_MSEC(timeout_ms)) != 0) {
		(void)net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
		return -ETIMEDOUT;
	}

	return apply_power_save();
}

int plant_wifi_disconnect(void)
{
	struct net_if *iface = net_if_get_first_wifi();
	int ret;

	if (iface == NULL) {
		return -ENODEV;
	}
	ret = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
	if (ret < 0) {
		return ret;
	}
	ret = net_if_down(iface);
	return (ret == -EALREADY) ? 0 : ret;
}

int plant_wifi_rssi(int16_t *rssi_dbm)
{
	struct net_if *iface = net_if_get_first_wifi();
	struct wifi_iface_status status = {0};
	int ret;

	if (rssi_dbm == NULL) {
		return -EINVAL;
	}
	if (iface == NULL) {
		return -ENODEV;
	}
	ret = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status, sizeof(status));
	if (ret < 0) {
		return ret;
	}
	if (status.state < WIFI_STATE_ASSOCIATED) {
		return -ENOTCONN;
	}
	*rssi_dbm = (int16_t)status.rssi;
	return 0;
}
