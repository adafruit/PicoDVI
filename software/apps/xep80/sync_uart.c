/*

*/

#include "sync_uart.h"

static void inline sync_tx(){

    sync_time = delayed_by_us(sync_time, UART_RESP_DELAY_MS);

	while (absolute_time_diff_us(get_absolute_time(), sync_time) < 0)
	{
		sync_time = delayed_by_us(sync_time, 64);
	}

	while (absolute_time_diff_us(get_absolute_time(), sync_time) > 0)
	{
		sync_time = sync_time;
	}
}