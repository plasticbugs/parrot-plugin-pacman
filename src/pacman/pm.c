// license:BSD-3-Clause
// copyright-holders:Nicola Salmoria, Stephane Humbert, Aaron Giles, AJR, Couriersud
//
// pm.c -- the Namco Pac-Man board as MAME 0.288 emulates it, in C for the
// cart:
//
//   pacman.cpp, pacman_v.cpp (Nicola Salmoria): the memory map, inputs,
//     interrupt, palette, tilemap and sprites
//   namco.cpp (Nicola Salmoria, Aaron Giles): the 3-voice WSG
//   74259.cpp (AJR), watchdog.cpp (Aaron Giles): the latch and watchdog
//   resnet.cpp (Couriersud): the palette's resistor weights
//   schedule.cpp, diexec.cpp, screen.cpp, sound.cpp (Aaron Giles and
//     others): MAME's timeslices, timers, VBLANK, input line events and
//     its stream updates
//
// The Z80 runs in MAME's timeslices, up to the next timer, and a write that
// changes the interrupt line ends its timeslice as it does in MAME, so the
// CPU sees VBLANK on the same cycle. MAME's license (BSD-3-Clause) is in
// README.md.
#include "pm.h"

#include <string.h>

#include "z80.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define PM_HOT __attribute__((section(".time_critical.pm")))
#else
#define PM_HOT
#endif

// ---------------------------------------------------------------------------
// attotime (src/emu/attotime.h)

typedef struct { int32_t s; int64_t a; } at_t;
#define AS_PER_SEC 1000000000000000000LL
#define AT_NEVER_S 1000000000
static const at_t at_zero = { 0, 0 };
static const at_t at_never = { AT_NEVER_S, 0 };

static inline bool at_is_never(at_t t) { return t.s >= AT_NEVER_S; }
static inline at_t at_add(at_t x, at_t y) {
	if (at_is_never(x) || at_is_never(y)) return at_never;
	at_t r = { x.s + y.s, x.a + y.a };
	if (r.a >= AS_PER_SEC) { r.a -= AS_PER_SEC; r.s++; }
	if (r.s >= AT_NEVER_S) return at_never;
	return r;
}
static inline at_t at_sub(at_t x, at_t y) {
	if (at_is_never(x)) return at_never;
	at_t r = { x.s - y.s, x.a - y.a };
	if (r.a < 0) { r.a += AS_PER_SEC; r.s--; }
	return r;
}
static inline int at_cmp(at_t x, at_t y) {
	if (x.s != y.s) return x.s < y.s ? -1 : 1;
	if (x.a != y.a) return x.a < y.a ? -1 : 1;
	return 0;
}
static inline at_t at_as(int64_t a) { at_t r = { 0, a }; return r; }
// floor(a * f / 1e18) for 0 <= a < 1e18, without 128-bit sums
static inline uint64_t mul_frac(int64_t a, uint32_t f) {
	uint64_t ah = (uint64_t)a / 1000000000u, al = (uint64_t)a % 1000000000u;
	uint64_t x = ah * f, y = al * f;
	uint64_t q1 = x / 1000000000u, r1 = x % 1000000000u;
	return q1 + (r1 * 1000000000u + y) / (uint64_t)AS_PER_SEC;
}
static inline uint64_t at_ticks(at_t t, uint32_t f) { return (uint64_t)t.s * f + mul_frac(t.a, f); }

// ---------------------------------------------------------------------------
// the machine's constants

#define PIX_AS (AS_PER_SEC / 6144000)   // HZ_TO_ATTOSECONDS(XTAL(18'432'000) / 3)
#define HTOTAL 384
#define VTOTAL 264
#define VBSTART 224
#define SCAN_AS (PIX_AS * HTOTAL)
#define FRAME_AS (SCAN_AS * VTOTAL)
#define VBLANK_AS (SCAN_AS * (VTOTAL - VBSTART))

