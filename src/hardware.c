#include "hardware.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"

#include "app_config.h"

// Los LEDs de estado son salidas digitales normales.
void configure_status_leds(void)
{
    gpio_config_t config = {
        .pin_bit_mask = (1ULL << PIN_LED_GREEN) | (1ULL << PIN_LED_RED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&config));
    set_status_leds(false);
}

// LEDC separa la configuracion en dos piezas:
// - timer: frecuencia y resolucion del PWM
// - channel: pin fisico y canal que usara ese timer
void configure_white_led_pwm(void)
{
    ledc_timer_config_t timer_config = {
        .speed_mode = PWM_MODE,
        .duty_resolution = PWM_RESOLUTION,
        .timer_num = PWM_TIMER,
        .freq_hz = PWM_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    ledc_channel_config_t channel_config = {
        .gpio_num = PIN_LED_WHITE,
        .speed_mode = PWM_MODE,
        .channel = PWM_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = PWM_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));
}

// LEDC no aplica el duty hasta llamar a ledc_update_duty().
void write_white_led_pwm(uint32_t duty)
{
    if (duty > PWM_MAX_DUTY) {
        duty = PWM_MAX_DUTY;
    }

    ESP_ERROR_CHECK(ledc_set_duty(PWM_MODE, PWM_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(PWM_MODE, PWM_CHANNEL));
}

// Verde = dentro de tolerancia; rojo = fuera de tolerancia.
void set_status_leds(bool in_set)
{
    gpio_set_level(PIN_LED_GREEN, in_set);
    gpio_set_level(PIN_LED_RED, !in_set);
}
