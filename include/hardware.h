#pragma once

#include <stdbool.h>
#include <stdint.h>

// Inicializa los dos LEDs de estado: verde cuando esta en SET, rojo cuando no.
void configure_status_leds(void);

// Configura el periferico LEDC, que es el generador PWM hardware del ESP32.
void configure_white_led_pwm(void);

// Cambia el duty del LED blanco. El valor esperado es 0..PWM_MAX_DUTY.
void write_white_led_pwm(uint32_t duty);

// Actualiza los LEDs de estado segun el resultado del PID.
void set_status_leds(bool in_set);
