#include "rs485.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include "app_config.h"
#include "app_state.h"

void configure_rs485(void)
{
    gpio_config_t direction_config = {
        .pin_bit_mask = 1ULL << PIN_RS485_DE_RE,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&direction_config));

    // Reposo en recepcion: el MAX485 no conduce el bus hasta que queramos
    // transmitir. Esto evita peleas cuando haya un segundo nodo conectado.
    gpio_set_level(PIN_RS485_DE_RE, 0);

    uart_config_t uart_config = {
        .baud_rate = RS485_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(RS485_UART_PORT, 1024, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(RS485_UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(RS485_UART_PORT,
                                 PIN_RS485_TX,
                                 PIN_RS485_RX,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));
}

void send_rs485_telemetry(void)
{
    char line[96];
    int len = snprintf(line,
                       sizeof(line),
                       "PV=%.1f;RAW=%.1f;ADC=%d;SP=%.1f;PWM=%.0f;SET=%d\r\n",
                       state.input,
                       state.raw_percent,
                       state.raw,
                       config.setpoint,
                       state.output,
                       state.in_set ? 1 : 0);
    if (len <= 0) {
        return;
    }
    if (len >= (int)sizeof(line)) {
        len = sizeof(line) - 1;
    }

    gpio_set_level(PIN_RS485_DE_RE, 1);
    uart_write_bytes(RS485_UART_PORT, line, len);
    uart_wait_tx_done(RS485_UART_PORT, pdMS_TO_TICKS(100));
    gpio_set_level(PIN_RS485_DE_RE, 0);
}
