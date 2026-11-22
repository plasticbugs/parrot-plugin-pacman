// pmcv.c -- see pmcv.h. The lists and the sound are qbcv.c's (../qbert),
// the layout and the sprites are new.
#include "pmcv.h"

#include <math.h>
#include <string.h>

#include "pmdac3.h"

#include "../sdk/cvscale.h"

// ---- lists (qbcv.c's, from gbkernel.asm's costs) ---------------------------------
#define FRAME_T    (262 * 228)
#define VBLANK_T   (70 * 228)
#define C_START    480
#define C_RUN_VB   106
#define C_BYTE_VB  23
#define C_RUN_ACT  106
#define C_BYTE_ACT 31
#define C_PSG      75
#define C_PSG_BYTE 52
#define C_REG      114
#define C_END_VB   54
#define C_END      52
#define VB_LIMIT   (VBLANK_T - 600)
#define LIST_LIMIT (FRAME_T - 1200)

// The console goes from one list straight into the next, so a list's
// length is the sample clock (qbcv.c's)
#define C_FRAME    357
#define PCM_L      PMCV_LIST_T
#define PCM_FIRST  (C_FRAME + C_END_VB)
#define C_SAMPLE   (C_PSG + 3 * C_PSG_BYTE)
#define PCM_STEP   (((PCM_L - PCM_FIRST - C_SAMPLE - C_END) << 8) / (PMCV_PCM_N - 1))

#define PATTERNS 0x0000
#define COLORS   0x2000
#define NAMES    0x1800
#define SAT      0x1B00
#define SPAT     0x3800
#define BLACK    1

typedef struct {
    uint8_t *b;
    size_t n;
    uint32_t t;
    bool vblank;
} list_t;

static void l_run_head(list_t *l, uint16_t addr, int n) {
    l->t += l->vblank ? C_RUN_VB + n * C_BYTE_VB : C_RUN_ACT + n * C_BYTE_ACT;
    l->b[l->n++] = 1;
    l->b[l->n++] = (uint8_t)addr;
    l->b[l->n++] = (uint8_t)((addr >> 8) & 0x3F) | 0x40;
    l->b[l->n++] = (uint8_t)n;
}

static void l_psg(list_t *l, const uint8_t *d, int n) {
    l->t += C_PSG + C_PSG_BYTE * n;
    l->b[l->n++] = 2;
    l->b[l->n++] = (uint8_t)n;
    memcpy(l->b + l->n, d, n);
    l->n += n;
}

static void l_reg(list_t *l, uint8_t r, uint8_t v) {
    l->t += C_REG;
    l->b[l->n++] = 3;
    l->b[l->n++] = v;
    l->b[l->n++] = 0x80 | r;
}

static void l_end_vblank(list_t *l) {
    if (!l->vblank) return;
    l->vblank = false;
    l->t += C_END_VB;
    l->b[l->n++] = 0;
}

static int room(const list_t *l) {
    int t = l->vblank ? ((int)VB_LIMIT - (int)l->t - C_END_VB - C_RUN_VB) / C_BYTE_VB
                      : ((int)LIST_LIMIT - (int)l->t - C_END - C_RUN_ACT) / C_BYTE_ACT;
    return t > 255 ? 255 : t;
}

// ---- colors -----------------------------------------------------------------------
const uint32_t pmcv_tms_rgb[16] = {0x000000, 0x000000, 0x21C842, 0x5EDC78, 0x5455ED, 0x7D76FC, 0xD4524D, 0x42EBF5,
                                   0xFC5554, 0xFF7978, 0xD4C154, 0xE6CE80, 0x21B03B, 0xC95BBA, 0xCCCCCC, 0xFFFFFF};

// The board's 16 colors (82s123.7f, as MAME's resistor weights make them)
// as TMS9918 colors: the nearest in OKLab, but for the characters, which
// must stay apart. By the board's color:
//   0 black, 4 8 10 black too (unused)
//   1 red     Blinky, the cherry               8 red
//   2 brown   the fruits' stems                10 dark yellow
//   3 pink    Pinky (nearest grey)             13 magenta
//   5 cyan    Inky                             7 cyan
//   6 sky     (the nearest is cyan)            5 light blue
//   7 orange  Clyde (nearest light yellow,     10 dark yellow
//             Pac-Man's; light red was too
//             close to Blinky's red)
//   9 yellow  Pac-Man                          11 light yellow
//   11 blue   the walls, blue ghosts           4 dark blue
//   12 green, 13 teal: the fruits              3 light green, 12 dark green
//   14 peach  the dots, the ghosts' faces      14 grey
//   15 white  the text, the eyes               15 white
uint8_t pmcv_color[16] = {1, 8, 10, 13, 1, 7, 5, 10, 1, 11, 1, 4, 3, 12, 14, 15};

static float tms_lab[16][3];
static uint8_t nearer[16][16][16];      // [c][a][b]: 1 if c is nearer a than b
static bool colors_ready;

static void oklab(uint32_t rgb, float *out) {
    float c[3];
    for (int i = 0; i < 3; i++) {
        float v = (float)((rgb >> (16 - 8 * i)) & 255) / 255.0f;
        c[i] = v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
    }
    float l = cbrtf(0.4122214708f * c[0] + 0.5363325363f * c[1] + 0.0514459929f * c[2]);
    float m = cbrtf(0.2119034982f * c[0] + 0.6806995451f * c[1] + 0.1073969566f * c[2]);
    float s = cbrtf(0.0883024619f * c[0] + 0.2817188376f * c[1] + 0.6299787005f * c[2]);
    out[0] = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
    out[1] = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
    out[2] = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
}

static float d2(const float *a, const float *b) {
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return x * x + y * y + z * z;
}

static void colors_init(void) {
    for (int i = 1; i < 16; i++) oklab(pmcv_tms_rgb[i], tms_lab[i]);
    for (int c = 1; c < 16; c++)
        for (int a = 1; a < 16; a++)
            for (int b = 1; b < 16; b++) nearer[c][a][b] = d2(tms_lab[c], tms_lab[a]) <= d2(tms_lab[c], tms_lab[b]);
    colors_ready = true;
}

// ---- the layout ------------------------------------------------------------------
// MAME's picture, turned (224 x 288, 28 x 36 tiles), in pieces of tiles,
// each scaled by 3/4 to its place on the TMS9918's screen. The maze in the
// middle; on the left, 1UP and its score, HIGH SCORE and its score, and at
// the bottom the lives (and CREDIT); on the right 2UP and its score, and at
// the bottom the fruit, the newest three lowest. The sides are 40 pixels
// (5 of the TMS9918's 8-pixel places), so no place has both the maze and
// the text in it; a score is 6 digits (it goes round at a million), 36.
typedef struct { uint8_t c0, r0, nc, nr; uint8_t x, y; } piece_t;
#define MAZE_W (28 * 6)
#define MAZE_H (31 * 6)
#define MAZE_Y 3
#define LEFT  2
#define RIGHT 218
int pmcv_maze_x = 44;
static piece_t layout[] = {
    {0, 3, 28, 31, 44, MAZE_Y},     // the maze (its x: pmcv_maze_x)
    {1, 0, 6, 2, LEFT, 6},          // "1UP", its score
    {9, 0, 4, 1, LEFT + 6, 26},     // "HIGH"
    {14, 0, 5, 1, LEFT + 3, 33},    // "SCORE"
    {11, 1, 6, 1, LEFT, 40},        // its score
    {20, 0, 6, 2, RIGHT, 6},        // "2UP", its score
    {2, 34, 6, 2, LEFT, 160},       // lives 1-3, "CREDIT"
    {8, 34, 4, 2, LEFT, 174},       // lives 4-5, the credits (not 12-13: the seventh fruit's)
    {20, 34, 6, 2, RIGHT, 174},     // the fruit: the newest three
    {14, 34, 6, 2, RIGHT, 160},     // three more
    {12, 34, 2, 2, RIGHT + 24, 146},  // and the seventh
};
#define PIECES (int)(sizeof layout / sizeof layout[0])

