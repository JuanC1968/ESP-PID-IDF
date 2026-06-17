#pragma once

#include "esp_adc/adc_oneshot.h"

adc_oneshot_unit_handle_t configure_ldr_adc(void);
float read_filtered_light_percent(adc_oneshot_unit_handle_t adc_handle);
