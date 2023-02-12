

#include "uart_log.h"

char uart_8n1_buffer[UART_8N1_BUF_SIZE];
char uart_8n1_index_end = 0;
char uart_8n1_index_cur = 0;

void __not_in_flash("uart_8n1_tx_loop") uart_8n1_tx_loop(PIO pio, uint sm)
{
    while (uart_8n1_index_end != uart_8n1_index_cur)
    {
        if (pio_sm_is_tx_fifo_full(pio, sm))
        {
            return;
        }
        pio_sm_put(pio, sm, uart_8n1_buffer[uart_8n1_index_cur++]);
        if (uart_8n1_index_cur == UART_8N1_BUF_SIZE)
        {
            uart_8n1_index_cur = 0;
        }
    }
}

void __not_in_flash("uart_8n1_tx_send") uart_8n1_tx_send(char *s)
{
    while (*s)
    {
        uart_8n1_buffer[uart_8n1_index_end++] = *s++;
        if (uart_8n1_index_end == UART_8N1_BUF_SIZE)
        {
            uart_8n1_index_end = 0;
        }
    }
}

void __not_in_flash("uart_log_init") uart_log_init(PIO pio, uint sm)
{
    uint offset_tx_log = pio_add_program(pio, &uart_8n1_tx_program);
    uart_8n1_tx_program_init(pio, sm, offset_tx_log, UART_LOG_TX_PIN, UART_LOG_BAUD_RATE);
}