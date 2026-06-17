#include "web_server.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_wifi.h"

#include "app_config.h"
#include "app_state.h"
#include "storage.h"

#define WEB_AP_SSID "ESP-PID-IDF"
#define WEB_AP_PASSWORD "12345678"
#define WEB_AP_CHANNEL 6
#define WEB_AP_MAX_CONNECTIONS 4
#define FILE_PATH_MAX 64
#define POST_BODY_MAX 256

static const char *TAG = "web";

// Monta la particion SPIFFS como si fuera una carpeta del sistema:
// /spiffs/index.html, /spiffs/app.js, etc. PlatformIO genera esa particion a
// partir de la carpeta data/ cuando hacemos pio run -t uploadfs.
static esp_err_t mount_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true,
    };

    ESP_RETURN_ON_ERROR(esp_vfs_spiffs_register(&conf), TAG, "No se pudo montar SPIFFS");

    size_t total = 0;
    size_t used = 0;
    ESP_RETURN_ON_ERROR(esp_spiffs_info(NULL, &total, &used), TAG, "No se pudo leer SPIFFS");
    ESP_LOGI(TAG, "SPIFFS montado: %u/%u bytes usados", (unsigned)used, (unsigned)total);
    return ESP_OK;
}

// Crea una red WiFi propia del ESP32. En esta primera version no se conecta al
// router de casa: el movil/PC se conecta directamente al punto de acceso.
static esp_err_t start_wifi_ap(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "No se pudo iniciar netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "No se pudo crear event loop");
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "No se pudo iniciar WiFi");

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WEB_AP_SSID,
            .ssid_len = strlen(WEB_AP_SSID),
            .channel = WEB_AP_CHANNEL,
            .password = WEB_AP_PASSWORD,
            .max_connection = WEB_AP_MAX_CONNECTIONS,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "No se pudo poner WiFi en AP");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wifi_config), TAG, "No se pudo configurar AP");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "No se pudo arrancar AP");

    ESP_LOGI(TAG, "AP listo: SSID=%s password=%s", WEB_AP_SSID, WEB_AP_PASSWORD);
    return ESP_OK;
}

// El navegador necesita saber el tipo de archivo que recibe para interpretarlo
// bien. No es lo mismo enviar HTML que CSS o JavaScript.
static const char *content_type_for_path(const char *path)
{
    const char *extension = strrchr(path, '.');
    if (extension == NULL) {
        return "text/plain";
    }
    if (strcmp(extension, ".html") == 0) {
        return "text/html";
    }
    if (strcmp(extension, ".css") == 0) {
        return "text/css";
    }
    if (strcmp(extension, ".js") == 0) {
        return "application/javascript";
    }
    if (strcmp(extension, ".json") == 0) {
        return "application/json";
    }
    return "text/plain";
}

