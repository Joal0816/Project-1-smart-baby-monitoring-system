#include "sensors.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "soft_i2c.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

/* Spark-1 soft-I2C buses, verified against the official user manual:
 *   I2C2: SCL PF1, SDA PF0, AP3216C (0x1E) and ICM20608 (0x68)
 *   I2C3: SCL PE0, SDA PE1, AHT10/AHT21 (0x38)
 */
#define AHT10_ADDRESS   0x38U
#define AP3216C_ADDRESS 0x1EU
#define ICM20608_ADDRESS 0x68U

#define AHT10_CMD_NORMAL      0xA8U
#define AHT10_CMD_CALIBRATE   0xE1U
#define AHT10_CMD_MEASURE     0xACU

#define AP3216C_REG_SYSTEM    0x00U
#define AP3216C_REG_ALS_LOW   0x0CU
#define AP3216C_REG_PS_LOW    0x0EU

#define ICM20608_REG_ACCEL    0x3BU
#define ICM20608_REG_GYRO     0x43U
#define ICM20608_REG_PWR1     0x6BU
#define ICM20608_REG_PWR2     0x6CU
#define ICM20608_REG_GYRO_CFG 0x1BU
#define ICM20608_REG_ACCEL_CFG1 0x1CU
#define ICM20608_REG_ACCEL_CFG2 0x1DU
#define ICM20608_REG_WHO_AM_I 0x75U

static SoftI2C sensor_bus;
static SoftI2C environment_bus;
static SemaphoreHandle_t bus_mutex;
static volatile bool sensors_initialized;

static int32_t round_nearest(int32_t numerator, int32_t denominator)
{
    if (numerator >= 0) {
        return (numerator + denominator / 2) / denominator;
    }
    return (numerator - denominator / 2) / denominator;
}

static void scan_bus(const char *name, SoftI2C *bus)
{
    printf("i2c %s found:", name);
    for (uint8_t address = 0x08U; address <= 0x77U; ++address) {
        if (soft_i2c_probe(bus, address)) {
            printf(" 0x%02X", address);
        }
    }
    printf("\r\n");
}

static bool ap3216c_write(uint8_t reg, uint8_t value)
{
    const uint8_t frame[2] = {reg, value};
    return soft_i2c_write(&sensor_bus, AP3216C_ADDRESS, frame, sizeof(frame));
}

static bool ap3216c_read_reg(uint8_t reg, uint8_t *value)
{
    const uint8_t reg_value = reg;
    return soft_i2c_write_read(&sensor_bus, AP3216C_ADDRESS,
                               &reg_value, 1U, value, 1U);
}

static bool icm20608_write(uint8_t reg, uint8_t value)
{
    const uint8_t frame[2] = {reg, value};
    return soft_i2c_write(&sensor_bus, ICM20608_ADDRESS, frame, sizeof(frame));
}

static bool icm20608_read(uint8_t reg, uint8_t *data, size_t length)
{
    const uint8_t reg_value = reg;
    return soft_i2c_write_read(&sensor_bus, ICM20608_ADDRESS,
                               &reg_value, 1U, data, length);
}

static bool aht10_command(const uint8_t *command, size_t length)
{
    for (uint8_t attempt = 0U; attempt < 3U; ++attempt) {
        if (soft_i2c_write(&environment_bus, AHT10_ADDRESS, command, length)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(50U));
    }
    return false;
}

static bool aht10_read(SensorSnapshot *snapshot);

static bool aht10_init(void)
{
    const uint8_t normal[3] = {AHT10_CMD_NORMAL, 0x00U, 0x00U};
    const uint8_t calibrate[3] = {AHT10_CMD_CALIBRATE, 0x08U, 0x00U};
    bool configured = true;

    vTaskDelay(pdMS_TO_TICKS(100U));
    if (!aht10_command(normal, sizeof(normal))) {
        /* The factory-calibrated AHT21 on this board NACKs the AHT10
         * normal-mode command but still measures correctly. */
        printf("aht10: normal command not acknowledged\r\n");
        configured = false;
    } else {
        vTaskDelay(pdMS_TO_TICKS(500U));
        if (!aht10_command(calibrate, sizeof(calibrate))) {
            printf("aht10: calibration command not acknowledged\r\n");
            configured = false;
        }
        vTaskDelay(pdMS_TO_TICKS(450U));
    }

    SensorSnapshot probe = {0};
    const bool measured = aht10_read(&probe);
    printf("aht10: configured=%u measured=%u\r\n",
           configured ? 1U : 0U, measured ? 1U : 0U);
    return measured;
}

