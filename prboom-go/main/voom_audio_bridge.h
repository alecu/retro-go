#pragma once

// Emit Doom sound/music triggers to the host that actually plays them (the
// desktop emulator over TCP, or the hardware base-station over serial). The
// spinning board has no audio output of its own. Names are the Doom short
// lump names (e.g. "pistol", "e1m1"), namespaced as "voom/<name>" on the wire
// so they don't collide with the MicroPython games' sound names.
void voom_audio_sfx(const char *name);
void voom_audio_music(const char *name);
void voom_audio_music_stop(void);