static const pm_roms_t *roms;
pm_inputs_t pm_in = { PM_IN0, PM_IN1, PM_DSW1, PM_DSW2 };
uint8_t pm_vram[0x400], pm_cram[0x400], pm_ram[0x400], pm_spr2[0x10];
uint8_t pm_latch;
uint32_t pm_frames, pm_resets;
void (*pm_on_vblank)(void);

static bool irq_mask;
static uint8_t irq_vector;
static int watchdog_counter;
static bool watchdog_fired;
static uint8_t sink[0x100];             // writes that go nowhere
static uint8_t nop_read[0x100];         // 0x4800-0x4bff: 0xBF (pacman_read_nop)

// ---------------------------------------------------------------------------
// the scheduler (src/emu/schedule.cpp, src/emu/diexec.cpp), for one CPU

typedef struct {
	at_t local;                         // m_localtime
	int64_t aspc;                       // m_attoseconds_per_cycle
	uint32_t cps;                       // m_cycles_per_second
	int divshift;
	uint32_t divisor;
	uint64_t total;                     // m_totalcycles
} exec_t;

static exec_t ex;
static bool executing;

enum {
	TM_VBSTART, TM_VBEND, TM_SCAN0, TM_SND50,
	TM_SYNC,                            // temporary timers (MAME's synchronize()) from here
	TM_COUNT = TM_SYNC + 8
};
enum { SY_IRQ };

typedef struct {
	at_t expire, period;
	uint32_t seq;
	bool enabled, used;
	int type, param;
} tmr_t;

static tmr_t tm[TM_COUNT];
static uint32_t tm_seq;
static at_t basetime;
static bool in_callback;
static int cb_timer;
static at_t cb_expire;
static bool cb_modified;
static const int64_t quantum = AS_PER_SEC / 60; // the 60 Hz default quantum

static void exec_init(exec_t *e, uint32_t clock) {
	e->local = at_zero;
	e->cps = clock;
	e->aspc = AS_PER_SEC / clock;
	int64_t attos = e->aspc;
	e->divshift = 0;
	while (attos >= (1LL << 31)) { e->divshift++; attos >>= 1; }
	e->divisor = (uint32_t)attos;
	e->total = 0;
}

static inline at_t cycles_to_at(const exec_t *e, uint64_t c) {
	if (c < e->cps) return at_as((int64_t)c * e->aspc);
	at_t r = { (int32_t)(c / e->cps), (int64_t)(c % e->cps) * e->aspc };
	return r;
}

// machine().time(): the timer's time in a callback, the CPU's while it runs, else the base
static at_t now(void) {
	if (in_callback) return cb_expire;
	if (executing) return at_add(ex.local, cycles_to_at(&ex, (uint64_t)z80_done()));
	return basetime;
}

static int head(void) {
	int h = -1;
	for (int i = 0; i < TM_COUNT; i++) {
		if (!tm[i].used || !tm[i].enabled) continue;
		if (h < 0) { h = i; continue; }
		int c = at_cmp(tm[i].expire, tm[h].expire);
		if (c < 0 || (c == 0 && tm[i].seq < tm[h].seq)) h = i;
	}
	return h;
}

static at_t head_expire(void) {
	int h = head();
	return h < 0 ? at_never : tm[h].expire;
}

static void tm_insert(int i) {
	tm[i].seq = ++tm_seq;
	if (in_callback && i == cb_timer) cb_modified = true;
	if (head() == i && executing) z80_abort();
}

static void tm_adjust(int i, at_t delay, int param) {
	tmr_t *t = &tm[i];
	t->used = true;
	t->enabled = true;
	t->param = param;
	t->period = at_never;
	t->expire = at_add(now(), delay);
	if (at_is_never(t->expire)) t->enabled = false;
	tm_insert(i);
}

static void synchronize(int type, int param) {
	for (int i = TM_SYNC; i < TM_COUNT; i++) {
		if (tm[i].used) continue;
		tm[i].type = type;
		tm_adjust(i, at_zero, param);
		return;
	}
}

// the IRQ line's event queue (device_input::set_state_synced): the first
// event of a batch sets a timer for now, which applies them all
static uint8_t irq_queue[32];
static int irq_qn;