static int8_t tile_piece[36][28];       // the piece each of MAME's tiles is in (-1: none)
static uint16_t tile_at[0x400];         // where each of the board's tile RAM bytes is: row * 28 + col, or 0xFFFF
static uint16_t tile_index[36][28];     // and the other way

// a row of MAME's tiles, the right way up (the board's colors, 0-15), and
// the picture scaled and laid out; small's room is the plugin's before the
// game (pmcv_scratch), and made black again at the first picture
static uint8_t strip[8][224];
static union {
    uint8_t px[PMCV_H][PMCV_W];
    uint8_t room[52 * 1024];
} small_u;
#define small small_u.px
static bool layout_ready, small_clear;

static void layout_init(void) {
    layout[0].x = (uint8_t)pmcv_maze_x;
    memset(tile_piece, -1, sizeof tile_piece);
    for (int p = 0; p < PIECES; p++)
        for (int r = layout[p].r0; r < layout[p].r0 + layout[p].nr; r++)
            for (int c = layout[p].c0; c < layout[p].c0 + layout[p].nc; c++) tile_piece[r][c] = (int8_t)p;
    for (int i = 0; i < 0x400; i++) tile_at[i] = 0xFFFF;
    // pacman_scan_rows: MAME's tile at raw column col (0-35), row (0-27) is
    // the turned picture's row col, column 27 - row
    for (int col = 0; col < 36; col++)
        for (int row = 0; row < 28; row++) {
            int r = row + 2, c = col - 2;
            int i = (c & 0x20) ? r + ((c & 0x1f) << 5) : c + (r << 5);
            tile_at[i] = (uint16_t)(col * 28 + 27 - row);
            tile_index[col][27 - row] = (uint16_t)i;
        }
    layout_ready = true;
}

uint8_t *pmcv_scratch(size_t *n) {
    *n = sizeof small_u.room;
    small_clear = false;
    return small_u.room;
}

// ---- the 8 x 8 places to make again ----------------------------------------------------
static uint32_t cells[24];              // a bit a column, a word a band

static void mark(int x0, int y0, int x1, int y1) {     // pixels x0..x1 - 1, y0..y1 - 1
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > PMCV_W) x1 = PMCV_W;
    if (y1 > PMCV_H) y1 = PMCV_H;
    if (x0 >= x1 || y0 >= y1) return;
    uint32_t m = (x1 + 7) / 8 >= 32 ? ~0u : (1u << ((x1 + 7) / 8)) - 1;
    m &= ~((1u << (x0 / 8)) - 1);
    for (int b = y0 / 8; b <= (y1 - 1) / 8; b++) cells[b] |= m;
}

// the board's tiles turned: the picture's pixel (u, v) of a tile is the
// raw tile's x v, y 7 - u; here row v's pens, pixel u at bits 2u (made from
// pm_tile_rows at the first picture)
static uint16_t turned[256][8];
static bool turned_ready;

// Glyphs the 3/4 scale spoils (S came out as a 3), drawn again at 6 x 6
// (a row a byte, bit 5 the left) in the font's pen. Each goes in as 8 x 8
// with its 2nd and 5th rows and columns doubled, which the scale gives
// back as it is.
static const struct { uint8_t code, rows[6]; } redrawn[] = {
    {0x53, {0x0F, 0x10, 0x0E, 0x01, 0x1E, 0x00}},     // S: ..#### .#.... ..###. .....# .####.
    {0x05, {0x00, 0x1F, 0x10, 0x1E, 0x01, 0x1E}},     // 5: .##### .#.... .####. .....# .####.
};

static void turn_tiles(void) {
    for (int t = 0; t < 256; t++)
        for (int v = 0; v < 8; v++) {
            uint16_t w = 0;
            for (int u = 0; u < 8; u++) w |= (uint16_t)(((pm_tile_rows[t][7 - u] >> (2 * v)) & 3) << (2 * u));
            turned[t][v] = w;
        }
    static const uint8_t from[8] = {0, 1, 1, 2, 3, 4, 4, 5};      // the 8's row or column to the 6's
    for (size_t g = 0; g < sizeof redrawn / sizeof redrawn[0]; g++) {
        uint16_t *t = turned[redrawn[g].code];
        unsigned pen = 0;
        for (int v = 0; v < 8 && !pen; v++)
            for (int u = 0; u < 8 && !pen; u++) pen = t[v] >> (2 * u) & 3;
        for (int v = 0; v < 8; v++) {
            uint16_t w = 0;
            for (int u = 0; u < 8; u++)
                if (redrawn[g].rows[from[v]] >> (5 - from[u]) & 1) w |= (uint16_t)(pen << (2 * u));
            t[v] = w;
        }
    }
    turned_ready = true;
}

const uint16_t *pmcv_turned(int code) {
    if (!turned_ready) turn_tiles();
    return turned[code & 0xFF];
}

static const pmcv_board_t *bd;          // the board the picture is made from

// a tile of MAME's picture into the strip, from the board's tile and color RAM
static void draw_tile(int r, int c) {
    int i = tile_index[r][c];
    const uint16_t *rows = turned[bd->vram[i]];
    const uint8_t *clut = pm_clut + (bd->cram[i] & 0x1f) * 4;
    uint16_t pair[16];                  // two pens (4 bits) to their two colors
    for (int k = 0; k < 16; k++) pair[k] = (uint16_t)(clut[k & 3] | clut[k >> 2] << 8);
    for (int v = 0; v < 8; v++) {
        uint8_t *o = &strip[v][c * 8];
        unsigned w = rows[v];
        uint16_t p[4] = {pair[w & 15], pair[w >> 4 & 15], pair[w >> 8 & 15], pair[w >> 12]};
        memcpy(o, p, 8);
    }
}

// Row r's changed tiles (a bit a column) scaled again, with the tiles
// either side of each, which look at its edge: in each piece the row is
// in, from the first to the last of them, drawn into the strip with one
// more tile each side to look at
static void scale_row(int r, uint32_t changed) {
    for (int p = 0; p < PIECES; p++) {
        const piece_t *pc = &layout[p];
        if (r < pc->r0 || r >= pc->r0 + pc->nr) continue;
        uint32_t in = ((1u << pc->nc) - 1) << pc->c0, m = changed & in;
        if (!m) continue;
        int lo = __builtin_ctz(m) - 1, hi = 31 - __builtin_clz(m) + 1;
        if (lo < pc->c0) lo = pc->c0;
        if (hi > pc->c0 + pc->nc - 1) hi = pc->c0 + pc->nc - 1;
        for (int c = lo - 1; c <= hi + 1; c++)
            if (c >= pc->c0 && c < pc->c0 + pc->nc) draw_tile(r, c);
        uint8_t *dst = &small[pc->y + (r - pc->r0) * 6][pc->x];
        cvscale34(&strip[0][pc->c0 * 8], 224, pc->nc * 8, 8, (lo - pc->c0) * 8, (hi + 1 - pc->c0) * 8, dst, PMCV_W);
        for (uint32_t k = (m | m << 1 | m >> 1) & in; k; k &= k - 1) {
            int x = pc->x + (__builtin_ctz(k) - pc->c0) * 6, y = pc->y + (r - pc->r0) * 6;
            mark(x, y, x + 6, y + 6);
        }
    }
}

