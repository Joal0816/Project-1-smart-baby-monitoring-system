#include "wifi_probe.h"

#include "stm32f4xx_hal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(WIFI_PROBE)
#if __has_include("wifi_config.local.h")
#include "wifi_config.local.h"
#define HAVE_WIFI_CONFIG 1
#endif
#endif

struct rw007_join_payload {
    uint32_t security;
    uint32_t band;
    uint32_t datarate;
    int16_t channel;
    int16_t rssi;
    uint8_t ssid_len;
    uint8_t ssid[33];
    uint8_t bssid[6];
    uint8_t hidden;
    uint8_t pad[3];
    char passwd[32];
} __attribute__((packed));

/* RT-Spark BSP CONFIG_RW007_* and SPI2 MSP: SCK PB13, MISO PC2,
 * MOSI PC3, CS PF10, INT PG11, RST PG15. */
#define RW007_CS_PORT GPIOF
#define RW007_CS_PIN GPIO_PIN_10
#define RW007_INT_PORT GPIOG
#define RW007_INT_PIN GPIO_PIN_11
#define RW007_RST_PORT GPIOG
#define RW007_RST_PIN GPIO_PIN_15

#define MASTER_MAGIC1 0x67452301U
#define MASTER_MAGIC2 0xEFCDAB89U
#define SLAVE_MAGIC1 0x98BADCFEU
#define SLAVE_MAGIC2 0x10325476U

static SPI_HandleTypeDef probe_spi;

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = RW007_CS_PIN;
    HAL_GPIO_Init(RW007_CS_PORT, &gpio);
    HAL_GPIO_WritePin(RW007_CS_PORT, RW007_CS_PIN, GPIO_PIN_SET);

    gpio.Pin = RW007_RST_PIN;
    HAL_GPIO_Init(RW007_RST_PORT, &gpio);
    HAL_GPIO_WritePin(RW007_RST_PORT, RW007_RST_PIN, GPIO_PIN_SET);

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Pin = RW007_INT_PIN;
    HAL_GPIO_Init(RW007_INT_PORT, &gpio);

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI2;
    gpio.Pin = GPIO_PIN_13;
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_3;
    HAL_GPIO_Init(GPIOC, &gpio);
    gpio.Pin = GPIO_PIN_2;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC, &gpio);
}

static void spi_init(void)
{
    __HAL_RCC_SPI2_CLK_ENABLE();
    probe_spi.Instance = SPI2;
    probe_spi.Init.Mode = SPI_MODE_MASTER;
    probe_spi.Init.Direction = SPI_DIRECTION_2LINES;
    probe_spi.Init.DataSize = SPI_DATASIZE_8BIT;
    probe_spi.Init.CLKPolarity = SPI_POLARITY_LOW;
    probe_spi.Init.CLKPhase = SPI_PHASE_1EDGE;
    probe_spi.Init.NSS = SPI_NSS_SOFT;
    probe_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
    probe_spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    probe_spi.Init.TIMode = SPI_TIMODE_DISABLE;
    probe_spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    probe_spi.Init.CRCPolynomial = 10;
    if (HAL_SPI_Init(&probe_spi) != HAL_OK) {
        printf("wifi probe: SPI2 init failed\r\n");
    }
}

static void reset_module(void)
{
    HAL_GPIO_WritePin(RW007_RST_PORT, RW007_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(RW007_RST_PORT, RW007_RST_PIN, GPIO_PIN_SET);
}

static bool wait_int_high(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    while (HAL_GPIO_ReadPin(RW007_INT_PORT, RW007_INT_PIN) == GPIO_PIN_RESET) {
        if ((HAL_GetTick() - start) > timeout_ms) {
            return false;
        }
        HAL_Delay(5);
    }
    return true;
}

static void hex_dump(const char *label, const uint8_t *data, uint32_t len)
{
    printf("%s", label);
    for (uint32_t i = 0; i < len; i++) {
        printf("%02X%s", data[i], (i + 1U == len) ? "\r\n" : " ");
    }
}

static void pin_output(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState level)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = pin;
    HAL_GPIO_Init(port, &gpio);
    HAL_GPIO_WritePin(port, pin, level);
}

static void sck_af(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI2;
    gpio.Pin = GPIO_PIN_13;
    HAL_GPIO_Init(GPIOB, &gpio);
}

