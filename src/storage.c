#include "storage.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "app_config.h"
#include "app_state.h"

#define STORAGE_NAMESPACE "pid"
#define STORAGE_CONFIG_KEY "config"
#define STORAGE_CONFIG_VERSION 1

static const char *TAG = "storage";

// Guardamos version + configuracion en un blob. La version permite detectar
// cambios futuros en la estructura y no interpretar datos viejos como actuales.
typedef struct {
    uint32_t version;
    pid_config_t config;
} stored_pid_config_t;

// Defensa basica: aunque lleguen datos raros desde NVS o la web, los campos con
// limites fisicos vuelven a un rango razonable.
static void sanitize_pid_config(pid_config_t *next_config)
{
    next_config->setpoint = clamp_float(next_config->setpoint, 0.0f, 100.0f);
    next_config->out_min = clamp_float(next_config->out_min, 0.0f, (float)PWM_MAX_DUTY);
    next_config->out_max = clamp_float(next_config->out_max, next_config->out_min, (float)PWM_MAX_DUTY);
}

// NVS puede quedarse sin paginas libres tras cambios de version/particion. En
// ese caso se borra y se inicializa de nuevo, que es el patron habitual de IDF.
esp_err_t init_storage(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "No se pudo borrar NVS");
        err = nvs_flash_init();
    }
    return err;
}

// Lee el blob guardado. Cualquier ausencia de datos se trata como situacion
// normal: simplemente se mantienen los parametros por defecto de app_state.c.
esp_err_t load_pid_config(void)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "Sin configuracion guardada, se usan valores por defecto");
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "No se pudo abrir NVS para leer");

    stored_pid_config_t stored = {0};
    size_t stored_size = sizeof(stored);
    err = nvs_get_blob(handle, STORAGE_CONFIG_KEY, &stored, &stored_size);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "Sin configuracion guardada, se usan valores por defecto");
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "No se pudo leer configuracion PID");

    if (stored_size != sizeof(stored) || stored.version != STORAGE_CONFIG_VERSION) {
        ESP_LOGW(TAG, "Configuracion guardada incompatible, se usan valores por defecto");
        return ESP_OK;
    }

    config = stored.config;
    sanitize_pid_config(&config);
    ESP_LOGI(TAG, "Configuracion PID cargada desde NVS");
    return ESP_OK;
}

// Escribe la configuracion completa de una vez y hace commit. Sin nvs_commit(),
// el cambio no queda garantizado en flash.
esp_err_t save_pid_config(void)
{
    sanitize_pid_config(&config);

    stored_pid_config_t stored = {
        .version = STORAGE_CONFIG_VERSION,
        .config = config,
    };

    nvs_handle_t handle = 0;
    ESP_RETURN_ON_ERROR(nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &handle),
                        TAG,
                        "No se pudo abrir NVS para escribir");

    esp_err_t err = nvs_set_blob(handle, STORAGE_CONFIG_KEY, &stored, sizeof(stored));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    ESP_RETURN_ON_ERROR(err, TAG, "No se pudo guardar configuracion PID");
    ESP_LOGI(TAG, "Configuracion PID guardada en NVS");
    return ESP_OK;
}