// ---- the characters ------------------------------------------------------------------
typedef struct {
    bool on;
    int x, y;                           // the scaled picture's top left, on the TMS9918's screen
    uint8_t px[12][12];                 // the board's colors, 0 see-through, clipped to the maze
    uint8_t main;                       // its main color (the board's), the sprite's
    uint16_t pat[16];                   // the sprite's pixels (bit 15 - x of row y)
} chr_t;
static chr_t chr[PMCV_SPRITES];

// the front character at a pixel (index + 1; 0: none): the board's sprite 0 is in front
static inline int front_at(int x, int y) {
    for (int i = 0; i < PMCV_SPRITES; i++) {
        const chr_t *ch = &chr[i];
        unsigned u = (unsigned)(x - ch->x), v = (unsigned)(y - ch->y);
        if (ch->on && u < 12 && v < 12 && ch->px[v][u]) return i + 1;
    }
    return 0;
}

static int fdiv4(int a) { return a >= 0 ? a / 4 : -((-a + 3) / 4); }

static void make_char(chr_t *ch, int i) {
    int offs = 2 * i;
    int code = bd->spr[offs] >> 2, color = bd->spr[offs + 1] & 0x1f;
    bool fx = bd->spr[offs] & 1, fy = bd->spr[offs] & 2;
    int sx = 272 - bd->spr2[offs + 1], sy = bd->spr2[offs] - 31 + (offs <= 4 ? 1 : 0);
    // turned: the sprite's 16 x 16 at x 208 - sy, y sx of MAME's 224 x 288;
    // its pixel (u, v) is the raw sprite's x v, y 15 - u
    int X = 208 - sy, Y = sx;
    uint8_t img[16][16];
    const uint32_t *rows = pm_sprite_rows[code & 63];
    const uint8_t *clut = pm_clut + color * 4;
    bool any = false;
    for (int v = 0; v < 16; v++)
        for (int u = 0; u < 16; u++) {
            int px = v, py = 15 - u;
            int row = fy ? 15 - py : py, col = fx ? 15 - px : px;
            uint8_t c = clut[(rows[row] >> (2 * col)) & 3];
            // MAME's clip: raw x 16 to 271, the picture's y 16 to 271
            if (Y + v < 16 || Y + v > 271 || X + u < 0 || X + u > 223) c = 0;
            img[v][u] = c;
            any |= c != 0;
        }
    ch->on = false;
    if (!any) return;
    cvscale34(&img[0][0], 16, 16, 16, 0, 16, &ch->px[0][0], 12);
    ch->x = pmcv_maze_x + fdiv4(3 * X + 2);
    ch->y = MAZE_Y + fdiv4(3 * (Y - 24) + 2);
    // clipped to the maze's place on the screen
    unsigned count[16] = {0};
    for (int v = 0; v < 12; v++)
        for (int u = 0; u < 12; u++) {
            int x = ch->x + u, y = ch->y + v;
            if (x < pmcv_maze_x || x >= pmcv_maze_x + MAZE_W || y < MAZE_Y || y >= MAZE_Y + MAZE_H) ch->px[v][u] = 0;
            count[ch->px[v][u]]++;
        }
    count[0] = 0;
    uint8_t best = 0;
    for (int c = 1; c < 16; c++)
        if (count[c] > count[best]) best = (uint8_t)c;
    if (!best) return;
    ch->main = best;
    ch->on = true;
}

// ---- the picture -------------------------------------------------------------------
// 8 pixels (TMS9918 colors; 0: the sprite covers it, any color will do,
// with `under` the color it would show) as a pattern byte and a color
// byte, the color byte kept as the console has it where it will do
static inline __attribute__((always_inline)) void span(pmcv_t *e, unsigned a, const uint8_t *c, const uint8_t *under) {
    uint8_t vc = e->vram_col[a], vp = e->vram_pat[a];
    uint8_t c0 = c[0];
    if (c0 && c[1] == c0 && c[2] == c0 && c[3] == c0 && c[4] == c0 && c[5] == c0 && c[6] == c0 && c[7] == c0) {
        // one color, all shown (as below: kept as the console has it if
        // either of its colors is this one)
        uint8_t vf = vc >> 4, vb = vc & 15;
        if (vf == c0 || vb == c0) {
            e->want_col[a] = vc;
            e->want_pat[a] = vf == vb ? vp : vf == c0 ? 0xFF : 0x00;
        } else {
            e->want_col[a] = (uint8_t)(c0 << 4 | c0);
            e->want_pat[a] = vp;
        }
        return;
    }
    if (c0) {                           // two colors, all shown (the general case's answer)
        uint8_t d = 0;
        unsigned m0 = 0;
        int i = 0;
        for (; i < 8; i++) {
            uint8_t x = c[i];
            if (x == c0) m0 |= 0x80u >> i;
            else if (!x || (d && x != d)) break;
            else d = x;
        }
        if (i == 8) {
            static const uint8_t pop4[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
            int n0 = pop4[m0 & 15] + pop4[m0 >> 4];     // (the M33 has no popcount instruction)
            uint8_t a1 = n0 > 4 ? c0 : n0 < 4 ? d : (c0 < d ? c0 : d), a2 = a1 == c0 ? d : c0;
            uint8_t vf = vc >> 4, vb = vc & 15, fg, bg;
            if (vf == a1 && vb == a2) { fg = a1; bg = a2; }
            else { fg = a2; bg = a1; }
            e->want_pat[a] = (uint8_t)(fg == c0 ? m0 : ~m0 & 0xFF);
            e->want_col[a] = (uint8_t)(fg << 4 | bg);
            return;
        }
    }
    uint8_t cnt[16] = {0};
    int shown = 0;
    for (int i = 0; i < 8; i++)
        if (c[i]) cnt[c[i]]++, shown++;
    if (!shown) {                       // all covered: as it is, if the console has it so
        if (vc) { e->want_pat[a] = vp; e->want_col[a] = vc; return; }
        c = under;
        for (int i = 0; i < 8; i++) cnt[c[i]]++;
    }
    // the two most used (ties: the lower color), the second 0 if one
    uint8_t a1 = 0, a2 = 0;
    for (int k = 1; k < 16; k++)
        if (cnt[k] > cnt[a1] || (!a1 && cnt[k])) a1 = (uint8_t)k;
    for (int k = 1; k < 16; k++)
        if (k != a1 && cnt[k] && (!a2 || cnt[k] > cnt[a2])) a2 = (uint8_t)k;
    uint8_t fg, bg;
    uint8_t vf = vc >> 4, vb = vc & 15;
    if (!a2) {                          // one color
        if (vf == a1 || vb == a1) { fg = vf; bg = vb; }
        else { fg = a1; bg = a1; }
    } else if (vf == a1 && vb == a2) { fg = a1; bg = a2; }
    else if (vf == a2 && vb == a1) { fg = a2; bg = a1; }
    else { fg = a2; bg = a1; }
    uint8_t pat = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t x = c[i];
        bool on;
        if (!x) {                       // covered: the console's bit, if it shows what's under well enough
            uint8_t u = under[i];
            bool keep = (vp >> (7 - i)) & 1;
            uint8_t kc = keep ? fg : bg;
            if (kc == u || fg == bg) on = keep;
            else on = u == fg || (u != bg && nearer[u][fg][bg]);
        } else {
            on = x == fg || (x != bg && fg != bg && nearer[x][fg][bg]);
            if (fg == bg) on = (vp >> (7 - i)) & 1;
        }
        pat = (uint8_t)(pat << 1 | on);
    }
    e->want_pat[a] = pat;
    e->want_col[a] = (uint8_t)(fg << 4 | bg);
}

