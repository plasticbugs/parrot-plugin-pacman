# Pac-Man

This plugin plays Namco's 1980 Pac-Man arcade game, Midway's version, on the ColecoVision. It needs the game's ROMs from MAME, which you supply.

## Setup

1. Copy `PACMAN.CVP` into the `/PLUGINS` folder on the card.
2. Make a folder named `/PACMAN` and put MAME's `pacman.zip` in it as it is, or the files from it loose. You can also put the zip in the card's top folder as `/PACMAN.ZIP`.
3. Start Pac-Man from the Plugins page in the menu.

The files can have any names, because the plugin knows each ROM by its size and contents. If a ROM is missing, the screen names it.

## Controls

On the ColecoVision controller:

- The joystick moves Pac-Man.
- Keypad * or # adds a coin.
- Keypad 1 starts a one-player game, and so does either fire button.
- Keypad 2 starts a two-player game.
- Press RESET on the console to go back to the menu, or hold both fire buttons and \* for 2 seconds (Select and Start on a USB pad).

On a USB pad, through a powered hub:

- The D-pad or the left stick moves Pac-Man.
- Minus adds a coin. Plus, or the bottom or right face button, starts a one-player game.

The arcade stick has four ways. A diagonal turns Pac-Man the way you just pressed, as in MAME.

## Picture and sound

The arcade screen stands on its side and is 224 by 288 pixels. The ColecoVision shows 256 by 192, so the plugin scales the maze to three quarters of its size, 168 by 186, in the middle of the screen. The scores go on the left and right, with the lives at the bottom left and the fruit at the bottom right.

Pac-Man and the ghosts' bodies are the ColecoVision's sprites. When more than four share a line, they take turns, so some may flicker.

Colors are the nearest of the ColecoVision's 15. Pinky is magenta, and Clyde, orange in the arcade, is dark yellow.

The sound chip's three voices play through the ColecoVision's sound chip as 5,962 samples a second.

High scores last until you go back to the menu, as on the arcade machine when it is switched off.

## Credits

The arcade emulation is a port of MAME 0.288's code, under the BSD-3-Clause license. Its authors are:

- Nicola Salmoria and Stephane Humbert, for the Pac-Man driver.
- Nicola Salmoria and Aaron Giles, for the Namco sound chip.
- Aaron Giles, for MAME's scheduler and the watchdog.
- Juergen Buchmueller and Andrei I. Holub, for the Z80 CPU.
- AJR, for the 74LS259 latch.
- Couriersud, for the resistor network colors.

You can find MAME at https://www.mamedev.org. The zip reader follows Mark Adler's puff, from zlib. USB support comes from TinyUSB by Ha Thach, under the MIT license.

The plugin itself is by Parrot Cart, and it's free. Its source is in this repository, https://github.com/plasticbugs/parrot-plugin-pacman.

Pac-Man is a trademark of Bandai Namco. Parrot Cart isn't connected with them. This plugin doesn't include any game ROMs.

## Build it yourself

You need the Raspberry Pi Pico SDK 2.1.1, an Arm GNU toolchain, CMake,
Ninja, Python 3, sjasmplus and picotool. Releases are built with the Arm
GNU Toolchain 9-2019-q4 (GCC 9.2.1) and picotool 2.3.1. With those you get
the same file, byte for byte.

```sh
cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
ninja -C build
```

This makes `build/PACMAN.CVP`. Copy it into the `/PLUGINS` folder on the card.

On Ubuntu, `tools/ci/setup.sh` installs the same tools that GitHub uses to
build each release.

`CART-README.md` is the text the cart shows on the plugin's page. This
README starts with the same text.

## License

The plugin is under the BSD 3-Clause license (`LICENSE`). `THIRD-PARTY.md`
lists the parts that came from elsewhere, and their licenses.
