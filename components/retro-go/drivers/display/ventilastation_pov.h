#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rg_surface.h"

#if defined(ESP_PLATFORM) && defined(RG_VS_ENABLE_POV_DISPLAY) && (RG_VS_ENABLE_POV_DISPLAY)
#define RG_VENTILASTATION_POV_ENABLED 1
#else
#define RG_VENTILASTATION_POV_ENABLED 0
#endif

bool rg_vs_pov_enabled(void);

// Call once from rg_display_init(). Starts the SPI display task and hall sensor.
// Framebuffer allocation happens lazily on the first rg_vs_pov_submit_surface() call.
void rg_vs_pov_init(void);

// Submit a surface for display. Handles all rg_surface_t pixel formats:
// indexed (PAL565_LE/BE, PAL888) and direct RGB (565_LE/BE, 888).
// Updates the hardware SPI path and sends over TCP bridge if connected.
void rg_vs_pov_submit_surface(const rg_surface_t *surface);

// Legacy: explicit 32-bit palette override. Optional; rg_vs_pov_submit_surface()
// reads the palette from the surface automatically.
void rg_vs_pov_set_palette32(const uint32_t *palette, size_t count);

// Legacy: indexed-only path retained for callers that don't have an rg_surface_t.
void rg_vs_pov_submit_frame(const uint8_t *framebuffer, size_t length);

// Register WiFi display bridge callbacks so the POV driver can forward frames
// and palette to the desktop pyglet emulator. Pass NULLs to disable.
typedef void (*rg_vs_tcp_send_fn)(const char *line, const uint8_t *data, size_t len);
typedef bool (*rg_vs_tcp_connected_fn)(void);
void rg_vs_pov_set_tcp_bridge(rg_vs_tcp_send_fn send_fn, rg_vs_tcp_connected_fn connected_fn);