static bool probe_attempt(const char *name, GPIO_TypeDef *cs_port,
                          uint16_t cs_pin, GPIO_PinState boot_cs,
                          GPIO_PinState boot_sck)
{
    uint8_t tx[32] = {0};
    uint8_t rx[32] = {0};

    pin_output(GPIOF, GPIO_PIN_10, GPIO_PIN_SET);
    pin_output(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
    pin_output(GPIOB, GPIO_PIN_13, boot_sck);
    pin_output(cs_port, cs_pin, boot_cs);

    reset_module();
    const bool ready = wait_int_high(2000);
    HAL_Delay(200);

    sck_af();
    HAL_GPIO_WritePin(cs_port, cs_pin, GPIO_PIN_SET);
    HAL_Delay(1);

    tx[1] = (uint8_t)((0x01U << 4) | 0x01U); /* master_cmd_phase, MRDY=1 */
    put_le16(&tx[4], 1);
    put_le32(&tx[8], MASTER_MAGIC1);
    put_le32(&tx[12], MASTER_MAGIC2);

    HAL_GPIO_WritePin(cs_port, cs_pin, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(
        &probe_spi, tx, rx, sizeof(tx), 100);
    HAL_GPIO_WritePin(cs_port, cs_pin, GPIO_PIN_SET);

    int offset = -1;
    for (int i = 0; i <= (int)sizeof(rx) - 16; i++) {
        if (get_le32(&rx[i + 8]) == SLAVE_MAGIC1 &&
            get_le32(&rx[i + 12]) == SLAVE_MAGIC2) {
            offset = i;
            break;
        }
    }
    printf("wifi probe: %s cs=%d sck=%d int=%d status=%d off=%d rx=", name,
           (int)boot_cs, (int)boot_sck, ready ? 1 : 0, (int)status, offset);
    for (uint32_t i = 0; i < sizeof(rx); i++) {
        printf("%02X%s", rx[i], (i + 1U == sizeof(rx)) ? "\r\n" : " ");
    }
    bool ok = false;
    if (offset >= 0) {
        const uint8_t type = (uint8_t)(rx[offset + 1] >> 4);
        const uint16_t seq = get_le16(&rx[offset + 4]);
        const uint16_t len = get_le16(&rx[offset + 6]);
        printf("wifi probe: %s header type=%u seq=%u len=%u\r\n",
               name, type, seq, len);
        ok = (type == 3);
    }
    printf("wifi probe: %s %s\r\n", name, ok ? "RW007 SPI link OK" : "no valid response");
    return ok;
}


static uint8_t bitbang_byte(uint8_t out)
{
    uint8_t in = 0;
    for (int bit = 7; bit >= 0; bit--) {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_3,
                          (out & (1U << bit)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        for (volatile int d = 0; d < 30; d++) {
        }
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET);
        for (volatile int d = 0; d < 15; d++) {
        }
        if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_2) == GPIO_PIN_SET) {
            in |= (uint8_t)(1U << bit);
        }
        for (volatile int d = 0; d < 15; d++) {
        }
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
    }
    return in;
}

static void bitbang_attempt(void)
{
    uint8_t tx[32] = {0};
    uint8_t rx[32] = {0};

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = GPIO_PIN_13;
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_3;
    HAL_GPIO_Init(GPIOC, &gpio);
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOC, &gpio);
    pin_output(GPIOF, GPIO_PIN_10, GPIO_PIN_SET);

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
    reset_module();
    wait_int_high(2000);
    HAL_Delay(200);

    tx[1] = (uint8_t)((0x01U << 4) | 0x01U);
    put_le16(&tx[4], 1);
    put_le32(&tx[8], MASTER_MAGIC1);
    put_le32(&tx[12], MASTER_MAGIC2);

    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_10, GPIO_PIN_RESET);
    for (uint32_t i = 0; i < sizeof(tx); i++) {
        rx[i] = bitbang_byte(tx[i]);
    }
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_10, GPIO_PIN_SET);

    printf("wifi probe: bitbang rx=");
    for (uint32_t i = 0; i < sizeof(rx); i++) {
        printf("%02X%s", rx[i], (i + 1U == sizeof(rx)) ? "\r\n" : " ");
    }
    printf("wifi probe: bitbang magic@8=%08lX/%08lX @16=%08lX/%08lX\r\n",
           (unsigned long)get_le32(&rx[8]), (unsigned long)get_le32(&rx[12]),
           (unsigned long)get_le32(&rx[16]), (unsigned long)get_le32(&rx[20]));
}


static uint16_t cmd_seq = 1;

static void master_header(uint8_t *h, uint8_t type, uint16_t seq,
                          uint16_t m2s_len, bool mrdy)
{
    memset(h, 0, 16);
    h[1] = (uint8_t)((type << 4) | (mrdy ? 0x01U : 0U));
    put_le16(&h[4], seq);
    put_le16(&h[6], m2s_len);
    put_le32(&h[8], MASTER_MAGIC1);
    put_le32(&h[12], MASTER_MAGIC2);
}

