// license:BSD-3-Clause
// copyright-holders:Nicola Salmoria, Stephane Humbert, Aaron Giles, AJR, Couriersud
//
// pm.h -- the Namco Pac-Man board as MAME 0.288 emulates it (pacman.cpp,
// pacman_v.cpp, namco.cpp, 74259.cpp, watchdog.cpp and the Z80 core), with
// MAME's scheduler, so the Z80 runs the cycles MAME's does.
#ifndef PM_BOARD_H
#define PM_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#define PM_W 288                        // the raw screen, before MAME's ROT90
#define PM_H 224
#define PM_CLOCK 3072000u               // the Z80: XTAL(18'432'000) / 6
#define PM_WSG_RATE 192000u             // the WSG's stream: 96 kHz, doubled as MAME does

typedef struct {
	const uint8_t *maincpu;             // 16K: pacman.6e, 6f, 6h, 6j
	const uint8_t *gfx;                 // 8K: pacman.5e (tiles), pacman.5f (sprites)
	const uint8_t *proms;               // 0x120: 82s123.7f (palette), 82s126.4a (color lookup)
	const uint8_t *wave;                // 0x100: 82s126.1m (the WSG's waveforms)
} pm_roms_t;

// the ports as MAME's read them (active low, but DSW2). MAME's defaults:
// IN0 0xFF, IN1 0xFF (upright), DSW1 0xC9 (1 coin 1 credit, 3 lives, bonus
// at 10000, normal, normal ghost names), DSW2 0x00
typedef struct {
	uint8_t in0;                        // 0x01 up, 0x02 left, 0x04 right, 0x08 down, 0x10 rack test, 0x20 coin 1, 0x40 coin 2, 0x80 service
	uint8_t in1;                        // 0x01-0x08 player 2's stick, 0x10 service mode, 0x20 start 1, 0x40 start 2, 0x80 upright
	uint8_t dsw1, dsw2;
} pm_inputs_t;
#define PM_IN0 0xff
#define PM_IN1 0xff
#define PM_DSW1 0xc9
#define PM_DSW2 0x00

extern pm_inputs_t pm_in;

void pm_init(const pm_roms_t *roms);    // power on
bool pm_frame(void);                    // run to the next VBLANK start; false if the watchdog reset the board

// The cart's way: run to the Z80's cycle `until` (from power on), stopping
// at the first of MAME's timeslice ends at or past it. Returns the VBLANKs
// passed; pm_on_vblank, if set, is called at each, with the frame's state.
uint32_t pm_run_to(uint64_t until);
uint64_t pm_time(void);                 // where the board is, in Z80 cycles
extern void (*pm_on_vblank)(void);

// RAM, as the Z80 sees it
extern uint8_t pm_vram[0x400];          // 0x4000 tiles
extern uint8_t pm_cram[0x400];          // 0x4400 their colors
extern uint8_t pm_ram[0x400];           // 0x4c00-0x4fff (0x4ff0-0x4fff: sprite codes and colors)
extern uint8_t pm_spr2[0x10];           // 0x5060-0x506f: sprite positions (write only)
#define pm_spr (pm_ram + 0x3f0)
extern uint8_t pm_latch;                // the 74LS259: bit 0 IRQ enable, 1 sound on, 3 flip
extern uint32_t pm_frames;              // VBLANKs so far
extern uint32_t pm_resets;              // watchdog resets so far

// the picture: MAME's screen bitmap (raw, 288 x 224) in pens 0-15, the
// 82s123's colors; pm_rgb[] is each as MAME's resistor weights make it
void pm_render(uint8_t fb[PM_H][PM_W]);
extern uint32_t pm_rgb[16];             // 0xRRGGBB
// the graphics, decoded: a tile's row y (raw orientation), 8 pens of 2
// bits, pixel x at bits 2x; a sprite's row, 16 pens the same way; and the
// 82s126's color lookup (pen p of color c: pm_clut[c * 4 + p], 0-15)
extern uint16_t pm_tile_rows[256][8];
extern uint32_t pm_sprite_rows[64][16];
extern uint8_t pm_clut[128];

// sound: the WSG's stream, MAME's samples summed into the bins of the rate
// set here (a box filter): each bin is the mean of its samples, in units of
// 1/384 of MAME's -1..1 (MAME's mix: wave -8..7 times volume 0..15 per
// voice, three voices, over 384)
void pm_audio_rate_q8(uint32_t hz_q8);  // in 256ths of a hertz
int pm_audio_take(int32_t *sum, uint16_t *count, int max);  // bins done: their sums and sample counts

// for the tests
uint64_t pm_cycles(void);               // the Z80's cycles so far
// a WSG voice as the game has set it: its frequency (20 bits for voice 0,
// 16 shifted up 4 for 1 and 2: the waveform's rate is freq * 96000 / 2^20
// a second), volume 0-15 and waveform 0-7
void pm_wsg_voice(int v, uint32_t *freq, uint8_t *vol, uint8_t *wave);
const int8_t *pm_wsg_wave(int wave);    // its 32 steps, -8..7
uint8_t pm_peek(uint16_t a);            // as the Z80 reads, without side effects

#endif
