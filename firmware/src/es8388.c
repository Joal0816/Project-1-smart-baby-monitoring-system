#include "es8388.h"
#include "FreeRTOS.h"
#include "sensors.h"
#include "soft_i2c.h"
#include "task.h"

#include <stdio.h>

/* ES8388 control registers used by the Spark-1 audio path. */
#define ES8388_ADDRESS          0x10U
#define ES8388_CONTROL1         0x00U
#define ES8388_CONTROL2         0x01U
#define ES8388_CHIPPOWER        0x02U
#define ES8388_ADCPOWER         0x03U
#define ES8388_DACPOWER         0x04U
#define ES8388_MASTERMODE       0x08U
#define ES8388_ADCCONTROL1      0x09U
#define ES8388_ADCCONTROL2      0x0AU
#define ES8388_ADCCONTROL3      0x0BU
#define ES8388_ADCCONTROL4      0x0CU
#define ES8388_ADCCONTROL5      0x0DU
#define ES8388_ADCCONTROL8      0x10U
#define ES8388_ADCCONTROL9      0x11U
#define ES8388_DACCONTROL1      0x17U
#define ES8388_DACCONTROL2      0x18U
#define ES8388_DACCONTROL3      0x19U
#define ES8388_DACCONTROL4      0x1AU
#define ES8388_DACCONTROL5      0x1BU
#define ES8388_DACCONTROL16     0x26U
#define ES8388_DACCONTROL17     0x27U
#define ES8388_DACCONTROL20     0x2AU
#define ES8388_DACCONTROL21     0x2BU
#define ES8388_DACCONTROL23     0x2DU
#define ES8388_DACCONTROL24     0x2EU
#define ES8388_DACCONTROL25     0x2FU

static bool write_reg(SoftI2C *bus, uint8_t reg, uint8_t value)
{
    const uint8_t frame[2] = {reg, value};
    return soft_i2c_write(bus, ES8388_ADDRESS, frame, sizeof(frame));
}

static bool write_sequence(SoftI2C *bus)
{
    /* Ported from the Spark-1 BSP ES8388 initialization. */
    const uint8_t sequence[][2] = {
        {ES8388_DACCONTROL3, 0x04U},
        {ES8388_CONTROL2, 0x50U},
        {ES8388_CHIPPOWER, 0x00U},
        /* MSC=0: codec is the I2S slave; the STM32 I2S3 master supplies
         * BCLK/LRCK, and I2S3ext receives the ADC data on PB4. */
        {ES8388_MASTERMODE, 0x00U},
        {ES8388_DACPOWER, 0xC0U},
        {ES8388_CONTROL1, 0x12U},
        {ES8388_DACCONTROL1, 0x18U},
        {ES8388_DACCONTROL2, 0x02U},
        {ES8388_DACCONTROL16, 0x00U},
        {ES8388_DACCONTROL17, 0x9CU},
        {ES8388_DACCONTROL20, 0x9CU},
        {ES8388_DACCONTROL21, 0x80U},
        {ES8388_DACCONTROL23, 0x00U},
        {ES8388_DACCONTROL5, 0x00U},
        {ES8388_DACCONTROL4, 0x00U},
        {ES8388_DACPOWER, 0x3CU},
        {ES8388_ADCPOWER, 0xFFU},
        {ES8388_ADCCONTROL1, 0xBBU},
        {ES8388_ADCCONTROL2, 0x00U},
        {ES8388_ADCCONTROL3, 0x00U},
        {ES8388_ADCCONTROL4, 0x0CU},
        {ES8388_ADCCONTROL5, 0x02U},
        {ES8388_ADCCONTROL8, 0x00U},
        {ES8388_ADCCONTROL9, 0x00U},
        {ES8388_ADCPOWER, 0x09U},
        {ES8388_DACCONTROL24, 0x1EU},
        {ES8388_DACCONTROL25, 0x1EU},
    };

    for (size_t index = 0U; index < sizeof(sequence) / sizeof(sequence[0]); ++index) {
        if (!write_reg(bus, sequence[index][0], sequence[index][1])) {
            printf("es8388: write failed reg=0x%02X\r\n", sequence[index][0]);
            return false;
        }
    }
    return true;
}

bool es8388_init(void)
{
    if (!sensors_bus_lock(500U)) {
        printf("es8388: bus lock timeout\r\n");
        return false;
    }

    SoftI2C *bus = sensors_i2c2();
    const bool present = soft_i2c_probe(bus, ES8388_ADDRESS);
    const bool configured = present && write_sequence(bus);
    sensors_bus_unlock();

    if (!present) {
        printf("es8388: not detected\r\n");
        return false;
    }
    printf("es8388: configured=%u\r\n", configured ? 1U : 0U);
    return configured;
}

bool es8388_start_adc(void)
{
    if (!sensors_bus_lock(500U)) {
        return false;
    }

    SoftI2C *bus = sensors_i2c2();
    const bool ok = write_reg(bus, ES8388_DACCONTROL21, 0x80U) &&
                    write_reg(bus, ES8388_ADCPOWER, 0x00U);
    sensors_bus_unlock();
    vTaskDelay(pdMS_TO_TICKS(20U));
    return ok;
}
