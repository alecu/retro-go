#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Initialise WiFi (reading credentials from NVS slot 0) and start a TCP
// server on port 5005 so the desktop pyglet emulator can connect.
void wb_init(void);

// Send one comms-protocol message: writes "line\n" followed by `len` bytes
// of `data`. Safe to call when not connected (no-op).
void wb_send(const char *line, const uint8_t *data, size_t len);

// Non-blocking read of one button-byte from the emulator.
// Returns 0-255 on success, -1 if nothing is available.
// Bit layout matches the pyglet emulator:
//   bit0=left  bit1=right  bit2=up  bit3=down
//   bit4=fire  bit5=accel  bit6=use bit7=esc
int wb_recv_input(void);

bool wb_connected(void);
