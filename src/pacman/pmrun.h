// pmrun: the cart's loop, a list at a time, shared by the plugin and the
// Mac tests so both run the board the same way. Portable. (qbrun.c's way.)
//
// Each list runs the board for the list's time (PMCV_LIST_T of the
// console's T: 16.77 ms, a little more than the board's 16.50 ms frame), to
// a cycle count kept from the start so nothing drifts; PMCV_PCM_N samples of
// the sound go with the list. The board runs PMRUN_LEAD cycles ahead of the
// samples taken, so a list always finds its samples made.
//
// The picture: the board at the last VBLANK in the list's time. The board
// stops at the first of MAME's timeslice ends past the list's end, which
// can be up to a frame later, so a run may pass the next list's VBLANK
// too: the board is copied at each VBLANK and the copy kept for the list
// it belongs to. So each list has one picture, of its own frame, and
// about one frame in 62 is never shown (60.6 frames a second, 59.6 lists).
// A picture that changes more than PMCV_TILES tiles (the power-on test's
// screens) leaves the rest for the next picture, and the list after it
// makes none, so the board's busiest lists fit.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pm.h"
#include "pmcv.h"

#define PMRUN_LEAD 2600                 // Z80 cycles: 0.85 ms, about 5 samples
#define PMRUN_FRAME 50688               // the board's frame: 264 lines of 192 cycles

typedef struct {
    pmcv_t *cv;
    uint32_t lists;                     // since the start
    uint64_t target;                    // the Z80 cycle this list runs to
    uint32_t underruns;                 // samples a list had to make up
    uint32_t pictures;
    uint32_t rested;                    // lists with no picture, after one that left tiles
    bool rest;
    int16_t pcm[PMCV_PCM_N];
    pmcv_board_t board[2];              // the board at the VBLANK for this list ([cur]), and one past its end
    bool have[2];
    uint8_t cur;
    const pmcv_board_t *shown;          // the one the last picture was made from (for the tests)
} pmrun_t;

// the board started (pm_init done) and pmcv's sound with it
void pmrun_start(pmrun_t *r, pmcv_t *cv);
// one list: the board for its time, then the list into out; returns its length
size_t pmrun_list(pmrun_t *r, uint8_t *out);
