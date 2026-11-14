#include "audio_capture.h"
#include "FreeRTOS.h"
#include "es8388.h"
#include "sensors.h"
#include "task.h"

#include <math.h>
#include <stdio.h>

#define AUDIO_DMA_SAMPLES 1024U
#define AUDIO_HALF_SAMPLES (AUDIO_DMA_SAMPLES / 2U)
#define AUDIO_SAMPLE_RATE 16000U
/* 2 s mono window: 32,000 samples = 64 KiB. */
#define AUDIO_WINDOW_SAMPLES (AUDIO_SAMPLE_RATE * 2U)
/* Provisional gate; must be calibrated on live microphone data. */
#define AUDIO_ACTIVITY_RMS 800U

static I2S_HandleTypeDef i2s3;
static I2S_HandleTypeDef i2s3_ext;
static DMA_HandleTypeDef i2s3_rx_dma;
static DMA_HandleTypeDef i2s3_tx_dma;
static int16_t audio_rx_buffer[AUDIO_DMA_SAMPLES];
static int16_t audio_tx_silence[AUDIO_DMA_SAMPLES];
/* 64 KiB CCM keeps the 2 s window out of the 128 KiB main SRAM. */
static int16_t *const audio_window = (int16_t *)0x10000000U;
static volatile bool audio_window_ready;
static bool audio_window_capturing;
static uint32_t audio_window_index;
static uint64_t audio_window_sum_squares;
static uint32_t audio_window_peak;
static TickType_t audio_window_cooldown;
static volatile uint32_t audio_half_ready;
static volatile uint32_t audio_errors;
static volatile uint32_t audio_last_half_cycle;
static volatile uint32_t audio_half_period_cycles;
static uint32_t audio_halves;
static uint32_t audio_max_rms;
static uint32_t audio_max_peak;
static volatile uint32_t audio_meter_rms;
static volatile uint32_t audio_meter_peak;
static volatile uint32_t audio_meter_frequency_hz;
static TickType_t audio_last_log;

static void i2s3_gpio_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF6_SPI3;
    gpio.Pin = GPIO_PIN_15;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_3 | GPIO_PIN_5;
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_7;
    HAL_GPIO_Init(GPIOC, &gpio);

    gpio.Pin = GPIO_PIN_4;
    gpio.Alternate = GPIO_AF7_I2S3ext;
    HAL_GPIO_Init(GPIOB, &gpio);
}

void HAL_I2S_MspInit(I2S_HandleTypeDef *hi2s)
{
    /* Only the main SPI3 handle owns the pins and DMA streams; I2S3ext shares
     * the clocks and has its own data register. */
    if (hi2s->Instance != SPI3) {
        return;
    }

    __HAL_RCC_SPI3_CLK_ENABLE();
    i2s3_gpio_init();

    __HAL_RCC_DMA1_CLK_ENABLE();

    i2s3_rx_dma.Instance = DMA1_Stream0;
    /* I2S3ext_RX is DMA1 Stream0 Channel3; the reference-manual table
     * for this row is known to list the wrong I2S instance. */
    i2s3_rx_dma.Init.Channel = DMA_CHANNEL_3;
    i2s3_rx_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    i2s3_rx_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    i2s3_rx_dma.Init.MemInc = DMA_MINC_ENABLE;
    i2s3_rx_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    i2s3_rx_dma.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    i2s3_rx_dma.Init.Mode = DMA_CIRCULAR;
    i2s3_rx_dma.Init.Priority = DMA_PRIORITY_MEDIUM;
    i2s3_rx_dma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&i2s3_rx_dma) != HAL_OK) {
        printf("audio: RX DMA init failed\r\n");
        return;
    }

    i2s3_tx_dma.Instance = DMA1_Stream5;
    i2s3_tx_dma.Init.Channel = DMA_CHANNEL_0;
    i2s3_tx_dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
    i2s3_tx_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    i2s3_tx_dma.Init.MemInc = DMA_MINC_ENABLE;
    i2s3_tx_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    i2s3_tx_dma.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    i2s3_tx_dma.Init.Mode = DMA_CIRCULAR;
    i2s3_tx_dma.Init.Priority = DMA_PRIORITY_MEDIUM;
    i2s3_tx_dma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&i2s3_tx_dma) != HAL_OK) {
        printf("audio: TX DMA init failed\r\n");
        return;
    }

    __HAL_LINKDMA(hi2s, hdmarx, i2s3_rx_dma);
    __HAL_LINKDMA(hi2s, hdmatx, i2s3_tx_dma);

    HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
}

void DMA1_Stream0_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&i2s3_rx_dma);
}

void DMA1_Stream5_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&i2s3_tx_dma);
}

