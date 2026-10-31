#ifndef PLANT_SENSORS_SOIL_H
#define PLANT_SENSORS_SOIL_H

#include <stdint.h>

/* Module-private: soil ADC channel setup and one conversion. */
int plant_soil_init(void);
int plant_soil_read(uint16_t *raw, float *pct);

#endif
