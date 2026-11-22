// pmcv: the cart's side of Pac-Man on the ColecoVision. Portable C (no
// pico-sdk), used by the plugin and by the Mac tests.
//
// The console runs gbkernel (coleco/gbcv/kernel/gbkernel.asm): each TV
// frame it takes a command list from the cart and runs it. This makes those
// lists, from the board's RAM and sound (pm.h):
//
//   once:                         pmcv_init(e)
//   at each VBLANK the cart uses: pmcv_picture(e, board)   (or pmcv_canvas for a message)
//   each list:                    pmcv_sound(e, samples, PMCV_PCM_N)
//                                 len = pmcv_tv_frame(e, list)
//
// The picture. MAME shows the board's 288 x 224 turned a quarter (ROT90):
// 224 wide and 288 high, 36 rows of 8 x 8 tiles. The maze (rows 3 to 33,
// 31 rows, 248 lines) is scaled by 3/4 both ways (sdk/cvscale.h) to 168 x
// 186, in the middle of the TMS9918's 256 x 192. The rows above and below
// it (the scores, the lives, the fruit, the credits) go to the sides, in
// pieces of up to 7 tiles, scaled the same way (layout[] in pmcv.c).
// The TMS9918 shows the tiles in Graphics II, each 8 x 1 in two colors.
//
// The characters (the board's 8 sprites) are scaled by themselves, so they
// keep their shape as they move. Each one's main color (the most of its
// pixels: a ghost's body, Pac-Man) is a TMS9918 sprite, 16 x 16; its other
// colors (a ghost's eyes) go into the tiles under it, where the sprite has
// holes. The sprites are ordered as the board's (0 in front); where more
// than 4 share a line, the order turns each picture so all are seen.
//
// A list carries the bytes that differ from what the console holds, the
// sprites' first, as many as the frame has time for.
//
// The sound. The WSG's three voices, mixed as MAME mixes them (pm.c), go
// out as PMCV_PCM_N samples a list through two of the SN76489's tones
// parked at a period of 1, their two attenuators summed as a DAC (Q*bert's
// way, ../qbert/qbcv.c). The console runs the lists back to back, so a
// list's length is the sample clock: PMCV_LIST_T T, 100 samples, 5,962 a
// second. The plugin runs the board for just that time between lists.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pm.h"

#define PMCV_W      256
#define PMCV_H      192
#define PMCV_BYTES  6144            // pattern bytes, and as many color bytes
#define PMCV_LIST_MAX 3000
#define PMCV_PCM_N  100             // samples a list
#define PMCV_LIST_T 60036           // a list's length in the console's T (3,579,545 a second)
#define PMCV_SPRITES 8              // the board's
// the most tiles a picture draws again (of the 868 shown); the rest, rows
// at a time, go to the next (pmrun.c skips a list between). A whole new
// screen (the power-on test, a new maze) then takes two pictures, while its
// bytes take four or five lists to send anyway.
#define PMCV_TILES 480
// the board's Z80 cycles (3.072 MHz) in a list, << 16
#define PMCV_LIST_CYCLES_Q16 ((uint64_t)PMCV_LIST_T * PM_CLOCK * 65536u / 3579545u)
// the samples' rate, << 8: PMCV_PCM_N a list
#define PMCV_RATE_Q8 ((uint32_t)((uint64_t)PMCV_PCM_N * 3579545u * 256u / PMCV_LIST_T))

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define PMCV_HOT __attribute__((section(".time_critical.pmcv")))
#else
#define PMCV_HOT
#endif

typedef struct {
    uint32_t tv_frames, pictures, pat_bytes, col_bytes, spr_bytes, runs, refreshed, pcm_frames;
    uint32_t settled;               // lists that ended with nothing left to send
    uint32_t t_max;                 // the longest list, in T
    uint32_t tiles;                 // tiles drawn again
    uint32_t deferred;              // pictures that left changed tiles to the next
    uint32_t cells;                 // 8 x 1 places made again
    uint32_t crowded;               // pictures with more than 4 sprites on a line
    uint32_t clipped;               // samples the limiter took in
} pmcv_stats_t;

