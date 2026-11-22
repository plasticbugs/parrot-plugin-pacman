# What in this repository came from elsewhere

The plugin's own code, in `src/`, is under the BSD 3-Clause license
(`LICENSE`). The copy of the cart's SDK in `sdk/` is under the MIT license
(`sdk/LICENSE`). These parts came from elsewhere and keep their own terms.

| Where | What | License |
|---|---|---|
| `sdk/firmware/revc/sdk/tinyusb/` | [TinyUSB](https://github.com/hathach/tinyusb) (Ha Thach and contributors), with the cart's fixes, listed in its `VENDORED.md` | MIT (`sdk/firmware/revc/sdk/tinyusb/LICENSE`) |
| `cmake/pico_sdk_import.cmake` | from the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) | BSD 3-Clause |

The built plugin also contains the Raspberry Pi Pico SDK's runtime (BSD
3-Clause) and TinyUSB (MIT).

## MAME

Parts of the code in `src/` are ports of MAME's code, or were written with
MAME's code as the reference. That code is under the BSD 3-Clause license
(`LICENSE`). It comes from MAME (<https://github.com/mamedev/mame>, tag
`mame0288`), and its copyright holders are these.

| Here | From MAME | Copyright holders |
|---|---|---|
| `src/pacman/pm.c`: the memory map, inputs, interrupt, palette, tiles, sprites | `src/mame/pacman/pacman.cpp`, `pacman_v.cpp` | Nicola Salmoria, Stephane Humbert |
| `src/pacman/pm.c`: the WSG | `src/devices/sound/namco.cpp` | Nicola Salmoria, Aaron Giles |
| `src/pacman/pm.c`: the latch | `src/devices/machine/74259.cpp` | AJR |
| `src/pacman/pm.c`: the palette's resistor weights | `src/emu/video/resnet.cpp` | Couriersud |
| `src/pacman/pm.c`: timers, timeslices, VBLANK, the watchdog | `src/emu/schedule.cpp`, `diexec.cpp`, `screen.cpp`, `sound.cpp`, `src/devices/machine/watchdog.cpp` | Aaron Giles |
| `src/pacman/z80.c`, `src/pacman/z80_ops.h`, `src/pacman/tools/z80gen.py` | `src/devices/cpu/z80` (`z80.cpp`, `z80.lst`, `z80make.py`) | Juergen Buchmueller, Andrei I. Holub |
| `src/pacman/pmpad.h`: the 4-way stick | `src/emu/ioport.cpp` | Aaron Giles |

The zip reader in `src/starwars/swzip.c` follows RFC 1951 and the method of
zlib's `contrib/puff` by Mark Adler (zlib license). It was written afresh,
for the cart's Star Wars plugin.

Pac-Man is a trademark of Bandai Namco. Parrot Cart isn't connected with
them. This plugin doesn't include any game ROMs.

The 8x8 screen font's letter shapes come from the 1982 arcade game Mr. Do!