// the 8 x 8 places marked in cells[], made again (`chars`: those a
// character is on, a bit a column)
PMCV_HOT static void make_cells(pmcv_t *e, const uint32_t *chars) {
    uint8_t c[8], u[8];
    for (int b = 0; b < 24; b++) {
        if (!cells[b]) continue;
        e->bands |= 1u << b;
        for (uint32_t m = cells[b]; m; m &= m - 1) {
            int cx = __builtin_ctz(m);
            if (!(chars[b] >> cx & 1)) {        // the picture alone
                for (int k = 0; k < 8; k++) {
                    const uint8_t *row = &small[b * 8 + k][cx * 8];
                    uint64_t w;
                    memcpy(&w, row, 8);
                    if (w == row[0] * 0x0101010101010101ull) {      // one color: span's first case
                        unsigned a = (unsigned)(b * 256 + cx * 8 + k);
                        uint8_t c0 = pmcv_color[row[0]], vc = e->vram_col[a], vf = vc >> 4, vb = vc & 15;
                        if (vf == c0 || vb == c0) {
                            e->want_col[a] = vc;
                            e->want_pat[a] = vf == vb ? e->vram_pat[a] : vf == c0 ? 0xFF : 0x00;
                        } else {
                            e->want_col[a] = (uint8_t)(c0 << 4 | c0);
                            e->want_pat[a] = e->vram_pat[a];
                        }
                        continue;
                    }
                    for (int i = 0; i < 8; i++) c[i] = pmcv_color[row[i]];
                    span(e, (unsigned)(b * 256 + cx * 8 + k), c, c);
                }
                e->stats.cells += 8;
                continue;
            }
            for (int k = 0; k < 8; k++) {
                int y = b * 8 + k;
                for (int i = 0; i < 8; i++) {
                    int x = cx * 8 + i;
                    uint8_t bgc = pmcv_color[small[y][x]];
                    u[i] = bgc;
                    int f = front_at(x, y);
                    if (!f) { c[i] = bgc; continue; }
                    const chr_t *ch = &chr[f - 1];
                    uint8_t p = ch->px[y - ch->y][x - ch->x];
                    c[i] = p == ch->main && pmcv_sprites ? 0 : pmcv_color[p];
                }
                span(e, (unsigned)(b * 256 + cx * 8 + k), c, u);
                e->stats.cells++;
            }
        }
        cells[b] = 0;
    }
}

// ---- the sprites' tables ---------------------------------------------------------------
// The patterns go in slots the console's attributes don't use, so a
// sprite never shows a pattern half sent; the attributes go after them.
static int pattern_slot(pmcv_t *e, const uint8_t *p, uint64_t used) {
    for (int s = 0; s < 64; s++)
        if (!memcmp(e->want_spat[s], p, 32)) return s;
    // a slot neither table points at, the one the console has longest
    // (the lowest: slots are taken from the top of those free)
    uint64_t busy = used;
    for (int k = 0; k < 32 && e->vram_sat[4 * k] != 0xD0; k++) busy |= 1ull << (e->vram_sat[4 * k + 2] >> 2);
    for (int k = 0; k < 32 && e->want_sat[4 * k] != 0xD0; k++) busy |= 1ull << (e->want_sat[4 * k + 2] >> 2);
    static uint8_t next;
    for (int t = 0; t < 64; t++) {
        int s = (next + t) & 63;
        if (busy >> s & 1) continue;
        next = (uint8_t)(s + 1);
        memcpy(e->want_spat[s], p, 32);
        if (memcmp(e->want_spat[s], e->vram_spat[s], 32)) e->spat_dirty |= 1ull << s;
        else e->spat_dirty &= ~(1ull << s);
        return s;
    }
    return 0;
}

bool pmcv_sprites = true;

static void make_sprites(pmcv_t *e) {
    if (!pmcv_sprites) { e->want_sat[0] = 0xD0; return; }
    // the characters in the board's order (0 in front); crowded (more
    // than 4 on a line): turned by one each picture
    int order[PMCV_SPRITES], n = 0;
    for (int i = 0; i < PMCV_SPRITES; i++)
        if (chr[i].on) order[n++] = i;
    uint8_t per_line[PMCV_H + 32] = {0};
    bool crowded = false;
    for (int k = 0; k < n; k++) {
        int y = chr[order[k]].y;
        for (int l = y; l < y + 16; l++)
            if (l >= 0 && l < PMCV_H && ++per_line[l] > 4) crowded = true;
    }
    if (crowded) {
        e->stats.crowded++;
        e->turn++;
        int t[PMCV_SPRITES];
        for (int k = 0; k < n; k++) t[k] = order[(k + e->turn) % n];
        memcpy(order, t, sizeof t);
    }
    uint8_t sat[128];
    memset(sat, 0, sizeof sat);
    uint64_t used = 0;
    for (int k = 0; k < n; k++) {
        const chr_t *ch = &chr[order[k]];
        uint8_t p[32] = {0};
        for (int y = 0; y < 16; y++) {
            uint16_t w = ch->pat[y];
            p[y] = (uint8_t)(w >> 8);           // left half, top and bottom quarters: 0-15
            p[16 + y] = (uint8_t)w;             // right half: 16-31
        }
        int s = pattern_slot(e, p, used);
        used |= 1ull << s;
        int y = ch->y - 1;
        sat[4 * k] = (uint8_t)(y < -32 ? 0xE0 : y & 0xFF);
        if (sat[4 * k] == 0xD0) sat[4 * k] = 0xD1;
        sat[4 * k + 1] = (uint8_t)ch->x;
        sat[4 * k + 2] = (uint8_t)(s << 2);
        sat[4 * k + 3] = pmcv_color[ch->main];
    }
    sat[4 * n] = 0xD0;
    memcpy(e->want_sat, sat, 4 * n + 1);
}

void pmcv_board(pmcv_board_t *b) {
    memcpy(b->vram, pm_vram, sizeof b->vram);
    memcpy(b->cram, pm_cram, sizeof b->cram);
    memcpy(b->spr, pm_spr, sizeof b->spr);
    memcpy(b->spr2, pm_spr2, sizeof b->spr2);
}