static bool aht10_read(SensorSnapshot *snapshot)
{
    const uint8_t measure[3] = {AHT10_CMD_MEASURE, 0x33U, 0x00U};
    uint8_t data[6] = {0};

    if (!soft_i2c_write(&environment_bus, AHT10_ADDRESS, measure, sizeof(measure))) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(80U));
    if (!soft_i2c_read(&environment_bus, AHT10_ADDRESS, data, sizeof(data))) {
        return false;
    }
    if ((data[0] & 0x80U) != 0U || (data[0] & 0x08U) == 0U) {
        return false;
    }

    const uint32_t raw_humidity = ((uint32_t)data[1] << 12) |
                                  ((uint32_t)data[2] << 4) |
                                  ((uint32_t)data[3] >> 4);
    const uint32_t raw_temperature = (((uint32_t)data[3] & 0x0FU) << 16) |
                                     ((uint32_t)data[4] << 8) |
                                     (uint32_t)data[5];
    snapshot->humidity_pct_x10 =
        (int32_t)(((uint64_t)raw_humidity * 1000U) / (1U << 20));
    snapshot->temperature_c_x10 =
        (int32_t)(((uint64_t)raw_temperature * 2000U) / (1U << 20)) - 500;
    snapshot->humidity_valid = true;
    snapshot->temperature_valid = true;
    return true;
}

static bool ap3216c_init(void)
{
    if (!ap3216c_write(AP3216C_REG_SYSTEM, 0x04U)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100U));
    if (!ap3216c_write(AP3216C_REG_SYSTEM, 0x03U)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100U));
    return true;
}

static bool ap3216c_read(SensorSnapshot *snapshot)
{
    uint8_t als_low = 0U;
    uint8_t als_high = 0U;
    uint8_t ps_low = 0U;
    uint8_t ps_high = 0U;

    if (!ap3216c_read_reg(AP3216C_REG_ALS_LOW, &als_low) ||
        !ap3216c_read_reg((uint8_t)(AP3216C_REG_ALS_LOW + 1U), &als_high) ||
        !ap3216c_read_reg(AP3216C_REG_PS_LOW, &ps_low) ||
        !ap3216c_read_reg((uint8_t)(AP3216C_REG_PS_LOW + 1U), &ps_high)) {
        return false;
    }

    const uint16_t raw_light = (uint16_t)als_low | ((uint16_t)als_high << 8);
    const uint16_t raw_ps = (uint16_t)ps_low | ((uint16_t)ps_high << 8);

    /* Default ALS range is 0.35 lux/count. Proximity uses the low 4 bits
     * of the first byte and the low 6 bits of the second byte. */
    snapshot->light_lux_x10 =
        round_nearest((int32_t)raw_light * 35, 10);
    snapshot->proximity = (uint16_t)((raw_ps & 0x000FU) |
                                     (((raw_ps >> 8) & 0x003FU) << 4));
    snapshot->light_valid = true;
    snapshot->proximity_valid = true;
    return true;
}

static bool icm20608_init(void)
{
    uint8_t identity = 0U;

    if (!icm20608_read(ICM20608_REG_WHO_AM_I, &identity, 1U)) {
        return false;
    }
    /* The BSP does not gate init on WHO_AM_I; this board reports 0xAF
     * rather than the ICM20608 datasheet value 0xAE. */
    printf("icm20608: WHO_AM_I=0x%02X\r\n", identity);

    if (!icm20608_write(ICM20608_REG_PWR1, 0x80U)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100U));
    /* Reset leaves the device asleep; explicitly wake it before sampling. */
    if (!icm20608_write(ICM20608_REG_PWR1, 0x00U)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10U));
    if (!icm20608_write(ICM20608_REG_PWR2, 0x00U) ||
        !icm20608_write(ICM20608_REG_GYRO_CFG, 0x00U) ||
        !icm20608_write(ICM20608_REG_ACCEL_CFG1, 0x00U) ||
        !icm20608_write(ICM20608_REG_ACCEL_CFG2, 0x01U)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10U));

    uint8_t pwr1 = 0U;
    uint8_t accel_config = 0U;
    (void)icm20608_read(ICM20608_REG_PWR1, &pwr1, 1U);
    (void)icm20608_read(ICM20608_REG_ACCEL_CFG1, &accel_config, 1U);
    printf("icm20608: PWR1=0x%02X ACCEL_CFG1=0x%02X\r\n", pwr1, accel_config);
    return true;
}

