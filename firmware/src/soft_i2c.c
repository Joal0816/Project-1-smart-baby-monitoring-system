#include "soft_i2c.h"
#include "FreeRTOS.h"

#include <stdbool.h>

#define SOFT_I2C_DELAY_US 5U

static void soft_i2c_delay(void)
{
    const uint32_t cycles = SOFT_I2C_DELAY_US * (SystemCoreClock / 1000000U);
    const uint32_t started = DWT->CYCCNT;
    while ((DWT->CYCCNT - started) < cycles) {
    }
}

static void sda_high(const SoftI2C *bus)
{
    HAL_GPIO_WritePin(bus->port, bus->sda_pin, GPIO_PIN_SET);
}

static void sda_low(const SoftI2C *bus)
{
    HAL_GPIO_WritePin(bus->port, bus->sda_pin, GPIO_PIN_RESET);
}

static void scl_high(const SoftI2C *bus)
{
    HAL_GPIO_WritePin(bus->port, bus->scl_pin, GPIO_PIN_SET);
}

static void scl_low(const SoftI2C *bus)
{
    HAL_GPIO_WritePin(bus->port, bus->scl_pin, GPIO_PIN_RESET);
}

static bool sda_read(const SoftI2C *bus)
{
    return HAL_GPIO_ReadPin(bus->port, bus->sda_pin) == GPIO_PIN_SET;
}

static void i2c_start(const SoftI2C *bus)
{
    sda_high(bus);
    scl_high(bus);
    soft_i2c_delay();
    sda_low(bus);
    soft_i2c_delay();
    scl_low(bus);
    soft_i2c_delay();
}

static void i2c_stop(const SoftI2C *bus)
{
    sda_low(bus);
    soft_i2c_delay();
    scl_high(bus);
    soft_i2c_delay();
    sda_high(bus);
    soft_i2c_delay();
}

static bool write_byte(const SoftI2C *bus, uint8_t value)
{
    for (uint8_t mask = 0x80U; mask != 0U; mask >>= 1) {
        if ((value & mask) != 0U) {
            sda_high(bus);
        } else {
            sda_low(bus);
        }
        soft_i2c_delay();
        scl_high(bus);
        soft_i2c_delay();
        scl_low(bus);
        soft_i2c_delay();
    }

    sda_high(bus);
    soft_i2c_delay();
    scl_high(bus);
    soft_i2c_delay();
    const bool acknowledged = !sda_read(bus);
    scl_low(bus);
    soft_i2c_delay();
    return acknowledged;
}

static uint8_t read_byte(const SoftI2C *bus, bool acknowledge)
{
    uint8_t value = 0U;

    sda_high(bus);
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
        soft_i2c_delay();
        scl_high(bus);
        soft_i2c_delay();
        value = (uint8_t)((value << 1) | (sda_read(bus) ? 1U : 0U));
        scl_low(bus);
        soft_i2c_delay();
    }

    if (acknowledge) {
        sda_low(bus);
    } else {
        sda_high(bus);
    }
    soft_i2c_delay();
    scl_high(bus);
    soft_i2c_delay();
    scl_low(bus);
    soft_i2c_delay();
    sda_high(bus);
    return value;
}

void soft_i2c_init(SoftI2C *bus, GPIO_TypeDef *port, uint16_t scl_pin, uint16_t sda_pin)
{
    bus->port = port;
    bus->scl_pin = scl_pin;
    bus->sda_pin = sda_pin;

    if (port == GPIOE) {
        __HAL_RCC_GPIOE_CLK_ENABLE();
    } else if (port == GPIOF) {
        __HAL_RCC_GPIOF_CLK_ENABLE();
    } else {
        configASSERT(0);
    }

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    GPIO_InitTypeDef pins = {0};
    pins.Pin = scl_pin | sda_pin;
    pins.Mode = GPIO_MODE_OUTPUT_OD;
    pins.Pull = GPIO_PULLUP;
    pins.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(port, &pins);

    sda_high(bus);
    scl_high(bus);

    /* Recover a stuck bus: clock up to nine bits until the slave releases SDA. */
    if (!sda_read(bus)) {
        for (uint8_t pulse = 0U; pulse < 9U && !sda_read(bus); ++pulse) {
            scl_low(bus);
            soft_i2c_delay();
            scl_high(bus);
            soft_i2c_delay();
        }
    }
    i2c_stop(bus);
}

bool soft_i2c_probe(SoftI2C *bus, uint8_t address)
{
    i2c_start(bus);
    const bool acknowledged = write_byte(bus, (uint8_t)(address << 1));
    i2c_stop(bus);
    return acknowledged;
}

bool soft_i2c_write(SoftI2C *bus, uint8_t address, const uint8_t *data, size_t length)
{
    i2c_start(bus);
    if (!write_byte(bus, (uint8_t)(address << 1))) {
        i2c_stop(bus);
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        if (!write_byte(bus, data[index])) {
            i2c_stop(bus);
            return false;
        }
    }
    i2c_stop(bus);
    return true;
}

bool soft_i2c_read(SoftI2C *bus, uint8_t address, uint8_t *data, size_t length)
{
    if (length == 0U) {
        return true;
    }

    i2c_start(bus);
    if (!write_byte(bus, (uint8_t)((address << 1) | 1U))) {
        i2c_stop(bus);
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        data[index] = read_byte(bus, index + 1U < length);
    }
    i2c_stop(bus);
    return true;
}

bool soft_i2c_write_read(SoftI2C *bus, uint8_t address,
                         const uint8_t *tx, size_t tx_length,
                         uint8_t *rx, size_t rx_length)
{
    if (rx_length == 0U) {
        return soft_i2c_write(bus, address, tx, tx_length);
    }

    i2c_start(bus);
    if (!write_byte(bus, (uint8_t)(address << 1))) {
        i2c_stop(bus);
        return false;
    }
    for (size_t index = 0U; index < tx_length; ++index) {
        if (!write_byte(bus, tx[index])) {
            i2c_stop(bus);
            return false;
        }
    }

    i2c_start(bus);
    if (!write_byte(bus, (uint8_t)((address << 1) | 1U))) {
        i2c_stop(bus);
        return false;
    }
    for (size_t index = 0U; index < rx_length; ++index) {
        rx[index] = read_byte(bus, index + 1U < rx_length);
    }
    i2c_stop(bus);
    return true;
}