PMCV_HOT bool pmcv_picture(pmcv_t *e, const pmcv_board_t *b) {
    bd = b;
    if (!colors_ready) colors_init();
    if (!layout_ready) layout_init();
    if (!small_clear) {                 // (the plugin's room before the game)
        memset(small_u.room, 0, sizeof small_u.room);
        small_clear = true;
        e->pic_valid = false;
    }
    if (!turned_ready) turn_tiles();
    // the tiles that changed (all of them after a message), row by row,
    // and those an earlier picture left
    uint32_t changed[36];
    memcpy(changed, e->pending, sizeof changed);
    for (int i = 0; i < 0x400; i++) {
        uint16_t at = tile_at[i];
        if (at == 0xFFFF) continue;
        int r = at / 28, col = at % 28;
        if (tile_piece[r][col] < 0) continue;
        uint8_t v = b->vram[i], c = b->cram[i];
        if (e->pic_valid && v == e->tile_v[i] && c == e->tile_c[i]) continue;
        e->tile_v[i] = v;
        e->tile_c[i] = c;
        changed[r] |= 1u << col;
    }
    // drawn from row_next on, up to PMCV_TILES; the rows past that wait
    int done = 0, r0 = e->row_next;
    bool full = false;
    for (int k = 0; k < 36; k++) {
        int r = r0 + k < 36 ? r0 + k : r0 + k - 36;
        e->pending[r] = 0;
        if (!changed[r]) continue;
        int n = __builtin_popcount(changed[r]);
        if (!full && done && done + n > PMCV_TILES) {
            full = true;
            e->row_next = (uint8_t)r;
            e->stats.deferred++;
        }
        if (full) {
            e->pending[r] = changed[r];
            continue;
        }
        done += n;
        scale_row(r, changed[r]);
    }
    e->stats.tiles += (uint32_t)done;
    if (!e->pic_valid) {                // the black between the pieces too
        for (int b = 0; b < 24; b++) cells[b] = ~0u;
        e->pic_valid = true;
    }
    // the characters: where they were and where they are, made again
    for (int b = 0; b < 24; b++) cells[b] |= e->cells_prev[b];
    for (int i = PMCV_SPRITES - 1; i >= 0; i--) make_char(&chr[i], i);
    uint32_t now[24] = {0};
    for (int i = PMCV_SPRITES - 1; i >= 0; i--) {
        chr_t *ch = &chr[i];
        if (!ch->on) continue;
        int x0 = ch->x, y0 = ch->y;
        for (int b = (y0 < 0 ? 0 : y0) / 8; b <= (y0 + 11) / 8 && b < 24; b++) {
            uint32_t m = (1u << ((x0 + 11) / 8 + 1)) - 1;
            if ((x0 + 11) / 8 >= 31) m = ~0u;
            m &= ~((1u << (x0 / 8)) - 1);
            now[b] |= m;
        }
    }
    // each sprite's pixels: its main color where it is in front
    for (int i = 0; i < PMCV_SPRITES; i++) {
        chr_t *ch = &chr[i];
        memset(ch->pat, 0, sizeof ch->pat);
        if (!ch->on) continue;
        for (int v = 0; v < 12; v++)
            for (int u = 0; u < 12; u++) {
                int x = ch->x + u, y = ch->y + v;
                if (ch->px[v][u] == ch->main && front_at(x, y) == i + 1) ch->pat[v] |= (uint16_t)(0x8000 >> u);
            }
    }
    for (int b = 0; b < 24; b++) {
        cells[b] |= now[b];
        e->cells_prev[b] = now[b];
    }
    make_cells(e, now);
    make_sprites(e);
    e->stats.pictures++;
    e->dirty = true;
    return !full;
}

void pmcv_canvas(pmcv_t *e, const uint8_t *px) {
    if (!colors_ready) colors_init();
    if (!layout_ready) layout_init();
    e->pic_valid = false;
    for (int i = 0; i < PMCV_SPRITES; i++) chr[i].on = false;
    uint8_t c[8];
    for (int y = 0; y < PMCV_H; y++)
        for (int cx = 0; cx < 32; cx++) {
            for (int i = 0; i < 8; i++) {
                uint8_t v = px[y * PMCV_W + cx * 8 + i];
                c[i] = v ? v : BLACK;
            }
            span(e, (unsigned)((y >> 3) * 256 + cx * 8 + (y & 7)), c, c);
        }
    e->bands = ~0u;
    e->want_sat[0] = 0xD0;              // no sprites
    memset(e->cells_prev, 0xff, sizeof e->cells_prev);
    e->stats.pictures++;
    e->dirty = true;
}

static void screen_of(const uint8_t *pat, const uint8_t *colt, const uint8_t *sat, const uint8_t (*spat)[32], uint8_t *px) {
    for (int y = 0; y < PMCV_H; y++)
        for (int x = 0; x < PMCV_W; x++) {
            unsigned a = (unsigned)(((y >> 3) * 32 + (x >> 3)) * 8 + (y & 7));
            uint8_t c = colt[a];
            c = (pat[a] & (0x80 >> (x & 7))) ? c >> 4 : c & 15;
            px[y * PMCV_W + x] = c ? c : BLACK;
        }
    // the sprites, the TMS9918's way: 4 a line at most, the first in front
    uint8_t seen[PMCV_H] = {0};
    for (int k = 0; k < 32 && sat[4 * k] != 0xD0; k++) {
        int y0 = sat[4 * k] >= 0xE0 ? (int)sat[4 * k] - 256 + 1 : sat[4 * k] + 1;
        int x0 = sat[4 * k + 1], s = sat[4 * k + 2] >> 2;
        uint8_t col = sat[4 * k + 3] & 15;
        for (int v = 0; v < 16; v++) {
            int y = y0 + v;
            if (y < 0 || y >= PMCV_H || seen[y] >= 4) continue;
            seen[y]++;
            uint16_t w = (uint16_t)(spat[s][v] << 8 | spat[s][16 + v]);
            for (int u = 0; u < 16; u++) {
                int x = x0 + u;
                if (x < PMCV_W && col && (w & (0x8000 >> u)) && !(px[y * PMCV_W + x] & 0x80))
                    px[y * PMCV_W + x] = col | 0x80;    // (the front sprite's pixel stays)
            }
        }
    }
    for (int i = 0; i < PMCV_W * PMCV_H; i++) px[i] &= 15;
}

void pmcv_screen(const pmcv_t *e, uint8_t *px) {
    screen_of(e->vram_pat, e->vram_col, e->vram_sat, (const uint8_t (*)[32])e->vram_spat, px);
}
void pmcv_wanted(const pmcv_t *e, uint8_t *px) {
    screen_of(e->want_pat, e->want_col, e->want_sat, (const uint8_t (*)[32])e->want_spat, px);
}

void pmcv_scaled(uint8_t *px) { memcpy(px, small, sizeof small); }

void pmcv_layout(int i, int *c0, int *r0, int *nc, int *nr, int *x, int *y) {
    if (!layout_ready) layout_init();
    const piece_t *p = &layout[i];
    *c0 = p->c0, *r0 = p->r0, *nc = p->nc, *nr = p->nr, *x = p->x, *y = p->y;
}
int pmcv_pieces(void) { return PIECES; }

void pmcv_ideal(uint8_t *px) {
    for (int y = 0; y < PMCV_H; y++)
        for (int x = 0; x < PMCV_W; x++) {
            int f = front_at(x, y);
            uint8_t p = small[y][x];
            if (f) p = chr[f - 1].px[y - chr[f - 1].y][x - chr[f - 1].x];
            px[y * PMCV_W + x] = pmcv_color[p];
        }
}