static void set_irq_line(int state) {
	if (irq_qn == (int)sizeof irq_queue) {          // full: applied now (MAME logs it)
		for (int i = 0; i < irq_qn; i++) z80_set_irq(irq_queue[i]);
		irq_qn = 0;
	}
	irq_queue[irq_qn++] = (uint8_t)state;
	if (irq_qn == 1) synchronize(SY_IRQ, 0);
}

static void irq_events(void) {
	for (int i = 0; i < irq_qn; i++) z80_set_irq(irq_queue[i]);
	irq_qn = 0;
}

// ---------------------------------------------------------------------------
// the WSG (namco_wsg_device): its stream at 192 kHz, summed into bins

static struct { uint32_t freq, counter; uint8_t vol, sel; } voice[3];
static uint8_t soundregs[0x20];
static bool sound_enable;
static uint64_t wsg_next;               // the stream's next sample (its write position)
static int8_t wave[8][32];              // the 82s126's low nibble less 8, per waveform

#ifndef PM_NBIN
#define PM_NBIN 2048                    // the bins the sound can be ahead (the cart needs few)
#endif
#define NBIN PM_NBIN
static int32_t bin_sum[NBIN];
static uint16_t bin_n[NBIN];
static uint32_t bin_w, bin_r;
static uint64_t bin_step_fx;            // stream samples per bin, 32.32
static uint32_t bin_frac;               // where the current bin started, the fraction
static uint32_t bin_len, bin_have;      // the current bin's length, and its samples so far
static int32_t bin_acc;

static void bin_start(void) {
	uint64_t e = (uint64_t)bin_frac + bin_step_fx;
	bin_len = (uint32_t)(e >> 32);
	bin_frac = (uint32_t)e;
	bin_have = 0;
	bin_acc = 0;
}

void pm_audio_rate_q8(uint32_t hz_q8) {
	bin_step_fx = (((uint64_t)PM_WSG_RATE << 40) / hz_q8);
	bin_frac = 0;
	bin_start();
}

static PM_HOT void wsg_samples(uint32_t n) {
	while (n) {
		uint32_t m = bin_len - bin_have;
		if (m > n) m = n;
		if (sound_enable) {
			for (int v = 0; v < 3; v++) {
				if (!voice[v].vol) continue;
				const int8_t *w = wave[voice[v].sel];
				uint32_t c = voice[v].counter, f = voice[v].freq;
				int32_t s = 0;
				for (uint32_t i = 0; i < m; i++) {
					s += w[(c >> 16) & 31];
					c += f;
				}
				voice[v].counter = c;
				bin_acc += s * voice[v].vol;
			}
		}
		bin_have += m;
		n -= m;
		if (bin_have == bin_len) {
			if (bin_w - bin_r < NBIN) {
				bin_sum[bin_w % NBIN] = bin_acc;
				bin_n[bin_w % NBIN] = (uint16_t)bin_len;
				bin_w++;
			}
			bin_start();
		}
	}
}

// sound_stream::update(): the samples up to now, now's included
static void wsg_update(void) {
	at_t t = now();
	if (t.s == 0 && t.a == 0) return;
	uint64_t idx = at_ticks(t, PM_WSG_RATE);
	if (idx + 1 > wsg_next) {
		wsg_samples((uint32_t)(idx + 1 - wsg_next));
		wsg_next = idx + 1;
	}
}

static void wsg_write(int offset, uint8_t data) {       // pacman_sound_w
	data &= 0x0f;
	if (soundregs[offset] == data) return;
	wsg_update();
	soundregs[offset] = data;
	int ch;
	if (offset < 0x10) ch = (offset - 5) / 5;
	else if (offset == 0x10) ch = 0;
	else ch = (offset - 0x11) / 5;
	if (ch < 0 || ch >= 3) return;      // (C's division truncates as C++'s does: offsets 1-4 give 0)
	switch (offset - ch * 5) {
	case 0x05:
		voice[ch].sel = data & 7;
		break;
	case 0x10: case 0x11: case 0x12: case 0x13: case 0x14:
		voice[ch].freq = ch == 0 ? soundregs[0x10] : 0;
		voice[ch].freq += (uint32_t)soundregs[ch * 5 + 0x11] << 4;
		voice[ch].freq += (uint32_t)soundregs[ch * 5 + 0x12] << 8;
		voice[ch].freq += (uint32_t)soundregs[ch * 5 + 0x13] << 12;
		voice[ch].freq += (uint32_t)soundregs[ch * 5 + 0x14] << 16;
		break;
	case 0x15:
		voice[ch].vol = data;
		break;
	}
}

