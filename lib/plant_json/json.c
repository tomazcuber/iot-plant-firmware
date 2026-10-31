#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <plant/json.h>

/* Append-only writer over a caller buffer. Once a write does not fit, `ok`
 * stays false and later writes are skipped, so the first failure is final.
 */
struct writer {
	char *buf;
	size_t len;
	size_t pos;
	bool ok;
	bool first;
};

static void put(struct writer *w, const char *fmt, ...)
{
	va_list ap;
	int n;

	if (!w->ok) {
		return;
	}
	va_start(ap, fmt);
	n = vsnprintf(w->buf + w->pos, w->len - w->pos, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= w->len - w->pos) {
		w->ok = false;
		return;
	}
	w->pos += (size_t)n;
}

static struct writer writer_begin(char *buf, size_t len)
{
	struct writer w = { .buf = buf, .len = len, .pos = 0, .ok = len > 0, .first = true };

	put(&w, "{");
	return w;
}

static int writer_finish(struct writer *w)
{
	put(w, "}");
	return w->ok ? (int)w->pos : -ENOMEM;
}

/* Start a member: the separating comma (except for the first) and the key. */
static void put_key(struct writer *w, const char *key)
{
	put(w, "%s\"%s\":", w->first ? "" : ",", key);
	w->first = false;
}

static void put_null(struct writer *w, const char *key)
{
	put_key(w, key);
	put(w, "null");
}

static void put_float(struct writer *w, const char *key, bool valid, float val)
{
	if (!valid || !isfinite(val)) {
		put_null(w, key);
		return;
	}
	put_key(w, key);
	put(w, "%.1f", (double)val);
}

/* Every integer field in the payloads fits in int64_t. */
static void put_int(struct writer *w, const char *key, bool valid, int64_t val)
{
	if (!valid) {
		put_null(w, key);
		return;
	}
	put_key(w, key);
	put(w, "%lld", (long long)val);
}

/* ADR-0002 wire strings. An unknown value is encoded as null rather than
 * guessed, so a bad enum never reads as a real wake cause.
 */
static const char *wake_reason_str(enum plant_wake_reason r)
{
	switch (r) {
	case PLANT_WAKE_TIMER:
		return "timer";
	case PLANT_WAKE_SOIL_MOISTURE_ALERT:
		return "soil_moisture_alert";
	}
	return NULL;
}

static void put_wake_reason(struct writer *w, const char *key, bool present,
			    enum plant_wake_reason r)
{
	const char *str = wake_reason_str(r);

	if (!present || str == NULL) {
		put_null(w, key);
		return;
	}
	put_key(w, key);
	put(w, "\"%s\"", str);
}

int plant_json_encode_raw(const struct plant_sensor_values *v,
			  const int64_t *timestamp_ms,
			  char *buf, size_t len)
{
	struct writer w = writer_begin(buf, len);

	put_float(&w, "temperature_c", v->valid & PLANT_VALID_TEMPERATURE_C, v->temperature_c);
	put_float(&w, "bmp280_temperature_c", v->valid & PLANT_VALID_BMP280_TEMPERATURE_C,
		  v->bmp280_temperature_c);
	put_float(&w, "humidity_pct", v->valid & PLANT_VALID_HUMIDITY_PCT, v->humidity_pct);
	put_float(&w, "pressure_hpa", v->valid & PLANT_VALID_PRESSURE_HPA, v->pressure_hpa);
	put_float(&w, "lux", v->valid & PLANT_VALID_LUX, v->lux);
	put_float(&w, "soil_moisture_pct", v->valid & PLANT_VALID_SOIL_MOISTURE_PCT,
		  v->soil_moisture_pct);
	put_int(&w, "soil_moisture_raw_adc", v->valid & PLANT_VALID_SOIL_MOISTURE_RAW_ADC,
		v->soil_moisture_raw_adc);
	put_int(&w, "timestamp_ms", timestamp_ms != NULL, timestamp_ms ? *timestamp_ms : 0);
	return writer_finish(&w);
}

int plant_json_encode_status(const struct plant_status *s, char *buf, size_t len)
{
	struct writer w = writer_begin(buf, len);

	put_wake_reason(&w, "wake_reason", s->present & PLANT_STATUS_WAKE_REASON, s->wake_reason);
	put_int(&w, "t_active_ms", s->present & PLANT_STATUS_T_ACTIVE_MS, s->t_active_ms);
	put_int(&w, "t_sensor_ms", s->present & PLANT_STATUS_T_SENSOR_MS, s->t_sensor_ms);
	put_int(&w, "t_wifi_ms", s->present & PLANT_STATUS_T_WIFI_MS, s->t_wifi_ms);
	put_int(&w, "t_ble_ms", s->present & PLANT_STATUS_T_BLE_MS, s->t_ble_ms);
	put_int(&w, "t_sleep_ms", s->present & PLANT_STATUS_T_SLEEP_MS, s->t_sleep_ms);
	put_int(&w, "wifi_rssi_dbm", s->present & PLANT_STATUS_WIFI_RSSI_DBM, s->wifi_rssi_dbm);
	put_int(&w, "cycle_count", s->present & PLANT_STATUS_CYCLE_COUNT, s->cycle_count);
	put_int(&w, "timestamp_ms", s->present & PLANT_STATUS_TIMESTAMP_MS, s->timestamp_ms);
	return writer_finish(&w);
}
