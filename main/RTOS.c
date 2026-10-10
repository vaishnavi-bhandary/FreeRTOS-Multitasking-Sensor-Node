#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "SENSOR_NODE";

/* Configuration Parameters */
#define QUEUE_LENGTH            10
#define SENSOR_TASK_CORE        0
#define TELEMETRY_TASK_CORE     1

#define SENSOR_TASK_PRIORITY    5  /* Higher priority: deterministic sampling */
#define TELEMETRY_TASK_PRIORITY 3  /* Lower priority: asynchronous transfer */

/* Shared Resource / Data Structures */
typedef struct {
    uint32_t timestamp_ms;
    float temperature;
    float humidity;
} sensor_data_t;

typedef struct {
    uint32_t total_samples;
    uint32_t dropped_packets;
} node_stats_t;

/* Global FreeRTOS Handles */
static QueueHandle_t xSensorQueue = NULL;
static SemaphoreHandle_t xStatsMutex = NULL;
static node_stats_t g_node_stats = {0, 0};

/* Mock Hardware Interfaces */
static float read_mock_temperature(void) {
    return 20.0f + ((float)(esp_random() % 150) / 10.0f);
}

static float read_mock_humidity(void) {
    return 40.0f + ((float)(esp_random() % 300) / 10.0f);
}

/**
 * @brief Sensor Sampling Task (Core 0)
 * Periodically samples sensor data and posts to telemetry queue.
 */
static void vSensorTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(100); // 10 Hz sampling rate
    sensor_data_t raw_data;

    ESP_LOGI(TAG, "Sensor Task running on Core %d", xPortGetCoreID());

    for (;;) {
        // Precise periodic execution
        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        raw_data.timestamp_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        raw_data.temperature = read_mock_temperature();
        raw_data.humidity = read_mock_humidity();

        // Safe state update protected by Mutex
        if (xSemaphoreTake(xStatsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            g_node_stats.total_samples++;
            xSemaphoreGive(xStatsMutex);
        }

        // Non-blocking send to telemetry queue
        if (xQueueSend(xSensorQueue, &raw_data, 0) != pdPASS) {
            ESP_LOGW(TAG, "Telemetry queue full! Packet dropped.");
            if (xSemaphoreTake(xStatsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                g_node_stats.dropped_packets++;
                xSemaphoreGive(xStatsMutex);
            }
        }
    }
}

/**
 * @brief Telemetry Dispatch Task (Core 1)
 * Blocks until queue data arrives, then transmits data via telemetry pipeline.
 */
static void vTelemetryTask(void *pvParameters) {
    sensor_data_t received_data;

    ESP_LOGI(TAG, "Telemetry Task running on Core %d", xPortGetCoreID());

    for (;;) {
        // Block indefinitely until telemetry data is queued
        if (xQueueReceive(xSensorQueue, &received_data, portMAX_DELAY) == pdTRUE) {
            // Simulate transmission over WiFi/MQTT or Cellular UART
            ESP_LOGI(TAG, "[Telemetry TX] Time: %lums | Temp: %.2fC | Hum: %.2f%%",
                     (unsigned long)received_data.timestamp_ms,
                     received_data.temperature,
                     received_data.humidity);

            // Print stats every 10 successful telemetry transmissions
            if (xSemaphoreTake(xStatsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                if (g_node_stats.total_samples % 10 == 0) {
                    ESP_LOGD(TAG, "Stats -> Total: %lu | Dropped: %lu",
                             (unsigned long)g_node_stats.total_samples,
                             (unsigned long)g_node_stats.dropped_packets);
                }
                xSemaphoreGive(xStatsMutex);
            }
            
            // Artificial network processing delay
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Initializing FreeRTOS Multitasking Sensor Node...");

    // Create Synchronization & Inter-Task Communication Primitives
    xSensorQueue = xQueueCreate(QUEUE_LENGTH, sizeof(sensor_data_t));
    xStatsMutex = xSemaphoreCreateMutex();

    if (xSensorQueue == NULL || xStatsMutex == NULL) {
        ESP_LOGE(TAG, "Failed to create RTOS primitives");
        return;
    }

    // Pin Sensor Sampling to Core 0
    xTaskCreatePinnedToCore(
        vSensorTask,
        "SensorTask",
        3072,
        NULL,
        SENSOR_TASK_PRIORITY,
        NULL,
        SENSOR_TASK_CORE
    );

    // Pin Telemetry Worker to Core 1
    xTaskCreatePinnedToCore(
        vTelemetryTask,
        "TelemetryTask",
        3072,
        NULL,
        TELEMETRY_TASK_PRIORITY,
        NULL,
        TELEMETRY_TASK_CORE
    );
}
