#include "wifi_net.h"

#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"
#include "lwip/init.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/altcp_tls.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "tls_ca.h"
#include "audio_capture.h"
#include "lwip/timeouts.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#if __has_include("wifi_config.local.h")
#include "wifi_config.local.h"
#define HAVE_WIFI_CONFIG 1
#endif

#define RW_CS_PORT GPIOF
#define RW_CS_PIN GPIO_PIN_10
#define RW_INT_PORT GPIOG
#define RW_INT_PIN GPIO_PIN_11
#define RW_RST_PORT GPIOG
#define RW_RST_PIN GPIO_PIN_15

#define MASTER_MAGIC1 0x67452301U
#define MASTER_MAGIC2 0xEFCDAB89U
#define SLAVE_MAGIC1 0x98BADCFEU
#define SLAVE_MAGIC2 0x10325476U

#define DATA_TYPE_STA_ETH 1U
#define DATA_TYPE_CMD 4U
#define DATA_TYPE_RESP 5U
#define DATA_TYPE_CB 6U

static SPI_HandleTypeDef wifi_spi;
static struct netif wifi_netif;
static uint16_t wifi_seq;
static bool wifi_joined;
static uint32_t wifi_tx_frames;
static uint32_t wifi_rx_frames;
static bool wifi_connect_seen;
static uint8_t wifi_mac[6] = {0x02, 0x52, 0x57, 0x30, 0x30, 0x31};
static volatile SensorSnapshot latest_sensors;
#define TELEMETRY_QUEUE_DEPTH 8U
typedef struct {
    SensorSnapshot sensors;
    uint32_t sequence;
} TelemetryItem;
static TelemetryItem telemetry_queue[TELEMETRY_QUEUE_DEPTH];
static uint32_t telemetry_head;
static uint32_t telemetry_tail;
static uint32_t telemetry_count;
static uint32_t telemetry_dropped;
static uint32_t telemetry_next_sequence = 6U;
static uint32_t telemetry_last_enqueue;

#if defined(HAVE_WIFI_CONFIG)
struct join_payload {
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
#endif

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wifi_pins(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = RW_CS_PIN;
    HAL_GPIO_Init(RW_CS_PORT, &gpio);
    HAL_GPIO_WritePin(RW_CS_PORT, RW_CS_PIN, GPIO_PIN_SET);
    gpio.Pin = RW_RST_PIN;
    HAL_GPIO_Init(RW_RST_PORT, &gpio);
    HAL_GPIO_WritePin(RW_RST_PORT, RW_RST_PIN, GPIO_PIN_SET);

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Pin = RW_INT_PIN;
    HAL_GPIO_Init(RW_INT_PORT, &gpio);

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

    __HAL_RCC_SPI2_CLK_ENABLE();
    wifi_spi.Instance = SPI2;
    wifi_spi.Init.Mode = SPI_MODE_MASTER;
    wifi_spi.Init.Direction = SPI_DIRECTION_2LINES;
    wifi_spi.Init.DataSize = SPI_DATASIZE_8BIT;
    wifi_spi.Init.CLKPolarity = SPI_POLARITY_LOW;
    wifi_spi.Init.CLKPhase = SPI_PHASE_1EDGE;
    wifi_spi.Init.NSS = SPI_NSS_SOFT;
    wifi_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
    wifi_spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    wifi_spi.Init.TIMode = SPI_TIMODE_DISABLE;
    wifi_spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    wifi_spi.Init.CRCPolynomial = 10;
    if (HAL_SPI_Init(&wifi_spi) != HAL_OK) {
        printf("wifi: SPI init failed\r\n");
    }
}

static bool wifi_ready(void)
{
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 2000U) {
        if (HAL_GPIO_ReadPin(RW_INT_PORT, RW_INT_PIN) == GPIO_PIN_SET) {
            return true;
        }
        HAL_Delay(5);
    }
    return false;
}

