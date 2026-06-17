#include "sensor.h"

#include <stdint.h>

#include "esp_err.h"
#include "esp_rom_sys.h"

#include "app_config.h"
#include "app_state.h"

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

adc_oneshot_unit_handle_t configure_ldr_adc(void)
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

float read_filtered_light_percent(adc_oneshot_unit_handle_t adc_handle)
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
