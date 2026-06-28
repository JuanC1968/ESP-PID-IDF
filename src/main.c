#include "esp_adc/adc_oneshot.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "app_state.h"
#include "control.h"
#include "display_ui.h"
#include "hardware.h"
#include "rs485.h"
#include "sensor.h"
#include "storage.h"
#include "web_server.h"

static const char *TAG = "ESP-PID-IDF";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP-PID-IDF iniciado");
    ESP_LOGI(TAG, "ESP-IDF version: %s", esp_get_idf_version());

    ESP_ERROR_CHECK(init_storage());
    ESP_ERROR_CHECK(load_pid_config());

    // Inicializacion de perifericos. La pantalla se configura antes del ADC y la
    // web porque no es critica: si no aparece, el firmware sigue adelante.
    configure_status_leds();
    configure_white_led_pwm();
    configure_display();
    configure_rs485();
    adc_oneshot_unit_handle_t adc_handle = configure_ldr_adc();
    start_web_server();

    uint32_t elapsed_ms = 0;
    uint32_t display_elapsed_ms = 0;
    while (true) {
        // El lazo principal es cooperativo: una iteracion de control y una pausa
        // fija. No hay interrupciones ni tareas extra para el PID.
        update_pid(adc_handle);
        elapsed_ms += CONTROL_INTERVAL_MS;
        display_elapsed_ms += CONTROL_INTERVAL_MS;

        // El log serie sirve como telemetria humana, no como parte del control.
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
            send_rs485_telemetry();
        }

        // La OLED se refresca mas despacio que el PID para ahorrar tiempo I2C.
        if (display_elapsed_ms >= DISPLAY_INTERVAL_MS) {
            display_elapsed_ms = 0;
            draw_display();
        }

        vTaskDelay(pdMS_TO_TICKS(CONTROL_INTERVAL_MS));
    }
}
