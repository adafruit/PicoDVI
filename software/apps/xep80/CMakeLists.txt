add_executable(xep80
	main.c
	tmds_encode_font_2bpp.S
	tmds_encode_font_2bpp.h
	xep80.c
	gfx.c
	uart_log.c
)

pico_generate_pio_header(xep80 ${CMAKE_CURRENT_LIST_DIR}/uart_9n1_rx.pio)
pico_generate_pio_header(xep80 ${CMAKE_CURRENT_LIST_DIR}/uart_9n1_tx.pio)
pico_generate_pio_header(xep80 ${CMAKE_CURRENT_LIST_DIR}/uart_8n1_tx.pio)


target_compile_definitions(xep80 PRIVATE
	DVI_VERTICAL_REPEAT=1
	)

# We have a lot in SRAM4 (particularly TMDS LUT) but don't need much stack on
# core 1. Probably even 256 bytes would be fine.
target_compile_definitions(xep80 PRIVATE
	DVI_DEFAULT_SERIAL_CONFIG=${DVI_DEFAULT_SERIAL_CONFIG}
	PICO_CORE1_STACK_SIZE=0x200
	)

target_link_libraries(xep80
	pico_stdlib
	pico_multicore
	libdvi
	hardware_pio
)

# create map/bin/hex file etc.
pico_add_extra_outputs(xep80)

pico_enable_stdio_usb(xep80 0)
pico_enable_stdio_uart(xep80 1)