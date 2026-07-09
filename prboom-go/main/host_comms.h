#pragma once

// Host comms transport selector for prboom-go.
//
// Sound/music triggers (outbound) and input bytes (inbound) ride the UART
// serial link to the base-station host, framed as "line\n" + data. The
// display path is independent (the POV driver's SPI LED strip).

#include "vs_host_bridge.h"

#define host_init        vs_host_bridge_init
#define host_send        vs_host_bridge_send
#define host_recv_input  vs_host_bridge_recv_input
#define host_connected   vs_host_bridge_connected
