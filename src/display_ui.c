#include "display_ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

#include "app_config.h"
#include "app_state.h"

// En los OLED SSD1306/SH1107 el primer byte de cada transferencia indica si lo
// que viene despues son comandos de control o datos de pantalla.
#define OLED_CONTROL_COMMAND 0x00
#define OLED_CONTROL_DATA 0x40

// La pantalla es monocroma: 1 bit por pixel. La memoria se organiza en paginas
// de 8 pixels de alto, por eso 128x128 necesita 128 * 16 = 2048 bytes.
#define OLED_BUFFER_SIZE (OLED_WIDTH * OLED_PAGES)
#define FONT_WIDTH 5
#define FONT_HEIGHT 7

static const char *TAG = "display";

// Handles del nuevo driver I2C de ESP-IDF. El bus representa SDA/SCL; el device
// representa la pantalla concreta colgada de ese bus.
static uint8_t oled_address = OLED_ADDRESS_PRIMARY;
static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t oled_device = NULL;

// buffer es nuestra copia en RAM de lo que queremos ver en pantalla. Dibujamos
// aqui primero y luego oled_flush() lo envia al OLED de una vez.
static uint8_t buffer[OLED_BUFFER_SIZE];

// Historial circular para la grafica. graph_index apunta al siguiente hueco
// donde se escribira; cuando llega al final vuelve a cero.
static float graph_input[GRAPH_WIDTH];
static float graph_setpoint[GRAPH_WIDTH];
static uint8_t graph_index = 0;
static uint8_t graph_count = 0;
static bool display_ready = false;

// Fuente 5x7 clasica. Cada byte representa una columna y cada bit un pixel.
// La expresion ['A' - ' '] coloca cada dibujo en el indice ASCII relativo al
// espacio, que es el primer caracter imprimible que aceptamos.
static const uint8_t font_5x7[][FONT_WIDTH] = {
    [' ' - ' '] = {0x00, 0x00, 0x00, 0x00, 0x00},
    ['%' - ' '] = {0x62, 0x64, 0x08, 0x13, 0x23},
    ['-' - ' '] = {0x08, 0x08, 0x08, 0x08, 0x08},
    ['.' - ' '] = {0x00, 0x60, 0x60, 0x00, 0x00},
    ['0' - ' '] = {0x3E, 0x51, 0x49, 0x45, 0x3E},
    ['1' - ' '] = {0x00, 0x42, 0x7F, 0x40, 0x00},
    ['2' - ' '] = {0x42, 0x61, 0x51, 0x49, 0x46},
    ['3' - ' '] = {0x21, 0x41, 0x45, 0x4B, 0x31},
    ['4' - ' '] = {0x18, 0x14, 0x12, 0x7F, 0x10},
    ['5' - ' '] = {0x27, 0x45, 0x45, 0x45, 0x39},
    ['6' - ' '] = {0x3C, 0x4A, 0x49, 0x49, 0x30},
    ['7' - ' '] = {0x01, 0x71, 0x09, 0x05, 0x03},
    ['8' - ' '] = {0x36, 0x49, 0x49, 0x49, 0x36},
    ['9' - ' '] = {0x06, 0x49, 0x49, 0x29, 0x1E},
    ['A' - ' '] = {0x7E, 0x11, 0x11, 0x11, 0x7E},
    ['D' - ' '] = {0x7F, 0x41, 0x41, 0x22, 0x1C},
    ['E' - ' '] = {0x7F, 0x49, 0x49, 0x49, 0x41},
    ['F' - ' '] = {0x7F, 0x09, 0x09, 0x09, 0x01},
    ['I' - ' '] = {0x00, 0x41, 0x7F, 0x41, 0x00},
    ['K' - ' '] = {0x7F, 0x08, 0x14, 0x22, 0x41},
    ['L' - ' '] = {0x7F, 0x40, 0x40, 0x40, 0x40},
    ['N' - ' '] = {0x7F, 0x02, 0x0C, 0x10, 0x7F},
    ['O' - ' '] = {0x3E, 0x41, 0x41, 0x41, 0x3E},
    ['P' - ' '] = {0x7F, 0x09, 0x09, 0x09, 0x06},
    ['R' - ' '] = {0x7F, 0x09, 0x19, 0x29, 0x46},
    ['S' - ' '] = {0x46, 0x49, 0x49, 0x49, 0x31},
    ['T' - ' '] = {0x01, 0x01, 0x7F, 0x01, 0x01},
    ['V' - ' '] = {0x1F, 0x20, 0x40, 0x20, 0x1F},
    ['W' - ' '] = {0x7F, 0x20, 0x18, 0x20, 0x7F},
    ['d' - ' '] = {0x38, 0x44, 0x44, 0x48, 0x7F},
    ['i' - ' '] = {0x00, 0x44, 0x7D, 0x40, 0x00},
    ['p' - ' '] = {0x7C, 0x14, 0x14, 0x14, 0x08},
};

