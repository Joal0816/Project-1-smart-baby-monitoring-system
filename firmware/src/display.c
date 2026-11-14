#include "display.h"
#include "display_font.h"
#include "stm32f4xx_hal.h"

#include <stdio.h>
#include <string.h>

/* Spark-1 LCD: ST7789V3, 240x240, 8080 parallel through FSMC bank 3.
 * Schematic: D0..D7 = PD14, PD15, PD0, PD1, PE7..PE10;
 * NOE=PD4, NWE=PD5, NE3=PG10, A18=PD13, RST=PD3, BL=PF9. */
#define LCD_BASE 0x6803FFFEU
#define LCD_RST_PORT GPIOD
#define LCD_RST_PIN GPIO_PIN_3
#define LCD_BL_PORT GPIOF
#define LCD_BL_PIN GPIO_PIN_9

#define COLOR_BG 0x0841U
#define COLOR_WHITE 0xFFFFU
#define COLOR_CYAN 0x07FFU
#define COLOR_GREEN 0x07E0U
#define COLOR_RED 0xF800U
#define COLOR_YELLOW 0xFFE0U
#define COLOR_GRAY 0x8410U

typedef struct {
    volatile uint8_t reg;
    uint8_t reserved;
    volatile uint8_t data8;
} LcdPort;

#define LCD ((LcdPort *)LCD_BASE)

static SRAM_HandleTypeDef lcd_sram;
static bool display_ready;

static void lcd_command(uint8_t command)
{
    LCD->reg = command;
}

static void lcd_data8(uint8_t value)
{
    LCD->data8 = value;
}

static void lcd_data16(uint16_t value)
{
    lcd_data8((uint8_t)(value >> 8));
    lcd_data8((uint8_t)value);
}

static void lcd_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    lcd_command(0x2A);
    lcd_data16(x0);
    lcd_data16(x1);
    lcd_command(0x2B);
    lcd_data16(y0);
    lcd_data16(y1);
    lcd_command(0x2C);
}

static void lcd_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                          uint16_t color)
{
    if (x1 >= DISPLAY_WIDTH) x1 = DISPLAY_WIDTH - 1U;
    if (y1 >= DISPLAY_HEIGHT) y1 = DISPLAY_HEIGHT - 1U;
    lcd_window(x0, y0, x1, y1);
    const uint32_t count = (uint32_t)(x1 - x0 + 1U) * (uint32_t)(y1 - y0 + 1U);
    for (uint32_t i = 0; i < count; ++i) {
        lcd_data16(color);
    }
}

static void lcd_text(uint16_t x, uint16_t y, uint16_t foreground,
                     uint16_t background, const char *text)
{
    while (*text != '\0' && x <= DISPLAY_WIDTH - 8U) {
        uint8_t ch = (uint8_t)*text++;
        if (ch < ' ' || ch > '}') ch = '?';
        const uint8_t *glyph = &asc2_1608[(uint16_t)(ch - ' ') * 16U];
        lcd_window(x, y, x + 7U, y + 15U);
        for (uint8_t row = 0; row < 16U; ++row) {
            const uint8_t bits = glyph[row];
            for (uint8_t column = 0; column < 8U; ++column) {
                lcd_data16((bits & (0x80U >> column)) ? foreground : background);
            }
        }
        x += 8U;
    }
}

static void lcd_line(uint16_t y, uint16_t foreground, const char *text)
{
    lcd_fill_rect(0, y, DISPLAY_WIDTH - 1U, y + 17U, COLOR_BG);
    lcd_text(4, y + 1U, foreground, COLOR_BG, text);
}

static void format_tenths(char *buffer, size_t size, int32_t value,
                          bool valid, const char *unit)
{
    if (!valid) {
        snprintf(buffer, size, "-- %s", unit);
        return;
    }
    const int32_t magnitude = value < 0 ? -value : value;
    snprintf(buffer, size, "%s%ld.%ld %s", value < 0 ? "-" : "",
             (long)(magnitude / 10), (long)(magnitude % 10), unit);
}

