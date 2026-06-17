#pragma once

#include "esp_adc/adc_oneshot.h"

// Crea y configura la unidad ADC que lee el LDR.
adc_oneshot_unit_handle_t configure_ldr_adc(void);

// Lee el sensor, aplica promedio + filtro exponencial y devuelve porcentaje.
float read_filtered_light_percent(adc_oneshot_unit_handle_t adc_handle);
