#pragma once

// Inicializa bus I2C y pantalla OLED. Si no hay pantalla conectada, el resto del
// programa sigue funcionando y draw_display() no hace nada.
void configure_display(void);

// Redibuja la pantalla con valores actuales y una muestra nueva de la grafica.
void draw_display(void);
