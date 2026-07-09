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

// Cached joy2/extra button bytes from the latest input-protocol-v2 joystick
// frame (see vs_host_bridge_recv_input(), docs/input-protocol-v2.md). Extra
// bit 0 is BUTTON_D, mirrored by rg_input.c into bit 7 of host_buttons.
uint8_t vs_host_bridge_get_joy2(void);
uint8_t vs_host_bridge_get_extra(void);