static void record_half_event(uint32_t half)
{
    const uint32_t now = DWT->CYCCNT;
    if (audio_last_half_cycle != 0U) {
        audio_half_period_cycles = now - audio_last_half_cycle;
    }
    audio_last_half_cycle = now;
    audio_half_ready = half;
}

void HAL_I2SEx_TxRxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s == &i2s3) {
        record_half_event(1U);
    }
}

void HAL_I2SEx_TxRxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s == &i2s3) {
        record_half_event(2U);
    }
}

void HAL_I2S_ErrorCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s == &i2s3 || hi2s == &i2s3_ext) {
        audio_errors++;
    }
}

static bool i2s3_init(void)
{
    RCC_PeriphCLKInitTypeDef clock = {0};
    clock.PeriphClockSelection = RCC_PERIPHCLK_I2S;
    /* F407 PLLI2S uses the main PLLM input divider: 8 MHz / 8 = 1 MHz,
     * VCO = 192 MHz, I2S clock = 96 MHz. */
    clock.PLLI2S.PLLI2SN = 192U;
    clock.PLLI2S.PLLI2SR = 2U;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) {
        printf("audio: PLLI2S config failed\r\n");
        return false;
    }

    /* Main I2S3 is the clock master and sends silence to the codec DAC.
     * I2S3ext is the slave receiver for the codec ADC data on PB4. */
    i2s3.Instance = SPI3;
    i2s3.Init.Mode = I2S_MODE_MASTER_TX;
    i2s3.Init.Standard = I2S_STANDARD_PHILIPS;
    i2s3.Init.DataFormat = I2S_DATAFORMAT_16B;
    i2s3.Init.MCLKOutput = I2S_MCLKOUTPUT_ENABLE;
    i2s3.Init.AudioFreq = I2S_AUDIOFREQ_16K;
    i2s3.Init.CPOL = I2S_CPOL_LOW;
    i2s3.Init.ClockSource = I2S_CLOCK_PLL;
    i2s3.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_ENABLE;
    if (HAL_I2S_Init(&i2s3) != HAL_OK) {
        printf("audio: I2S3 init failed\r\n");
        return false;
    }
    /* STM32 is the clock master; MCLK is 96 MHz / (2*11 + 1) ~= 4.17 MHz.
     * The 512-sample half buffer then arrives every ~15.7 ms. */
    i2s3.Instance->I2SPR = 11U | (1U << 8U) | (1U << 9U);

    i2s3_ext.Instance = I2S3ext;
    i2s3_ext.Init.Mode = I2S_MODE_SLAVE_RX;
    i2s3_ext.Init.Standard = I2S_STANDARD_PHILIPS;
    i2s3_ext.Init.DataFormat = I2S_DATAFORMAT_16B;
    i2s3_ext.Init.MCLKOutput = I2S_MCLKOUTPUT_ENABLE;
    i2s3_ext.Init.AudioFreq = I2S_AUDIOFREQ_16K;
    i2s3_ext.Init.CPOL = I2S_CPOL_HIGH;
    i2s3_ext.Init.ClockSource = I2S_CLOCK_PLL;
    i2s3_ext.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
    if (HAL_I2S_Init(&i2s3_ext) != HAL_OK) {
        printf("audio: I2S3ext init failed\r\n");
        return false;
    }

    __HAL_I2S_ENABLE(&i2s3);
    __HAL_I2S_ENABLE(&i2s3_ext);
    return true;
}