// ---- what the console needs ----------------------------------------------------------
static inline bool need_pat(const pmcv_t *e, unsigned j) { return e->want_pat[j] != e->vram_pat[j]; }
static inline bool need_col(const pmcv_t *e, unsigned j) { return e->want_col[j] != e->vram_col[j]; }

// The next bytes to send, from the cursor on (qbcv.c's next_run)
PMCV_HOT static bool next_run(pmcv_t *e, bool *color, unsigned *s, int *m, unsigned most) {
    if (!e->dirty) return false;
    unsigned i = e->cursor;
    int band = -1;
    for (unsigned k = 0; k < PMCV_BYTES;) {
        if (!(i & 255)) {
            if (band >= 0) e->bands &= ~(1u << band);
            band = -1;
            if (k + 256 <= PMCV_BYTES && !(e->bands >> (i >> 8) & 1)) {
                k += 256;
                i = i + 256 == PMCV_BYTES ? 0 : i + 256;
                continue;
            }
            band = (int)(i >> 8);
        }
        if (!(i & 3) && k + 4 <= PMCV_BYTES) {
            uint32_t w, v, wc, vc;
            memcpy(&w, e->want_pat + i, 4);
            memcpy(&v, e->vram_pat + i, 4);
            memcpy(&wc, e->want_col + i, 4);
            memcpy(&vc, e->vram_col + i, 4);
            if (w == v && wc == vc) {
                k += 4;
                i = i + 4 == PMCV_BYTES ? 0 : i + 4;
                continue;
            }
        }
        bool nc = need_col(e, i);
        if (!nc && !need_pat(e, i)) {
            k++;
            i = i + 1 == PMCV_BYTES ? 0 : i + 1;
            continue;
        }
        e->cursor = (uint16_t)i;
        *color = nc;
        *s = i;
        unsigned end = i + 1, gap = 0;
        for (unsigned j = i + 1; j < PMCV_BYTES && j - i < most; j++) {
            if (nc ? need_col(e, j) : need_pat(e, j)) { end = j + 1; gap = 0; }
            else if (++gap > 3) break;
        }
        *m = (int)(end - i);
        return true;
    }
    e->dirty = false;
    return false;
}

static void put_run(pmcv_t *e, list_t *l, bool color, unsigned s, int n) {
    l_run_head(l, (uint16_t)((color ? COLORS : PATTERNS) + s), n);
    uint8_t *d = l->b + l->n;
    if (color) {
        memcpy(d, e->want_col + s, n);
        memcpy(e->vram_col + s, d, n);
        e->stats.col_bytes += (unsigned)n;
    } else {
        memcpy(d, e->want_pat + s, n);
        memcpy(e->vram_pat + s, d, n);
        e->stats.pat_bytes += (unsigned)n;
    }
    l->n += n;
    e->stats.runs++;
}

// The sprites' bytes go before the picture's, a run at a time: the
// patterns first (into slots no attribute points at, so none shows half
// sent), then the attributes, from the end back, a sprite's 4 bytes in one
// run. From the end back, a longer table is in place before the old end
// mark goes, and a shorter one's end mark goes first.
static int sat_len(const uint8_t *sat) {
    int n = 0;
    while (n < 32 && sat[4 * n] != 0xD0) n++;
    return n < 32 ? 4 * n + 1 : 128;
}

static bool sprites_pending(const pmcv_t *e) {
    return e->spat_dirty || memcmp(e->vram_sat, e->want_sat, sat_len(e->want_sat));
}

// one run of at most `most` bytes; false if none was sent
static bool sprite_piece(pmcv_t *e, list_t *l, int most) {
    while (e->spat_dirty) {
        int s = __builtin_ctzll(e->spat_dirty);
        const uint8_t *w = e->want_spat[s];
        uint8_t *v = e->vram_spat[s];
        int j = 0, end = 32;
        while (j < 32 && w[j] == v[j]) j++;
        while (end > j && w[end - 1] == v[end - 1]) end--;
        if (j == 32) { e->spat_dirty &= ~(1ull << s); continue; }
        int n = end - j < most ? end - j : most;
        if (n < 1) return false;
        l_run_head(l, (uint16_t)(SPAT + 32 * s + j), n);
        memcpy(l->b + l->n, w + j, n);
        memcpy(v + j, w + j, n);
        l->n += n;
        e->stats.spr_bytes += (unsigned)n;
        if (!memcmp(w, v, 32)) e->spat_dirty &= ~(1ull << s);
        return true;
    }
    int len = sat_len(e->want_sat), d = len - 1;
    while (d >= 0 && e->want_sat[d] == e->vram_sat[d]) d--;
    if (d < 0) return false;
    int s = d / 4 * 4;
    if (d - s + 1 > most) return false;
    while (s >= 4 && d - (s - 4) + 1 <= most && memcmp(e->want_sat + s - 4, e->vram_sat + s - 4, 4)) s -= 4;
    int n = d - s + 1;
    l_run_head(l, (uint16_t)(SAT + s), n);
    memcpy(l->b + l->n, e->want_sat + s, n);
    memcpy(e->vram_sat + s, e->want_sat + s, n);
    l->n += n;
    e->stats.spr_bytes += (unsigned)n;
    return true;
}

// what's on screen written again as it is (qbcv.c's)
static void refresh_run(pmcv_t *e, list_t *l, int n) {
    unsigned at = e->refresh_at;
    bool color = at >= PMCV_BYTES;
    unsigned s = color ? at - PMCV_BYTES : at;
    if (s + n > PMCV_BYTES) n = (int)(PMCV_BYTES - s);
    l_run_head(l, (uint16_t)((color ? COLORS : PATTERNS) + s), n);
    memcpy(l->b + l->n, (color ? e->vram_col : e->vram_pat) + s, n);
    l->n += n;
    e->refresh_at = (uint16_t)((at + n) % (2 * PMCV_BYTES));
    e->stats.refreshed += (unsigned)n;
}

uint32_t pmcv_backlog(const pmcv_t *e) {
    uint32_t n = 0;
    if (e->dirty)
        for (unsigned j = 0; j < PMCV_BYTES; j++) n += need_pat(e, j) + need_col(e, j);
    for (uint64_t m = e->spat_dirty; m; m &= m - 1) n += 32;
    int len = sat_len(e->want_sat);
    for (int j = 0; j < len; j++) n += e->want_sat[j] != e->vram_sat[j];
    return n;
}

// the name table: name i at place i in each third (it changes never)
static void names_run(pmcv_t *e, list_t *l, int n) {
    int left = 768 - e->names_at;
    if (n > left) n = left;
    l_run_head(l, (uint16_t)(NAMES + e->names_at), n);
    for (int k = 0; k < n; k++) l->b[l->n++] = (uint8_t)(e->names_at + k);
    e->names_at = (uint16_t)(e->names_at + n);
    if (e->names_at == 768) e->names_done = true;
}

