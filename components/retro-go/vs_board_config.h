#pragma once

#include <stdbool.h>
#include <stdint.h>

// The physical main-board wiring is shared by MicroPython and the native apps.
// It is provisioned into NVS namespace "vs_board" by tools/provision_board.py.
typedef struct {
    int32_t hall_gpio;
    int32_t irdiode_gpio;
    int32_t led_spi_host;
    int32_t led_clk;
    int32_t led_mosi;
    int32_t led_cs;
    int32_t led_freq;
    int32_t serial_uart;
    int32_t serial_tx;
    int32_t serial_rx;
    int32_t serial_baud;
} vs_board_config_t;

// Returns false unless every board key is present and NVS is usable. Callers
// must keep their hardware path disabled on failure rather than guessing pins.
bool vs_board_config_load(vs_board_config_t *config);