static void render_status(const SensorSnapshot *snapshot, bool wifi_up,
                          const char *ip, bool cry_alert, bool muted,
                          uint32_t audio_rms, uint32_t audio_frequency_hz)
{
    char line[32];
    char value[20];

    lcd_line(4, COLOR_CYAN, "LITTLE MONITOR");
    format_tenths(value, sizeof(value), snapshot->temperature_c_x10,
                  snapshot->temperature_valid, "C");
    snprintf(line, sizeof(line), "TEMP  %s", value);
    lcd_line(26, COLOR_WHITE, line);
    format_tenths(value, sizeof(value), snapshot->humidity_pct_x10,
                  snapshot->humidity_valid, "%");
    snprintf(line, sizeof(line), "HUM   %s", value);
    lcd_line(48, COLOR_WHITE, line);
    format_tenths(value, sizeof(value), snapshot->light_lux_x10,
                  snapshot->light_valid, "LUX");
    snprintf(line, sizeof(line), "LIGHT %s", value);
    lcd_line(70, COLOR_WHITE, line);
    snprintf(line, sizeof(line), "PROX  %u", (unsigned)snapshot->proximity);
    lcd_line(92, COLOR_WHITE, line);
    snprintf(line, sizeof(line), "WIFI  %s", wifi_up ? ip : "OFFLINE");
    lcd_line(114, wifi_up ? COLOR_GREEN : COLOR_RED, line);
    snprintf(line, sizeof(line), "AUDIO %luHZ", (unsigned long)audio_frequency_hz);
    lcd_line(136, cry_alert ? COLOR_RED : (audio_rms >= 80U ? COLOR_CYAN : COLOR_GRAY), line);
    lcd_line(158, muted ? COLOR_YELLOW : COLOR_GRAY,
             muted ? "BUZZER MUTED" : "BUZZER ARMED");
    lcd_line(180, COLOR_GRAY, "K0 ACK  K1 MUTE");
    lcd_line(202, COLOR_GRAY, "K2 PAGE  WKUP BEEP");
}

static void render_network(const SensorSnapshot *snapshot, bool wifi_up,
                           const char *ip)
{
    char line[32];
    (void)snapshot;
    lcd_line(4, COLOR_CYAN, "NETWORK");
    snprintf(line, sizeof(line), "RW007 %s", wifi_up ? "CONNECTED" : "OFFLINE");
    lcd_line(30, wifi_up ? COLOR_GREEN : COLOR_RED, line);
    snprintf(line, sizeof(line), "IP %s", wifi_up ? ip : "--");
    lcd_line(54, COLOR_WHITE, line);
    lcd_line(78, COLOR_WHITE, "TLS 1.2 PINNED CA");
    lcd_line(102, COLOR_WHITE, "AUDIO 16 KHZ PCM16");
    lcd_line(126, COLOR_WHITE, "BACKEND PORT 8443");
    lcd_line(150, COLOR_WHITE, "FCM APP INSTALL");
    lcd_line(180, COLOR_GRAY, "K0 ACK  K1 MUTE");
    lcd_line(202, COLOR_GRAY, "K2 PAGE  WKUP BEEP");
}


static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return (uint16_t)(((uint16_t)(red & 0xF8U) << 8U) |
                      ((uint16_t)(green & 0xFCU) << 3U) |
                      (uint16_t)(blue >> 3U));
}

static void render_audio_visualizer(uint32_t audio_rms, uint32_t audio_frequency_hz)
{
    uint8_t red = 0U;
    uint8_t green = 0U;
    uint8_t blue = 0U;
    if (audio_frequency_hz < 800U) {
        red = 255U;
    } else if (audio_frequency_hz < 1200U) {
        red = 255U;
        green = 255U;
    } else {
        blue = 255U;
    }

    uint32_t level = audio_rms * 3U;
    if (level > 255U) level = 255U;
    if (level < 24U) level = 24U;
    red = (uint8_t)((red * level) / 255U);
    green = (uint8_t)((green * level) / 255U);
    blue = (uint8_t)((blue * level) / 255U);

    char line[32];
    lcd_fill_rect(0, 0, DISPLAY_WIDTH - 1U, DISPLAY_HEIGHT - 1U, COLOR_BG);
    lcd_line(4, COLOR_CYAN, "SOUND COLOUR");
    lcd_fill_rect(20, 32, 219, 190, rgb565(red, green, blue));
    snprintf(line, sizeof(line), "FREQ %lu HZ", (unsigned long)audio_frequency_hz);
    lcd_line(198, COLOR_WHITE, line);
    const uint32_t bar = (audio_rms > 70U) ? 220U : (audio_rms * 3U);
    lcd_fill_rect(20, 224, 219, 233, COLOR_GRAY);
    if (bar > 0U) {
        lcd_fill_rect(20, 224, 20U + (uint16_t)bar, 233, COLOR_GREEN);
    }
}

