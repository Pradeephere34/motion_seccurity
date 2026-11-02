#include "app_main.h"

#include <stdbool.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "freertos/event_groups.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "event_manager.h"
#include "pir.h"

#define APP_EVENT_QUEUE_LENGTH 16U
#define WIFI_CONNECTED_BIT BIT0
#define MQTT_CONNECTED_BIT BIT1
#define SENSOR_SAMPLE_PERIOD_MS 200U
#define EVENT_WAIT_PERIOD_MS 100U

static const char *TAG = "motion_security";
static event_manager_t event_manager;
static EventGroupHandle_t connection_events;
static esp_mqtt_client_handle_t mqtt_client;

static void wifi_event_handler(void *argument,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)argument;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_err_t error = esp_wifi_connect();
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "Could not start Wi-Fi connection: %s", esp_err_to_name(error));
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(connection_events, WIFI_CONNECTED_BIT);
        esp_err_t error = esp_wifi_connect();
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "Could not reconnect to Wi-Fi: %s", esp_err_to_name(error));
        }
        ESP_LOGW(TAG, "Wi-Fi disconnected; reconnecting");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(connection_events, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Connected to Wi-Fi");
    }
}

static esp_err_t wifi_start(void)
{
    esp_err_t error = esp_netif_init();
    if (error != ESP_OK) {
        return error;
    }

    error = esp_event_loop_create_default();
    if (error != ESP_OK) {
        return error;
    }

    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_FAIL;
    }

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    error = esp_wifi_init(&wifi_init_config);
    if (error != ESP_OK) {
        return error;
    }

    error = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    if (error != ESP_OK) {
        return error;
    }
    error = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);
    if (error != ESP_OK) {
        return error;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_MOTION_WIFI_SSID,
            .password = CONFIG_MOTION_WIFI_PASSWORD,
        },
    };

    error = esp_wifi_set_mode(WIFI_MODE_STA);
    if (error != ESP_OK) {
        return error;
    }
    error = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (error != ESP_OK) {
        return error;
    }
    return esp_wifi_start();
}

static void mqtt_event_handler(void *argument,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)argument;
    (void)event_base;
    (void)event_data;

    if (event_id == MQTT_EVENT_CONNECTED) {
        xEventGroupSetBits(connection_events, MQTT_CONNECTED_BIT);
        ESP_LOGI(TAG, "Connected to MQTT broker");
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        xEventGroupClearBits(connection_events, MQTT_CONNECTED_BIT);
        ESP_LOGW(TAG, "Disconnected from MQTT broker");
    } else if (event_id == MQTT_EVENT_ERROR) {
        ESP_LOGE(TAG, "MQTT client reported a connection error");
    }
}

static esp_err_t mqtt_start(void)
{
    const esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = CONFIG_MOTION_MQTT_BROKER_URI,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_config);
    if (mqtt_client == NULL) {
        return ESP_FAIL;
    }

    esp_err_t error = esp_mqtt_client_register_event(
        mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (error != ESP_OK) {
        return error;
    }
    return esp_mqtt_client_start(mqtt_client);
}

static void pir_monitor_task(void *argument)
{
    (void)argument;
    pir_sensor_t sensor = {0};
    esp_err_t error = pir_init(&sensor, (gpio_num_t)CONFIG_MOTION_PIR_GPIO);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "PIR initialization failed: %s", esp_err_to_name(error));
        vTaskDelete(NULL);
        return;
    }

    bool has_previous_state = false;
    bool previous_motion = false;

    for (;;) {
        bool motion_detected = false;
        error = pir_read(&sensor, &motion_detected);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "PIR read failed: %s", esp_err_to_name(error));
        } else if (!has_previous_state || motion_detected != previous_motion) {
            security_event_t event = {
                .type = SECURITY_EVENT_MOTION_STATE_CHANGED,
                .timestamp_us = esp_timer_get_time(),
                .source_id = (uint16_t)CONFIG_MOTION_PIR_GPIO,
                .value = motion_detected ? 1 : 0,
                .previous_value = has_previous_state && previous_motion ? 1 : 0,
            };
            error = event_manager_post(&event_manager, &event, 0);
            if (error != ESP_OK) {
                ESP_LOGE(TAG, "Could not queue PIR state: %s", esp_err_to_name(error));
            } else {
                previous_motion = motion_detected;
                has_previous_state = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(SENSOR_SAMPLE_PERIOD_MS));
    }
}

