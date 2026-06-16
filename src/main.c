#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ESP-PID-IDF";

#define PIN_LED_WHITE GPIO_NUM_25
#define PIN_LED_GREEN GPIO_NUM_26
#define PIN_LED_RED GPIO_NUM_27

#define ADC_LDR_UNIT ADC_UNIT_1
#define ADC_LDR_CHANNEL ADC_CHANNEL_6
#define ADC_MAX_RAW 4095
#define ADC_SAMPLES 16
#define ADC_SAMPLE_DELAY_US 250

#define PWM_FREQUENCY_HZ 5000
#define PWM_RESOLUTION LEDC_TIMER_10_BIT
#define PWM_MAX_DUTY ((1U << 10) - 1)
#define PWM_TIMER LEDC_TIMER_0
#define PWM_CHANNEL LEDC_CHANNEL_0
#define PWM_MODE LEDC_LOW_SPEED_MODE

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

static void configure_white_led_pwm(void)
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

static void write_white_led_pwm(uint32_t duty)
{
    if (duty > PWM_MAX_DUTY) {
        duty = PWM_MAX_DUTY;
    }

    ESP_ERROR_CHECK(ledc_set_duty(PWM_MODE, PWM_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(PWM_MODE, PWM_CHANNEL));
}

static adc_oneshot_unit_handle_t configure_ldr_adc(void)
{
    adc_oneshot_unit_handle_t adc_handle = NULL;
    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_LDR_UNIT,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &adc_handle));

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_LDR_CHANNEL, &channel_config));

    return adc_handle;
}

static float read_light_percent(adc_oneshot_unit_handle_t adc_handle, int *raw)
{
    uint32_t accumulator = 0;

    for (uint8_t i = 0; i < ADC_SAMPLES; i++) {
        int sample = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_LDR_CHANNEL, &sample));
        accumulator += (uint32_t)sample;
        esp_rom_delay_us(ADC_SAMPLE_DELAY_US);
    }

    *raw = (int)(accumulator / ADC_SAMPLES);
    return ((float)(*raw) * 100.0f) / ADC_MAX_RAW;
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP-PID-IDF iniciado");
    ESP_LOGI(TAG, "ESP-IDF version: %s", esp_get_idf_version());

    configure_status_leds();
    configure_white_led_pwm();
    adc_oneshot_unit_handle_t adc_handle = configure_ldr_adc();

    bool green_on = false;
    int32_t pwm_duty = 0;
    int32_t pwm_step = PWM_MAX_DUTY / 4;

    while (true) {
        green_on = !green_on;
        gpio_set_level(PIN_LED_GREEN, green_on);
        gpio_set_level(PIN_LED_RED, !green_on);
        write_white_led_pwm((uint32_t)pwm_duty);

        int raw = 0;
        float light_percent = read_light_percent(adc_handle, &raw);

        ESP_LOGI(TAG, "loop vivo: verde=%s rojo=%s pwm=%ld/%u adc=%d luz=%.1f%%",
                 green_on ? "ON" : "OFF",
                 green_on ? "OFF" : "ON",
                 (long)pwm_duty,
                 PWM_MAX_DUTY,
                 raw,
                 light_percent);

        pwm_duty += pwm_step;
        if (pwm_duty >= (int32_t)PWM_MAX_DUTY) {
            pwm_duty = PWM_MAX_DUTY;
            pwm_step = -pwm_step;
        } else if (pwm_duty <= 0) {
            pwm_duty = 0;
            pwm_step = -pwm_step;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
