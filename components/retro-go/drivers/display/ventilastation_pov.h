#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(ESP_PLATFORM) && defined(RG_VS_ENABLE_POV_DISPLAY) && (RG_VS_ENABLE_POV_DISPLAY)
#define RG_VENTILASTATION_POV_ENABLED 1
#else
#define RG_VENTILASTATION_POV_ENABLED 0
#endif

bool rg_vs_pov_enabled(void);
void rg_vs_pov_init(int screen_width, int screen_height);
void rg_vs_pov_set_palette32(const uint32_t *palette, size_t count);
void rg_vs_pov_submit_frame(const uint8_t *framebuffer, size_t length);

// Optional WiFi display bridge: register send/connected callbacks so the POV
// driver can forward projected frames to the desktop pyglet emulator.
// Call once after wb_init() in app_main(); pass NULLs to disable.
typedef void (*rg_vs_tcp_send_fn)(const char *line, const uint8_t *data, size_t len);
typedef bool (*rg_vs_tcp_connected_fn)(void);
void rg_vs_pov_set_tcp_bridge(rg_vs_tcp_send_fn send_fn, rg_vs_tcp_connected_fn connected_fn);
