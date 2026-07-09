#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// UART host link for hardware (LED+SPI) mode. Mirrors the vs_host_bridge API but
// over UART2, talking to the base-station host the same way MicroPython does in
// ventilastation/serialcomms.py (same pins/baud, same "line\n" + data framing).
void sb_init(void);
void sb_send(const char *line, const uint8_t *data, size_t len);
bool sb_connected(void);
int sb_recv_input(void);
