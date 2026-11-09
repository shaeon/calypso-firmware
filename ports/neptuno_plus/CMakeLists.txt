# Neptuno+ build of calypso-firmware.
#
# Everything specific to Neptuno+ lives in this directory: the upstream files
# are not modified, so changes from teiram/calypso-firmware merge cleanly.
#
#   mkdir build && cd build
#   cmake ../ports/neptuno_plus
#   make
#
# The upstream libraries (src/core, src/mist and the mist-firmware submodule)
# are reused as they are. A handful of upstream sources are swapped for the
# ones in ports/neptuno_plus/src, which implement the same classes/functions
# against the unchanged upstream headers:
#
#   src/core/JTAG.cxx            -> src/JTAG.cxx       (Passive Serial loader)
#   src/core/SPIDevice.cxx       -> src/SPIDevice.cxx  (+ PIO bus for the SD)
#   src/core/DB9Joystick.cxx     -> (not used)
#   src/mist/db9.cxx             -> src/db9.cxx        (shift register, 2 ports)
#   src/mist/buttons.cxx         -> src/buttons.cxx    (no user button)
#   src/mist/midireceiver.cxx, midi_processor.cxx,
#   tap_processor.cxx            -> src/stubs.cxx      (features not enabled)
#   src/mist/FirmwareUpdate.cxx     kept, entry points wrapped by
#                                   src/FirmwareGuard.cxx (board + flash check)
#   src/mist/compat.c               kept, mist_loop and MCUReset wrapped by
#                                   src/CoreWatch.cxx (JTAG-loaded cores are
#                                   adopted without restarting the MCU)
#   src/main.cxx                 -> src/main.cxx
#
# If upstream renames or drops one of those files, configuration stops with
# an error instead of building a mix.

cmake_minimum_required(VERSION 3.18)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
# Same as upstream ("something odd happens with optimization")
set(CMAKE_BUILD_TYPE RELEASE)

get_filename_component(CALYPSO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../.. ABSOLUTE)
set(UPSTREAM_SRC ${CALYPSO_ROOT}/src)
set(PORT_DIR ${CMAKE_CURRENT_LIST_DIR})

# Firmware version: taken from the upstream top-level CMakeLists.txt
file(STRINGS ${CALYPSO_ROOT}/CMakeLists.txt _fv_line REGEX "^set\\(FVERSION")
string(REGEX MATCH "\"([^\"]+)\"" _fv_match "${_fv_line}")
set(FVERSION ${CMAKE_MATCH_1})

include(${CALYPSO_ROOT}/pico_sdk_import.cmake)
include(${CALYPSO_ROOT}/pico_extras_import.cmake)

project(neptuno-plus-firmware)

# 16 MB flash, as on Calypso (confirmed for the Neptuno+ middleboard)
set(PICO_FLASH_SIZE_BYTES 16777216 CACHE STRING "RP2040 XIP flash size" FORCE)

# Optional transmit-only debug console on a PIO, e.g. -DNEPTUNO_DEBUG_UART_PIN=23
set(NEPTUNO_DEBUG_UART_PIN "" CACHE STRING "GPIO for the PIO debug UART TX (empty = off)")

pico_sdk_init()

# --- Upstream libraries, unchanged -------------------------------------------
add_subdirectory(${UPSTREAM_SRC}/core ${CMAKE_BINARY_DIR}/upstream/core)
add_subdirectory(${UPSTREAM_SRC}/mist ${CMAKE_BINARY_DIR}/upstream/mist)

# Drop upstream sources replaced by this port. Fails if one is not found.
function(neptuno_remove_sources target)
    get_target_property(_srcs ${target} SOURCES)
    foreach(_name ${ARGN})
        set(_before ${_srcs})
        list(FILTER _srcs EXCLUDE REGEX "(^|/)${_name}$")
        if ("${_before}" STREQUAL "${_srcs}")
            message(FATAL_ERROR "Neptuno+ port: ${_name} is no longer in target '${target}'. "
                "Upstream changed: review ports/neptuno_plus before building.")
        endif()
    endforeach()
    set_property(TARGET ${target} PROPERTY SOURCES ${_srcs})
endfunction()

neptuno_remove_sources(core JTAG.cxx SPIDevice.cxx DB9Joystick.cxx)
neptuno_remove_sources(mist db9.cxx buttons.cxx
    midireceiver.cxx midi_processor.cxx tap_processor.cxx)

# The upstream SD card updater (src/mist/FirmwareUpdate.cxx) is built as it
# is, with its three entry points renamed so src/FirmwareGuard.cxx can check
# the board and the flash size before handing over to it.
if (NOT EXISTS ${UPSTREAM_SRC}/mist/FirmwareUpdate.cxx)
    message(FATAL_ERROR "Neptuno+ port: src/mist/FirmwareUpdate.cxx is gone. "
        "Upstream changed: review ports/neptuno_plus before building.")
