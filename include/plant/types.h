#ifndef PLANT_TYPES_H
#define PLANT_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Validity bits for struct plant_sensor_values.valid */
#define PLANT_VALID_TEMPERATURE_C          (1u << 0)  /* HDC1080 */
#define PLANT_VALID_HUMIDITY_PCT           (1u << 1)  /* HDC1080 */
#define PLANT_VALID_BMP280_TEMPERATURE_C   (1u << 2)  /* BMP280 */
#define PLANT_VALID_PRESSURE_HPA           (1u << 3)  /* BMP280 */
#define PLANT_VALID_LUX                    (1u << 4)  /* BH1750FVI */
#define PLANT_VALID_SOIL_MOISTURE_PCT      (1u << 5)  /* soil probe */
#define PLANT_VALID_SOIL_MOISTURE_RAW_ADC  (1u << 6)  /* soil probe */

/* One sensor phase. A field is meaningful only when its bit is set in `valid`. */
struct plant_sensor_values {
	float temperature_c;
	float humidity_pct;
	float bmp280_temperature_c;
	float pressure_hpa;
	float lux;
	float soil_moisture_pct;
	uint16_t soil_moisture_raw_adc;
	uint8_t valid;
};

enum plant_wake_reason {
	PLANT_WAKE_TIMER,                /* "timer" */
	PLANT_WAKE_SOIL_MOISTURE_ALERT,  /* "soil_moisture_alert" */
};

/* Presence bits for struct plant_status.present */
#define PLANT_STATUS_WAKE_REASON    (1u << 0)
#define PLANT_STATUS_T_ACTIVE_MS    (1u << 1)
#define PLANT_STATUS_T_SENSOR_MS    (1u << 2)
#define PLANT_STATUS_T_WIFI_MS      (1u << 3)
#define PLANT_STATUS_T_BLE_MS       (1u << 4)
#define PLANT_STATUS_T_SLEEP_MS     (1u << 5)
#define PLANT_STATUS_WIFI_RSSI_DBM  (1u << 6)
#define PLANT_STATUS_CYCLE_COUNT    (1u << 7)
#define PLANT_STATUS_TIMESTAMP_MS   (1u << 8)

/* One `status` payload. A field absent from `present` is encoded as JSON null. */
struct plant_status {
	enum plant_wake_reason wake_reason;
	uint32_t t_active_ms;
	uint32_t t_sensor_ms;
	uint32_t t_wifi_ms;
	uint32_t t_ble_ms;
	uint32_t t_sleep_ms;
	int16_t wifi_rssi_dbm;
	uint32_t cycle_count;
	int64_t timestamp_ms;
	uint16_t present;
};

#ifdef __cplusplus
}
#endif
#endif
