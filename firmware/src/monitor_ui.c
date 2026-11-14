#include "monitor_ui.h"
#include "audio_capture.h"
#include "board_io.h"
#include "display.h"
#include "led_matrix.h"
#include "stm32f4xx_hal.h"
#include "wifi_net.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define KEY0_PORT GPIOC
#define KEY0_PIN GPIO_PIN_0
#define KEY1_PORT GPIOC
#define KEY1_PIN GPIO_PIN_1
#define KEY2_PORT GPIOC
#define KEY2_PIN GPIO_PIN_4
#define WKUP_PORT GPIOC
#define WKUP_PIN GPIO_PIN_5
#define BUZZER_PORT GPIOB
#define BUZZER_PIN GPIO_PIN_0

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    bool previous;
    uint32_t changed_at;
} Button;

static Button buttons[4];
static bool muted;
static bool initialized;
static uint8_t page;
static uint32_t last_render;
static uint32_t beep_until;
static bool last_alert;
static uint8_t last_page;
static uint32_t last_meter_log;
static uint32_t led_test_until;
static uint32_t led_hold_until;
static uint8_t held_red;
static uint8_t held_green;
static uint8_t held_blue;

static void button_init(Button *button, GPIO_TypeDef *port, uint16_t pin)
{
    button->port = port;
    button->pin = pin;
    button->previous = true;
    button->changed_at = 0U;
}

static void read_button(Button *button, uint32_t now, uint8_t index)
{
    const bool pressed = HAL_GPIO_ReadPin(button->port, button->pin) == GPIO_PIN_RESET;
    if (pressed != button->previous && (now - button->changed_at) >= 25U) {
        button->changed_at = now;
        if (!pressed) {
            button->previous = false;
            return;
        }
        button->previous = true;
        switch (index) {
        case 0:
            wifi_net_clear_cry_alert();
            page = 0;
            printf("ui: key0 acknowledge\r\n");
            break;
        case 1:
            muted = !muted;
            printf("ui: key1 mute=%u\r\n", muted ? 1U : 0U);
            break;
        case 2:
            page = (uint8_t)((page + 1U) % 4U);
            printf("ui: key2 page=%u\r\n", page);
            break;
        default:
            beep_until = now + 100U;
            printf("ui: wkup beep\r\n");
            break;
        }
    }
}

void monitor_ui_init(void)
{
    if (initialized) return;
    initialized = true;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = BUZZER_PIN;
    HAL_GPIO_Init(BUZZER_PORT, &gpio);
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin = KEY0_PIN | KEY1_PIN | KEY2_PIN | WKUP_PIN;
    HAL_GPIO_Init(GPIOC, &gpio);

    button_init(&buttons[0], KEY0_PORT, KEY0_PIN);
    button_init(&buttons[1], KEY1_PORT, KEY1_PIN);
    button_init(&buttons[2], KEY2_PORT, KEY2_PIN);
    button_init(&buttons[3], WKUP_PORT, WKUP_PIN);

    led_matrix_init();
    const bool display_ok = display_init();
    printf("ui: display=%s; buttons and buzzer active\r\n",
           display_ok ? "ready" : "unavailable");
    last_render = HAL_GetTick() - 1000U;
}

void monitor_ui_step(const SensorSnapshot *snapshot)
{
    if (!initialized || snapshot == NULL) return;
    const uint32_t now = HAL_GetTick();
    if (led_test_until == 1U) {
        led_test_until = now + 1500U;
    }

    for (uint8_t i = 0; i < 4U; ++i) {
        read_button(&buttons[i], now, i);
    }

    const bool alert = wifi_net_cry_alert();
    uint32_t audio_rms = 0U;
    uint32_t audio_peak = 0U;
    uint32_t audio_frequency_hz = 0U;
    audio_capture_meter(&audio_rms, &audio_peak, &audio_frequency_hz);
    const bool audio_active = audio_rms >= 25U && audio_peak >= 80U;
    uint8_t matrix_red = 0U;
    uint8_t matrix_green = 0U;
    uint8_t matrix_blue = 0U;
    bool blue = false;
    if (led_test_until != 0U && (int32_t)(now - led_test_until) < 0) {
        matrix_red = 64U;
        matrix_green = 64U;
        matrix_blue = 64U;
        blue = true;
    } else if (alert) {
        const bool blink = ((now / 250U) & 1U) != 0U;
        matrix_red = blink ? 40U : 0U;
        matrix_blue = blink ? 40U : 0U;
        blue = blink;
    } else if (audio_active) {
        const uint32_t brightness = audio_rms > 64U ? 64U : (audio_rms < 8U ? 8U : audio_rms);
        if (audio_frequency_hz < 800U) {
            matrix_red = (uint8_t)brightness;
        } else if (audio_frequency_hz < 1200U) {
            matrix_red = (uint8_t)brightness;
            matrix_green = (uint8_t)brightness;
        } else {
            matrix_blue = (uint8_t)brightness;
        }
        blue = audio_frequency_hz >= 1200U;
        held_red = matrix_red;
        held_green = matrix_green;
        held_blue = matrix_blue;
        led_hold_until = now + 300U;
    } else if (led_hold_until != 0U && (int32_t)(now - led_hold_until) < 0) {
        matrix_red = held_red;
        matrix_green = held_green;
        matrix_blue = held_blue;
    } else {
        matrix_blue = 10U;
    }
    led_matrix_show(matrix_red, matrix_green, matrix_blue);
    board_led_blue_set(blue);
    if ((now - last_meter_log) >= 500U) {
        last_meter_log = now;
        printf("ui: meter rms=%lu peak=%lu freq=%lu mat=%u/%u/%u blue=%u\r\n",
               (unsigned long)audio_rms, (unsigned long)audio_peak,
               (unsigned long)audio_frequency_hz, matrix_red, matrix_green,
               matrix_blue, blue ? 1U : 0U);
    }
    if (alert && !muted) {
        const bool on = ((now / 500U) & 1U) != 0U;
        HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
    }
    if (beep_until != 0U && (int32_t)(now - beep_until) < 0) {
        HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_SET);
    } else if (beep_until != 0U) {
        beep_until = 0U;
        HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
    }

    const uint8_t render_page = (page == 0U && audio_active) ? 3U : page;
    if (alert != last_alert || render_page != last_page ||
        (now - last_render) >= 1000U) {
        display_render(snapshot, wifi_net_connected(), wifi_net_ip(),
                       alert, muted, render_page, audio_rms, audio_frequency_hz);
        last_alert = alert;
        last_page = render_page;
        last_render = now;
    }
}