// ---- a list, the console waiting for the vertical blank (messages) -------------------
static void plain_list(pmcv_t *e, list_t *l) {
    if (e->resync) {
        l_end_vblank(l);
        if (e->tone_n) l_psg(l, e->tone_bytes, e->tone_n);
        if (e->resync-- == 2) return;
    } else {
        // the sprites 16 x 16 (R1), every time: a list may run twice
        l_reg(l, 1, 0xE2);
        e->regs_done = true;
        if (e->tone_n) l_psg(l, e->tone_bytes, e->tone_n);
    }
    while (!e->names_done) {
        int n = room(l);
        if (n < 16 && l->vblank) { l_end_vblank(l); continue; }
        if (n < 1) break;
        names_run(e, l, n);
    }
    while (e->names_done) {
        int n = room(l);
        if (n < 8 && l->vblank) { l_end_vblank(l); continue; }
        if (n < 4 || !sprite_piece(e, l, n)) break;
    }
    bool color;
    unsigned s;
    int m;
    while (e->names_done && next_run(e, &color, &s, &m, 255)) {
        int n = room(l);
        if (n < m && l->vblank) {
            if (n >= 8) put_run(e, l, color, s, n);
            l_end_vblank(l);
            continue;
        }
        if (n < 1) break;
        put_run(e, l, color, s, n < m ? n : m);
    }
    for (int k = 0; k < 2 && e->names_done; k++) {
        int n = room(l);
        if (n < 32) break;
        refresh_run(e, l, n);
    }
    l_end_vblank(l);
}

// ---- a list with samples in it (qbcv.c's pcm_list) -------------------------------------
PMCV_HOT static void fill(pmcv_t *e, list_t *l, uint32_t until) {
    int avail = (int)until - (int)l->t;
    if (avail < C_RUN_ACT + C_BYTE_ACT) return;
    int n = (avail - C_RUN_ACT) / C_BYTE_ACT;
    if (n > 255) n = 255;
    if (!e->names_done) { names_run(e, l, n); return; }
    bool color;
    unsigned s;
    int m;
    if (!next_run(e, &color, &s, &m, 1)) {
        if (e->refresh_at % PMCV_BYTES + (unsigned)n > PMCV_BYTES) e->refresh_at = (uint16_t)(e->refresh_at / PMCV_BYTES * PMCV_BYTES);
        refresh_run(e, l, n);
        return;
    }
    if (s + (unsigned)n > PMCV_BYTES) s = PMCV_BYTES - (unsigned)n;
    put_run(e, l, color, s, n);
}

PMCV_HOT static void pcm_list(pmcv_t *e, list_t *l) {
    l->t = C_FRAME;
    l_end_vblank(l);
    unsigned tone_at = 0;
    for (int k = 0; k < PMCV_PCM_N; k++) {
        uint8_t b[10];
        int n = 0;
        b[n++] = e->pcm_bytes[k][0];
        b[n++] = e->pcm_bytes[k][1];
        b[n++] = e->pcm_bytes[k][2];
        while (tone_at < e->tone_n && n < 5) {
            unsigned g = 1;
            while (tone_at + g < e->tone_n && !(e->tone_bytes[tone_at + g] & 0x80)) g++;
            if (n + (int)g > 6) break;
            memcpy(b + n, e->tone_bytes + tone_at, g);
            n += (int)g;
            tone_at += g;
        }
        if (k + 1 == PMCV_PCM_N) {
            l_psg(l, b, n);
            break;
        }
        uint32_t until = PCM_FIRST + (((uint32_t)(k + 1) * PCM_STEP) >> 8);
        int avail = (int)until - (int)l->t - (C_PSG + C_PSG_BYTE * n);
        // the sprites first, a run of theirs and then the picture's; or
        // a short run of the picture's as one of two (qbcv.c's)
        bool color;
        unsigned s;
        int m = 0;
        bool sp = e->names_done && sprites_pending(e) && avail >= 2 * C_RUN_ACT + C_BYTE_ACT * 5;
        bool two = !sp && e->names_done && next_run(e, &color, &s, &m, 7) && m <= 2 &&
                   avail >= 2 * C_RUN_ACT + C_BYTE_ACT * (m + 2);
        int need = sp ? 5 : two ? m + 1 : 1;
        int base = avail - (sp || two ? 2 : 1) * C_RUN_ACT, best = 0, best_left = 99;
        for (int r = 0; r < 3 && base - C_PSG_BYTE * r >= C_BYTE_ACT * need; r++) {
            int left = (base - C_PSG_BYTE * r) % C_BYTE_ACT;
            if (left < best_left) { best_left = left; best = r; }
        }
        while (best--) b[n++] = e->pcm_bytes[k][2];     // (the last attenuator again: no change)
        l_psg(l, b, n);
        if (sp) sprite_piece(e, l, ((int)until - (int)l->t - 2 * C_RUN_ACT) / C_BYTE_ACT - 1);
        if (two) put_run(e, l, color, s, m);
        fill(e, l, until);
    }
    e->stats.pcm_frames++;
    e->resync = 2;
}

size_t pmcv_tv_frame(pmcv_t *e, uint8_t *out) {
    list_t l = {out, 0, C_START, true};
    if (e->pcm && e->regs_done) pcm_list(e, &l);
    else plain_list(e, &l);
    l.t += C_END;
    l.b[l.n++] = 0;
    e->tone_n = 0;
    e->stats.tv_frames++;
    if (l.t > e->stats.t_max) e->stats.t_max = l.t;
    if (!e->dirty) e->stats.settled++;
    return l.n;
}

// ---- the sound (qbcv.c's) --------------------------------------------------------------
// The DAC: the three tones at a period of 1, their attenuators as measured
// on the user's console (coleco/ladder.json), summed (Donkey Kong 0.1.2's,
// ../dkong/dkcv.c). pmdac3.h's 512 levels go from 0 to 2.40, each a triple
// of attenuators chosen as Movies' table is (tools/mkdac3.py: within 1.5
// levels of its level, neighbouring levels' triples alike, so a small change
// makes small partial sums between a sample's three writes). The sound rests
// at the middle (DAC_MID, 1.20). Up to 0.1.3 two tones made the DAC (128
// levels, 0 to 1.40): the third gives 1.7 times the room either side of
// the rest. The steps stay about as fine: neighbouring levels often share a
// triple (214 different ones in the 512), which keeps the partial sums small.
#define DAC_LEVELS PMDAC3_LEVELS
#define DAC_MID    255
#define DAC_HALF   255.0f           // the levels either side of DAC_MID
// The WSG's three voices together reach -0.94 to 0.82 of MAME's full scale,
// one voice at full volume +-0.31: SCALE_LEVELS levels for 0.25 (8,192 of
// 32,767). Up to 0.1.3 that was 35 of the two-tone DAC's 128 levels (82 of
// these), and the game played some 13 dB under an ordinary ColecoVision
// cartridge (a game's square tones swing twice as far as a parked tone's
// level can): hard to hear. 0.1.4 plays it 3.2 times as loud (+10 dB),
// and the soft knee (straight to 3/4 of the 255 levels either side, then
// rounding off toward 255) takes the peaks of two or three voices
// together, so they never clip. 5.6% of the scripted game's samples go
// into the knee (0.6% before), mostly the start tune: sound to difference
// against MAME's overall 15.7 dB (21.3), the median second 20.6 (23.2),
// log-spectral distance 4.5 dB (6.7).
#ifndef SCALE_LEVELS
#define SCALE_LEVELS 260
#endif
#define SCALE_FULL   8192
#define LOUD_KNEE    191.0f         // the soft knee: 3/4 of the 255 levels either side
#define HP_SHIFT     6              // a pole at 15 Hz
static const float ladder[3][16] = {
    {1.0f, 0.7914f, 0.6320f, 0.5084f, 0.4019f, 0.3161f, 0.2459f, 0.1912f, 0.1396f, 0.1080f, 0.0844f, 0.0672f, 0.0524f, 0.0421f, 0.0310f, 0.0f},
    {1.0f, 0.7816f, 0.6125f, 0.4850f, 0.3925f, 0.3109f, 0.2416f, 0.1872f, 0.1373f, 0.1058f, 0.0828f, 0.0653f, 0.0533f, 0.0412f, 0.0306f, 0.0f},
    {1.0f, 0.7979f, 0.6229f, 0.4921f, 0.3942f, 0.3004f, 0.2348f, 0.1874f, 0.1419f, 0.1106f, 0.0846f, 0.0677f, 0.0534f, 0.0420f, 0.0328f, 0.0f}};
