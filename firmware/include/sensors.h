#ifndef SMART_BABY_SENSORS_H
#define SMART_BABY_SENSORS_H

#include "soft_i2c.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool aht10_present;
    bool ap3216c_present;
    bool icm20608_present;
    bool temperature_valid;
    bool humidity_valid;
    bool light_valid;
    bool proximity_valid;
    bool motion_valid;
    int32_t temperature_c_x10;
    int32_t humidity_pct_x10;
    int32_t light_lux_x10;
    uint16_t proximity;
    int16_t accel_raw[3];
    int16_t gyro_raw[3];
} SensorSnapshot;

void sensors_init(void);
void sensors_read(SensorSnapshot *snapshot);
bool sensors_ready(void);
SoftI2C *sensors_i2c2(void);
bool sensors_bus_lock(uint32_t timeout_ms);
void sensors_bus_unlock(void);

#endif
