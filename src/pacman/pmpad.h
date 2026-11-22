// pmpad.h -- the controls: the ColecoVision controller's joystick and
// keypad, or a USB pad's d-pad or left stick, as Pac-Man's 4-way stick.
// Portable, header-only.
//
// The stick goes through MAME 0.288's 4-way logic (digital_joystick::
// frame_update in emu/ioport.cpp), once a list as MAME does it once a
// frame: up with down, or left with right, count as neither; a diagonal
// keeps the way that just came on (left held, then up too: up), and a
// diagonal straight from the middle, or from the other diagonal, goes
// across (MAME picks the horizontal axis).
//
// The keypad: * or # coin, 1 one player, 2 two players (the arcade plugins'
// keys). A USB pad: - coin, + or the bottom or right face button one
// player. Either fire button: one player. One coin a press, however long it
// is held.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "../sdk/gamepad.h"

// the ColecoVision keypad's codes, as the kernel reports them
enum { PMKEY_0 = 0xA, PMKEY_1 = 0xD, PMKEY_2 = 0x7, PMKEY_3 = 0xC, PMKEY_STAR = 0x9, PMKEY_HASH = 0x6, PMKEY_NONE = 0xF };

// directions: MAME's joystick bits (JOYDIR_UP, DOWN, LEFT, RIGHT)
enum { PMDIR_UP = 1, PMDIR_DOWN = 2, PMDIR_LEFT = 4, PMDIR_RIGHT = 8 };

typedef struct {
    uint8_t current, previous, way4;    // MAME's m_current, m_previous, m_current4way
    uint8_t coin_left;                  // frames of the coin pulse still to give the game
    uint8_t coin_wait;                  // frames the coin key must still be up before a press counts
} pmpad_t;

// A coin is a pulse of PMPAD_COIN frames from the press, however long the
// key is held; another needs the key up for PMPAD_COIN_UP frames (one coin
// for a key that bounces)
#define PMPAD_COIN    6
#define PMPAD_COIN_UP 3

// MAME's digital_joystick::frame_update for a 4-way stick: the 8 ways in
// (PMDIR_ bits), the 4-way's out
static inline uint8_t pmpad_4way(pmpad_t *s, unsigned dirs) {
    s->previous = s->current;
    s->current = (uint8_t)(dirs & 15);
    if ((s->current & (PMDIR_UP | PMDIR_DOWN)) == (PMDIR_UP | PMDIR_DOWN)) s->current &= (uint8_t)~(PMDIR_UP | PMDIR_DOWN);
    if ((s->current & (PMDIR_LEFT | PMDIR_RIGHT)) == (PMDIR_LEFT | PMDIR_RIGHT)) s->current &= (uint8_t)~(PMDIR_LEFT | PMDIR_RIGHT);
    if (s->current != s->previous) {
        s->way4 = s->current;
        if ((s->way4 & (PMDIR_UP | PMDIR_DOWN)) && (s->way4 & (PMDIR_LEFT | PMDIR_RIGHT))) s->way4 ^= s->way4 & s->previous;
        if ((s->way4 & (PMDIR_UP | PMDIR_DOWN)) && (s->way4 & (PMDIR_LEFT | PMDIR_RIGHT))) s->way4 &= (uint8_t)~(PMDIR_UP | PMDIR_DOWN);
    } else {
        s->way4 &= s->current;
    }
    return s->way4;
}

// The console's joystick nibble (low = held: bit 0 up, 1 right, 2 down, 3 left) as PMDIR_ bits
static inline unsigned pmpad_joy_dirs(uint8_t joy) {
    return (!(joy & 1) ? PMDIR_UP : 0) | (!(joy & 2) ? PMDIR_RIGHT : 0) | (!(joy & 4) ? PMDIR_DOWN : 0) |
           (!(joy & 8) ? PMDIR_LEFT : 0);
}

// sdk/cvpad.h's cvpad_dirs() (GP_UP ...) as PMDIR_ bits
static inline unsigned pmpad_gp_dirs(uint32_t gp) {
    return (gp & GP_UP ? PMDIR_UP : 0) | (gp & GP_DOWN ? PMDIR_DOWN : 0) | (gp & GP_LEFT ? PMDIR_LEFT : 0) |
           (gp & GP_RIGHT ? PMDIR_RIGHT : 0);
}

// One list's inputs. joy, key: the console's controller as cvf_input()
// gives them; fire: its fire buttons (bit 0 left, bit 1 right, low = held).
// pad_dirs: cvpad_dirs() of a USB pad (0: none), pad_buttons its buttons.
// Sets IN0 (the stick, the coin) and IN1 (the starts), active low as MAME's.
static inline void pmpad_frame(pmpad_t *s, uint8_t joy, uint8_t fire, uint8_t key, uint32_t pad_dirs, uint32_t pad_buttons,
                               uint8_t *in0, uint8_t *in1) {
    uint8_t w = pmpad_4way(s, pmpad_joy_dirs(joy) | pmpad_gp_dirs(pad_dirs));
    uint8_t v0 = 0xff, v1 = 0xff;
    if (w & PMDIR_UP) v0 &= (uint8_t)~0x01;
    if (w & PMDIR_LEFT) v0 &= (uint8_t)~0x02;
    if (w & PMDIR_RIGHT) v0 &= (uint8_t)~0x04;
    if (w & PMDIR_DOWN) v0 &= (uint8_t)~0x08;
    bool coin = key == PMKEY_STAR || key == PMKEY_HASH || (pad_buttons & GP_MINUS);
    if (key == PMKEY_1 || (pad_buttons & (GP_PLUS | GP_SOUTH | GP_EAST)) || (fire & 3) != 3) v1 &= (uint8_t)~0x20;
    if (key == PMKEY_2) v1 &= (uint8_t)~0x40;
    if (coin && !s->coin_wait) s->coin_left = PMPAD_COIN;
    if (coin) s->coin_wait = PMPAD_COIN_UP;
    else if (s->coin_wait) s->coin_wait--;
    if (s->coin_left) { v0 &= (uint8_t)~0x20; s->coin_left--; }
    *in0 = v0;
    *in1 = v1;
}
