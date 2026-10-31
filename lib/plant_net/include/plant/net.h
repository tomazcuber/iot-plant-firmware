/* plant_net: blocking WiFi / SNTP primitives (SM-40, SM-41).
 * MQTT primitives are added by a later slice.
 * Not thread-safe: call everything from one thread. */
#ifndef PLANT_NET_H
#define PLANT_NET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Register network-management callbacks. Call once per boot. */
int plant_net_init(void);

/* Associate, wait for a DHCP lease, then apply the Kconfig power-save mode
 * (SM-16). Blocks for at most timeout_ms; -ETIMEDOUT on expiry. */
int plant_wifi_connect(const char *ssid, const char *psk, int32_t timeout_ms);

/* Disassociate and bring the interface down. */
int plant_wifi_disconnect(void);

/* Current RSSI of the associated AP. */
int plant_wifi_rssi(int16_t *rssi_dbm);

/* Query CONFIG_PLANT_NET_SNTP_SERVER. On success, *epoch_ms is wall-clock time
 * in ms since the Unix epoch at the moment of return. -ETIMEDOUT on timeout. */
int plant_sntp_sync(int32_t timeout_ms, int64_t *epoch_ms);

#ifdef __cplusplus
}
#endif
#endif
