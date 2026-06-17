#include <stdbool.h>
#include <math.h>

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

#define CONTROL_INTERVAL_MS 100
#define SERIAL_INTERVAL_MS 1000
#define SENSOR_FILTER_ALPHA 0.12f
#define SET_ENTER_BAND 1.0f
#define SET_EXIT_BAND 1.0f

#define PWM_FREQUENCY_HZ 5000
#define PWM_RESOLUTION LEDC_TIMER_10_BIT
#define PWM_MAX_DUTY ((1U << 10) - 1)
#define PWM_TIMER LEDC_TIMER_0
#define PWM_CHANNEL LEDC_CHANNEL_0
#define PWM_MODE LEDC_LOW_SPEED_MODE

typedef struct {
    float setpoint;
    float kp;
    float ki;
    float kd;
    float out_min;
    float out_max;
    bool invert_sensor;
} pid_config_t;

typedef struct {
    float input;
    float raw_percent;
    float output;
    float error;
    float integral;
    float derivative;
    float last_error;
    int raw;
    bool in_set;
    bool filter_ready;
} runtime_state_t;

static pid_config_t config = {
    .setpoint = 55.0f,
    .kp = 7.0f,
    .ki = 0.6f,
    .kd = 0.15f,
    .out_min = 0.0f,
    .out_max = (float)PWM_MAX_DUTY,
    .invert_sensor = false,
};

static runtime_state_t state = {0};

static float clamp_float(float value, float minimum, float maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

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
    float percent = ((float)(*raw) * 100.0f) / ADC_MAX_RAW;
    if (config.invert_sensor) {
        percent = 100.0f - percent;
    }

    return clamp_float(percent, 0.0f, 100.0f);
}

static float read_filtered_light_percent(adc_oneshot_unit_handle_t adc_handle)
{
    state.raw_percent = read_light_percent(adc_handle, &state.raw);

    if (!state.filter_ready) {
        state.input = state.raw_percent;
        state.filter_ready = true;
    } else {
        state.input += SENSOR_FILTER_ALPHA * (state.raw_percent - state.input);
    }

    return state.input;
}

static void update_status_leds(void)
{
    float abs_error = fabsf(state.error);
    if (state.in_set) {
        state.in_set = abs_error <= SET_EXIT_BAND;
    } else {
        state.in_set = abs_error <= SET_ENTER_BAND;
    }

    gpio_set_level(PIN_LED_GREEN, state.in_set);
    gpio_set_level(PIN_LED_RED, !state.in_set);
}

static void update_pid(adc_oneshot_unit_handle_t adc_handle)
{
    state.input = read_filtered_light_percent(adc_handle);
    state.error = config.setpoint - state.input;
    state.integral += state.error * ((float)CONTROL_INTERVAL_MS / 1000.0f);
    state.derivative = (state.error - state.last_error) / ((float)CONTROL_INTERVAL_MS / 1000.0f);

    float unclamped = (config.kp * state.error) +
                      (config.ki * state.integral) +
                      (config.kd * state.derivative);
    state.output = clamp_float(unclamped, config.out_min, config.out_max);

    bool saturated_high = unclamped > config.out_max && state.error > 0.0f;
    bool saturated_low = unclamped < config.out_min && state.error < 0.0f;
    if (saturated_high || saturated_low) {
        state.integral -= state.error * ((float)CONTROL_INTERVAL_MS / 1000.0f);
    }

    state.last_error = state.error;
    write_white_led_pwm((uint32_t)state.output);
    update_status_leds();
}

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
