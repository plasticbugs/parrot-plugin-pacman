// pmrun.c -- see pmrun.h.
#include <string.h>

#include "pmrun.h"

static pmrun_t *running;

// each VBLANK's board, for this list or (past its end) the next
static void vblank(void) {
    pmrun_t *r = running;
    if (!r) return;
    int k = r->cur ^ (pm_time() > r->target - PMRUN_LEAD);
    pmcv_board(&r->board[k]);
    r->have[k] = true;
}

void pmrun_start(pmrun_t *r, pmcv_t *cv) {
    memset(r, 0, sizeof *r);
    r->cv = cv;
    running = r;
    pm_on_vblank = vblank;
    pm_audio_rate_q8(PMCV_RATE_Q8);
    r->target = pm_time();
}

size_t pmrun_list(pmrun_t *r, uint8_t *out) {
    r->lists++;
    r->target = ((uint64_t)r->lists * PMCV_LIST_CYCLES_Q16 >> 16) + PMRUN_LEAD;
    if (r->have[r->cur ^ 1]) r->cur ^= 1;       // the last run passed this list's VBLANK
    pm_run_to(r->target);
    if (r->have[r->cur] && r->rest) {          // after a picture that left tiles, none
        r->have[r->cur] = false;
        r->rest = false;
        r->rested++;
    }
    if (r->have[r->cur]) {
        r->rest = !pmcv_picture(r->cv, &r->board[r->cur]);
        r->have[r->cur] = false;
        r->shown = &r->board[r->cur];
        r->pictures++;
    }
    int32_t sum[PMCV_PCM_N];
    uint16_t cnt[PMCV_PCM_N];
    int n = pm_audio_take(sum, cnt, PMCV_PCM_N);
    if (n < PMCV_PCM_N) r->underruns += (uint32_t)(PMCV_PCM_N - n);
    // a bin's mean, in MAME's -1..1 as +-32767: sum / (384 count)
    for (int k = 0; k < n; k++) {
        int32_t v = cnt[k] ? (int32_t)((int64_t)sum[k] * 32767 / (384 * (int32_t)cnt[k])) : 0;
        r->pcm[k] = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
    }
    if (n == 0) r->pcm[n++] = 0;
    pmcv_sound(r->cv, r->pcm, n);
    return pmcv_tv_frame(r->cv, out);
}