static void wifi_reset(void)
{
    HAL_GPIO_WritePin(RW_CS_PORT, RW_CS_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RW_RST_PORT, RW_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(RW_RST_PORT, RW_RST_PIN, GPIO_PIN_SET);
}

static void header(uint8_t *h, uint8_t type, uint16_t seq, uint16_t m2s)
{
    memset(h, 0, 16);
    h[1] = (uint8_t)((type << 4) | 0x01U);
    put16(&h[4], seq);
    put16(&h[6], m2s);
    put32(&h[8], MASTER_MAGIC1);
    put32(&h[12], MASTER_MAGIC2);
}

static bool stage1(uint16_t seq, uint16_t m2s, uint8_t *resp)
{
    uint8_t h[16];
    header(h, 1, seq, m2s);
    HAL_GPIO_WritePin(RW_CS_PORT, RW_CS_PIN, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(
        &wifi_spi, h, resp, 16, 100);
    HAL_GPIO_WritePin(RW_CS_PORT, RW_CS_PIN, GPIO_PIN_SET);
    return st == HAL_OK &&
           get32(&resp[8]) == SLAVE_MAGIC1 &&
           get32(&resp[12]) == SLAVE_MAGIC2 &&
           (resp[1] >> 4) == 3;
}

static bool stage2(uint16_t seq, uint16_t m2s, const uint8_t *tx,
                   uint8_t *rx, uint16_t max_len, uint16_t *s2m_out)
{
    uint8_t h[16];
    uint8_t resp[16];
    header(h, 2, seq, m2s);
    HAL_GPIO_WritePin(RW_CS_PORT, RW_CS_PIN, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(
        &wifi_spi, h, resp, 16, 100);
    const uint32_t s2m = get16(&resp[6]);
    if (s2m_out) {
        *s2m_out = (uint16_t)s2m;
    }
    uint32_t xfer = m2s > s2m ? m2s : s2m;
    xfer = (xfer + 3U) & ~3U;
    if (xfer > max_len) {
        xfer = max_len;
    }
    if (st == HAL_OK && xfer > 0) {
        HAL_Delay(1);
        st = HAL_SPI_TransmitReceive(&wifi_spi, (uint8_t *)tx, rx,
                                     (uint16_t)xfer, 200);
    }
    HAL_GPIO_WritePin(RW_CS_PORT, RW_CS_PIN, GPIO_PIN_SET);
    return st == HAL_OK &&
           get32(&resp[8]) == SLAVE_MAGIC1 &&
           get32(&resp[12]) == SLAVE_MAGIC2 &&
           (resp[1] >> 4) == 4;
}

static bool send_packet(uint32_t type, const void *data, uint16_t len)
{
    uint8_t pkt[8 + 1600];
    uint8_t rx[8 + 1600];
    uint16_t s2m = 0;
    if (len > 1600U) {
        return false;
    }
    uint32_t total = 8U + len;
    put32(&pkt[0], len);
    put32(&pkt[4], type);
    if (len) {
        memcpy(&pkt[8], data, len);
    }
    wifi_seq++;
    const uint16_t seq = wifi_seq;
    if (!stage1(seq, (uint16_t)total, rx)) {
        return false;
    }
    return stage2(seq, (uint16_t)total, pkt, rx, sizeof(rx), &s2m);
}

static bool fetch_packet(uint8_t *data, uint16_t *len, uint32_t *type)
{
    uint8_t resp[16];
    uint8_t pkt[8 + 1600];
    uint8_t zero[8];
    uint16_t s2m = 0;

    wifi_seq++;
    const uint16_t seq = wifi_seq;
    if (!stage1(seq, 0, resp)) {
        return false;
    }
    if (!stage2(seq, 0, zero, pkt, sizeof(pkt), &s2m) || s2m < 8U) {
        return false;
    }
    const uint32_t data_len = get32(&pkt[0]);
    if (data_len > s2m - 8U || data_len > *len) {
        return false;
    }
    *len = (uint16_t)data_len;
    *type = get32(&pkt[4]);
    memcpy(data, &pkt[8], data_len);
    return true;
}

static bool send_cmd(uint32_t cmd, const void *value, uint16_t value_len)
{
    uint8_t pkt[16 + 128];
    uint8_t rx[16 + 128];
    uint8_t resp[16];
    uint16_t s2m = 0;

    if (value_len > 128U) {
        return false;
    }
    const uint32_t payload_len = 8U + value_len;
    const uint32_t total = 8U + payload_len;
    put32(&pkt[0], payload_len);
    put32(&pkt[4], DATA_TYPE_CMD);
    put32(&pkt[8], cmd);
    put32(&pkt[12], value_len);
    if (value_len) {
        memcpy(&pkt[16], value, value_len);
    }
    wifi_seq++;
    const uint16_t seq = wifi_seq;
    if (!stage1(seq, (uint16_t)total, resp)) {
        return false;
    }
    return stage2(seq, (uint16_t)total, pkt, rx, sizeof(rx), &s2m);
}

static bool wait_resp(uint32_t wanted, uint32_t timeout_ms, int32_t *result)
{
    uint8_t pkt[8 + 1600];
    uint32_t type = 0;
    uint16_t len = sizeof(pkt);
    const uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout_ms) {
        len = sizeof(pkt);
        if (fetch_packet(pkt, &len, &type)) {
            if (type == DATA_TYPE_CB && len >= 8U) {
                printf("wifi: cb %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                       pkt[0], pkt[1], pkt[2], pkt[3],
                       pkt[4], pkt[5], pkt[6], pkt[7]);
                if (pkt[0] == 1U) {
                    wifi_connect_seen = true;
                }
            }
            if (type == DATA_TYPE_RESP && len >= 12U &&
                get32(&pkt[0]) == wanted) {
                printf("wifi: resp cmd=%lu len=%lu result=%ld value=%ld\r\n",
                       (unsigned long)get32(&pkt[0]),
                       (unsigned long)get32(&pkt[4]),
                       (long)(int32_t)get32(&pkt[8]),
                       (long)(int32_t)(len >= 16U ? get32(&pkt[12]) : 0U));
                if (result) {
                    *result = (int32_t)get32(&pkt[8]);
                }
                return true;
            }
        }
        HAL_Delay(20);
    }
    return false;
}

static bool read_value(uint32_t wanted, uint8_t *out, uint16_t *out_len,
                       uint32_t timeout_ms)
{
    uint8_t pkt[8 + 1600];
    uint32_t type = 0;
    uint16_t len = sizeof(pkt);
    const uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout_ms) {
        len = sizeof(pkt);
        if (fetch_packet(pkt, &len, &type) && type == DATA_TYPE_RESP &&
            len >= 12U && get32(&pkt[0]) == wanted) {
            uint32_t n = get32(&pkt[4]);
            if (n > *out_len) {
                n = *out_len;
            }
            if (n && len >= 12U + n) {
                memcpy(out, &pkt[12], n);
            }
            *out_len = (uint16_t)n;
            return true;
        }
        HAL_Delay(20);
    }
    return false;
}

static struct altcp_pcb *http_pcb;
static struct altcp_tls_config *tls_config;
static bool http_started;
static bool http_done;
static uint32_t http_rx_len;
static uint32_t http_next_attempt;
static uint32_t http_last_done;
static bool time_synced;
static int64_t time_epoch;
static uint32_t time_tick;

static void set_epoch(int64_t epoch)
{
    time_epoch = epoch;
    time_tick = HAL_GetTick();
    time_synced = true;
}

static void format_captured_at(char *out, size_t size)
{
    int64_t epoch = time_synced
        ? time_epoch + (int64_t)((HAL_GetTick() - time_tick) / 1000U)
        : 1794584400; /* 2026-11-13T15:40:00Z fallback */
    time_t value = (time_t)epoch;
    struct tm tm;
    gmtime_r(&value, &tm);
    strftime(out, size, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

static void update_time_from_headers(const uint8_t *data, uint16_t len)
{
    char buffer[192];
    uint16_t n = len < sizeof(buffer) - 1 ? len : (uint16_t)(sizeof(buffer) - 1);
    memcpy(buffer, data, n);
    buffer[n] = 0;
    const char *date = strstr(buffer, "date: ");
    if (date == NULL) {
        date = strstr(buffer, "Date: ");
    }
    if (date == NULL || strlen(date) < 29) {
        printf("wifi: no date hdr [%s]\r\n", buffer);
        return;
    }
    printf("wifi: date %s\r\n", date);
    const char *p = date + 6;
    const int day = (p[5] - '0') * 10 + (p[6] - '0');
    const int year = (p[12] - '0') * 1000 + (p[13] - '0') * 100 +
                     (p[14] - '0') * 10 + (p[15] - '0');
    const int hour = (p[17] - '0') * 10 + (p[18] - '0');
    const int minute = (p[20] - '0') * 10 + (p[21] - '0');
    const int second = (p[23] - '0') * 10 + (p[24] - '0');
    char month[4] = {p[8], p[9], p[10], 0};
    static const char *months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    int mon = -1;
    for (int i = 0; i < 12; i++) {
        if (strncmp(month, months[i], 3) == 0) {
            mon = i;
            break;
        }
    }
    if (mon < 0) {
        return;
    }
    int y = year;
    int m = mon + 1;
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = (int64_t)era * 146097 + doe - 719468;
    set_epoch(days * 86400 + hour * 3600 + minute * 60 + second);
}
static bool audio_mode;
static bool audio_pending;
static uint32_t audio_remaining;
static uint32_t audio_offset;
static uint32_t audio_sequence;
static bool last_audio_upload;
static int http_status;
static void audio_pump(struct altcp_pcb *pcb);


static err_t http_recv(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err)
{
    (void)arg;
    (void)err;
    if (p == NULL) {
        http_done = true;
        http_last_done = HAL_GetTick();
        altcp_close(pcb);
        http_pcb = NULL;
        if (http_status == 200) {
            if (audio_mode) {
                audio_capture_window_release();
                audio_sequence++;
                last_audio_upload = true;
                printf("wifi: audio accepted seq=%lu\r\n", (unsigned long)(audio_sequence - 1U));
            } else {
                last_audio_upload = false;
                taskENTER_CRITICAL();
                if (telemetry_count > 0U) {
                    telemetry_head = (telemetry_head + 1U) % TELEMETRY_QUEUE_DEPTH;
                    telemetry_count--;
                }
                taskEXIT_CRITICAL();
            }
        }
        printf("wifi: http closed status=%d rx=%lu\r\n",
               http_status, (unsigned long)http_rx_len);
        return ERR_OK;
    }
    altcp_recved(pcb, p->tot_len);
    uint8_t header[192];
    uint16_t hn = pbuf_copy_partial(p, header, sizeof(header), 0);
    update_time_from_headers(header, hn);
    uint8_t chunk[128];
    uint16_t n = pbuf_copy_partial(p, chunk, sizeof(chunk) - 1, 0);
    chunk[n] = 0;
    if (http_status == 0 && strstr((char *)chunk, "HTTP/1.") != NULL) {
        const char *space = strchr((char *)chunk, ' ');
        if (space != NULL) {
            http_status = atoi(space + 1);
        }
    }
    printf("wifi: http %s", (char *)chunk);
    http_rx_len += p->tot_len;
    pbuf_free(p);
    return ERR_OK;
}

static err_t http_connected(void *arg, struct altcp_pcb *pcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK) {
        printf("wifi: tls connect err=%d\r\n", (int)err);
        return err;
    }
    if (audio_mode) {
        if (!audio_capture_window_ready()) {
            audio_mode = false;
            return ERR_OK;
        }
        char captured_at[24];
        char request[512];
        format_captured_at(captured_at, sizeof(captured_at));
        const unsigned int audio_bytes = (unsigned int)(audio_capture_window_length() * sizeof(int16_t));
        int n = snprintf(request, sizeof(request),
                 "POST /api/v1/devices/%s/audio-windows HTTP/1.1\r\n"
                 "Host: 192.168.137.1:8443\r\n"
                 "Authorization: Bearer %s\r\n"
                 "Content-Type: application/octet-stream\r\n"
                 "X-Captured-At: %s\r\n"
                 "X-Sequence: %lu\r\n"
                 "X-Sample-Rate-Hz: 16000\r\n"
                 "Content-Length: %u\r\n"
                 "Connection: close\r\n\r\n",
                 MONITOR_DEVICE_ID, MONITOR_DEVICE_TOKEN, captured_at,
                 (unsigned long)audio_sequence, audio_bytes);
        if (n <= 0 || (size_t)n >= sizeof(request)) {
            audio_mode = false;
            return ERR_OK;
        }
        if (altcp_write(pcb, request, (uint16_t)n, TCP_WRITE_FLAG_COPY) != ERR_OK) {
            audio_mode = false;
            return ERR_OK;
        }
        altcp_output(pcb);
        audio_pending = true;
        audio_offset = 0;
        audio_remaining = audio_bytes;
        printf("wifi: tls audio headers sent seq=%lu bytes=%u\r\n",
               (unsigned long)audio_sequence, audio_bytes);
        audio_pump(pcb);
        return ERR_OK;
    }
    char body[512];
    char request[768];
    char metrics[320];
    char faults[128];
    SensorSnapshot s;
    uint32_t sequence;
    taskENTER_CRITICAL();
    if (telemetry_count > 0U) {
        s = telemetry_queue[telemetry_head].sensors;
        sequence = telemetry_queue[telemetry_head].sequence;
    } else {
        s = latest_sensors;
        sequence = telemetry_next_sequence++;
    }
    taskEXIT_CRITICAL();
    size_t used = 0;
    metrics[0] = 0;
    faults[0] = 0;
#define ADD_METRIC(...) do { \
    int n = snprintf(metrics + used, sizeof(metrics) - used, __VA_ARGS__); \
    if (n > 0 && (size_t)n < sizeof(metrics) - used) used += (size_t)n; \
} while (0)
    if (s.temperature_valid) {
        ADD_METRIC("\"temperature_c\":%ld.%ld,",
                   (long)(s.temperature_c_x10 / 10),
                   (long)(s.temperature_c_x10 < 0 ? -(s.temperature_c_x10 % 10) : s.temperature_c_x10 % 10));
    } else {
        strncat(faults, "\"temperature\",", sizeof(faults) - strlen(faults) - 1);
    }
    if (s.humidity_valid) {
        ADD_METRIC("\"humidity_pct\":%ld.%ld,",
                   (long)(s.humidity_pct_x10 / 10),
                   (long)(s.humidity_pct_x10 < 0 ? -(s.humidity_pct_x10 % 10) : s.humidity_pct_x10 % 10));
    } else {
        strncat(faults, "\"humidity\",", sizeof(faults) - strlen(faults) - 1);
    }
    if (s.light_valid) {
        ADD_METRIC("\"light_lux\":%ld.%ld,",
                   (long)(s.light_lux_x10 / 10),
                   (long)(s.light_lux_x10 < 0 ? -(s.light_lux_x10 % 10) : s.light_lux_x10 % 10));
    } else {
        strncat(faults, "\"light\",", sizeof(faults) - strlen(faults) - 1);
    }
    if (s.proximity_valid) {
        ADD_METRIC("\"proximity_state\":\"%s\",",
                   s.proximity >= 50U ? "obstructed" : "clear");
    } else {
        strncat(faults, "\"proximity\",", sizeof(faults) - strlen(faults) - 1);
    }
    if (s.motion_valid) {
        ADD_METRIC("\"tilt_deg\":0,");
    } else {
        strncat(faults, "\"motion\",", sizeof(faults) - strlen(faults) - 1);
    }
    if (faults[0] != 0) {
        faults[strlen(faults) - 1] = 0;
    }
    char captured_at[24];
    format_captured_at(captured_at, sizeof(captured_at));
    snprintf(body, sizeof(body),
             "{\"captured_at\":\"%s\",\"sequence\":%lu,"
             "%s\"wifi_rssi_dbm\":-62,\"firmware_version\":\"rw007-tls\","
             "\"sensor_faults\":[%s]}",
             captured_at, (unsigned long)sequence, metrics, faults);
#ifdef MONITOR_DEVICE_TOKEN
    snprintf(request, sizeof(request),
             "POST /api/v1/devices/%s/telemetry HTTP/1.1\r\n"
             "Host: 192.168.137.1:8443\r\n"
             "Authorization: Bearer %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %u\r\n"
             "Connection: close\r\n\r\n%s",
             MONITOR_DEVICE_ID, MONITOR_DEVICE_TOKEN,
             (unsigned)strlen(body), body);
#else
    snprintf(request, sizeof(request),
             "GET /health HTTP/1.1\r\nHost: 192.168.137.1:8443\r\n"
             "Connection: close\r\n\r\n");
#endif
    altcp_write(pcb, request, strlen(request), TCP_WRITE_FLAG_COPY);
    altcp_output(pcb);
    printf("wifi: tls telemetry sent\r\n");
    audio_mode = false;
    audio_pending = false;
    return ERR_OK;
}
static void http_err(void *arg, err_t err)
{
    (void)arg;
    printf("wifi: tls error %d\r\n", (int)err);
    http_done = true;
    http_last_done = HAL_GetTick();
    http_pcb = NULL;
}

static void audio_pump(struct altcp_pcb *pcb)
{
    if (!audio_pending || pcb == NULL) {
        return;
    }
    const bool real = audio_capture_window_ready();
    const uint8_t *window = real ? (const uint8_t *)audio_capture_window_data() : NULL;
    if (!real) {
        audio_pending = false;
        return;
    }
    while (audio_remaining > 0) {
        uint16_t space = altcp_sndbuf(pcb);
        if (space < 256) {
            break;
        }
        uint16_t n = 1024;
        if (n > space) n = space;
        if (n > audio_remaining) n = (uint16_t)audio_remaining;
        const uint8_t *src = &window[audio_offset];
        if (altcp_write(pcb, src, n, TCP_WRITE_FLAG_COPY) != ERR_OK) {
            break;
        }
        audio_offset += n;
        audio_remaining -= n;
        altcp_output(pcb);
    }
    if (audio_remaining == 0) {
        audio_pending = false;
        printf("wifi: audio body sent, waiting response\r\n");
    }
}

static void http_start(void)
{
    http_status = 0;
    audio_mode = audio_capture_window_ready() && !last_audio_upload &&
                 telemetry_count < TELEMETRY_QUEUE_DEPTH;
    ip4_addr_t server;
    IP4_ADDR(&server, 192, 168, 137, 1);
    if (tls_config == NULL) {
        tls_config = altcp_tls_create_config_client(tls_ca_der, tls_ca_der_len);
    }
    if (tls_config == NULL) {
        printf("wifi: tls config failed\r\n");
        return;
    }
    http_pcb = altcp_tls_new(tls_config, IPADDR_TYPE_V4);
    if (http_pcb == NULL) {
        printf("wifi: no tls pcb\r\n");
        return;
    }
    altcp_recv(http_pcb, http_recv);
    altcp_err(http_pcb, http_err);
    mbedtls_ssl_context *ssl = (mbedtls_ssl_context *)altcp_tls_context(http_pcb);
    if (ssl != NULL) {
        mbedtls_ssl_set_hostname(ssl, "baby-monitor.local");
    }
    err_t err = altcp_connect(http_pcb, &server, 8443, http_connected);
    printf("wifi: tls connect err=%d\r\n", (int)err);
    http_started = true;
    http_next_attempt = HAL_GetTick() + 5000U;
}

static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    (void)netif;
    if (p->tot_len < 14U || p->tot_len > 1600U) {
        return ERR_IF;
    }
    uint8_t frame[1600];
    pbuf_copy_partial(p, frame, p->tot_len, 0);
    if (send_packet(DATA_TYPE_STA_ETH, frame, (uint16_t)p->tot_len)) {
        wifi_tx_frames++;
        return ERR_OK;
    }
    return ERR_IF;
}

