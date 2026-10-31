#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include "net_util.h"

static int failures;

#define CHECK(cond)                                                          \
	do {                                                                 \
		if (!(cond)) {                                               \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++;                                          \
		}                                                            \
	} while (0)

int main(void)
{
	/* SNTP fraction is in units of 2^-32 s. */
	CHECK(plant_sntp_epoch_ms(1700000000ULL, 0) == 1700000000000LL);
	CHECK(plant_sntp_epoch_ms(1, 0x80000000u) == 1500);
	CHECK(plant_sntp_epoch_ms(0, 0xFFFFFFFFu) == 999);
	CHECK(plant_sntp_epoch_ms(1, 0x40000000u) == 1250);

	/* Credential limits: SSID 1..32 bytes, PSK 8..64 bytes (open networks are not supported). */
	CHECK(plant_wifi_credentials_valid("net", "password") == 0);
	CHECK(plant_wifi_credentials_valid("", "password") == -EINVAL);
	CHECK(plant_wifi_credentials_valid("net", "short") == -EINVAL);
	CHECK(plant_wifi_credentials_valid(NULL, "password") == -EINVAL);
	CHECK(plant_wifi_credentials_valid("net", NULL) == -EINVAL);
	CHECK(plant_wifi_credentials_valid("012345678901234567890123456789012", "password") == -EINVAL);
	CHECK(plant_wifi_credentials_valid("012345678901234567890123456789ab", "password") == 0);

	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("test_net_util: all passed\n");
	return 0;
}