static void render_controls(void)
{
    lcd_line(4, COLOR_CYAN, "CONTROLS");
    lcd_line(32, COLOR_WHITE, "K0  ACK / CLEAR ALERT");
    lcd_line(56, COLOR_WHITE, "K1  MUTE BUZZER");
    lcd_line(80, COLOR_WHITE, "K2  NEXT PAGE");
    lcd_line(104, COLOR_WHITE, "WKUP  TEST BEEP");
    lcd_line(132, COLOR_GRAY, "LOCAL UI - NO INFANT");
    lcd_line(156, COLOR_GRAY, "EDUCATIONAL PROTOTYPE");
    lcd_line(180, COLOR_GRAY, "K0 ACK  K1 MUTE");
    lcd_line(202, COLOR_GRAY, "K2 PAGE  WKUP BEEP");
}

bool display_is_ready(void)
{
    return display_ready;
}

bool display_init(void)
{
    if (display_ready) return true;

    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    RCC->AHB3ENR |= RCC_AHB3ENR_FSMCEN;
    __DSB();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF12_FSMC;
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 |
               GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &gpio);
    gpio.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOE, &gpio);
    gpio.Pin = GPIO_PIN_10;
    HAL_GPIO_Init(GPIOG, &gpio);

    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = LCD_RST_PIN;
    HAL_GPIO_Init(LCD_RST_PORT, &gpio);
    gpio.Pin = LCD_BL_PIN;
    HAL_GPIO_Init(LCD_BL_PORT, &gpio);
    HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(120);

    FSMC_NORSRAM_TimingTypeDef read_timing = {0};
    FSMC_NORSRAM_TimingTypeDef write_timing = {0};
    lcd_sram.Instance = FSMC_NORSRAM_DEVICE;
    lcd_sram.Extended = FSMC_NORSRAM_EXTENDED_DEVICE;
    lcd_sram.Init.NSBank = FSMC_NORSRAM_BANK3;
    lcd_sram.Init.DataAddressMux = FSMC_DATA_ADDRESS_MUX_DISABLE;
    lcd_sram.Init.MemoryType = FSMC_MEMORY_TYPE_SRAM;
    lcd_sram.Init.MemoryDataWidth = FSMC_NORSRAM_MEM_BUS_WIDTH_8;
    lcd_sram.Init.BurstAccessMode = FSMC_BURST_ACCESS_MODE_DISABLE;
    lcd_sram.Init.WaitSignalPolarity = FSMC_WAIT_SIGNAL_POLARITY_LOW;
    lcd_sram.Init.WrapMode = FSMC_WRAP_MODE_DISABLE;
    lcd_sram.Init.WaitSignalActive = FSMC_WAIT_TIMING_BEFORE_WS;
    lcd_sram.Init.WriteOperation = FSMC_WRITE_OPERATION_ENABLE;
    lcd_sram.Init.WaitSignal = FSMC_WAIT_SIGNAL_DISABLE;
    lcd_sram.Init.ExtendedMode = FSMC_EXTENDED_MODE_ENABLE;
    lcd_sram.Init.AsynchronousWait = FSMC_ASYNCHRONOUS_WAIT_DISABLE;
    lcd_sram.Init.WriteBurst = FSMC_WRITE_BURST_DISABLE;
    lcd_sram.Init.PageSize = FSMC_PAGE_SIZE_NONE;

    read_timing.AddressSetupTime = 0x0F;
    read_timing.AddressHoldTime = 0x00;
    read_timing.DataSetupTime = 60;
    read_timing.BusTurnAroundDuration = 0x00;
    read_timing.CLKDivision = 0x00;
    read_timing.DataLatency = 0x00;
    read_timing.AccessMode = FSMC_ACCESS_MODE_A;
    write_timing.AddressSetupTime = 9;
    write_timing.AddressHoldTime = 0x00;
    write_timing.DataSetupTime = 8;
    write_timing.BusTurnAroundDuration = 0x00;
    write_timing.CLKDivision = 0x00;
    write_timing.DataLatency = 0x00;
    write_timing.AccessMode = FSMC_ACCESS_MODE_A;
    if (HAL_SRAM_Init(&lcd_sram, &read_timing, &write_timing) != HAL_OK) {
        printf("display: FSMC init failed\r\n");
        return false;
    }

    lcd_command(0x36); lcd_data8(0x00);
    lcd_command(0x3A); lcd_data8(0x65);
    lcd_command(0xB2); lcd_data8(0x0C); lcd_data8(0x0C);
    lcd_data8(0x00); lcd_data8(0x33); lcd_data8(0x33);
    lcd_command(0xB7); lcd_data8(0x35);
    lcd_command(0xBB); lcd_data8(0x37);
    lcd_command(0xC0); lcd_data8(0x2C);
    lcd_command(0xC2); lcd_data8(0x01);
    lcd_command(0xC3); lcd_data8(0x12);
    lcd_command(0xC4); lcd_data8(0x20);
    lcd_command(0xC6); lcd_data8(0x0F);
    lcd_command(0xD0); lcd_data8(0xA4); lcd_data8(0xA1);
    lcd_command(0xE0);
    lcd_data8(0xD0); lcd_data8(0x04); lcd_data8(0x0D); lcd_data8(0x11);
    lcd_data8(0x13); lcd_data8(0x2B); lcd_data8(0x3F); lcd_data8(0x54);
    lcd_data8(0x4C); lcd_data8(0x18); lcd_data8(0x0D); lcd_data8(0x0B);
    lcd_data8(0x1F); lcd_data8(0x23);
    lcd_command(0xE1);
    lcd_data8(0xD0); lcd_data8(0x04); lcd_data8(0x0C); lcd_data8(0x11);
    lcd_data8(0x13); lcd_data8(0x2C); lcd_data8(0x3F); lcd_data8(0x44);
    lcd_data8(0x51); lcd_data8(0x2F); lcd_data8(0x1F); lcd_data8(0x1F);
    lcd_data8(0x20); lcd_data8(0x23);
    lcd_command(0x21);
    lcd_command(0x11);
    HAL_Delay(120);
    lcd_command(0x29);

    /* ST7789v3 can accept the faster write timing once initialized. */
    FSMC_Bank1E->BWTR[6] &= ~(0xFU << 0);
    FSMC_Bank1E->BWTR[6] &= ~(0xFU << 8);
    FSMC_Bank1E->BWTR[6] |= 3U << 0;
    FSMC_Bank1E->BWTR[6] |= 2U << 8;

    display_ready = true;
    lcd_fill_rect(0, 0, DISPLAY_WIDTH - 1U, DISPLAY_HEIGHT - 1U, COLOR_BG);
    HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, GPIO_PIN_SET);
    printf("display: ST7789 240x240 ready\r\n");
    return true;
}

void display_render(const SensorSnapshot *snapshot, bool wifi_up,
                    const char *ip, bool cry_alert, bool muted, uint8_t page,
                    uint32_t audio_rms, uint32_t audio_frequency_hz)
{
    if (!display_ready || snapshot == NULL) return;
    if (page == 1U) {
        render_network(snapshot, wifi_up, ip);
    } else if (page == 2U) {
        render_controls();
    } else if (page == 3U) {
        render_audio_visualizer(audio_rms, audio_frequency_hz);
    } else {
        render_status(snapshot, wifi_up, ip, cry_alert, muted,
                      audio_rms, audio_frequency_hz);
    }
}
