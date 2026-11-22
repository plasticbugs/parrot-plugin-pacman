// pmroms.c -- see pmroms.h. The sizes and CRC-32s are MAME 0.288's
// (pacman.cpp's pacman).
#include "pmroms.h"

#include <string.h>

pm_rom_store_t pm_rom_store;

#define S pm_rom_store
const pm_rom_file_t pm_rom_files[PM_ROM_FILES] = {
    {"pacman.6e", 0x1000, 0xc1e6ab10u, S.maincpu},
    {"pacman.6f", 0x1000, 0x1a6fb2d4u, S.maincpu + 0x1000},
    {"pacman.6h", 0x1000, 0xbcdd1bebu, S.maincpu + 0x2000},
    {"pacman.6j", 0x1000, 0x817d94e3u, S.maincpu + 0x3000},
    {"pacman.5e", 0x1000, 0x0c944964u, S.gfx},
    {"pacman.5f", 0x1000, 0x958fedf9u, S.gfx + 0x1000},
    {"82s123.7f", 0x0020, 0x2fc650bdu, S.proms},
    {"82s126.4a", 0x0100, 0x3eb3a8e4u, S.proms + 0x20},
    {"82s126.1m", 0x0100, 0xa9cc86bfu, S.wave},
};
#undef S

uint32_t pm_crc32(uint32_t crc, const uint8_t *p, uint32_t n) {
    static const uint32_t t[16] = {0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4,
                                   0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
                                   0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c};
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ t[crc & 15];
        crc = (crc >> 4) ^ t[crc & 15];
    }
    return ~crc;
}

int pmroms_wanted(uint32_t size, uint32_t crc, uint32_t found) {
    for (int i = 0; i < PM_ROM_FILES; i++)
        if (pm_rom_files[i].size == size && pm_rom_files[i].crc == crc && !(found & 1u << i)) return i;
    return -1;
}

bool pmroms_size_ok(uint32_t size) {
    for (int i = 0; i < PM_ROM_FILES; i++)
        if (pm_rom_files[i].size == size) return true;
    return false;
}

void pmroms_put(int i, const uint8_t *data) { memcpy(pm_rom_files[i].place, data, pm_rom_files[i].size); }

void pmroms_roms(pm_roms_t *r) {
    r->maincpu = pm_rom_store.maincpu;
    r->gfx = pm_rom_store.gfx;
    r->proms = pm_rom_store.proms;
    r->wave = pm_rom_store.wave;
}
