#include "board_io.h"
#include "FreeRTOS.h"

#include <stdio.h>
#include <sys/stat.h>

static UART_HandleTypeDef monitor_uart;

void board_io_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    GPIO_InitTypeDef led = {0};
    led.Pin = BOARD_LED_RED_PIN | BOARD_LED_BLUE_PIN;
    led.Mode = GPIO_MODE_OUTPUT_PP;
    led.Pull = GPIO_NOPULL;
    led.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOF, &led);
    /* Common-anode LEDs: high is off. */
    HAL_GPIO_WritePin(GPIOF, BOARD_LED_RED_PIN | BOARD_LED_BLUE_PIN, GPIO_PIN_SET);

    GPIO_InitTypeDef uart_pins = {0};
    uart_pins.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    uart_pins.Mode = GPIO_MODE_AF_PP;
    uart_pins.Pull = GPIO_PULLUP;
    uart_pins.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    uart_pins.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &uart_pins);

    monitor_uart.Instance = USART1;
    monitor_uart.Init.BaudRate = 115200U;
    monitor_uart.Init.WordLength = UART_WORDLENGTH_8B;
    monitor_uart.Init.StopBits = UART_STOPBITS_1;
    monitor_uart.Init.Parity = UART_PARITY_NONE;
    monitor_uart.Init.Mode = UART_MODE_TX_RX;
    monitor_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    monitor_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    configASSERT(HAL_UART_Init(&monitor_uart) == HAL_OK);
    setvbuf(stdout, NULL, _IONBF, 0);
}

void board_led_toggle(void)
{
    HAL_GPIO_TogglePin(BOARD_LED_RED_PORT, BOARD_LED_RED_PIN);
}

void board_led_blue_set(bool on)
{
    /* Common-anode LED: low is on. */
    HAL_GPIO_WritePin(BOARD_LED_BLUE_PORT, BOARD_LED_BLUE_PIN,
                      on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

int board_uart_write(const char *data, size_t length)
{
    if (data == NULL || length == 0U) {
        return 0;
    }
    const HAL_StatusTypeDef status =
        HAL_UART_Transmit(&monitor_uart, (const uint8_t *)data, (uint16_t)length, 200U);
    return status == HAL_OK ? (int)length : -1;
}

/* Newlib retarget: printf and friends send through the ST-Link VCP. */
int _write(int file, char *ptr, int len)
{
    (void)file;
    return board_uart_write(ptr, (size_t)len);
}

int _read(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    return -1;
}

int _close(int file)
{
    (void)file;
    return -1;
}

int _lseek(int file, int ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

int _isatty(int file)
{
    (void)file;
    return 1;
}

int _fstat(int file, struct stat *status)
{
    (void)file;
    status->st_mode = S_IFCHR;
    return 0;
}
