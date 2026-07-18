#pragma once

#include "gnuboy.h"

typedef struct
{
	int rate, cycles;
	int frame_cycles; // Ventilastation: monotonic within an emu-audio frame,
	                  // reset by gb_sound_frame_reset(); gives the audio
	                  // bridge a stable sample position for register-write
	                  // taps regardless of the sound buffer's own flush
	                  // cadence (which doesn't line up with video frames).
	byte wave[16];
	struct {
		unsigned on, pos;
		int cnt, encnt, swcnt;
		int len, enlen, swlen;
		int swfreq, freq;
		int envol, endir;
	} ch[4];
} gb_snd_t;

gb_snd_t *gb_sound_init(void);
void gb_sound_write(byte r, byte b);
void gb_sound_dirty(void);
void gb_sound_reset(bool hard);
void gb_sound_emulate(void);
void gb_sound_frame_reset(void);
#define gb_sound_advance(count) do { GB.snd->cycles += (count); GB.snd->frame_cycles += (count); } while (0)
