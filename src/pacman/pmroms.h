// pmroms.h -- where the ROMs go: MAME's "pacman" set, each file known by its
// size and CRC-32, never by its name. Portable: the plugin and the Mac tests
// share it.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "pm.h"

typedef struct {
    const char *name;                   // MAME's name, for messages
    uint32_t size, crc;
    uint8_t *place;                     // where its bytes go
} pm_rom_file_t;

#define PM_ROM_FILES 9                  // (not 82s126.3m, the timing PROM, which MAME doesn't use)
#define PMROMS_ALL   ((1u << PM_ROM_FILES) - 1)
extern const pm_rom_file_t pm_rom_files[PM_ROM_FILES];

typedef struct {
    uint8_t maincpu[0x4000], gfx[0x2000], proms[0x120], wave[0x100];
} pm_rom_store_t;
extern pm_rom_store_t pm_rom_store;

uint32_t pm_crc32(uint32_t crc, const uint8_t *p, uint32_t n);
// which of the files still to find (not in found) has this size and CRC-32 (-1: none)
int pmroms_wanted(uint32_t size, uint32_t crc, uint32_t found);
bool pmroms_size_ok(uint32_t size);     // one of the files' sizes
// a file's bytes (size of them, its CRC-32 right) to its place
void pmroms_put(int i, const uint8_t *data);
// the board's ROMs from the store
void pmroms_roms(pm_roms_t *r);
