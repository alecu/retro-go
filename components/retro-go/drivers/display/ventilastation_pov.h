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
