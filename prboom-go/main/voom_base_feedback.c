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

void voom_base_feedback_update(int health, int armorpoints)
{
    // Doom's ordinary health scale is 0..100. Values above 100 (health
    // pickups) intentionally hold the servo at the healthy endpoint.
    if (health < 0) health = 0;
    if (health > 100) health = 100;
    const int servo = health * 255 / 100;
    int red = palette_red, green = palette_green, blue = palette_blue;

    // Doom blue armor is 200 points. The base API calls this shield.
    if (armorpoints < 0) armorpoints = 0;
    if (armorpoints > 200) armorpoints = 200;
    const int armor_blue = armorpoints * 255 / 200;

    if (palette_red == 0 && palette_green == 0 && palette_blue == 0) {
        // Normal palette: the player is okay, so the strip is armor blue.
        red = green = 0;
        blue = armor_blue;
    } else {
        const int hit = palette_red > palette_green ? palette_red : palette_green;
        if (hit <= 63) {
            // A weak red/green palette flash mixes with armor. Map 0..63 to
            // full..zero armor blue; >=64 switches to the palette unchanged.
            blue = palette_blue + armor_blue * (63 - hit) / 63;
            if (blue > 255) blue = 255;
        }
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
