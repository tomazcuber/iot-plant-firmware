/* Private, Zephyr-free helpers for plant_net (host-testable, see tests/host). */
#ifndef PLANT_NET_UTIL_H
#define PLANT_NET_UTIL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SNTP seconds + 2^-32 fraction (Unix epoch) -> milliseconds, fraction truncated. */
int64_t plant_sntp_epoch_ms(uint64_t seconds, uint32_t fraction);

/* Validate WiFi credentials: SSID 1..32 bytes, PSK 8..64 bytes.
 * Returns 0 or -EINVAL. */
int plant_wifi_args_check(const char *ssid, const char *psk);

#ifdef __cplusplus
}
#endif
#endif
