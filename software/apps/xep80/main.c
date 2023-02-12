#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/vreg.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/ssi.h"
#include "hardware/dma.h"
#include "pico/sem.h"

#include "dvi.h"
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"
#include "tmds_encode_font_2bpp.h"
#include "hardware/pio.h"
#include "uart_9n1_rx.pio.h"
#include "uart_9n1_tx.pio.h"
#include "uart_log.h"

#include "gfx.h"
#include "xep80.h"

struct dvi_inst dvi0;

PIO pio = pio1;
uint sm = 0;

//#define XEP80_UART_ID uart1
#define XEP80_BAUD_RATE 15700
#define XEP80_RX_PIN 3
#define XEP80_TX_PIN 4

#define XEP80_DATA_BITS 8
#define XEP80_STOP_BITS 1
#define XEP80_PARITY UART_PARITY_NONE



uint frame_cnt = 0;
char buf[30];

void __not_in_flash("process_loop") process_loop()
{
	uart_8n1_tx_send("\e[0m");
	uart_8n1_tx_send("starting loop\r\n");

	sleep_ms(1000);

	ColdStart();
	uart_8n1_tx_send("started\r\n");

	uart_8n1_tx_send("\e[1;31mdata sent to Atari\r\n");
	uart_8n1_tx_send("\e[1;32mdata received by XEP80\r\n");


	while (true)
	{
		uart_8n1_tx_loop( pio, UART_TX_LOG_PIO_SM);
		if (uart_9n1_rx_program_avail(pio, sm))
		{
			uint16_t c = uart_9n1_rx_program_getc(pio, sm);
			ReceiveWord(c);
		}
#ifdef STATUS_LINE_POSY
		sprintf(buf, "frame: %09d", frame_cnt);
		x_print_at(64, STATUS_LINE_POSY, buf);
#endif
	}
	__builtin_unreachable();
}

void core1_main()
{
	dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
	dvi_start(&dvi0);
	while (true)
	{
		for (uint y = 0; y < FRAME_HEIGHT; ++y)
		{
			uint32_t *tmdsbuf;
			queue_remove_blocking(&dvi0.q_tmds_free, &tmdsbuf);
			for (int plane = 0; plane < 3; ++plane)
			{
				tmds_encode_font_2bpp(
					(const uint8_t *)&charbuf[(y / CHAR_VERT_FACTOR) / FONT_CHAR_HEIGHT * CHAR_COLS],
					&colourbuf[(y / CHAR_VERT_FACTOR) / FONT_CHAR_HEIGHT * (COLOUR_PLANE_SIZE_WORDS / CHAR_ROWS) + plane * COLOUR_PLANE_SIZE_WORDS],
					tmdsbuf + plane * (FRAME_WIDTH / DVI_SYMBOLS_PER_WORD),
					FRAME_WIDTH,
					(const uint8_t *)&font_8x8[(y / CHAR_VERT_FACTOR) % FONT_CHAR_HEIGHT * FONT_N_CHARS] - FONT_FIRST_ASCII);
			}
			queue_add_blocking(&dvi0.q_tmds_valid, &tmdsbuf);
		}
		frame_cnt++;
		if (frame_cnt%32==0){
			HandleBlink();
		}

	}
	__builtin_unreachable();
}

int __not_in_flash("main") main()
{
	vreg_set_voltage(VREG_VSEL);
	sleep_ms(10);
	// Run system at TMDS bit clock
	set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);

	dvi0.timing = &DVI_TIMING;
	dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
	dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

	// setup_default_uart();
	// printf("pico-dvi started");

	for (uint y = 0; y < CHAR_ROWS; ++y)
	{
		for (uint x = 0; x < CHAR_COLS; ++x)
		{
			x_set_char(x, y, ' ');
			x_set_colour_at(x, y, 255, 0);
		}
	}

	hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
	multicore_launch_core1(core1_main);

	uint offset = pio_add_program(pio, &uart_9n1_rx_program);
	uart_9n1_rx_program_init(pio, sm, offset, XEP80_RX_PIN, XEP80_BAUD_RATE);

	uint offset_tx = pio_add_program(pio, &uart_9n1_tx_program);
	uart_9n1_tx_program_init(pio, 1, offset_tx, XEP80_TX_PIN, XEP80_BAUD_RATE);


	uart_log_init(pio, UART_TX_LOG_PIO_SM);
	uart_8n1_tx_send("STARTED");

	process_loop();
	__builtin_unreachable();
}
