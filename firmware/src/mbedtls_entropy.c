#include "stm32f4xx_hal.h"
#include <stddef.h>

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len,
                          size_t *olen)
{
    (void)data;
    RCC->AHB2ENR |= RCC_AHB2ENR_RNGEN;
    RNG->CR |= RNG_CR_RNGEN;
    for (size_t i = 0; i < len; i++) {
        uint32_t guard = 0;
        while ((RNG->SR & RNG_SR_DRDY) == 0U && ++guard < 100000U) {
        }
        if ((RNG->SR & RNG_SR_DRDY) == 0U) {
            *olen = i;
            return -1;
        }
        output[i] = (unsigned char)RNG->DR;
    }
    *olen = len;
    return 0;
}
