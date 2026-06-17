#include "app_state.h"

pid_config_t config = {
    .setpoint = 55.0f,
    .kp = 7.0f,
    .ki = 0.6f,
    .kd = 0.15f,
    .out_min = 0.0f,
    .out_max = (float)PWM_MAX_DUTY,
    .invert_sensor = false,
};

runtime_state_t state = {0};

float clamp_float(float value, float minimum, float maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

void reset_pid_state(void)
{
    state.integral = 0.0f;
    state.derivative = 0.0f;
    state.last_error = 0.0f;
    state.output = 0.0f;
    state.in_set = false;
    state.filter_ready = false;
}
