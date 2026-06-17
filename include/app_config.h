#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "hal/adc_types.h"

#define PIN_LED_WHITE GPIO_NUM_25
#define PIN_LED_GREEN GPIO_NUM_26
#define PIN_LED_RED GPIO_NUM_27
#define PIN_I2C_SDA GPIO_NUM_21
#define PIN_I2C_SCL GPIO_NUM_22

#define ADC_LDR_UNIT ADC_UNIT_1
#define ADC_LDR_CHANNEL ADC_CHANNEL_6
#define ADC_MAX_RAW 4095
#define ADC_SAMPLES 16
#define ADC_SAMPLE_DELAY_US 250

#define CONTROL_INTERVAL_MS 100
#define DISPLAY_INTERVAL_MS 250
#define SERIAL_INTERVAL_MS 1000
#define SENSOR_FILTER_ALPHA 0.12f
#define SET_ENTER_BAND 1.0f
#define SET_EXIT_BAND 1.0f

#define OLED_I2C_PORT I2C_NUM_0
#define OLED_I2C_FREQUENCY_HZ 400000
#define OLED_ADDRESS_PRIMARY 0x3C
#define OLED_ADDRESS_SECONDARY 0x3D
#define OLED_WIDTH 128
#define OLED_HEIGHT 128
#define OLED_PAGES (OLED_HEIGHT / 8)
#define GRAPH_WIDTH 116
#define GRAPH_X 6
#define GRAPH_Y 74
#define GRAPH_HEIGHT 48

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
