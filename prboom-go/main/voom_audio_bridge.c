#include "voom_audio_bridge.h"
#include "host_comms.h"

#include <stdio.h>

// Doom short names are at most 6 chars; "music voom/" + name + " loop" + NUL fits easily.
#define VOOM_AUDIO_LINE_MAX 32

void voom_audio_sfx(const char *name)
{
    char line[VOOM_AUDIO_LINE_MAX];
    snprintf(line, sizeof(line), "sound voom/%s", name);
    host_send(line, NULL, 0);
}

void voom_audio_music(const char *name)
{
    char line[VOOM_AUDIO_LINE_MAX];
    // Doom level music loops until the level changes; the host honors the "loop" flag.
    snprintf(line, sizeof(line), "music voom/%s loop", name);
    host_send(line, NULL, 0);
}

void voom_audio_music_stop(void)
{
    host_send("musicstop", NULL, 0);
}
