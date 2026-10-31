#ifndef PLANT_JSON_H
#define PLANT_JSON_H

#include <stddef.h>
#include <stdint.h>
#include <plant/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLANT_JSON_RAW_MAX     256
#define PLANT_JSON_STATUS_MAX  256

/* Encode the sensors/raw payload (FR-MA-10). timestamp_ms may be NULL → null.
 * Returns the length written (without the NUL), or -ENOMEM if buf is too small. */
int plant_json_encode_raw(const struct plant_sensor_values *v,
			  const int64_t *timestamp_ms,
			  char *buf, size_t len);

/* Encode the status payload (FR-MA-10).
 * Returns the length written (without the NUL), or -ENOMEM. */
int plant_json_encode_status(const struct plant_status *s,
			     char *buf, size_t len);

#ifdef __cplusplus
}
#endif
#endif
