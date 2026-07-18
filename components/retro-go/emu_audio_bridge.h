#pragma once

// Ventilastation emulator audio bridge.
//
// retro-go's console cores (Genesis/gwenesis, NES/nofrendo, Lynx/handy)
// synthesize audio in real time from emulated sound chips. On the spinning
// LED-POV board there is no DAC, and the only link to the base-station host is
// a 115200 8N1 UART — far too slow for raw PCM (~100 KB/s vs ~11.5 KB/s).
//
// Instead of streaming samples, we stream the *sound-chip register writes* (a
// VGM-style "score") and let the host re-run a copy of the same chip emulator
// to regenerate the audio. This mirrors the Doom bridge, where the host owns
// the sound assets and the device only sends triggers — here the host owns the
// synthesizer and the device sends the register writes that drive it.
//
// Wire protocol (same "<line>\n" + binary framing as the rest of the link):
//   achip <system> [<nbytes>]\n [+ <nbytes> payload]
//       once at app start: host resets its synth. The optional payload is
//       the running ROM's filename (no path), used by cores whose fidelity
//       needs the actual ROM bytes (e.g. NES DMC sample playback reads
//       cartridge PRG-ROM) -- the host locates the same file from its own
//       roms/ tree, which is the source the board's own ROMs were synced
//       from. Omitted (no <nbytes>) for chips that need no ROM access.
//   aframe <nbytes> <nsamples>\n + <nbytes> payload : one emulated video frame
//   astop\n                    at app exit
//
// aframe payload is a sequence of records:
//   varint(delta_samples) op(1) val(1)
// where delta_samples is the gap (in audio samples) since the previous write
// within the frame, giving the host sub-frame timing without absolute clocks.

#include <stdint.h>
#include <stddef.h>

// op byte encodes which chip/register the write targets.
//   Genesis YM2612 bus write (a = 0..3): op = 0x00..0x03
//   Genesis SN76489 byte:                op = EMU_OP_SN76489
//   NES APU (addr 0x4000..0x401f):        op = EMU_OP_NES_BASE | (addr & 0x1f)
//   SMS/GG PSG byte:                     op = EMU_OP_SMS_PSG
//   SMS/GG stereo byte:                  op = EMU_OP_SMS_GGSTEREO
#define EMU_OP_YM2612_BASE 0x00
#define EMU_OP_SN76489     0x04
#define EMU_OP_NES_BASE    0x20  // 0x20..0x3f
#define EMU_OP_SMS_PSG     0x40
#define EMU_OP_SMS_GGSTEREO 0x41

// Announce the running emulator to the host and bring up the UART link.
// `system` is a short token: "genesis", "sms-ntsc"/"sms-pal",
// "nes-ntsc"/"nes-pal", "lynx". `rom_name` is the ROM's filename (no
// directory), or NULL/empty if the host synth needs no ROM access.
void emu_audio_begin(const char *system, const char *rom_name);

// Tear down: tells the host to stop and flush its synth.
void emu_audio_end(void);

// Begin a new audio frame: clears the per-frame register log.
void emu_audio_frame_begin(void);

// Record one sound-chip register write captured at `sample_idx` (the audio
// sample position within the current frame). Cheap and safe to call from the
// emulation hot path; a no-op when the bridge is inactive.
void emu_audio_write(uint8_t op, uint8_t val, uint16_t sample_idx);

// Flush the frame's register log to the host. `nsamples` is how many audio
// samples this frame represents (the host renders exactly that many).
void emu_audio_frame_end(uint16_t nsamples);