static void mqtt_publish_task(void *argument)
{
    (void)argument;
    bool has_state = false;
    bool motion_detected = false;
    bool published = false;
    int64_t state_timestamp_us = 0;

    for (;;) {
        security_event_t event = {0};
        esp_err_t error = event_manager_receive(
            &event_manager, &event, pdMS_TO_TICKS(EVENT_WAIT_PERIOD_MS));
        if (error == ESP_OK && event.type == SECURITY_EVENT_MOTION_STATE_CHANGED) {
            motion_detected = event.value != 0;
            state_timestamp_us = event.timestamp_us;
            has_state = true;
            published = false;
        } else if (error != ESP_OK && error != ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "Could not receive PIR state: %s", esp_err_to_name(error));
        }

        const EventBits_t bits = xEventGroupGetBits(connection_events);
        if ((bits & MQTT_CONNECTED_BIT) == 0) {
            published = false;
        } else if (has_state && !published) {
            char payload[96];
            int payload_length = snprintf(payload,
                                          sizeof(payload),
                                          "{\"motion\":%s,\"gpio\":%d,\"timestamp_us\":%lld}",
                                          motion_detected ? "true" : "false",
                                          CONFIG_MOTION_PIR_GPIO,
                                          (long long)state_timestamp_us);
            if (payload_length < 0 || (size_t)payload_length >= sizeof(payload)) {
                ESP_LOGE(TAG, "Could not format MQTT payload");
            } else {
                int message_id = esp_mqtt_client_publish(mqtt_client,
                                                        CONFIG_MOTION_MQTT_TOPIC,
                                                        payload,
                                                        payload_length,
                                                        1,
                                                        1);
                if (message_id < 0) {
                    ESP_LOGE(TAG, "Could not queue MQTT motion update");
                } else {
                    published = true;
                    ESP_LOGI(TAG, "Published motion=%s",
                             motion_detected ? "true" : "false");
                }
            }
        }
    }
}

void app_main(void)
{
    if (CONFIG_MOTION_WIFI_SSID[0] == '\0') {
        ESP_LOGE(TAG, "Set the Wi-Fi SSID in menuconfig under Motion Security IoT");
        return;
    }

    if (CONFIG_MOTION_MQTT_BROKER_URI[0] == '\0') {
        ESP_LOGE(TAG, "Set the MQTT broker URI in menuconfig under Motion Security IoT");
        return;
    }

    esp_err_t error = nvs_flash_init();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(error));
        return;
    }

    connection_events = xEventGroupCreate();
    if (connection_events == NULL) {
        ESP_LOGE(TAG, "Could not create connection event group");
        return;
    }

    error = event_manager_init(&event_manager, APP_EVENT_QUEUE_LENGTH);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Event manager initialization failed: %s", esp_err_to_name(error));
        return;
    }

    error = wifi_start();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi initialization failed: %s", esp_err_to_name(error));
        return;
    }

    error = mqtt_start();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "MQTT initialization failed: %s", esp_err_to_name(error));
        return;
    }

    if (xTaskCreate(pir_monitor_task, "pir_monitor", 3072, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Could not create PIR monitor task");
        return;
    }
    if (xTaskCreate(mqtt_publish_task, "mqtt_publish", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Could not create MQTT publish task");
        return;
    }

    ESP_LOGI(TAG, "PIR-to-MQTT reporting started on GPIO %d", CONFIG_MOTION_PIR_GPIO);
}