static bool rw007_stage1(uint16_t seq, uint16_t m2s_len, bool mrdy,
                         uint8_t *resp)
{
    uint8_t hdr[16];
    master_header(hdr, 0x01, seq, m2s_len, mrdy);
    HAL_GPIO_WritePin(RW007_CS_PORT, RW007_CS_PIN, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(
        &probe_spi, hdr, resp, sizeof(hdr), 100);
    HAL_GPIO_WritePin(RW007_CS_PORT, RW007_CS_PIN, GPIO_PIN_SET);
    return st == HAL_OK &&
           get_le32(&resp[8]) == SLAVE_MAGIC1 &&
           get_le32(&resp[12]) == SLAVE_MAGIC2 &&
           (resp[1] >> 4) == 3;
}

static bool rw007_stage2(uint16_t seq, uint16_t m2s_len,
                         const uint8_t *tx, uint8_t *rx, uint16_t max_len,
                         uint16_t *s2m_out)
{
    uint8_t hdr[16];
    uint8_t resp[16];
    HAL_Delay(2);
    master_header(hdr, 0x02, seq, m2s_len, true);
    HAL_GPIO_WritePin(RW007_CS_PORT, RW007_CS_PIN, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(
        &probe_spi, hdr, resp, sizeof(hdr), 100);
    const uint32_t s2m = get_le16(&resp[6]);
    if (s2m_out) {
        *s2m_out = (uint16_t)s2m;
    }
    uint32_t xfer = m2s_len > s2m ? m2s_len : s2m;
    xfer = (xfer + 3U) & ~3U;
    if (xfer > max_len) {
        xfer = max_len;
    }
    printf("rw007: stage2 hdr st=%d type=%u flag=%u seq=%u s2m=%lu\r\n",
           (int)st, resp[1] >> 4, resp[1] & 0xF, get_le16(&resp[4]),
           (unsigned long)s2m);
    if (st == HAL_OK && xfer > 0) {
        HAL_Delay(1);
        st = HAL_SPI_TransmitReceive(&probe_spi, (uint8_t *)tx, rx,
                                     (uint16_t)xfer, 200);
    }
    HAL_GPIO_WritePin(RW007_CS_PORT, RW007_CS_PIN, GPIO_PIN_SET);
    return st == HAL_OK &&
           get_le32(&resp[8]) == SLAVE_MAGIC1 &&
           get_le32(&resp[12]) == SLAVE_MAGIC2 &&
           (resp[1] >> 4) == 4;
}
static bool rw007_send_cmd(uint32_t cmd, const void *value, uint32_t value_len)
{
    uint8_t pkt[16 + 128] = {0};
    uint8_t rx[16 + 128] = {0};
    uint8_t resp[16] = {0};

    if (value_len > 128) {
        return false;
    }
    const uint32_t payload_len = 8 + value_len;
    const uint32_t total_len = 8 + payload_len;
    put_le32(&pkt[0], payload_len);
    put_le32(&pkt[4], 4); /* DATA_TYPE_CMD */
    put_le32(&pkt[8], cmd);
    put_le32(&pkt[12], value_len);
    if (value_len) {
        memcpy(&pkt[16], value, value_len);
    }

    cmd_seq++;
    const uint16_t seq = cmd_seq;
    if (!rw007_stage1(seq, (uint16_t)total_len, true, resp)) {
        printf("rw007: stage1 command failed\r\n");
        return false;
    }
    uint16_t s2m = 0;
    if (!rw007_stage2(seq, (uint16_t)total_len, pkt, rx, sizeof(rx), &s2m)) {
        printf("rw007: stage2 command failed\r\n");
        return false;
    }
    printf("rw007: cmd %lu sent, s2m=%u rx=", (unsigned long)cmd, s2m);
    for (uint32_t i = 0; i < s2m && i < sizeof(rx); i++) {
        printf("%02X%s", rx[i], (i + 1U == s2m) ? "\r\n" : " ");
    }
    if (s2m == 0) {
        printf("\r\n");
    }
    if (s2m >= 20 && get_le32(&rx[4]) == 5) {
        printf("rw007: inline resp cmd=%lu len=%lu result=%ld\r\n",
               (unsigned long)get_le32(&rx[8]),
               (unsigned long)get_le32(&rx[12]),
               (long)(int32_t)get_le32(&rx[16]));
    }
    return true;
}
static bool rw007_fetch(uint32_t wanted, uint8_t *value, uint32_t *value_len)
{
    uint8_t resp[16] = {0};
    uint8_t rx[1528] = {0};
    uint8_t zero[1528] = {0};
    uint16_t s2m = 0;

    cmd_seq++;
    const uint16_t seq = cmd_seq;
    if (!rw007_stage1(seq, 0, true, resp)) {
        return false;
    }
    if (!rw007_stage2(seq, 0, zero, rx, sizeof(rx), &s2m) || s2m == 0) {
        return false;
    }
    const uint32_t data_len = get_le32(&rx[0]);
    const uint32_t data_type = get_le32(&rx[4]);
    printf("rw007: packet len=%lu type=%lu\r\n",
           (unsigned long)data_len, (unsigned long)data_type);
    if (data_type != 5) { /* DATA_TYPE_RESP */
        return false;
    }
    const uint32_t cmd = get_le32(&rx[8]);
    const uint32_t len = get_le32(&rx[12]);
    const int32_t result = (int32_t)get_le32(&rx[16]);
    printf("rw007: resp cmd=%lu len=%lu result=%ld\r\n",
           (unsigned long)cmd, (unsigned long)len, (long)result);
    if (cmd != wanted) {
        return false;
    }
    if (value && value_len) {
        uint32_t n = len < *value_len ? len : *value_len;
        memcpy(value, &rx[20], n);
        *value_len = n;
    }
    return result == 0;
}
static bool rw007_wait_int(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout_ms) {
        if (HAL_GPIO_ReadPin(RW007_INT_PORT, RW007_INT_PIN) == GPIO_PIN_RESET) {
            return true;
        }
        HAL_Delay(5);
    }
    return false;
}

static void rw007_join_test(void)
{
#if defined(HAVE_WIFI_CONFIG)
    uint32_t mode = 1; /* RT_WLAN_STATION */
    uint8_t value[8] = {0};
    uint32_t len = sizeof(value);

    printf("rw007: joining %s\r\n", MONITOR_WIFI_SSID);
    HAL_Delay(2000);

    struct rw007_join_payload p = {0};
    p.security = 0x00400004U; /* SECURITY_WPA2_AES_PSK */
    p.band = 1;               /* RT_802_11_BAND_2_4GHZ */
    p.ssid_len = (uint8_t)strlen(MONITOR_WIFI_SSID);
    memcpy(p.ssid, MONITOR_WIFI_SSID, p.ssid_len);
    strncpy(p.passwd, MONITOR_WIFI_PASSWORD, sizeof(p.passwd) - 1);

    bool sent = false;
    for (int attempt = 0; attempt < 6; attempt++) {
        if (rw007_send_cmd(7, &p, sizeof(p))) {
            sent = true;
            break;
        }
        HAL_Delay(500);
    }
    if (!sent) {
        return;
    }
    bool joined = false;
    for (int i = 0; i < 30; i++) {
        HAL_Delay(500);
        len = sizeof(value);
        if (rw007_fetch(7, value, &len)) {
            printf("rw007: join ok\r\n");
            joined = true;
            break;
        }
    }
    if (!joined) {
        printf("rw007: join not confirmed\r\n");
        return;
    }

    if (rw007_send_cmd(13, NULL, 0)) {
        for (int i = 0; i < 10; i++) {
            HAL_Delay(300);
            len = sizeof(value);
            if (rw007_fetch(13, value, &len)) {
                int32_t rssi = (int32_t)get_le32(value);
                printf("rw007: rssi=%ld dBm\r\n", (long)rssi);
                break;
            }
        }
    }
#else
    printf("rw007: wifi_config.local.h missing; join skipped\r\n");
#endif
}

static void rw007_command_test(void)
{
    uint8_t value[64] = {0};
    uint32_t len = sizeof(value);

    printf("rw007: command test\r\n");
    gpio_init();
    spi_init();
    pin_output(GPIOF, GPIO_PIN_10, GPIO_PIN_RESET);
    pin_output(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
    reset_module();
    if (!wait_int_high(2000)) {
        printf("rw007: INT did not go high\r\n");
        return;
    }
    HAL_Delay(200);
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_10, GPIO_PIN_SET);
    sck_af();

    if (!rw007_send_cmd(5, NULL, 0)) {
        return;
    }
    for (int i = 0; i < 20; i++) {
        HAL_Delay(500);
        printf("rw007: poll %d INT=%d\r\n", i,
               (int)HAL_GPIO_ReadPin(RW007_INT_PORT, RW007_INT_PIN));
        len = sizeof(value);
        if (rw007_fetch(5, value, &len)) {
            value[len < sizeof(value) ? len : sizeof(value) - 1] = 0;
            printf("rw007: version [%s]\r\n", value);
            break;
        }
    }
    rw007_join_test();
    return;
}
void wifi_probe_run(void)
{
    printf("wifi probe: RW007 command test\r\n");
    gpio_init();
    spi_init();
    rw007_command_test();
}
