#include "app_tasks.h"
#include "stm32f4xx_hal.h"
#include "alert_logic.h"
#include "audio_capture.h"
#include "board_io.h"
#include "monitor_ui.h"
#include "queue.h"
#include "sensors.h"
#include "wifi_net.h"

#include <stddef.h>
#include <stdio.h>

typedef struct {
    uint32_t sequence;
    TickType_t captured_tick;
    uint8_t sensor_data_available;
} SensorHeartbeat;

static QueueHandle_t sensor_queue;
static TaskHandle_t task_handles[5];
static volatile SensorSnapshot latest_snapshot;
static volatile bool latest_snapshot_valid;
volatile MonitorDiagnostics g_monitor_diagnostics;

enum { TASK_SENSOR = 0, TASK_AUDIO, TASK_NETWORK, TASK_LOCAL_UI, TASK_HEALTH };

static void record_wake_delay(size_t task, TickType_t scheduled)
{
    const TickType_t now = xTaskGetTickCount();
    if (now > scheduled) {
        const uint32_t delay_ms = (uint32_t)(((now - scheduled) * 1000U) / configTICK_RATE_HZ);
        if (delay_ms > g_monitor_diagnostics.max_task_wake_delay_ms[task]) {
            g_monitor_diagnostics.max_task_wake_delay_ms[task] = delay_ms;
        }
    }
}

void monitor_assert_failed(const char *file, unsigned line)
{
    (void)file;
    (void)line;
    __disable_irq();
    for (;;) { __NOP(); }
}

void vApplicationMallocFailedHook(void)
{
    monitor_assert_failed(__FILE__, __LINE__);
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    monitor_assert_failed(__FILE__, __LINE__);
}

static void SensorTask(void *argument)
{
    (void)argument;
    sensors_init();
    TickType_t wake_time = xTaskGetTickCount();
    uint32_t sequence = 0U;
    for (;;) {
        record_wake_delay(TASK_SENSOR, wake_time);
        SensorSnapshot snapshot;
        sensors_read(&snapshot);
        wifi_net_publish_sensors(&snapshot);
        const bool all_valid = snapshot.temperature_valid &&
                               snapshot.humidity_valid &&
                               snapshot.light_valid &&
                               snapshot.proximity_valid &&
                               snapshot.motion_valid;
        const SensorHeartbeat heartbeat = {
            .sequence = sequence++,
            .captured_tick = xTaskGetTickCount(),
            .sensor_data_available = all_valid ? 1U : 0U,
        };
        (void)xQueueOverwrite(sensor_queue, &heartbeat);
        taskENTER_CRITICAL();
        latest_snapshot = snapshot;
        latest_snapshot_valid = true;
        taskEXIT_CRITICAL();
        g_monitor_diagnostics.sensor_heartbeats++;
        g_monitor_diagnostics.hardware_unverified = all_valid ? 0U : 1U;
        vTaskDelayUntil(&wake_time, pdMS_TO_TICKS(1000U));
    }
}

static void AudioTask(void *argument)
{
    (void)argument;
    audio_capture_init();
    TickType_t wake_time = xTaskGetTickCount();
    for (;;) {
        record_wake_delay(TASK_AUDIO, wake_time);
        audio_capture_poll();
        vTaskDelayUntil(&wake_time, pdMS_TO_TICKS(5U));
    }
}

static void NetworkTask(void *argument)
{
    (void)argument;
    wifi_net_run();
}

static void LocalUiTask(void *argument)
{
    (void)argument;
    monitor_ui_init();
    TickType_t wake_time = xTaskGetTickCount();
    uint32_t last_led = 0U;
    for (;;) {
        record_wake_delay(TASK_LOCAL_UI, wake_time);
        SensorSnapshot snapshot = {0};
        taskENTER_CRITICAL();
        if (latest_snapshot_valid) {
            snapshot = latest_snapshot;
        }
        taskEXIT_CRITICAL();
        monitor_ui_step(&snapshot);
        const uint32_t now = HAL_GetTick();
        if ((now - last_led) >= 500U) {
            last_led = now;
            board_led_toggle();
        }
        vTaskDelayUntil(&wake_time, pdMS_TO_TICKS(50U));
    }
}

static void HealthTask(void *argument)
{
    (void)argument;
    TickType_t wake_time = xTaskGetTickCount();
    for (;;) {
        record_wake_delay(TASK_HEALTH, wake_time);
        g_monitor_diagnostics.minimum_free_heap = xPortGetMinimumEverFreeHeapSize();
        for (size_t index = 0U; index < 5U; ++index) {
            if (task_handles[index] != NULL) {
                g_monitor_diagnostics.stack_high_water_words[index] =
                    uxTaskGetStackHighWaterMark(task_handles[index]);
            }
        }
        printf("health: heartbeats=%lu heap_min=%lu stacks=%lu/%lu/%lu/%lu/%lu wake_ms=%lu/%lu/%lu/%lu/%lu\r\n",
               (unsigned long)g_monitor_diagnostics.sensor_heartbeats,
               (unsigned long)g_monitor_diagnostics.minimum_free_heap,
               (unsigned long)g_monitor_diagnostics.stack_high_water_words[TASK_SENSOR],
               (unsigned long)g_monitor_diagnostics.stack_high_water_words[TASK_AUDIO],
               (unsigned long)g_monitor_diagnostics.stack_high_water_words[TASK_NETWORK],
               (unsigned long)g_monitor_diagnostics.stack_high_water_words[TASK_LOCAL_UI],
               (unsigned long)g_monitor_diagnostics.stack_high_water_words[TASK_HEALTH],
               (unsigned long)g_monitor_diagnostics.max_task_wake_delay_ms[TASK_SENSOR],
               (unsigned long)g_monitor_diagnostics.max_task_wake_delay_ms[TASK_AUDIO],
               (unsigned long)g_monitor_diagnostics.max_task_wake_delay_ms[TASK_NETWORK],
               (unsigned long)g_monitor_diagnostics.max_task_wake_delay_ms[TASK_LOCAL_UI],
               (unsigned long)g_monitor_diagnostics.max_task_wake_delay_ms[TASK_HEALTH]);
        vTaskDelayUntil(&wake_time, pdMS_TO_TICKS(1000U));
    }
}

void monitor_tasks_start(void)
{
    sensor_queue = xQueueCreate(1U, sizeof(SensorHeartbeat));
    configASSERT(sensor_queue != NULL);
    vQueueAddToRegistry(sensor_queue, "sensor-latest");
    configASSERT(xTaskCreate(SensorTask, "Sensors", 256U, NULL, 4U,
                             &task_handles[TASK_SENSOR]) == pdPASS);
    configASSERT(xTaskCreate(AudioTask, "Audio", 384U, NULL, 5U,
                             &task_handles[TASK_AUDIO]) == pdPASS);
    configASSERT(xTaskCreate(NetworkTask, "Network", 2048U, NULL, 2U,
                             &task_handles[TASK_NETWORK]) == pdPASS);
    configASSERT(xTaskCreate(LocalUiTask, "LocalUI", 512U, NULL, 3U,
                             &task_handles[TASK_LOCAL_UI]) == pdPASS);
    configASSERT(xTaskCreate(HealthTask, "Health", 256U, NULL, 1U,
                             &task_handles[TASK_HEALTH]) == pdPASS);
    vTaskStartScheduler();
    monitor_assert_failed(__FILE__, __LINE__);
}
