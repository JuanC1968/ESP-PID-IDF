#include "esp_adc/adc_oneshot.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "app_state.h"
#include "control.h"
#include "hardware.h"
#include "sensor.h"

static const char *TAG = "ESP-PID-IDF";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP-PID-IDF iniciado");
    ESP_LOGI(TAG, "ESP-IDF version: %s", esp_get_idf_version());

    configure_status_leds();
    configure_white_led_pwm();
    adc_oneshot_unit_handle_t adc_handle = configure_ldr_adc();

    uint32_t elapsed_ms = 0;
    while (true) {
        update_pid(adc_handle);
        elapsed_ms += CONTROL_INTERVAL_MS;

        if (elapsed_ms >= SERIAL_INTERVAL_MS) {
            elapsed_ms = 0;
            ESP_LOGI(TAG,
                     "PV=%.1f%% RAW=%.1f%% ADC=%d SP=%.1f%% Error=%.1f PWM=%.0f Estado=%s",
                     state.input,
                     state.raw_percent,
                     state.raw,
                     config.setpoint,
                     state.error,
                     state.output,
                     state.in_set ? "SET" : "NO SET");
        }

        vTaskDelay(pdMS_TO_TICKS(CONTROL_INTERVAL_MS));
    }
}