int pm_audio_take(int32_t *sum, uint16_t *count, int max) {
	int k = 0;
	while (k < max && bin_r != bin_w) {
		sum[k] = bin_sum[bin_r % NBIN];
		count[k] = bin_n[bin_r % NBIN];
		bin_r++;
		k++;
	}
	return k;
}

// ---------------------------------------------------------------------------
// the latch, the watchdog, memory

static void latch_bit(int bit, int state) {             // ls259_device::write_bit
	if (((pm_latch >> bit) & 1) == state) return;
	pm_latch ^= (uint8_t)(1u << bit);
	switch (bit) {
	case 0:                                             // irq_mask_w
		irq_mask = state;
		if (!state) set_irq_line(0);
		break;
	case 1:                                             // namco sound_enable_w (no stream update)
		sound_enable = state;
		break;
	default:                                            // flip, coin counter, unconnected
		break;
	}
}

uint8_t pm_rd(uint16_t a) {             // 0x5000-0x5fff and its mirrors
	switch ((a >> 6) & 3) {
	case 0: return pm_in.in0;
	case 1: return pm_in.in1;
	case 2: return pm_in.dsw1;
	default: return pm_in.dsw2;
	}
}

void pm_wr(uint16_t a, uint8_t d) {
	uint8_t o = (uint8_t)a;
	if (o < 0x40) latch_bit(o & 7, d & 1);
	else if (o < 0x60) wsg_write(o - 0x40, d);
	else if (o < 0x70) pm_spr2[o & 0x0f] = d;
	else if (o >= 0xc0) watchdog_counter = 16;          // watchdog reset_w
}

uint8_t pm_io_in(uint16_t port) { (void)port; return 0xff; }  // (no I/O read is mapped: not reached by Pac-Man)
void pm_io_out(uint16_t port, uint8_t d) { if ((port & 0xff) == 0) irq_vector = d; }
uint8_t pm_irq_ack(void) { return irq_vector; }

static void map_pages(void) {
	for (int p = 0; p < 256; p++) {
		z80_rd_page[p] = 0;
		z80_wr_page[p] = sink;
		int m = p & 0x7f;                               // A15 isn't decoded
		if (m < 0x40) { z80_rd_page[p] = roms->maincpu + (m << 8); continue; }
		m &= 0x5f;                                      // nor is A13 above 0x4000
		if (m < 0x44) z80_rd_page[p] = z80_wr_page[p] = pm_vram + ((m & 3) << 8);
		else if (m < 0x48) z80_rd_page[p] = z80_wr_page[p] = pm_cram + ((m & 3) << 8);
		else if (m < 0x4c) z80_rd_page[p] = nop_read;
		else if (m < 0x50) z80_rd_page[p] = z80_wr_page[p] = pm_ram + ((m & 3) << 8);
		else z80_wr_page[p] = 0;                        // 0x5000-0x5fff: pm_rd, pm_wr
	}
}

uint8_t pm_peek(uint16_t a) {
	const uint8_t *p = z80_rd_page[a >> 8];
	return p ? p[a & 0xff] : pm_rd(a);
}

// ---------------------------------------------------------------------------
// video (pacman_v.cpp)

uint32_t pm_rgb[16];
uint16_t pm_tile_rows[256][8];
uint32_t pm_sprite_rows[64][16];
uint8_t pm_clut[128];
static uint8_t sprite_opaque[32];       // per color: bit p set if pen p isn't see-through

