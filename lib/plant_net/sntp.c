#include <errno.h>

#include <zephyr/net/sntp.h>

#include <plant/net.h>

#include "net_util.h"

int plant_sntp_sync(int32_t timeout_ms, int64_t *epoch_ms)
{
	struct sntp_time ts;
	int ret;

	if (epoch_ms == NULL || timeout_ms <= 0) {
		return -EINVAL;
	}

	ret = sntp_simple(CONFIG_PLANT_NET_SNTP_SERVER, (uint32_t)timeout_ms, &ts);
	if (ret < 0) {
		return ret;
	}

	*epoch_ms = plant_sntp_epoch_ms(ts.seconds, ts.fraction);
	return 0;
}