static err_t ethernetif_init(struct netif *netif)
{
    netif->name[0] = 'r';
    netif->name[1] = 'w';
    netif->hwaddr_len = 6;
    memcpy(netif->hwaddr, wifi_mac, 6);
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
                   NETIF_FLAG_LINK_UP;
    netif->output = etharp_output;
    netif->linkoutput = low_level_output;
    return ERR_OK;
}

static void wifi_poll(void)
{
    uint8_t pkt[8 + 1600];
    uint32_t type = 0;
    uint16_t len = sizeof(pkt);
    const bool got = fetch_packet(pkt, &len, &type);
    if (got) {
        printf("wifi: rx type=%lu len=%u\r\n", (unsigned long)type, len);
    }
    if (got && type == DATA_TYPE_CB && len >= 8U) {
        printf("wifi: event %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
               pkt[0], pkt[1], pkt[2], pkt[3],
               pkt[4], pkt[5], pkt[6], pkt[7]);
        if (pkt[0] == 1U) {
            wifi_connect_seen = true;
        }
    }
    if (got && type == DATA_TYPE_STA_ETH && len >= 14U) {
        wifi_rx_frames++;
        struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
        if (p != NULL) {
            pbuf_take(p, pkt, len);
            if (wifi_netif.input(p, &wifi_netif) != ERR_OK) {
                pbuf_free(p);
            }
        }
    }
}