static void video_init(void) {
	static const uint8_t l3[8] = { 0, 33, 71, 104, 151, 184, 222, 255 };   // resistor weights 1000, 470, 220
	static const uint8_t l2[4] = { 0, 81, 174, 255 };                       // 470, 220
	const uint8_t *prom = roms->proms;
	for (int i = 0; i < 16; i++) {
		uint8_t c = prom[i];
		pm_rgb[i] = (uint32_t)l3[c & 7] << 16 | (uint32_t)l3[(c >> 3) & 7] << 8 | l2[c >> 6];
	}
	for (int i = 0; i < 128; i++) pm_clut[i] = prom[0x20 + i] & 0x0f;
	for (int c = 0; c < 32; c++) {
		sprite_opaque[c] = 0;
		for (int p = 0; p < 4; p++)
			if (pm_clut[c * 4 + p] != 0) sprite_opaque[c] |= (uint8_t)(1u << p);
	}
	// tilelayout: planes at bits 0 and 4, x 64-67 then 0-3, y in bytes, 16 bytes a tile
	const uint8_t *g = roms->gfx;
	for (int t = 0; t < 256; t++)
		for (int y = 0; y < 8; y++) {
			uint16_t row = 0;
			for (int x = 0; x < 8; x++) {
				int bit = (x < 4 ? 64 + x : x - 4) + y * 8;
				uint8_t b = g[t * 16 + (bit >> 3)];
				int sh = bit & 7;
				int pen = ((b >> (7 - sh)) & 1) << 1 | ((b >> (3 - sh)) & 1);
				row |= (uint16_t)(pen << (2 * x));
			}
			pm_tile_rows[t][y] = row;
		}
	// spritelayout: x 64-67, 128-131, 192-195, 0-3; y 0-7 then 32-39 rows; 64 bytes a sprite
	static const int sx[16] = { 64, 65, 66, 67, 128, 129, 130, 131, 192, 193, 194, 195, 0, 1, 2, 3 };
	for (int s = 0; s < 64; s++)
		for (int y = 0; y < 16; y++) {
			uint32_t row = 0;
			int yo = (y < 8 ? y : y + 24) * 8;
			for (int x = 0; x < 16; x++) {
				int bit = sx[x] + yo;
				uint8_t b = g[0x1000 + s * 64 + (bit >> 3)];
				int sh = bit & 7;
				int pen = ((b >> (7 - sh)) & 1) << 1 | ((b >> (3 - sh)) & 1);
				row |= (uint32_t)pen << (2 * x);
			}
			pm_sprite_rows[s][y] = row;
		}
	memset(nop_read, 0xbf, sizeof nop_read);
}

// the tilemap's memory index of raw tile column col (0-35), row (0-27): pacman_scan_rows
static inline int tile_index(int col, int row) {
	row += 2;
	col -= 2;
	if (col & 0x20) return row + ((col & 0x1f) << 5);
	return col + (row << 5);
}

static void draw_sprite(uint8_t fb[PM_H][PM_W], int code, int color, int fx, int fy, int sx, int sy) {
	const uint32_t *rows = pm_sprite_rows[code & 63];
	const uint8_t *clut = pm_clut + (color & 0x1f) * 4;
	uint8_t opaque = sprite_opaque[color & 0x1f];
	for (int y = 0; y < 16; y++) {
		int py = sy + y;
		if (py < 0 || py > 28 * 8 - 1) continue;        // spriteclip: y 0-223
		uint32_t row = rows[fy ? 15 - y : y];
		for (int x = 0; x < 16; x++) {
			int px = sx + x;
			if (px < 2 * 8 || px > 34 * 8 - 1) continue; // x 16-271
			int pen = (row >> (2 * (fx ? 15 - x : x))) & 3;
			if (opaque & (1u << pen)) fb[py][px] = clut[pen];
		}
	}
}

