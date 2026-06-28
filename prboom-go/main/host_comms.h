#pragma once

// Host comms transport selector for prboom-go.
//
// Sound/music triggers (outbound) and input bytes (inbound) ride either the
// WiFi/TCP bridge (desktop pyglet emulator) or the UART serial link (spinning
// hardware base-station host), chosen at compile time by RG_VS_ENABLE_TCP_BRIDGE.
// Both transports use the same "line\n" + data framing. The display path is
// independent (TCP frames vs SPI LED strip).

#include "rg_system.h" // RG_VS_ENABLE_TCP_BRIDGE (from the target config.h)
#include "wifi_bridge.h"
#include "serial_bridge.h"

#if RG_VS_ENABLE_TCP_BRIDGE
#define host_init        wb_init
#define host_send        wb_send
#define host_recv_input  wb_recv_input
#else
#define host_init        sb_init
#define host_send        sb_send
#define host_recv_input  sb_recv_input
#endif
