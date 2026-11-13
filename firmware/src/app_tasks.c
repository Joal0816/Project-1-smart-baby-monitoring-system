#include "app_tasks.h"
#include "stm32f4xx_hal.h"
#include "alert_logic.h"
#include "audio_capture.h"
#include "board_io.h"
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
volatile MonitorDiagnostics g_monitor_diagnostics;

enum { TASK_SENSOR = 0, TASK_AUDIO, TASK_NETWORK, TASK_LOCAL_UI, TASK_HEALTH };

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
        g_monitor_diagnostics.sensor_heartbeats++;
        g_monitor_diagnostics.hardware_unverified = all_valid ? 0U : 1U;
        vTaskDelayUntil(&wake_time, pdMS_TO_TICKS(1000U));
    }
}

static void AudioTask(void *argument)
{
    (void)argument;
    audio_capture_init();
    for (;;) {
        audio_capture_poll();
        vTaskDelay(pdMS_TO_TICKS(5U));
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
    for (;;) {
        /* PF12 is the verified onboard red LED; display, buzzer, and buttons stay inactive. */
        board_led_toggle();
        vTaskDelay(pdMS_TO_TICKS(500U));
    }
}

static void HealthTask(void *argument)
{
    (void)argument;
    for (;;) {
        g_monitor_diagnostics.minimum_free_heap = xPortGetMinimumEverFreeHeapSize();
        for (size_t index = 0U; index < 5U; ++index) {
            if (task_handles[index] != NULL) {
                g_monitor_diagnostics.stack_high_water_words[index] =
                    uxTaskGetStackHighWaterMark(task_handles[index]);
            }
        }
        printf("health: heartbeats=%lu heap_min=%lu stacks=%lu/%lu/%lu/%lu/%lu\r\n",
               (unsigned long)g_monitor_diagnostics.sensor_heartbeats,
               (unsigned long)g_monitor_diagnostics.minimum_free_heap,
               (unsigned long)g_monitor_diagnostics.stack_high_water_words[TASK_SENSOR],
               (unsigned long)g_monitor_diagnostics.stack_high_water_words[TASK_AUDIO],
               (unsigned long)g_monitor_diagnostics.stack_high_water_words[TASK_NETWORK],
               (unsigned long)g_monitor_diagnostics.stack_high_water_words[TASK_LOCAL_UI],
               (unsigned long)g_monitor_diagnostics.stack_high_water_words[TASK_HEALTH]);
        vTaskDelay(pdMS_TO_TICKS(1000U));
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
    configASSERT(xTaskCreate(LocalUiTask, "LocalUI", 256U, NULL, 3U,
                             &task_handles[TASK_LOCAL_UI]) == pdPASS);
    configASSERT(xTaskCreate(HealthTask, "Health", 256U, NULL, 1U,
                             &task_handles[TASK_HEALTH]) == pdPASS);
    vTaskStartScheduler();
    monitor_assert_failed(__FILE__, __LINE__);
}
