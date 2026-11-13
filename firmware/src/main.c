#include "stm32f4xx_hal.h"
#include "app_tasks.h"
#include "board_io.h"
#ifdef WIFI_PROBE
#include "wifi_probe.h"
#endif

#include <stdbool.h>
#include <stdio.h>

static void clock_init_hsi(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clocks = {0};
    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    oscillator.HSIState = RCC_HSI_ON;
    oscillator.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    oscillator.PLL.PLLState = RCC_PLL_NONE;
    configASSERT(HAL_RCC_OscConfig(&oscillator) == HAL_OK);
    clocks.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                       RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clocks.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    clocks.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clocks.APB1CLKDivider = RCC_HCLK_DIV1;
    clocks.APB2CLKDivider = RCC_HCLK_DIV1;
    configASSERT(HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_0) == HAL_OK);
    SystemCoreClockUpdate();
    configASSERT(SystemCoreClock == 16000000U);
}

/* Spark-1 X2 is 8 MHz; standard STM32F407 168 MHz PLL configuration.
 * APB1 = 42 MHz, APB2 = 84 MHz, USB = 48 MHz. */
static bool clock_init_hse(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clocks = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    oscillator.HSEState = RCC_HSE_ON;
    oscillator.PLL.PLLState = RCC_PLL_ON;
    oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    oscillator.PLL.PLLM = 8U;
    oscillator.PLL.PLLN = 336U;
    oscillator.PLL.PLLP = RCC_PLLP_DIV2;
    oscillator.PLL.PLLQ = 7U;
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK) {
        return false;
    }

    clocks.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                       RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clocks.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clocks.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clocks.APB1CLKDivider = RCC_HCLK_DIV4;
    clocks.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_5) != HAL_OK) {
        return false;
    }
    SystemCoreClockUpdate();
    return SystemCoreClock == 168000000U;
}

int main(void)
{
    configASSERT(HAL_Init() == HAL_OK);
    const bool hse_ok = clock_init_hse();
    if (!hse_ok) {
        clock_init_hsi();
    }
    board_io_init();
    printf("\r\nSmart Baby Monitor: STM32F407 RT-Spark\r\n");
    printf("core clock: %lu Hz (%s), FreeRTOS tick: 1000 Hz\r\n",
           (unsigned long)SystemCoreClock, hse_ok ? "HSE/PLL" : "HSI fallback");
#ifdef WIFI_PROBE
    wifi_probe_run();
#endif
    monitor_tasks_start();
    return 0;
}
