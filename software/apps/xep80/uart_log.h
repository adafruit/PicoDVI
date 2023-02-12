#ifndef _UART_LOG_H_
#define _UART_LOG_H_

#include "uart_8n1_tx.pio.h"

#define UART_8N1_BUF_SIZE 1024
#define UART_TX_LOG_PIO_SM 2
#define UART_LOG_TX_PIN 0
#define UART_LOG_BAUD_RATE 115200

void uart_8n1_tx_loop(PIO pio, uint sm);
void uart_8n1_tx_send(char *s);
void uart_log_init(PIO pio, uint sm);
#endif