endif()
# compat.c is built as it is, with mist_loop and MCUReset renamed so
# src/CoreWatch.cxx can wrap them: a core loaded through JTAG is adopted
# without restarting the MCU, and the firmware recovers when no MiST core is
# recognised (upstream never leaves LEGACY_MODE).
if (NOT EXISTS ${UPSTREAM_SRC}/mist/compat.c)
    message(FATAL_ERROR "Neptuno+ port: src/mist/compat.c is gone. "
        "Upstream changed: review ports/neptuno_plus before building.")
endif()
set_source_files_properties(${UPSTREAM_SRC}/mist/compat.c
    DIRECTORY ${UPSTREAM_SRC}/mist
    PROPERTIES COMPILE_DEFINITIONS "mist_loop=upstream_mist_loop;MCUReset=upstream_MCUReset")

set_source_files_properties(${UPSTREAM_SRC}/mist/FirmwareUpdate.cxx
    DIRECTORY ${UPSTREAM_SRC}/mist
    PROPERTIES COMPILE_DEFINITIONS
    "CheckFirmware=upstream_CheckFirmware;WriteFirmware=upstream_WriteFirmware;GetFirmwareVersion=upstream_GetFirmwareVersion")

# The upstream libraries link the UART stdio driver, whose default pins
# (GP0/GP1) are the FPGA DCLK/DATA0 on this board. Swap it for plain stdio.
function(neptuno_drop_stdio_uart target)
    foreach(_prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(_libs ${target} ${_prop})
        if (_libs)
            list(FILTER _libs EXCLUDE REGEX "pico_stdio_uart")
            list(APPEND _libs pico_stdio)
            set_property(TARGET ${target} PROPERTY ${_prop} ${_libs})
        endif()
    endforeach()
endfunction()
neptuno_drop_stdio_uart(core)
neptuno_drop_stdio_uart(mist)

# Our Configuration.h must win over src/core/Configuration.h (mist/spi.cxx)
target_include_directories(mist BEFORE PRIVATE ${PORT_DIR}/include)

# --- Neptuno+ executable ------------------------------------------------------
add_executable(${PROJECT_NAME}
    src/main.cxx
    src/JTAG.cxx
    src/SPIDevice.cxx
    src/db9.cxx
    src/buttons.cxx
    src/stubs.cxx
    src/FirmwareGuard.cxx
    src/CoreWatch.cxx
    src/ShiftRegisterJoystick.cxx
    src/PioUartStdio.cxx
)

pico_generate_pio_header(${PROJECT_NAME} ${PORT_DIR}/src/sd_spi.pio)
pico_generate_pio_header(${PROJECT_NAME} ${PORT_DIR}/src/joy_sr.pio)
pico_generate_pio_header(${PROJECT_NAME} ${PORT_DIR}/src/pio_uart_tx.pio)

target_include_directories(${PROJECT_NAME} BEFORE PRIVATE ${PORT_DIR}/include)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${PORT_DIR}/src
    ${UPSTREAM_SRC}
    ${UPSTREAM_SRC}/core
    ${UPSTREAM_SRC}/mist
    ${UPSTREAM_SRC}/mist/usbhost
    ${CALYPSO_ROOT}/libs
)

if (NEPTUNO_DEBUG_UART_PIN)
    target_compile_definitions(${PROJECT_NAME} PRIVATE NEPTUNO_DEBUG_UART_PIN=${NEPTUNO_DEBUG_UART_PIN})
    message(STATUS "Neptuno+: PIO debug UART on GP${NEPTUNO_DEBUG_UART_PIN}")
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    pico_stdlib
    pico_stdio
    hardware_pio
    hardware_spi
    mist
    core
    tinyusb_host
    tinyusb_board
)

pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_add_extra_outputs(${PROJECT_NAME})

# FIRMWARE.UPG for the SD card update: a copy of the UF2 tagged by the
# upstream tools/uf2meta.py. The version starts with "NPP-", which is what
# src/FirmwareGuard.cxx checks to refuse Calypso update files (and Calypso
# refuses ours: different version prefix shown, same format). The plain UF2
# is left untagged for BOOTSEL.
string(TIMESTAMP FW_UPDATE_DATE "%Y%m%d")
execute_process(COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CALYPSO_ROOT}
    OUTPUT_VARIABLE FW_UPDATE_GIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if (NOT FW_UPDATE_GIT)
    set(FW_UPDATE_GIT "unknown")
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.uf2
        ${CMAKE_CURRENT_BINARY_DIR}/FIRMWARE.UPG
    COMMAND ${Python3_EXECUTABLE} ${CALYPSO_ROOT}/tools/uf2meta.py
        --version "NPP-${FVERSION}" --date "${FW_UPDATE_DATE}" --git "${FW_UPDATE_GIT}"
        ${CMAKE_CURRENT_BINARY_DIR}/FIRMWARE.UPG
    COMMENT "Writing FIRMWARE.UPG for the SD card update")
