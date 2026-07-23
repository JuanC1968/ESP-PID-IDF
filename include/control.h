#pragma once

#include "esp_adc/adc_oneshot.h"

// Ejecuta una iteracion completa del lazo PID: leer sensor, calcular salida,
// actualizar PWM y LEDs de estado.
void update_pid(adc_oneshot_unit_handle_t adc_handle);
