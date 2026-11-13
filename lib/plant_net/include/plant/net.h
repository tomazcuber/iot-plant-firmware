/* plant_net: blocking WiFi / SNTP / MQTT primitives (SM-40, SM-41).
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

/* Resolve broker_host (IPv4 literal or DNS name), open TCP to
 * CONFIG_PLANT_NET_MQTT_PORT, send CONNECT with client_id, wait for CONNACK.
 * client_id must be shorter than 64 bytes. -ETIMEDOUT on expiry,
 * -ECONNREFUSED if the broker rejects the CONNECT. */
int plant_mqtt_connect(const char *broker_host, const char *client_id,
		       int32_t timeout_ms);

/* Publish at QoS 0 (FR-MA-11). -ENOTCONN if not connected. */
int plant_mqtt_publish(const char *topic, const char *payload, size_t len);

/* Wait up to timeout_ms for incoming packets and send a keepalive PINGREQ
 * when due (never sleeps past the due time). Returns 0, or -ENOTCONN if the
 * connection dropped. */
int plant_mqtt_poll(int32_t timeout_ms);

/* Send DISCONNECT and close the socket. */
int plant_mqtt_disconnect(void);

#ifdef __cplusplus
}
#endif
#endif