void audio_capture_init(void)
{
    for (uint32_t wait = 0U; wait < 300U && !sensors_ready(); ++wait) {
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
    if (!sensors_ready()) {
        printf("audio: sensor bus not ready\r\n");
        return;
    }

    if (!es8388_init()) {
        printf("audio: codec init failed\r\n");
        return;
    }
    if (!i2s3_init()) {
        return;
    }
    if (!es8388_start_adc()) {
        printf("audio: ADC start failed\r\n");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(50U));

    if (HAL_I2SEx_TransmitReceive_DMA(&i2s3,
                                      (uint16_t *)audio_tx_silence,
                                      (uint16_t *)audio_rx_buffer,
                                      AUDIO_DMA_SAMPLES) != HAL_OK) {
        printf("audio: DMA start failed\r\n");
        return;
    }
    audio_last_log = xTaskGetTickCount();
    printf("audio: 16 kHz PCM16 capture started; connect a mic to CN3\r\n");
}

bool audio_capture_window_ready(void)
{
    return audio_window_ready;
}

const int16_t *audio_capture_window_data(void)
{
    return audio_window;
}

size_t audio_capture_window_length(void)
{
    return AUDIO_WINDOW_SAMPLES;
}

void audio_capture_window_release(void)
{
    audio_window_ready = false;
}

void audio_capture_meter(uint32_t *rms, uint32_t *peak, uint32_t *frequency_hz)
{
    if (rms != NULL) *rms = audio_meter_rms;
    if (peak != NULL) *peak = audio_meter_peak;
    if (frequency_hz != NULL) *frequency_hz = audio_meter_frequency_hz;
}

void audio_capture_poll(void)
{
    const uint32_t half = audio_half_ready;
    if (half == 0U) {
        const TickType_t now = xTaskGetTickCount();
        if ((now - audio_last_log) >= pdMS_TO_TICKS(1000U)) {
            printf("audio: no DMA data (errors=%lu)\r\n",
                   (unsigned long)audio_errors);
            audio_last_log = now;
        }
        return;
    }
    audio_half_ready = 0U;

    const int16_t *samples = (half == 1U)
                                 ? &audio_rx_buffer[0]
                                 : &audio_rx_buffer[AUDIO_HALF_SAMPLES];
    uint64_t sum_squares = 0U;
    uint32_t peak = 0U;
    for (size_t index = 0U; index < AUDIO_HALF_SAMPLES; ++index) {
        const int32_t sample = samples[index];
        const uint32_t magnitude = sample < 0 ? (uint32_t)(-sample) : (uint32_t)sample;
        if (magnitude > peak) {
            peak = magnitude;
        }
        sum_squares += (uint64_t)((int64_t)sample * sample);
    }
    const uint32_t rms = (uint32_t)sqrtf((float)sum_squares / AUDIO_HALF_SAMPLES);
    uint32_t crossings = 0U;
    uint32_t mono_frames = 0U;
    int32_t previous = 0;
    bool have_previous = false;
    for (size_t frame = 0U; frame + 1U < AUDIO_HALF_SAMPLES; frame += 2U) {
        const int32_t mono = ((int32_t)samples[frame] + samples[frame + 1U]) / 2;
        if (have_previous &&
            ((previous < 0 && mono >= 0) || (previous > 0 && mono <= 0))) {
            crossings++;
        }
        previous = mono;
        have_previous = true;
        mono_frames++;
    }
    audio_meter_rms = rms;
    audio_meter_peak = peak;
    audio_meter_frequency_hz = mono_frames > 0U
        ? crossings * AUDIO_SAMPLE_RATE / (2U * mono_frames)
        : 0U;
    audio_halves++;
    if (rms > audio_max_rms) {
        audio_max_rms = rms;
    }
    if (peak > audio_max_peak) {
        audio_max_peak = peak;
    }

    const TickType_t now = xTaskGetTickCount();
    if (!audio_window_ready) {
        if (!audio_window_capturing && rms >= AUDIO_ACTIVITY_RMS &&
            (int32_t)(now - audio_window_cooldown) >= 0) {
            audio_window_capturing = true;
            audio_window_index = 0U;
            audio_window_sum_squares = 0U;
            audio_window_peak = 0U;
        }
        if (audio_window_capturing) {
            for (size_t frame = 0U; frame + 1U < AUDIO_HALF_SAMPLES; frame += 2U) {
                const int32_t mono = ((int32_t)samples[frame] + samples[frame + 1U]) / 2;
                const int16_t value = (int16_t)mono;
                audio_window[audio_window_index++] = value;
                audio_window_sum_squares += (uint64_t)((int64_t)value * value);
                const uint32_t magnitude =
                    value < 0 ? (uint32_t)(-value) : (uint32_t)value;
                if (magnitude > audio_window_peak) {
                    audio_window_peak = magnitude;
                }
                if (audio_window_index >= AUDIO_WINDOW_SAMPLES) {
                    audio_window_capturing = false;
                    audio_window_ready = true;
                    audio_window_cooldown = now + pdMS_TO_TICKS(2000U);
                    printf("audio: window ready samples=%lu rms=%lu peak=%lu\r\n",
                           (unsigned long)AUDIO_WINDOW_SAMPLES,
                           (unsigned long)(uint32_t)sqrtf((float)audio_window_sum_squares /
                                                          AUDIO_WINDOW_SAMPLES),
                           (unsigned long)audio_window_peak);
                    break;
                }
            }
        }
    }
    if ((now - audio_last_log) >= pdMS_TO_TICKS(1000U)) {
        const uint32_t half_us = (uint32_t)((uint64_t)audio_half_period_cycles *
                                            1000000U / SystemCoreClock);
        printf("audio: halves=%lu half_us=%lu rms=%lu peak=%lu freq=%lu max_rms=%lu max_peak=%lu errors=%lu\r\n",
               (unsigned long)audio_halves, (unsigned long)half_us,
               (unsigned long)rms, (unsigned long)peak,
               (unsigned long)audio_meter_frequency_hz,
               (unsigned long)audio_max_rms, (unsigned long)audio_max_peak,
               (unsigned long)audio_errors);
        audio_last_log = now;
    }
}
