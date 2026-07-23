#pragma once

#include "esp_err.h"

// Inicializa NVS, la memoria no volatil usada para guardar configuracion.
esp_err_t init_storage(void);

// Carga parametros PID guardados. Si no existen, deja los valores por defecto.
esp_err_t load_pid_config(void);

// Guarda la configuracion PID actual para recuperarla tras reiniciar.
esp_err_t save_pid_config(void);
