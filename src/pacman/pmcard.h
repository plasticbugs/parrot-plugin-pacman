// pmcard.h -- the ROMs on the card, through fsro: loose or zipped in
// /PACMAN (MAME's pacman.zip, as it is), or /PACMAN.ZIP. Portable: the
// plugin and the Mac test (test/card.sh, on disk images) share it.
// (Q*bert's qbcard.c.)
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "../sdplayer/fsro.h"
#include "pmroms.h"

#define PMCARD_HOME "/PACMAN"
#define PMCARD_ZIP  "/PACMAN.ZIP"

// Every file in PMCARD_HOME, then PMCARD_ZIP, until all are found: each
// ROM to its place in pm_rom_store. A zip is never read whole: its
// directory, then each wanted entry's packed bytes into buf and unpacked
// after them. buf: max bytes (the directory, and a packed ROM with the ROM
// after it, must fit); tick: called between sectors (the plugin serves the
// console), or NULL. Returns pmroms' found bits.
uint32_t pmcard_load_roms(fs_t *fs, uint8_t *buf, uint32_t max, void (*tick)(void));
