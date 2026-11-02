#ifndef PIR_H
#define PIR_H

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
    gpio_num_t gpio_num;
    bool initialized;
} pir_sensor_t;

esp_err_t pir_init(pir_sensor_t *sensor, gpio_num_t gpio_num);
esp_err_t pir_read(const pir_sensor_t *sensor, bool *motion_detected);

#endif
