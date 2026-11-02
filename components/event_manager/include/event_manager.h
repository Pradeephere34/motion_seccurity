#ifndef EVENT_MANAGER_H
#define EVENT_MANAGER_H

#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum {
    SECURITY_EVENT_MOTION_DETECTED = 0,
    SECURITY_EVENT_MOTION_STATE_CHANGED,
    SECURITY_EVENT_ALARM_REQUESTED,
    SECURITY_EVENT_ALARM_TIMEOUT,
    SECURITY_EVENT_ARM_COMMAND,
    SECURITY_EVENT_DISARM_COMMAND,
    SECURITY_EVENT_SENSOR_FAULT,
    SECURITY_EVENT_NETWORK_STATUS_CHANGED,
    SECURITY_EVENT_SYSTEM_STATE_CHANGED,
    SECURITY_EVENT_TYPE_COUNT
} security_event_type_t;

typedef struct {
    security_event_type_t type;
    int64_t timestamp_us;
    uint16_t source_id;
    int32_t value;
    int32_t previous_value;
    uint32_t detail;
} security_event_t;

typedef struct {
    QueueHandle_t queue;
} event_manager_t;

/** Initialize a bounded event queue. The manager must be zero-initialized. */
esp_err_t event_manager_init(event_manager_t *manager, UBaseType_t queue_length);

/** Delete the queue after all producer and consumer tasks have stopped. */
void event_manager_deinit(event_manager_t *manager);

/** Queue an event, optionally waiting up to wait_ticks for space. */
esp_err_t event_manager_post(event_manager_t *manager,
                             const security_event_t *event,
                             TickType_t wait_ticks);

/** Receive an event, optionally waiting up to wait_ticks for an event. */
esp_err_t event_manager_receive(event_manager_t *manager,
                                security_event_t *event,
                                TickType_t wait_ticks);

#endif