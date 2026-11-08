// Neptuno+ replacement for src/mist/buttons.cxx: there is no user button on
// the RP2040 side (GP24 goes to an FPGA pin).

#include "buttons.h"

unsigned char MenuButton() {
    return 0;
}