void wifi_net_publish_sensors(const SensorSnapshot *snapshot)
{
    latest_sensors = *snapshot;
    const uint32_t now = HAL_GetTick();
    if (telemetry_count > 0U && (now - telemetry_last_enqueue) < 5000U) {
        return;
    }
    telemetry_last_enqueue = now;
    taskENTER_CRITICAL();
    if (telemetry_count >= TELEMETRY_QUEUE_DEPTH) {
        telemetry_head = (telemetry_head + 1U) % TELEMETRY_QUEUE_DEPTH;
        telemetry_count--;
        telemetry_dropped++;
    }
    telemetry_queue[telemetry_tail].sensors = *snapshot;
    telemetry_queue[telemetry_tail].sequence = telemetry_next_sequence++;
    telemetry_tail = (telemetry_tail + 1U) % TELEMETRY_QUEUE_DEPTH;
    telemetry_count++;
    taskEXIT_CRITICAL();
}

void wifi_net_run(void)
{
#if !defined(HAVE_WIFI_CONFIG)
    printf("wifi: no local credentials; network task idle\r\n");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000U));
    }
#else
    printf("wifi: starting RW007 network\r\n");
    wifi_pins();
    wifi_reset();
    if (!wifi_ready()) {
        printf("wifi: module not ready\r\n");
    } else {
        HAL_Delay(200);
        struct join_payload join = {0};
        join.security = 0x00800004U; /* WPA3 AES PSK */
        join.band = 1;
        join.ssid_len = (uint8_t)strlen(MONITOR_WIFI_SSID);
        memcpy(join.ssid, MONITOR_WIFI_SSID, join.ssid_len);
        strncpy(join.passwd, MONITOR_WIFI_PASSWORD, sizeof(join.passwd) - 1);
        HAL_Delay(1000);
        uint32_t mode = 1;
        int32_t mode_result = -1;
        if (send_cmd(1, &mode, sizeof(mode)) &&
            wait_resp(1, 5000, &mode_result)) {
            printf("wifi: sta mode result=%ld\r\n", (long)mode_result);
        }
        HAL_Delay(500);
        int32_t result = -1;
        for (int attempt = 0; attempt < 3 && !wifi_connect_seen; attempt++) {
            bool sent = false;
            for (int i = 0; i < 4 && !sent; i++) {
                sent = send_cmd(7, &join, sizeof(join));
                if (!sent) {
                    HAL_Delay(500);
                }
            }
            if (sent && wait_resp(7, 10000, &result) && result == 0) {
                wifi_joined = true;
                printf("wifi: join accepted %s (attempt %d)\r\n",
                       MONITOR_WIFI_SSID, attempt + 1);
            }
            for (int i = 0; i < 50 && !wifi_connect_seen; i++) {
                uint8_t discard[8 + 1600];
                uint32_t type = 0;
                uint16_t len = sizeof(discard);
                if (fetch_packet(discard, &len, &type) && type == DATA_TYPE_CB &&
                    len >= 1U && discard[0] == 1U) {
                    wifi_connect_seen = true;
                }
                HAL_Delay(100);
            }
            if (!wifi_connect_seen) {
                HAL_Delay(500);
            }
        }
    }

    if (wifi_joined) {
        printf("wifi: connect event=%d\r\n", wifi_connect_seen ? 1 : 0);
        uint32_t promisc = 1;
        int32_t promisc_result = -1;
        if (send_cmd(16, &promisc, sizeof(promisc)) &&
            wait_resp(16, 5000, &promisc_result)) {
            printf("wifi: promisc result=%ld\r\n", (long)promisc_result);
        }
        uint8_t mac[6] = {0};
        uint16_t mac_len = sizeof(mac);
        if (send_cmd(2, NULL, 0) && read_value(2, mac, &mac_len, 5000) &&
            mac_len == 6) {
            memcpy(wifi_mac, mac, 6);
        }
        printf("wifi: mac=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
               wifi_mac[0], wifi_mac[1], wifi_mac[2],
               wifi_mac[3], wifi_mac[4], wifi_mac[5]);
        int32_t rssi = 0;
        uint8_t rssi_buf[4] = {0};
        uint16_t rssi_len = sizeof(rssi_buf);
        if (send_cmd(13, NULL, 0) &&
            read_value(13, rssi_buf, &rssi_len, 5000) && rssi_len >= 4) {
            rssi = (int32_t)get32(rssi_buf);
            printf("wifi: rssi=%ld dBm\r\n", (long)rssi);
        }
        lwip_init();
        ip4_addr_t ip = {0}, mask = {0}, gw = {0};
        netif_add(&wifi_netif, &ip, &mask, &gw, NULL,
                  ethernetif_init, ethernet_input);
        netif_set_default(&wifi_netif);
        netif_set_link_up(&wifi_netif);
        netif_set_up(&wifi_netif);
        const err_t dhcp_err = dhcp_start(&wifi_netif);
        printf("wifi: DHCP started err=%d\r\n", (int)dhcp_err);
    }

    uint32_t last_print = 0;
    for (;;) {
        if (wifi_joined) {
            wifi_poll();
            sys_check_timeouts();
            if (audio_pending && http_pcb != NULL) {
                audio_pump(http_pcb);
            }
            const bool bound = dhcp_supplied_address(&wifi_netif);
            if (bound && !http_started && !http_done) {
                http_start();
            } else if (bound && http_done &&
                       (HAL_GetTick() - http_last_done) >=
                           (telemetry_count > 4U ? 1000U : 5000U)) {
                http_done = false;
                http_started = false;
                http_rx_len = 0;
                audio_mode = false;
            } else if (bound && http_started && !http_done && !audio_mode &&
                       http_rx_len == 0 && (int32_t)(HAL_GetTick() - http_next_attempt) >= 0) {
                if (http_pcb != NULL) {
                    altcp_abort(http_pcb);
                    http_pcb = NULL;
                }
                http_started = false;
            }
        }
        const uint32_t now = HAL_GetTick();
        if (wifi_joined && now - last_print >= 1000U) {
            last_print = now;
            printf("wifi: ip=%s tx=%lu rx=%lu q=%lu dropped=%lu\r\n",
                   ip4addr_ntoa(&wifi_netif.ip_addr),
                   (unsigned long)wifi_tx_frames,
                   (unsigned long)wifi_rx_frames,
                   (unsigned long)telemetry_count,
                   (unsigned long)telemetry_dropped);
        }
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
#endif
}
