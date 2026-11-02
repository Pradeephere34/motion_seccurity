#include "pir.h"

#include <stddef.h>

esp_err_t pir_init(pir_sensor_t *sensor, gpio_num_t gpio_num)
{
    if (sensor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (gpio_num < 0 || gpio_num >= GPIO_NUM_MAX) {
        sensor->initialized = false;
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t config = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t error = gpio_config(&config);
    if (error != ESP_OK) {
        sensor->initialized = false;
        return error;
    }

    sensor->gpio_num = gpio_num;
    sensor->initialized = true;
    return ESP_OK;
}

esp_err_t pir_read(const pir_sensor_t *sensor, bool *motion_detected)
{
    if (sensor == NULL || motion_detected == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!sensor->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    *motion_detected = gpio_get_level(sensor->gpio_num) == 1;
    return ESP_OK;
}
