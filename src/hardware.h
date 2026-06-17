#pragma once

#include <stdbool.h>
#include <stdint.h>

void configure_status_leds(void);
void configure_white_led_pwm(void);
void write_white_led_pwm(uint32_t duty);
void set_status_leds(bool in_set);