void pm_render(uint8_t fb[PM_H][PM_W]) {            // screen_update_pacman (bgpriority 0)
	bool flip = (pm_latch >> 3) & 1;
	for (int y = 0; y < PM_H; y++)
		for (int x = 0; x < PM_W; x++) {
			int tx = flip ? PM_W - 1 - x : x, ty = flip ? PM_H - 1 - y : y;
			int i = tile_index(tx >> 3, ty >> 3);
			int pen = (pm_tile_rows[pm_vram[i]][ty & 7] >> (2 * (tx & 7))) & 3;
			fb[y][x] = pm_clut[(pm_cram[i] & 0x1f) * 4 + pen];
		}
	for (int offs = 14; offs > 4; offs -= 2) {
		int sx = 272 - pm_spr2[offs + 1], sy = pm_spr2[offs] - 31;
		int code = pm_spr[offs] >> 2, color = pm_spr[offs + 1] & 0x1f;
		draw_sprite(fb, code, color, pm_spr[offs] & 1, pm_spr[offs] & 2, sx, sy);
		draw_sprite(fb, code, color, pm_spr[offs] & 1, pm_spr[offs] & 2, sx - 256, sy);
	}
	for (int offs = 4; offs >= 0; offs -= 2) {
		int sx = 272 - pm_spr2[offs + 1], sy = pm_spr2[offs] - 31;
		int code = pm_spr[offs] >> 2, color = pm_spr[offs + 1] & 0x1f;
		draw_sprite(fb, code, color, pm_spr[offs] & 1, pm_spr[offs] & 2, sx, sy + 1);
		draw_sprite(fb, code, color, pm_spr[offs] & 1, pm_spr[offs] & 2, sx - 256, sy + 1);
	}
}

// ---------------------------------------------------------------------------
// timers

static bool frame_done;
static at_t vblank_start_time;

static at_t time_until_pos(int v) {
	v += VTOTAL - VBSTART;
	v %= VTOTAL;
	int64_t target = (int64_t)v * SCAN_AS;
	int64_t cur = at_sub(now(), vblank_start_time).a;
	if (target <= cur + PIX_AS / 2) target += FRAME_AS;
	while (target <= cur) target += FRAME_AS;
	return at_as(target - cur);
}

static void cb_vbstart(void) {
	vblank_start_time = now();
	pm_frames++;                        // video().frame_update(): the picture is RAM as it is now
	frame_done = true;
	if (pm_on_vblank) pm_on_vblank();
	if (--watchdog_counter == 0) watchdog_fired = true;  // the screen's VBLANK callbacks
	if (irq_mask) set_irq_line(1);      // screen_vblank -> vblank_irq
	tm_adjust(TM_VBSTART, time_until_pos(VBSTART), 0);
	tm_adjust(TM_VBEND, at_sub(at_add(vblank_start_time, at_as(VBLANK_AS)), now()), 0);
}

static void execute_timers(void) {
	for (;;) {
		int h = head();
		if (h < 0 || at_cmp(tm[h].expire, basetime) > 0) break;
		tmr_t *t = &tm[h];
		bool was_enabled = t->enabled;
		if (at_is_never(t->period)) t->enabled = false;
		in_callback = true;
		cb_timer = h;
		cb_expire = t->expire;
		cb_modified = false;
		if (was_enabled) {
			switch (h) {
			case TM_VBSTART: cb_vbstart(); break;
			case TM_VBEND: break;
			case TM_SCAN0: tm_adjust(TM_SCAN0, time_until_pos(0), 0); break;
			case TM_SND50: wsg_update(); break;     // the sound manager's 50 Hz streams update
			default: if (t->type == SY_IRQ) irq_events(); break;
			}
		}
		if (!cb_modified) {
			if (h >= TM_SYNC) t->used = false;
			else if (!at_is_never(t->period)) {
				t->expire = at_add(t->expire, t->period);
				t->enabled = true;
				t->seq = ++tm_seq;
			}
		}
		in_callback = false;
	}
}

