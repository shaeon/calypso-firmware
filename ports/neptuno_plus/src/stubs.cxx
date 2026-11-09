// Neptuno+ stand-ins for upstream features that are not enabled on this board.
// The upstream sources that implement them are left out of the build.

#include <cstdio>
#include <inttypes.h>

extern "C" {
// --- Tape player (src/mist/tap_processor.cxx) -------------------------------
// No free line towards the FPGA: Calypso's tape pins are the PS/2 mouse here.
uint8_t tap_player_register() { return 0; }
}
