#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "hal/adc_types.h"

// Pines del montaje. GPIO34 no aparece aqui porque el ADC en IDF se configura
// por unidad/canal; en este ESP32, ADC1 canal 6 corresponde al GPIO34.
#define PIN_LED_WHITE GPIO_NUM_25
#define PIN_LED_GREEN GPIO_NUM_26
#define PIN_LED_RED GPIO_NUM_27
#define PIN_I2C_SDA GPIO_NUM_21
#define PIN_I2C_SCL GPIO_NUM_22

// RS485 de pruebas. Usamos UART2 para no tocar la UART0 que queda reservada al
// USB/monitor serie de ESP-IDF. DE y /RE del modulo MAX485 van unidos al mismo
// pin: alto transmite, bajo recibe.
#define RS485_UART_PORT UART_NUM_2
#define PIN_RS485_TX GPIO_NUM_17
#define PIN_RS485_RX GPIO_NUM_16
#define PIN_RS485_DE_RE GPIO_NUM_23
#define RS485_BAUD_RATE 9600

// Lectura del LDR por ADC. Tomamos varias muestras y las promediamos para que
// el PID no reaccione a ruido instantaneo del conversor analogico-digital.
#define ADC_LDR_UNIT ADC_UNIT_1
#define ADC_LDR_CHANNEL ADC_CHANNEL_6
#define ADC_MAX_RAW 4095
#define ADC_SAMPLES 16
#define ADC_SAMPLE_DELAY_US 250

// Ritmos de trabajo. El control va a 10 Hz, la pantalla se refresca mas despacio
// y el log serie se imprime aun mas despacio para no llenar el terminal.
#define CONTROL_INTERVAL_MS 100
#define DISPLAY_INTERVAL_MS 250
#define SERIAL_INTERVAL_MS 1000
#define SENSOR_FILTER_ALPHA 0.12f
#define SET_ENTER_BAND 1.0f
#define SET_EXIT_BAND 1.0f

// OLED SH1107 128x128 por I2C. Muchas pantallas vienen en 0x3C, pero algunas
// usan 0x3D; el codigo prueba ambas.
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

// PWM del LED blanco. Con 10 bits, la salida util va de 0 a 1023.
#define PWM_FREQUENCY_HZ 5000
#define PWM_RESOLUTION LEDC_TIMER_10_BIT
#define PWM_MAX_DUTY ((1U << 10) - 1)
#define PWM_TIMER LEDC_TIMER_0
#define PWM_CHANNEL LEDC_CHANNEL_0
#define PWM_MODE LEDC_LOW_SPEED_MODE

// Parametros modificables por la web y persistidos en NVS.
typedef struct {
    float setpoint;
    float kp;
    float ki;
    float kd;
    float out_min;
    float out_max;
    bool invert_sensor;
} pid_config_t;

// Estado vivo del controlador. Esto no se guarda: se recalcula continuamente.
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
