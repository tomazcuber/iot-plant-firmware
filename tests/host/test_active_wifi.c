/* Host tests for comparison baseline A's orchestration (apps/active-wifi):
 * skipped-slot accounting and connection recovery (FR-AW-04, issue #20). */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <plant/net.h>

#include "link.h"
#include "slot.h"

static int failures;

#define CHECK(cond)                                                          \
	do {                                                                 \
		if (!(cond)) {                                               \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++;                                          \
		}                                                            \
	} while (0)

/* Fake plant_net (the seam under test is <plant/net.h>). Each call appends a
 * letter to `calls`: R rssi, W wifi connect, S sntp, D mqtt disconnect,
 * M mqtt connect. */
static char calls[64];
static int rssi_ret, wifi_ret, sntp_ret, mqtt_ret;
static int64_t sntp_epoch_ms, uptime_ms;
static const char *last_ssid, *last_psk, *last_broker, *last_client_id;

static void log_call(char c)
{
	size_t n = strlen(calls);

	if (n + 1 < sizeof(calls)) {
		calls[n] = c;
		calls[n + 1] = '\0';
	}
}

int plant_wifi_rssi(int16_t *rssi_dbm)
{
	log_call('R');
	if (rssi_ret == 0) {
		*rssi_dbm = -55;
	}
	return rssi_ret;
}

int plant_wifi_connect(const char *ssid, const char *psk, int32_t timeout_ms)
{
	(void)timeout_ms;
	log_call('W');
	last_ssid = ssid;
	last_psk = psk;
	return wifi_ret;
}

int plant_sntp_sync(int32_t timeout_ms, int64_t *epoch_ms)
{
	(void)timeout_ms;
	log_call('S');
	if (sntp_ret == 0) {
		*epoch_ms = sntp_epoch_ms;
	}
	return sntp_ret;
}

int plant_mqtt_disconnect(void)
{
	log_call('D');
	return -ENOTCONN;
}

int plant_mqtt_connect(const char *broker_host, const char *client_id, int32_t timeout_ms)
{
	(void)timeout_ms;
	log_call('M');
	last_broker = broker_host;
	last_client_id = client_id;
	return mqtt_ret;
}

int64_t link_uptime_ms(void)
{
	return uptime_ms;
}

static const struct link_config cfg = {
	.ssid = "lab-ap",
	.psk = "password",
	.broker = "10.0.0.2",
	.client_id = "baseline-a",
	.wifi_timeout_ms = 30000,
	.mqtt_timeout_ms = 10000,
	.sntp_timeout_ms = 10000,
};

static struct link fresh_boot(void)
{
	struct link l;

	calls[0] = '\0';
	rssi_ret = -ENOTCONN;
	wifi_ret = 0;
	sntp_ret = 0;
	mqtt_ret = 0;
	sntp_epoch_ms = 0;
	uptime_ms = 0;
	link_init(&l, &cfg);
	return l;
}

/* Boot: the first attempt associates without probing, syncs SNTP right after
 * the association, then connects MQTT with the configured credentials. */
static void test_boot_connects_wifi_then_sntp_then_mqtt(void)
{
	struct link l = fresh_boot();

	CHECK(link_try_up(&l) == 0);
	CHECK(strcmp(calls, "WSDM") == 0);
	CHECK(strcmp(last_ssid, "lab-ap") == 0);
	CHECK(strcmp(last_psk, "password") == 0);
	CHECK(strcmp(last_broker, "10.0.0.2") == 0);
	CHECK(strcmp(last_client_id, "baseline-a") == 0);
}

/* MQTT drop with WiFi still associated: probe, then reconnect MQTT only. */
static void test_mqtt_drop_reconnects_mqtt_only(void)
{
	struct link l = fresh_boot();

	CHECK(link_try_up(&l) == 0);
	calls[0] = '\0';
	rssi_ret = 0;
	CHECK(link_try_up(&l) == 0);
	CHECK(strcmp(calls, "RDM") == 0);
}

/* WiFi drop: rssi says -ENOTCONN, so reassociate first, then MQTT. Later
 * reconnects never re-sync SNTP. */
static void test_wifi_drop_reassociates_then_mqtt_without_sntp(void)
{
	struct link l = fresh_boot();

	CHECK(link_try_up(&l) == 0);
	calls[0] = '\0';
	rssi_ret = -ENOTCONN;
	CHECK(link_try_up(&l) == 0);
	CHECK(strcmp(calls, "RWDM") == 0);
}

/* Any rssi result other than -ENOTCONN means only MQTT is down. */
static void test_other_rssi_error_reconnects_mqtt_only(void)
{
	struct link l = fresh_boot();

	CHECK(link_try_up(&l) == 0);
	calls[0] = '\0';
	rssi_ret = -EIO;
	CHECK(link_try_up(&l) == 0);
	CHECK(strcmp(calls, "RDM") == 0);
}

