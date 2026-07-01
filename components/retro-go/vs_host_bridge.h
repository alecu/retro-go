#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Shared Ventilastation host bridge.
//
// In hardware mode it talks to the base station over UART2.
// In emulator mode it exposes the same "line\n + optional payload" protocol
// over TCP so the desktop POV emulator can connect.

void vs_host_bridge_init(void);
void vs_host_bridge_send(const char *line, const uint8_t *data, size_t len);
int vs_host_bridge_recv_input(void);
bool vs_host_bridge_connected(void);
