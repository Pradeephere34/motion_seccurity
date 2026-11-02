#include "event_manager.h"

esp_err_t event_manager_init(event_manager_t *manager, UBaseType_t queue_length)
{
    if (manager == NULL || queue_length == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (manager->queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    manager->queue = xQueueCreate(queue_length, sizeof(security_event_t));
    return manager->queue == NULL ? ESP_ERR_NO_MEM : ESP_OK;
}

void event_manager_deinit(event_manager_t *manager)
{
    if (manager != NULL && manager->queue != NULL) {
        vQueueDelete(manager->queue);
        manager->queue = NULL;
    }
}

esp_err_t event_manager_post(event_manager_t *manager,
                             const security_event_t *event,
                             TickType_t wait_ticks)
{
    if (manager == NULL || event == NULL ||
        event->type < SECURITY_EVENT_MOTION_DETECTED ||
        event->type >= SECURITY_EVENT_TYPE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (manager->queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return xQueueSend(manager->queue, event, wait_ticks) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t event_manager_receive(event_manager_t *manager,
                                security_event_t *event,
                                TickType_t wait_ticks)
{
    if (manager == NULL || event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (manager->queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return xQueueReceive(manager->queue, event, wait_ticks) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}