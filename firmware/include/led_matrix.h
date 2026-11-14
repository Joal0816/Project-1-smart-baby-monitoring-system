#ifndef LED_MATRIX_H
#define LED_MATRIX_H

#include <stdint.h>

void led_matrix_init(void);
void led_matrix_show(uint8_t red, uint8_t green, uint8_t blue);
void led_matrix_off(void);

#endif
