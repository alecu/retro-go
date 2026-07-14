#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rg_surface.h"

// Pull in the target config so RG_VS_ENABLE_POV_DISPLAY is defined before the
// guard below is evaluated. Without this, a translation unit that includes this
// header before rg_system.h/config.h (e.g. ventilastation_pov.c itself, which
// includes its own header first) would see RG_VS_ENABLE_POV_DISPLAY undefined,
// silently compiling the whole POV driver down to no-op stubs.
#include "config.h"

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
// Updates the hardware SPI path.
void rg_vs_pov_submit_surface(const rg_surface_t *surface);

// Freeze the most recently submitted frame and black its LEDs from the outer
// edge to the centre over duration_ms.  This is used immediately before a
// native app returns to the MicroPython launcher.
void rg_vs_pov_fade_last_frame_to_black(uint32_t duration_ms);

// Legacy: explicit 32-bit palette override. Optional; rg_vs_pov_submit_surface()
// reads the palette from the surface automatically.
void rg_vs_pov_set_palette32(const uint32_t *palette, size_t count);


// Register WiFi display bridge callbacks so the POV driver can forward frames
// and palette to the desktop pyglet emulator. Pass NULLs to disable.
