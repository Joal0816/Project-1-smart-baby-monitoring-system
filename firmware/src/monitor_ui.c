#include "monitor_ui.h"
#include "board_io.h"
#include "display.h"
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
            page = (uint8_t)((page + 1U) % 3U);
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

    const bool display_ok = display_init();
    printf("ui: display=%s; buttons and buzzer active\r\n",
           display_ok ? "ready" : "unavailable");
    last_render = HAL_GetTick() - 1000U;
}

void monitor_ui_step(const SensorSnapshot *snapshot)
{
    if (!initialized || snapshot == NULL) return;
    const uint32_t now = HAL_GetTick();

    for (uint8_t i = 0; i < 4U; ++i) {
        read_button(&buttons[i], now, i);
    }

    const bool alert = wifi_net_cry_alert();
    board_led_blue_set(alert && !muted);
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

    if (alert != last_alert || page != last_page || (now - last_render) >= 1000U) {
        display_render(snapshot, wifi_net_connected(), wifi_net_ip(),
                       alert, muted, page);
        last_alert = alert;
        last_page = page;
        last_render = now;
    }
}
