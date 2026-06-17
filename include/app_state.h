#pragma once

#include "app_config.h"

extern pid_config_t config;
extern runtime_state_t state;

// Utilidad comun para limitar valores a un rango valido.
float clamp_float(float value, float minimum, float maximum);

// Limpia solo la memoria dinamica del PID, no los parametros configurados.
void reset_pid_state(void);