// Envia una transferencia I2C a la pantalla. Se usan dos buffers para evitar
// copiar datos grandes: primero el byte de control y despues el contenido real.
static esp_err_t oled_write(uint8_t control, const uint8_t *data, size_t length)
{
    i2c_master_transmit_multi_buffer_info_t buffers[] = {
        {.write_buffer = &control, .buffer_size = 1},
        {.write_buffer = data, .buffer_size = length},
    };

    return i2c_master_multi_buffer_transmit(oled_device, buffers, 2, 100);
}

// Atajo para mandar un unico comando del controlador SH1107.
static esp_err_t oled_command(uint8_t command)
{
    return oled_write(OLED_CONTROL_COMMAND, &command, 1);
}

// Borra solo el framebuffer en RAM. La pantalla fisica no cambia hasta flush().
static void oled_clear(void)
{
    memset(buffer, 0, sizeof(buffer));
}

// Enciende o apaga un pixel dentro del framebuffer. La division por 8 localiza
// la pagina vertical; el modulo 8 localiza el bit dentro de esa pagina.
static void oled_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
        return;
    }

    uint16_t index = (uint16_t)(y / 8) * OLED_WIDTH + x;
    uint8_t bit = (uint8_t)(1U << (y % 8));
    if (on) {
        buffer[index] |= bit;
    } else {
        buffer[index] &= (uint8_t)~bit;
    }
}

// Linea horizontal sencilla, util para la referencia del 50% en la grafica.
static void oled_hline(int x, int y, int width)
{
    for (int i = 0; i < width; i++) {
        oled_pixel(x + i, y, true);
    }
}

