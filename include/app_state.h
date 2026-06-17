#pragma once

#include "app_config.h"

extern pid_config_t config;
extern runtime_state_t state;

float clamp_float(float value, float minimum, float maximum);
