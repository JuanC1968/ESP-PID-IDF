#pragma once

#include "esp_err.h"

esp_err_t init_storage(void);
esp_err_t load_pid_config(void);
esp_err_t save_pid_config(void);