typedef struct {
    // the console's VRAM as the lists so far leave it, and the newest picture
    uint8_t vram_pat[PMCV_BYTES], vram_col[PMCV_BYTES];
    uint8_t want_pat[PMCV_BYTES], want_col[PMCV_BYTES];
    uint8_t vram_sat[128], want_sat[128];       // the sprites' attributes (0x1B00)
    uint8_t vram_spat[64][32], want_spat[64][32];   // their patterns, 16 x 16 (0x3800)
    uint64_t spat_dirty;            // the patterns that differ (a bit each)
    bool regs_done;                 // the sprites' registers set (16 x 16, their tables)
    bool names_done;
    uint16_t names_at;
    uint16_t cursor;                // where the next list's scan starts
    uint16_t refresh_at;
    bool dirty;                     // want differs from vram somewhere (or may)
    uint32_t bands;                 // the same, for each band of 8 lines (256 bytes)

    // the picture as it was made last: the board's tiles, and what each
    // character covered (to make again where it was)
    bool pic_valid;
    uint8_t tile_v[0x400], tile_c[0x400];
    uint32_t pending[36];           // tiles changed but not drawn yet (PMCV_TILES), row by row
    uint8_t row_next;               // the row the next picture starts at
    uint32_t cells_prev[24];        // the 8 x 8 places the characters were in
    uint8_t turn;                   // the sprites' order, turned where crowded

    // the sound
    bool pcm;                       // the lists carry samples (the game is on)
    uint8_t pcm_gain;               // 0-32: the DAC comes up over the first lists
    int32_t hp_x, hp_y;             // the high-pass's last input and output (<< 8)
    uint8_t pcm_bytes[PMCV_PCM_N][3];  // each sample's three attenuators, in the order written
    uint8_t dac_att[3];             // the attenuators as the last sample left them
    uint8_t tone_bytes[12];
    uint8_t tone_n;
    uint8_t resync;
    pmcv_stats_t stats;
} pmcv_t;

void pmcv_init(pmcv_t *e);
// room the plugin may use before the first pmcv_picture (a message's
// canvas, the card's reads); pmcv_canvas and pmcv_sound leave it alone
uint8_t *pmcv_scratch(size_t *n);

// what a picture is made from: the board's tile, color and sprite RAM at a
// VBLANK (pm_vram, pm_cram, pm_spr, pm_spr2)
typedef struct {
    uint8_t vram[0x400], cram[0x400], spr[0x10], spr2[0x10];
} pmcv_board_t;
void pmcv_board(pmcv_board_t *b);       // the board as it is
// a new picture of b
bool pmcv_picture(pmcv_t *e, const pmcv_board_t *b);     // false: it left tiles for the next
// a new picture: 256 x 192 pixels, a TMS9918 color each (1-15); for messages
void pmcv_canvas(pmcv_t *e, const uint8_t *px);

// The next list's sound: n samples at PMCV_RATE_Q8, MAME's +-1.0 as
// +-32767; n = 0: none (a message: the console waits for the vertical
// blank again, and the DAC is let go)
void pmcv_sound(pmcv_t *e, const int16_t *s, int n);

// the next list into out (PMCV_LIST_MAX bytes); returns its length
size_t pmcv_tv_frame(pmcv_t *e, uint8_t *out);

// the picture the console shows once the lists so far have run, sprites
// and all, a TMS9918 color a pixel (tests, previews)
void pmcv_screen(const pmcv_t *e, uint8_t *px);
// the picture wanted, the same way
void pmcv_wanted(const pmcv_t *e, uint8_t *px);
// the picture before the TMS9918's limits: the board's colors as TMS9918
// colors, scaled and laid out, every pixel its own color (tests, previews)
void pmcv_ideal(uint8_t *px);
// the tiles as scaled and laid out so far, the board's colors (tests); the
// layout's pieces of MAME's picture: its tiles' columns and rows, and where
// the piece goes
void pmcv_scaled(uint8_t *px);
// a tile as pmcv draws it: the board's, turned (row v's pens, pixel u at
// bits 2u), or one of the glyphs it draws again
const uint16_t *pmcv_turned(int code);
int pmcv_pieces(void);
void pmcv_layout(int i, int *c0, int *r0, int *nc, int *nr, int *x, int *y);
// bytes the console still needs (tests; a whole scan)
uint32_t pmcv_backlog(const pmcv_t *e);
// the DAC's level for an attenuator pair (tests: the lists' sound as a WAV)
float pmcv_dac_level(int att0, int att1, int att2);
// the TMS9918's colors as 0xRRGGBB (previews)
extern const uint32_t pmcv_tms_rgb[16];
// the board's colors (pm.h's pens 0-15) as TMS9918 colors
extern uint8_t pmcv_color[16];
// the maze's left column on the TMS9918 (tests: the layout's place)
extern int pmcv_maze_x;
// the characters' main colors as the TMS9918's sprites (true), or all in
// the tiles (tests: the two compared)
extern bool pmcv_sprites;
