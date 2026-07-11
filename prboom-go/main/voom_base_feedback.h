#pragma once

#include <stdint.h>

// Optional physical-base feedback for Voom. All public values are normalized;
// Arduino calibration is deliberately not represented here.
void voom_base_feedback_set_palette_black(uint32_t palette_entry);
void voom_base_feedback_update(int damagecount, int armorpoints);
