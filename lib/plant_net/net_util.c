#include <errno.h>
#include <string.h>

#include "net_util.h"

int64_t plant_sntp_epoch_ms(uint64_t seconds, uint32_t fraction)
{
	return (int64_t)(seconds * 1000u) + (int64_t)(((uint64_t)fraction * 1000u) >> 32);
}

int plant_wifi_args_check(const char *ssid, const char *psk)
{
	if (ssid == NULL || psk == NULL) {
		return -EINVAL;
	}
	size_t sl = strlen(ssid);
	size_t pl = strlen(psk);

	if (sl < 1 || sl > 32 || pl < 8 || pl > 64) {
		return -EINVAL;
	}
	return 0;
}