// Lee un archivo desde SPIFFS y lo envia al navegador en trozos pequenos.
// En un microcontrolador conviene no cargar el archivo completo en RAM.
static esp_err_t send_file(httpd_req_t *req, const char *relative_path)
{
    char path[FILE_PATH_MAX];
    int written = snprintf(path, sizeof(path), "/spiffs%s", relative_path);
    if (written < 0 || written >= (int)sizeof(path)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Ruta demasiado larga");
        return ESP_FAIL;
    }

    FILE *file = fopen(path, "r");
    if (file == NULL) {
        ESP_LOGW(TAG, "No se pudo abrir %s: errno=%d", path, errno);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Archivo no encontrado");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, content_type_for_path(path));

    char buffer[256];
    size_t read_bytes = 0;
    while ((read_bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        // httpd_resp_send_chunk permite ir mandando la respuesta por partes.
        if (httpd_resp_send_chunk(req, buffer, read_bytes) != ESP_OK) {
            fclose(file);
            httpd_resp_sendstr_chunk(req, NULL);
            return ESP_FAIL;
        }
    }

    fclose(file);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    return send_file(req, "/index.html");
}

static esp_err_t app_js_handler(httpd_req_t *req)
{
    return send_file(req, "/app.js");
}

static esp_err_t style_css_handler(httpd_req_t *req)
{
    return send_file(req, "/style.css");
}

// Devuelve los valores vivos del controlador PID. app.js consulta este endpoint
// cada segundo para actualizar las tarjetas de la pagina.
static esp_err_t status_handler(httpd_req_t *req)
{
    char response[256];
    snprintf(response,
             sizeof(response),
             "{\"input\":%.2f,\"rawPercent\":%.2f,\"output\":%.2f,"
             "\"error\":%.2f,\"raw\":%d,\"setpoint\":%.2f,\"inSet\":%s}",
             state.input,
             state.raw_percent,
             state.output,
             state.error,
             state.raw,
             config.setpoint,
             state.in_set ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, response);
}

// Devuelve la configuracion actual del PID. La pagina lo usa para rellenar el
// formulario cuando carga o despues de guardar cambios.
static esp_err_t config_json_handler(httpd_req_t *req)
{
    char response[256];
    snprintf(response,
             sizeof(response),
             "{\"setpoint\":%.2f,\"kp\":%.3f,\"ki\":%.3f,\"kd\":%.3f,"
             "\"outMin\":%.0f,\"outMax\":%.0f,\"invert\":%s}",
             config.setpoint,
             config.kp,
             config.ki,
             config.kd,
             config.out_min,
             config.out_max,
             config.invert_sensor ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, response);
}

// Los formularios HTML llegan como texto tipo:
// setpoint=55.0&kp=7.0&ki=0.6&invert=on
// Estas funciones pequenas buscan claves y convierten numeros dentro de ese texto.
static bool form_has_key(const char *body, const char *key)
{
    char needle[32];
    snprintf(needle, sizeof(needle), "%s=", key);
    return strstr(body, needle) != NULL;
}

static bool form_float(const char *body, const char *key, float *out)
{
    char needle[32];
    snprintf(needle, sizeof(needle), "%s=", key);

    const char *start = strstr(body, needle);
    if (start == NULL) {
        return false;
    }

    start += strlen(needle);
    char *end = NULL;
    float value = strtof(start, &end);
    if (end == start) {
        return false;
    }

    *out = value;
    return true;
}

// httpd_req_recv puede devolver el cuerpo en varios fragmentos. Esta funcion
// junta todo el POST en un buffer terminado en '\0' para poder tratarlo como string.
static esp_err_t read_post_body(httpd_req_t *req, char *body, size_t body_size)
{
    if (req->content_len >= body_size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Formulario demasiado grande");
        return ESP_FAIL;
    }

    int received = 0;
    while (received < req->content_len) {
        int chunk = httpd_req_recv(req, body + received, req->content_len - received);
        if (chunk <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No se pudo leer el formulario");
            return ESP_FAIL;
        }
        received += chunk;
    }

    body[received] = '\0';
    return ESP_OK;
}

// Recibe el formulario de parametros y actualiza la configuracion global del PID.
// Los clamp evitan valores fuera de rango en campos que si tienen limites fisicos.
static esp_err_t config_post_handler(httpd_req_t *req)
{
    char body[POST_BODY_MAX];
    if (read_post_body(req, body, sizeof(body)) != ESP_OK) {
        return ESP_FAIL;
    }

    float value = 0.0f;
    if (form_float(body, "setpoint", &value)) {
        config.setpoint = clamp_float(value, 0.0f, 100.0f);
    }
    if (form_float(body, "kp", &value)) {
        config.kp = value;
    }
    if (form_float(body, "ki", &value)) {
        config.ki = value;
    }
    if (form_float(body, "kd", &value)) {
        config.kd = value;
    }
    if (form_float(body, "outMin", &value)) {
        config.out_min = clamp_float(value, 0.0f, (float)PWM_MAX_DUTY);
    }
    if (form_float(body, "outMax", &value)) {
        config.out_max = clamp_float(value, config.out_min, (float)PWM_MAX_DUTY);
    }
    config.invert_sensor = form_has_key(body, "invert");

    if (save_pid_config() != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No se pudo guardar configuracion");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

// Reinicia el estado acumulado del PID sin reiniciar el ESP32 completo.
static esp_err_t reset_post_handler(httpd_req_t *req)
{
    reset_pid_state();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

// Relaciona cada URL con la funcion que debe atenderla. El servidor HTTP de IDF
// llama automaticamente al handler adecuado cuando llega una peticion.
static void register_uri_handlers(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_handler},
        {.uri = "/index.html", .method = HTTP_GET, .handler = index_handler},
        {.uri = "/app.js", .method = HTTP_GET, .handler = app_js_handler},
        {.uri = "/style.css", .method = HTTP_GET, .handler = style_css_handler},
        {.uri = "/status", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/config.json", .method = HTTP_GET, .handler = config_json_handler},
        {.uri = "/config", .method = HTTP_POST, .handler = config_post_handler},
        {.uri = "/reset", .method = HTTP_POST, .handler = reset_post_handler},
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &routes[i]));
    }
}

// Punto de entrada publico de este modulo: prepara almacenamiento, WiFi y HTTP.
// main.c solo necesita llamar a esta funcion una vez durante el arranque.
void start_web_server(void)
{
    ESP_ERROR_CHECK(mount_spiffs());
    ESP_ERROR_CHECK(start_wifi_ap());

    httpd_config_t config_httpd = HTTPD_DEFAULT_CONFIG();
    config_httpd.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config_httpd));
    register_uri_handlers(server);
    ESP_LOGI(TAG, "Servidor HTTP listo en http://192.168.4.1/");
}
