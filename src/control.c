#include "control.h"

#include <math.h>

#include "app_config.h"
#include "app_state.h"
#include "hardware.h"
#include "sensor.h"

static void update_status_leds(void)
{
    float abs_error = fabsf(state.error);
    if (state.in_set) {
        state.in_set = abs_error <= SET_EXIT_BAND;
    } else {
        state.in_set = abs_error <= SET_ENTER_BAND;
    }

    set_status_leds(state.in_set);
}

void update_pid(adc_oneshot_unit_handle_t adc_handle)
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
