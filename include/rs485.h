#pragma once

// Inicializa UART2 y el pin de direccion del modulo MAX485.
void configure_rs485(void);

// Envia una linea de telemetria del PID por RS485.
void send_rs485_telemetry(void);
