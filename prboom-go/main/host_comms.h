#pragma once

// Host comms transport selector for prboom-go.
//
// Sound/music triggers (outbound) and input bytes (inbound) ride either the
// WiFi/TCP bridge (desktop pyglet emulator) or the UART serial link (spinning
// hardware base-station host), chosen at compile time by RG_VS_ENABLE_TCP_BRIDGE.
// Both transports use the same "line\n" + data framing. The display path is
// independent (TCP frames vs SPI LED strip).

#include "vs_host_bridge.h"

#define host_init        vs_host_bridge_init
#define host_send        vs_host_bridge_send
#define host_recv_input  vs_host_bridge_recv_input
#define host_connected   vs_host_bridge_connected
