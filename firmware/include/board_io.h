#ifndef SMART_BABY_BOARD_IO_H
#define SMART_BABY_BOARD_IO_H

#include "stm32f4xx_hal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Verified against the Spark-1 schematic and user manual:
 *   USART1 TX = PA9, RX = PA10, ST-Link VCP at 115200 8N1
 *   RGB LED red = PF12, blue = PF11, common anode, active low
 */
#define BOARD_LED_RED_PORT   GPIOF
#define BOARD_LED_RED_PIN    GPIO_PIN_12
#define BOARD_LED_BLUE_PORT  GPIOF
#define BOARD_LED_BLUE_PIN   GPIO_PIN_11

void board_io_init(void);
void board_led_toggle(void);
void board_led_red_set(bool on);
void board_led_blue_set(bool on);
int board_uart_write(const char *data, size_t length);

#endif
