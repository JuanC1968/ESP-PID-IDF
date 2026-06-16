#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ESP-PID-IDF";

#define PIN_LED_GREEN GPIO_NUM_26
#define PIN_LED_RED GPIO_NUM_27

static void configure_status_leds(void)
{
    gpio_config_t config = {
        .pin_bit_mask = (1ULL << PIN_LED_GREEN) | (1ULL << PIN_LED_RED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&config));
    gpio_set_level(PIN_LED_GREEN, 0);
    gpio_set_level(PIN_LED_RED, 1);
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP-PID-IDF iniciado");
    ESP_LOGI(TAG, "ESP-IDF version: %s", esp_get_idf_version());

    configure_status_leds();

    bool green_on = false;
    while (true) {
        green_on = !green_on;
        gpio_set_level(PIN_LED_GREEN, green_on);
        gpio_set_level(PIN_LED_RED, !green_on);

        ESP_LOGI(TAG, "loop vivo: verde=%s rojo=%s",
                 green_on ? "ON" : "OFF",
                 green_on ? "OFF" : "ON");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
