# pacman (PACMAN.CVP): Namco's 1980 Pac-Man arcade game (Midway's set) on
# the ColecoVision. The cart runs the arcade board (a Z80 and the Namco WSG:
# src/pacman/pm.c, from MAME 0.288) from the user's ROMs on the card (/PACMAN), and
# pmcv sends the picture and the sound to the console as the Game Boy
# kernel's lists. RESET goes back to the menu.
#
#   cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
#   ninja -C build                      -> build/PACMAN.CVP
#
# Copy PACMAN.CVP into the card's /PLUGINS folder.
cmake_minimum_required(VERSION 3.13)

# the cart's SDK: the copy in sdk/ (sdk/README.md)
set(PARROTCART_SDK ${CMAKE_CURRENT_LIST_DIR}/sdk/firmware/revc)
set(PICO_BOARD_HEADER_DIRS ${PARROTCART_SDK}/cvcart_revc/boards)
set(PICO_BOARD cvcart_revc)
set(PICO_PLATFORM rp2350-arm-s)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
# the USB host: sdk/tinyusb (TinyUSB 0.21 and its fixes since), not the pico-sdk's 0.18
include(${PARROTCART_SDK}/sdk/cvpad_tinyusb.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/pico_sdk_import.cmake)

project(pacman C CXX ASM)
pico_sdk_init()
include(${PARROTCART_SDK}/sdk/cvplugin.cmake)

# The same source makes the same file, whenever it's built: the compiler's
# __DATE__ and __TIME__ (the build date in the binary info) come from
# SOURCE_DATE_EPOCH, fixed here at the tested build's day, 2026-11-21.
set(BUILD_EPOCH 1795262400)
set(CMAKE_C_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env SOURCE_DATE_EPOCH=${BUILD_EPOCH})
set(CMAKE_CXX_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env SOURCE_DATE_EPOCH=${BUILD_EPOCH})

# the plugin's code (the cart's firmware/revc/pacman/, with plugin/ beside it)
set(S ${CMAKE_CURRENT_LIST_DIR}/src/pacman)

set(OURS ${S}/pacman.c ${S}/pm.c ${S}/z80.c ${S}/pmcv.c ${S}/pmrun.c ${S}/pmcard.c ${S}/pmroms.c ${PARROTCART_SDK}/sdk/cvscale.c ${CMAKE_CURRENT_LIST_DIR}/src/starwars/swzip.c)
set_source_files_properties(${OURS} PROPERTIES COMPILE_OPTIONS "-Wall;-Wno-unused-function;-fstack-usage")
# the emulation's state is many small statics: in one section each, every
# use needs a separate address loaded, so not
set_source_files_properties(${S}/pm.c ${S}/z80.c PROPERTIES COMPILE_OPTIONS
                            "-Wall;-Wno-unused-function;-fstack-usage;-fno-data-sections")
# the card's code runs once, at the start: built for size
set_source_files_properties(${S}/pmcard.c ${S}/pmroms.c ${CMAKE_CURRENT_LIST_DIR}/src/starwars/swzip.c PROPERTIES COMPILE_OPTIONS
                            "-Wall;-Wno-unused-function;-fstack-usage;-Os")
set_source_files_properties(${PARROTCART_SDK}/sdplayer/fsro.c PROPERTIES COMPILE_OPTIONS "-Os")

add_executable(pacman ${OURS} ${PARROTCART_SDK}/midihello/vdp.c)
target_include_directories(pacman PRIVATE ${S} ${PARROTCART_SDK}/midihello
                           ${PARROTCART_SDK}/sdk)
# the sound's bins: a few lists' worth is plenty
# core 0's stack: all of scratch Y (4K; the picture goes 2K deep, test/armcount.sh);
# core 1's as before (scratch X also holds its bus loop)
target_compile_definitions(pacman PRIVATE PICO_PRINTF_SUPPORT_FLOAT=0 PICO_PRINTF_SUPPORT_EXPONENTIAL=0 PM_NBIN=512
    PICO_STACK_SIZE=0x1000 PICO_CORE1_STACK_SIZE=0x800)
cvframes_use(pacman)
cvframes_serve2(pacman)     # core 1's bus loop acts on two samples of the pins that agree (sdk/cvfserve.S)
cvsd_use(pacman)
cvpad_use(pacman)                           # USB game pads: the port is a host
pico_enable_stdio_usb(pacman 0)
pico_enable_stdio_uart(pacman 0)
cvcart_plugin(pacman ID parrotcart.pacman NAME "Pac-Man" VERSION 0.1.5 BY "Parrot Cart"
              HOME /PACMAN USES usb-host ICON src/pacman/pacman_icon.txt FILE PACMAN.CVP README CART-README.md)
