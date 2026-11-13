#ifndef SMART_BABY_SOFT_I2C_H
#define SMART_BABY_SOFT_I2C_H

#include "stm32f4xx_hal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    GPIO_TypeDef *port;
    uint16_t scl_pin;
    uint16_t sda_pin;
} SoftI2C;

/* Open-drain bit-banged I2C, matching the Spark-1 soft-I2C buses:
 *   I2C2: SCL PF1, SDA PF0 (AP3216C, ICM20608, ES8388)
 *   I2C3: SCL PE0, SDA PE1 (AHT10/AHT21)
 */
void soft_i2c_init(SoftI2C *bus, GPIO_TypeDef *port, uint16_t scl_pin, uint16_t sda_pin);
bool soft_i2c_probe(SoftI2C *bus, uint8_t address);
bool soft_i2c_write(SoftI2C *bus, uint8_t address, const uint8_t *data, size_t length);
bool soft_i2c_read(SoftI2C *bus, uint8_t address, uint8_t *data, size_t length);
bool soft_i2c_write_read(SoftI2C *bus, uint8_t address,
                         const uint8_t *tx, size_t tx_length,
                         uint8_t *rx, size_t rx_length);

#endif
