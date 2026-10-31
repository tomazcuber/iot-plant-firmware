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

static void put_float(struct writer *w, const char *key, bool valid, float val)
{
	if (valid && isfinite(val)) {
		put(w, "\"%s\":%.1f", key, (double)val);
	} else {
		put(w, "\"%s\":null", key);
	}
}

static void put_uint(struct writer *w, const char *key, bool valid, uint32_t val)
{
	if (valid) {
		put(w, "\"%s\":%u", key, (unsigned int)val);
	} else {
		put(w, "\"%s\":null", key);
	}
}

static void put_int64(struct writer *w, const char *key, bool valid, int64_t val)
{
	if (valid) {
		put(w, "\"%s\":%lld", key, (long long)val);
	} else {
		put(w, "\"%s\":null", key);
	}
}

static int finish(const struct writer *w)
{
	return w->ok ? (int)w->pos : -ENOMEM;
}

static struct writer begin(char *buf, size_t len)
{
	struct writer w = { .buf = buf, .len = len, .pos = 0, .ok = len > 0 };

	put(&w, "{");
	return w;
}

int plant_json_encode_raw(const struct plant_sensor_values *v,
			  const int64_t *timestamp_ms,
			  char *buf, size_t len)
{
	struct writer w = begin(buf, len);

	put_float(&w, "temperature_c", v->valid & PLANT_VALID_TEMPERATURE_C, v->temperature_c);
	put(&w, ",");
	put_float(&w, "bmp280_temperature_c", v->valid & PLANT_VALID_BMP280_TEMPERATURE_C,
		  v->bmp280_temperature_c);
	put(&w, ",");
	put_float(&w, "humidity_pct", v->valid & PLANT_VALID_HUMIDITY_PCT, v->humidity_pct);
	put(&w, ",");
	put_float(&w, "pressure_hpa", v->valid & PLANT_VALID_PRESSURE_HPA, v->pressure_hpa);
	put(&w, ",");
	put_float(&w, "lux", v->valid & PLANT_VALID_LUX, v->lux);
	put(&w, ",");
	put_float(&w, "soil_moisture_pct", v->valid & PLANT_VALID_SOIL_MOISTURE_PCT,
		  v->soil_moisture_pct);
	put(&w, ",");
	put_uint(&w, "soil_moisture_raw_adc", v->valid & PLANT_VALID_SOIL_MOISTURE_RAW_ADC,
		 v->soil_moisture_raw_adc);
	put(&w, ",");
	put_int64(&w, "timestamp_ms", timestamp_ms != NULL, timestamp_ms ? *timestamp_ms : 0);
	put(&w, "}");
	return finish(&w);
}

int plant_json_encode_status(const struct plant_status *s, char *buf, size_t len)
{
	struct writer w = begin(buf, len);

	if (s->present & PLANT_STATUS_WAKE_REASON) {
		put(&w, "\"wake_reason\":\"%s\"",
		    s->wake_reason == PLANT_WAKE_SOIL_MOISTURE_ALERT ? "soil_moisture_alert"
								      : "timer");
	} else {
		put(&w, "\"wake_reason\":null");
	}
	put(&w, ",");
	put_uint(&w, "t_active_ms", s->present & PLANT_STATUS_T_ACTIVE_MS, s->t_active_ms);
	put(&w, ",");
	put_uint(&w, "t_sensor_ms", s->present & PLANT_STATUS_T_SENSOR_MS, s->t_sensor_ms);
	put(&w, ",");
	put_uint(&w, "t_wifi_ms", s->present & PLANT_STATUS_T_WIFI_MS, s->t_wifi_ms);
	put(&w, ",");
	put_uint(&w, "t_ble_ms", s->present & PLANT_STATUS_T_BLE_MS, s->t_ble_ms);
	put(&w, ",");
	put_uint(&w, "t_sleep_ms", s->present & PLANT_STATUS_T_SLEEP_MS, s->t_sleep_ms);
	put(&w, ",");
	if (s->present & PLANT_STATUS_WIFI_RSSI_DBM) {
		put(&w, "\"wifi_rssi_dbm\":%d", (int)s->wifi_rssi_dbm);
	} else {
		put(&w, "\"wifi_rssi_dbm\":null");
	}
	put(&w, ",");
	put_uint(&w, "cycle_count", s->present & PLANT_STATUS_CYCLE_COUNT, s->cycle_count);
	put(&w, ",");
	put_int64(&w, "timestamp_ms", s->present & PLANT_STATUS_TIMESTAMP_MS, s->timestamp_ms);
	put(&w, "}");
	return finish(&w);
}
