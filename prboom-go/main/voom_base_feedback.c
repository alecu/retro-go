#include "voom_base_feedback.h"
#include "host_comms.h"

#include <stdio.h>

static uint8_t palette_red, palette_green, palette_blue;
static int last_red = -1, last_green = -1, last_blue = -1, last_servo = -1;

static uint8_t clamp_byte(int value)
{
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (uint8_t)value;
}

void voom_base_feedback_set_palette_black(uint32_t entry)
{
    // V_BuildPalette's 32-bit entries are RGB in the low 24 bits. Keep this
    // conversion here rather than hard-coding Doom damage red in the base.
    palette_red = (entry >> 16) & 0xff;
    palette_green = (entry >> 8) & 0xff;
    palette_blue = entry & 0xff;
}

void voom_base_feedback_update(int damagecount, int armorpoints)
{
    int damage = damagecount;
    if (damage < 0) damage = 0;
    if (damage > 100) damage = 100;
    const int servo = damage * 255 / 100;
    int red = 0, green = 0, blue;

    if (damage) {
        red = palette_red;
        green = palette_green;
        blue = palette_blue;
    } else {
        // Doom blue armor is 200 points. The base API calls this shield.
        if (armorpoints < 0) armorpoints = 0;
        if (armorpoints > 200) armorpoints = 200;
        blue = armorpoints * 255 / 200;
    }

    if (red != last_red || green != last_green || blue != last_blue) {
        char line[32];
        snprintf(line, sizeof(line), "base leds %d %d %d", red, green, blue);
        host_send(line, NULL, 0);
        last_red = red; last_green = green; last_blue = blue;
    }
    if (servo != last_servo) {
        char line[20];
        snprintf(line, sizeof(line), "base servo %d", servo);
        host_send(line, NULL, 0);
        last_servo = servo;
    }
}