// Dibuja una linea con el algoritmo de Bresenham. Solo usa enteros, asi que es
// rapido y encaja bien en microcontroladores.
static void oled_line(int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    while (true) {
        oled_pixel(x0, y0, true);
        if (x0 == x1 && y0 == y1) {
            break;
        }

        int twice_error = 2 * error;
        if (twice_error >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice_error <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

// Dibuja un caracter leyendo sus 5 columnas de la tabla font_5x7.
static void oled_char(int x, int y, char c)
{
    if (c < ' ' || c > 'z') {
        c = ' ';
    }

    const uint8_t *glyph = font_5x7[c - ' '];
    for (int col = 0; col < FONT_WIDTH; col++) {
        for (int row = 0; row < FONT_HEIGHT; row++) {
            // Si el bit de esa fila esta a 1, pintamos el pixel correspondiente.
            if ((glyph[col] & (1U << row)) != 0) {
                oled_pixel(x + col, y + row, true);
            }
        }
    }
}

// Dibuja texto avanzando 6 pixels por caracter: 5 de letra y 1 de separacion.
static void oled_text(int x, int y, const char *text)
{
    while (*text != '\0') {
        oled_char(x, y, *text);
        x += FONT_WIDTH + 1;
        text++;
    }
}

// Vuelca el framebuffer completo al OLED. El SH1107 trabaja por paginas de 8
// pixels de alto; antes de escribir cada pagina seleccionamos pagina y columna.
static esp_err_t oled_flush(void)
{
    for (uint8_t page = 0; page < OLED_PAGES; page++) {
        ESP_RETURN_ON_ERROR(oled_command((uint8_t)(0xB0 | page)), TAG, "No se pudo seleccionar pagina");
        ESP_RETURN_ON_ERROR(oled_command(0x00), TAG, "No se pudo seleccionar columna baja");
        ESP_RETURN_ON_ERROR(oled_command(0x10), TAG, "No se pudo seleccionar columna alta");

        // Enviamos 16 columnas por transferencia para no usar buffers grandes en
        // la pila y para mantener cada paquete I2C manejable.
        for (uint8_t column = 0; column < OLED_WIDTH; column += 16) {
            ESP_RETURN_ON_ERROR(oled_write(OLED_CONTROL_DATA,
                                           &buffer[(page * OLED_WIDTH) + column],
                                           16),
                                TAG,
                                "No se pudo enviar datos");
        }
    }
    return ESP_OK;
}

// Convierte porcentaje 0..100 a coordenada Y de la grafica. En pantalla Y crece
// hacia abajo, por eso el calculo esta invertido.
static uint8_t graph_y(float percent)
{
    percent = clamp_float(percent, 0.0f, 100.0f);
    return GRAPH_Y + GRAPH_HEIGHT - 1 - (uint8_t)((percent * (GRAPH_HEIGHT - 1)) / 100.0f);
}

// Guarda una muestra nueva en el historial circular de la grafica.
static void push_graph_sample(void)
{
    graph_input[graph_index] = state.input;
    graph_setpoint[graph_index] = config.setpoint;
    graph_index = (graph_index + 1) % GRAPH_WIDTH;
    if (graph_count < GRAPH_WIDTH) {
        graph_count++;
    }
}

// Dibuja PV como linea continua y SP como puntos, para distinguirlos en una
// pantalla monocroma sin colores.
static void draw_graph(void)
{
    oled_hline(GRAPH_X, graph_y(50.0f), GRAPH_WIDTH);
    oled_pixel(GRAPH_X - 2, graph_y(100.0f), true);
    oled_pixel(GRAPH_X - 2, graph_y(0.0f), true);

    int previous_x = -1;
    int previous_y = -1;
    int first_x = GRAPH_X + GRAPH_WIDTH - graph_count;
    for (uint8_t i = 0; i < graph_count; i++) {
        // Si el buffer ya esta lleno, graph_index apunta al dato mas antiguo.
        // Si aun no esta lleno, las muestras validas empiezan en cero.
        uint8_t sample_index = (graph_count == GRAPH_WIDTH) ? (graph_index + i) % GRAPH_WIDTH : i;
        int x = first_x + i;
        int input_y = graph_y(graph_input[sample_index]);
        int set_y = graph_y(graph_setpoint[sample_index]);

        if ((i % 3) == 0) {
            oled_pixel(x, set_y, true);
        }
        if (previous_x >= 0) {
            oled_line(previous_x, previous_y, x, input_y);
        }
        previous_x = x;
        previous_y = input_y;
    }
}

// Secuencia minima de inicializacion del controlador SH1107 para 128x128.
// Estos comandos preparan multiplexado, orientacion, contraste y encienden panel.
static esp_err_t oled_init(void)
{
    const uint8_t init_commands[] = {
        0xAE,       // Display off.
        0xD5, 0x50, // Clock divide.
        0xA8, 0x7F, // Multiplex 1/128.
        0xD3, 0x00, // Display offset.
        0x40,       // Start line.
        0xA1,       // Segment remap.
        0xC8,       // COM scan direction remap.
        0xDA, 0x12, // COM pins.
        0x81, 0xB4, // Contrast.
        0xD9, 0x22, // Pre-charge.
        0xDB, 0x35, // VCOM deselect.
        0xA4,       // Resume RAM display.
        0xA6,       // Normal display.
        0xAF,       // Display on.
    };

    for (size_t i = 0; i < sizeof(init_commands); i++) {
        ESP_RETURN_ON_ERROR(oled_command(init_commands[i]), TAG, "No se pudo iniciar OLED");
    }

    oled_clear();
    return oled_flush();
}

void configure_display(void)
{
    // Primero se crea el bus I2C fisico con SDA/SCL y pull-ups internos.
    i2c_master_bus_config_t bus_config = {
        .i2c_port = OLED_I2C_PORT,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));

    // Probamos las dos direcciones habituales de los modulos OLED I2C.
    esp_err_t probe_err = i2c_master_probe(i2c_bus, oled_address, 100);
    if (probe_err != ESP_OK) {
        oled_address = OLED_ADDRESS_SECONDARY;
        probe_err = i2c_master_probe(i2c_bus, oled_address, 100);
    }
    if (probe_err != ESP_OK) {
        ESP_LOGW(TAG, "OLED no detectada en 0x3C ni 0x3D");
        display_ready = false;
        return;
    }

    // Una vez encontrada la direccion, registramos la pantalla como dispositivo
    // I2C para poder transmitirle comandos y datos.
    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = oled_address,
        .scl_speed_hz = OLED_I2C_FREQUENCY_HZ,
    };

    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &device_config, &oled_device));

    esp_err_t err = oled_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OLED no detectada o no inicializada: %s", esp_err_to_name(err));
        display_ready = false;
        return;
    }

    display_ready = true;

    // Mensaje inicial visible unos instantes, hasta que el bucle principal llame
    // a draw_display() y pinte la pantalla de telemetria.
    oled_text(8, 8, "OLED OK");
    oled_text(8, 24, "ESP-PID-IDF");
    oled_flush();
}

void draw_display(void)
{
    // Si no hay OLED conectada, esta funcion queda anulada y el PID sigue vivo.
    if (!display_ready) {
        return;
    }

    // Cada refresco de pantalla añade una muestra a la grafica.
    push_graph_sample();
    oled_clear();

    // Las lineas de texto se formatean primero en un buffer pequeño y luego se
    // dibujan con la fuente 5x7.
    char line[24];
    oled_text(2, 2, "PID LDR");
    oled_text(70, 2, state.in_set ? "SET" : "NO SET");

    snprintf(line, sizeof(line), "SP %.1f%%", config.setpoint);
    oled_text(2, 17, line);
    snprintf(line, sizeof(line), "PV %.1f%%", state.input);
    oled_text(66, 17, line);

    snprintf(line, sizeof(line), "E %.1f", state.error);
    oled_text(2, 32, line);
    snprintf(line, sizeof(line), "PWM %.0f%%", (state.output * 100.0f) / PWM_MAX_DUTY);
    oled_text(66, 32, line);

    snprintf(line, sizeof(line), "Kp %.1f Ki %.1f", config.kp, config.ki);
    oled_text(2, 47, line);
    snprintf(line, sizeof(line), "Kd %.2f AP 4.1", config.kd);
    oled_text(2, 62, line);

    draw_graph();
    oled_flush();
}