static uint8_t limit[512];                  // |level| (0-511) to 0-255: straight to LOUD_KNEE, then soft
static int16_t lq[3][16];                   // ladder in 1/16384ths, for put_sample's integer sums
static bool dac_ready;

float pmcv_dac_level(int a0, int a1, int a2) { return ladder[0][a0 & 15] + ladder[1][a1 & 15] + ladder[2][a2 & 15]; }

static void dac_init(void) {
    for (int i = 0; i < 512; i++) {
        float v = (float)i;
        if (v > LOUD_KNEE) v = LOUD_KNEE + (DAC_HALF - LOUD_KNEE) * (1.0f - expf(-(v - LOUD_KNEE) / (DAC_HALF - LOUD_KNEE)));
        limit[i] = (uint8_t)(v + 0.5f);
    }
    for (int c = 0; c < 3; c++)
        for (int a = 0; a < 16; a++) lq[c][a] = (int16_t)lrintf(ladder[c][a] * 16384.0f);
    dac_ready = true;
}

// a sample's three writes, in the order whose two partial sums (the chip
// between the writes, 52 T each) are nearest the new level. Written a,
// then b, then c: off the new level by |d_b + d_c|, then by |d_c|. In
// integers, the orders tried in turn and the first of equals kept (Cloak's
// clcv.c; about half the cycles of floats).
static const uint8_t order[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
static inline int32_t iabs(int32_t x) { return x < 0 ? -x : x; }
static inline void put_sample(uint8_t *out, int *a0, int *a1, int *a2, uint16_t t) {
    const int w0 = t >> 8, w1 = t >> 4 & 15, w2 = t & 15;
    const int32_t d0 = lq[0][w0] - lq[0][*a0], d1 = lq[1][w1] - lq[1][*a1], d2 = lq[2][w2] - lq[2][*a2];
    const int32_t sum = d0 + d1 + d2;
    const int32_t f0 = iabs(sum - d0), f1 = iabs(sum - d1), f2 = iabs(sum - d2);
    const int32_t l0 = iabs(d0), l1 = iabs(d1), l2 = iabs(d2);
    int32_t best = f0 + l2, v;
    int pick = 0;
    if ((v = f0 + l1) < best) { best = v; pick = 1; }
    if ((v = f1 + l2) < best) { best = v; pick = 2; }
    if ((v = f1 + l0) < best) { best = v; pick = 3; }
    if ((v = f2 + l1) < best) { best = v; pick = 4; }
    if ((v = f2 + l0) < best) pick = 5;
    const uint32_t b = (uint32_t)(0x90 | w0) | (uint32_t)(0xB0 | w1) << 8 | (uint32_t)(0xD0 | w2) << 16;
    out[0] = (uint8_t)(b >> 8 * order[pick][0]);
    out[1] = (uint8_t)(b >> 8 * order[pick][1]);
    out[2] = (uint8_t)(b >> 8 * order[pick][2]);
    *a0 = w0; *a1 = w1; *a2 = w2;
}

void pmcv_sound(pmcv_t *e, const int16_t *s, int n) {
    if (!dac_ready) dac_init();
    e->tone_n = 0;
    if (n <= 0) {
        if (e->pcm) {
            static const uint8_t off[] = {0x9F, 0xBF, 0xDF};
            memcpy(e->tone_bytes, off, sizeof off);
            e->tone_n = sizeof off;
            e->pcm = false;
        }
        return;
    }
    if (!e->pcm) {
        e->pcm = true;
        e->pcm_gain = 0;
        e->hp_x = s[0];
        e->hp_y = 0;
        e->dac_att[0] = e->dac_att[1] = e->dac_att[2] = 15;     // (as the lists before left them)
    }
    // Every list, not just the first: tones 1 and 2 parked at a period of 1,
    // tone 0 and the noise silent. One byte the console misreads (a bus
    // hiccup) can leave a tone at an audible period, and with the park sent
    // once that was a drone until RESET (seen on the console after some play).
    // Now it lasts a list at most. Since 0.1.4 the rest of the chip too
    // (tone 0's period, the noise's control), so every register is written
    // each list: nine PSG bytes.
    static const uint8_t park[] = {0x81, 0x00, 0xA1, 0x00, 0xC1, 0x00, 0xE4, 0xFF};   // (the attenuators: every sample's)
    memcpy(e->tone_bytes, park, sizeof park);
    e->tone_n = sizeof park;
    // (the state in locals: the byte stores below could alias e's fields)
    int32_t hx = e->hp_x, hy = e->hp_y, gain = e->pcm_gain;
    int a0 = e->dac_att[0], a1 = e->dac_att[1], a2 = e->dac_att[2];
    uint32_t clipped = 0;
    for (int k = 0; k < PMCV_PCM_N; k++) {
        int32_t x = s[k < n ? k : n - 1];
        hy += ((x - hx) << 8) - (hy >> HP_SHIFT);
        hx = x;
        int32_t v = (hy >> 8) * SCALE_LEVELS / SCALE_FULL;
        int32_t a = v < 0 ? -v : v;
        if (a > LOUD_KNEE) clipped++;
        a = limit[a > 511 ? 511 : a];
        int32_t lvl = DAC_MID + (v < 0 ? -a : a);
        // (the DAC's rest is 1.7 times the two tones': up to it over 43 ms, as steep as before)
        if (gain < 32 && !(k & 7)) gain++;
        lvl = lvl * gain / 32;
        put_sample(e->pcm_bytes[k], &a0, &a1, &a2, pm_dac3[lvl < 0 ? 0 : lvl > DAC_LEVELS - 1 ? DAC_LEVELS - 1 : lvl]);
    }
    e->hp_x = hx;
    e->hp_y = hy;
    e->pcm_gain = (uint8_t)gain;
    e->dac_att[0] = (uint8_t)a0;
    e->dac_att[1] = (uint8_t)a1;
    e->dac_att[2] = (uint8_t)a2;
    e->stats.clipped += clipped;
}

void pmcv_init(pmcv_t *e) {
    memset(e, 0, sizeof *e);
    e->dirty = true;
    e->bands = (1u << (PMCV_BYTES / 256)) - 1;
    e->vram_sat[0] = 0xD0;              // the kernel's (no sprites)
    e->want_sat[0] = 0xD0;
    if (!colors_ready) colors_init();
    if (!dac_ready) dac_init();
    layout_ready = false;
    layout_init();
    turned_ready = false;               // (made at the first picture, from the board's ROMs)
}
