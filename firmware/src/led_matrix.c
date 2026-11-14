#include "led_matrix.h"

#include "stm32f4xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

#define LED_MATRIX_LEDS 19U
#define LED_MATRIX_DATA_PORT GPIOA
#define LED_MATRIX_DATA_PIN GPIO_PIN_7
#define LED_MATRIX_ENABLE_PORT GPIOF
#define LED_MATRIX_ENABLE_PIN GPIO_PIN_2

/* 19 SK6805/WS2812-class LEDs behind the plastic diffuser.
 * PF2 enables U6 (active low); PF12 is the separate red user LED.
 * Timing matches the Spark-1 BSP: 840 kHz, T0H=40/T0L=160,
 * T1H=140/T1L=60 CPU cycles at 168 MHz. */

static bool initialized;

static void delay_cycles(uint32_t cycles)
{
    const uint32_t start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < cycles) {
    }
}

static void send_byte(uint8_t value)
{
    for (uint8_t mask = 0x80U; mask != 0U; mask >>= 1U) {
        if ((value & mask) != 0U) {
            LED_MATRIX_DATA_PORT->BSRR = LED_MATRIX_DATA_PIN;
            delay_cycles(140U);
            LED_MATRIX_DATA_PORT->BSRR = (uint32_t)LED_MATRIX_DATA_PIN << 16U;
            delay_cycles(60U);
        } else {
            LED_MATRIX_DATA_PORT->BSRR = LED_MATRIX_DATA_PIN;
            delay_cycles(40U);
            LED_MATRIX_DATA_PORT->BSRR = (uint32_t)LED_MATRIX_DATA_PIN << 16U;
            delay_cycles(160U);
        }
    }
}

void led_matrix_init(void)
{
    if (initialized) return;
    initialized = true;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Pin = LED_MATRIX_DATA_PIN;
    HAL_GPIO_Init(LED_MATRIX_DATA_PORT, &gpio);
    HAL_GPIO_WritePin(LED_MATRIX_DATA_PORT, LED_MATRIX_DATA_PIN, GPIO_PIN_RESET);

    gpio.Pin = LED_MATRIX_ENABLE_PIN;
    HAL_GPIO_Init(LED_MATRIX_ENABLE_PORT, &gpio);
    HAL_GPIO_WritePin(LED_MATRIX_ENABLE_PORT, LED_MATRIX_ENABLE_PIN, GPIO_PIN_SET);

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void led_matrix_show(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!initialized || (red == 0U && green == 0U && blue == 0U)) {
        led_matrix_off();
        return;
    }

    __disable_irq();
    HAL_GPIO_WritePin(LED_MATRIX_ENABLE_PORT, LED_MATRIX_ENABLE_PIN, GPIO_PIN_RESET);
    delay_cycles(1700U);

    for (uint32_t led = 0U; led < LED_MATRIX_LEDS; ++led) {
        send_byte(green);
        send_byte(red);
        send_byte(blue);
    }

    LED_MATRIX_DATA_PORT->BSRR = (uint32_t)LED_MATRIX_DATA_PIN << 16U;
    delay_cycles(17000U);
    __enable_irq();
}

void led_matrix_off(void)
{
    if (!initialized) return;
    LED_MATRIX_DATA_PORT->BSRR = (uint32_t)LED_MATRIX_DATA_PIN << 16U;
    HAL_GPIO_WritePin(LED_MATRIX_ENABLE_PORT, LED_MATRIX_ENABLE_PIN, GPIO_PIN_SET);
}