/* A failed reassociation stops the attempt, and the next attempt probes again. */
static void test_failed_reassociation_is_retried(void)
{
	struct link l = fresh_boot();

	CHECK(link_try_up(&l) == 0);
	calls[0] = '\0';
	rssi_ret = -ENOTCONN;
	wifi_ret = -ETIMEDOUT;
	CHECK(link_try_up(&l) == -ETIMEDOUT);
	wifi_ret = 0;
	CHECK(link_try_up(&l) == 0);
	CHECK(strcmp(calls, "RWRWDM") == 0);
}

/* Boot-time failure: retries follow the same rules, and the single SNTP
 * attempt happens right after the first successful association. */
static void test_boot_failures_retry_and_sntp_follows_first_association(void)
{
	struct link l = fresh_boot();

	wifi_ret = -ETIMEDOUT;
	CHECK(link_try_up(&l) == -ETIMEDOUT);
	CHECK(link_try_up(&l) == -ETIMEDOUT);
	CHECK(strcmp(calls, "WW") == 0);

	calls[0] = '\0';
	wifi_ret = 0;
	mqtt_ret = -ECONNREFUSED;
	CHECK(link_try_up(&l) == -ECONNREFUSED);
	CHECK(strcmp(calls, "WSDM") == 0);

	/* WiFi stayed up: only MQTT is retried. */
	calls[0] = '\0';
	rssi_ret = 0;
	CHECK(link_try_up(&l) == -ECONNREFUSED);
	mqtt_ret = 0;
	CHECK(link_try_up(&l) == 0);
	CHECK(strcmp(calls, "RDMRDM") == 0);
}

/* Wall clock = boot epoch + uptime, with the epoch taken at the sync. */
static void test_wall_clock_follows_the_boot_sync(void)
{
	struct link l = fresh_boot();
	int64_t wall_ms = 0;

	CHECK(!link_wall_clock_ms(&l, 0, &wall_ms));

	sntp_epoch_ms = 1700000000000LL;
	uptime_ms = 5000; /* the sync returned 5 s after boot */
	CHECK(link_try_up(&l) == 0);
	CHECK(link_wall_clock_ms(&l, 65000, &wall_ms));
	CHECK(wall_ms == 1700000060000LL);
}

/* A failed sync leaves timestamp_ms null until the next boot. */
static void test_wall_clock_absent_after_failed_sync(void)
{
	struct link l = fresh_boot();
	int64_t wall_ms = 0;

	sntp_ret = -ETIMEDOUT;
	CHECK(link_try_up(&l) == 0);
	CHECK(!link_wall_clock_ms(&l, 65000, &wall_ms));
}

/* A failed SNTP attempt still uses up the boot's only attempt. */
static void test_failed_sntp_is_not_retried(void)
{
	struct link l = fresh_boot();

	sntp_ret = -ETIMEDOUT;
	CHECK(link_try_up(&l) == 0);
	calls[0] = '\0';
	rssi_ret = -ENOTCONN;
	CHECK(link_try_up(&l) == 0);
	CHECK(strcmp(calls, "RWDM") == 0);
}

/* Grid: t0 = 1000 ms, interval 60 s, so slot k is due at 1000 + 60000 k. */
static void test_slot_next(void)
{
	/* On time: the slot after the one just published. */
	CHECK(slot_next(1000, 60000, 1, 1000 + 2500) == 1);
	/* Due exactly now is not late. */
	CHECK(slot_next(1000, 60000, 1, 61000) == 1);
	/* 1 ms past due is late: skip to slot 2, publish it at 121000. */
	CHECK(slot_next(1000, 60000, 1, 61001) == 2);
	/* An outage from slot 5 to 2.5 intervals later skips slots 5..7,
	 * so cycle_count jumps from 4 to 8: the gap equals the outage. */
	CHECK(slot_next(1000, 60000, 5, 1000 + 7 * 60000 + 30000) == 8);
	/* A reconnect that finishes before the pending slot is due keeps it. */
	CHECK(slot_next(1000, 60000, 5, 1000 + 4 * 60000 + 59999) == 5);
}

int main(void)
{
	test_slot_next();
	test_boot_connects_wifi_then_sntp_then_mqtt();
	test_mqtt_drop_reconnects_mqtt_only();
	test_wifi_drop_reassociates_then_mqtt_without_sntp();
	test_other_rssi_error_reconnects_mqtt_only();
	test_failed_reassociation_is_retried();
	test_boot_failures_retry_and_sntp_follows_first_association();
	test_failed_sntp_is_not_retried();
	test_wall_clock_follows_the_boot_sync();
	test_wall_clock_absent_after_failed_sync();

	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("test_active_wifi: all passed\n");
	return 0;
}