static PM_HOT void timeslice(void) {
	while (at_cmp(basetime, head_expire()) < 0) {
		at_t target = at_add(basetime, at_as(quantum));
		at_t he = head_expire();
		if (at_cmp(he, target) < 0) target = he;
		if (target.s >= ex.local.s) {
			int64_t delta = target.a - ex.local.a;
			if (delta < 0 && target.s > ex.local.s) delta += AS_PER_SEC;
			if (delta >= ex.aspc) {
				int cycles = (int)(((uint64_t)delta >> ex.divshift) / ex.divisor);
				executing = true;
				int ran = z80_run(cycles);
				executing = false;
				ex.total += (uint64_t)ran;
				ex.local = at_add(ex.local, cycles_to_at(&ex, (uint64_t)ran));
				if (at_cmp(ex.local, target) < 0)
					target = at_cmp(ex.local, basetime) > 0 ? ex.local : basetime;
			}
		}
		basetime = target;
	}
	execute_timers();
}

// ---------------------------------------------------------------------------

uint64_t pm_cycles(void) { return ex.total + (executing ? (uint64_t)z80_done() : 0); }
uint64_t pm_time(void) { return at_ticks(basetime, PM_CLOCK); }

void pm_wsg_voice(int v, uint32_t *freq, uint8_t *vol, uint8_t *wave) {
	*freq = voice[v].freq;
	*vol = voice[v].vol;
	*wave = voice[v].sel;
}

const int8_t *pm_wsg_wave(int w) { return wave[w & 7]; }

static void board_reset(void) {         // device_reset of the CPU, the latch and the watchdog
	z80_reset();
	for (int b = 0; b < 8; b++) latch_bit(b, 0);        // ls259: clear_outputs(0)
	watchdog_counter = 16;
}

void pm_init(const pm_roms_t *r) {
	roms = r;
	memset(tm, 0, sizeof tm);
	tm_seq = 0;
	basetime = at_zero;
	executing = false;
	in_callback = false;
	exec_init(&ex, PM_CLOCK);
	memset(pm_vram, 0, sizeof pm_vram);
	memset(pm_cram, 0, sizeof pm_cram);
	memset(pm_ram, 0, sizeof pm_ram);
	memset(pm_spr2, 0, sizeof pm_spr2);
	pm_frames = pm_resets = 0;
	irq_mask = false;
	irq_vector = 0;
	irq_qn = 0;
	watchdog_fired = false;
	vblank_start_time = at_zero;
	for (int s = 0; s < 8; s++)
		for (int i = 0; i < 32; i++) wave[s][i] = (int8_t)((r->wave[s * 32 + i] & 0x0f) - 8);
	memset(voice, 0, sizeof voice);
	memset(soundregs, 0, sizeof soundregs);
	sound_enable = true;                // namco device_start: on, until the latch says
	wsg_next = 0;
	bin_w = bin_r = 0;
	if (!bin_step_fx) pm_audio_rate_q8(48000u << 8);
	bin_frac = 0;
	bin_start();
	video_init();
	map_pages();

	// device_start, in MAME's order: the CPU, the latch (outputs all 1s until its reset), the screen's timers
	z80_start();
	pm_latch = 0xff;                    // ls259's m_q
	tm_adjust(TM_VBSTART, time_until_pos(VBSTART), 0);
	tm_adjust(TM_SCAN0, time_until_pos(0), 0);
	tm[TM_SND50].used = true;
	tm[TM_SND50].enabled = true;
	tm[TM_SND50].period = at_as(AS_PER_SEC / 50);
	tm[TM_SND50].expire = tm[TM_SND50].period;
	tm[TM_SND50].seq = ++tm_seq;

	// device_reset (the latch's clear sets the IRQ line clear, through a timer for now)
	board_reset();
}

uint32_t pm_run_to(uint64_t until) {
	uint32_t f0 = pm_frames;
	while (at_ticks(basetime, PM_CLOCK) < until) timeslice();
	return pm_frames - f0;
}

bool pm_frame(void) {
	frame_done = false;
	while (!frame_done) timeslice();
	if (watchdog_fired) {               // schedule_soft_reset: every device's reset
		watchdog_fired = false;
		pm_resets++;
		board_reset();
		return false;
	}
	return true;
}
