#include "esp_idf_version.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ESP-PID-IDF";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP-PID-IDF iniciado");
    ESP_LOGI(TAG, "ESP-IDF version: %s", esp_get_idf_version());

    while (true) {
        ESP_LOGI(TAG, "loop vivo");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