static bool icm20608_read_motion(SensorSnapshot *snapshot)
{
    uint8_t accel[6] = {0};
    uint8_t gyro[6] = {0};

    if (!icm20608_read(ICM20608_REG_ACCEL, accel, sizeof(accel)) ||
        !icm20608_read(ICM20608_REG_GYRO, gyro, sizeof(gyro))) {
        return false;
    }

    for (size_t axis = 0U; axis < 3U; ++axis) {
        snapshot->accel_raw[axis] =
            (int16_t)(((uint16_t)accel[axis * 2U] << 8) | accel[axis * 2U + 1U]);
        snapshot->gyro_raw[axis] =
            (int16_t)(((uint16_t)gyro[axis * 2U] << 8) | gyro[axis * 2U + 1U]);
    }
    snapshot->motion_valid = true;
    return true;
}

bool sensors_ready(void)
{
    return sensors_initialized;
}

SoftI2C *sensors_i2c2(void)
{
    return &sensor_bus;
}

bool sensors_bus_lock(uint32_t timeout_ms)
{
    return bus_mutex != NULL &&
           xSemaphoreTake(bus_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void sensors_bus_unlock(void)
{
    if (bus_mutex != NULL) {
        (void)xSemaphoreGive(bus_mutex);
    }
}

void sensors_init(void)
{
    bus_mutex = xSemaphoreCreateMutex();
    configASSERT(bus_mutex != NULL);
    soft_i2c_init(&sensor_bus, GPIOF, GPIO_PIN_1, GPIO_PIN_0);
    soft_i2c_init(&environment_bus, GPIOE, GPIO_PIN_0, GPIO_PIN_1);

    scan_bus("PF1/PF0", &sensor_bus);
    scan_bus("PE0/PE1", &environment_bus);

    const bool aht10_present = soft_i2c_probe(&environment_bus, AHT10_ADDRESS);
    const bool ap3216c_present = soft_i2c_probe(&sensor_bus, AP3216C_ADDRESS);
    const bool icm20608_present = soft_i2c_probe(&sensor_bus, ICM20608_ADDRESS);
    const bool aht10_ready = aht10_present && aht10_init();
    const bool ap3216c_ready = ap3216c_present && ap3216c_init();
    const bool icm20608_ready = icm20608_present && icm20608_init();

    printf("sensors: aht10=%u/%u ap3216c=%u/%u icm20608=%u/%u (found/ready)\r\n",
           aht10_present ? 1U : 0U, aht10_ready ? 1U : 0U,
           ap3216c_present ? 1U : 0U, ap3216c_ready ? 1U : 0U,
           icm20608_present ? 1U : 0U, icm20608_ready ? 1U : 0U);
    sensors_initialized = true;
}

void sensors_read(SensorSnapshot *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    if (!sensors_bus_lock(200U)) {
        printf("sensors: bus lock timeout\r\n");
        return;
    }

    snapshot->aht10_present = soft_i2c_probe(&environment_bus, AHT10_ADDRESS);
    snapshot->ap3216c_present = soft_i2c_probe(&sensor_bus, AP3216C_ADDRESS);
    snapshot->icm20608_present = soft_i2c_probe(&sensor_bus, ICM20608_ADDRESS);

    if (snapshot->aht10_present) {
        (void)aht10_read(snapshot);
    }
    if (snapshot->ap3216c_present) {
        (void)ap3216c_read(snapshot);
    }
    if (snapshot->icm20608_present) {
        (void)icm20608_read_motion(snapshot);
    }

    printf("env: temp=%ld.%ldC rh=%ld.%ld%% light=%ld.%ldlux ps=%u%s\r\n",
           (long)(snapshot->temperature_c_x10 / 10),
           (long)(snapshot->temperature_c_x10 < 0
                      ? -(snapshot->temperature_c_x10 % 10)
                      : snapshot->temperature_c_x10 % 10),
           (long)(snapshot->humidity_pct_x10 / 10),
           (long)(snapshot->humidity_pct_x10 % 10),
           (long)(snapshot->light_lux_x10 / 10),
           (long)(snapshot->light_lux_x10 % 10),
           (unsigned)snapshot->proximity,
           (snapshot->temperature_valid && snapshot->humidity_valid &&
            snapshot->light_valid && snapshot->proximity_valid)
               ? ""
               : " (fault)");
    printf("motion: accel=%d/%d/%d gyro=%d/%d/%d%s\r\n",
           snapshot->accel_raw[0], snapshot->accel_raw[1], snapshot->accel_raw[2],
           snapshot->gyro_raw[0], snapshot->gyro_raw[1], snapshot->gyro_raw[2],
           snapshot->motion_valid ? "" : " (fault)");
    sensors_bus_unlock();
}
