// Neptuno+ replacement for src/mist/db9.cxx: two DB9 joysticks read through
// the Neptuno shift register instead of one on GPIOs.

#include "db9.h"
#include "ShiftRegisterJoystick.h"

using namespace calypso;

extern ShiftRegisterJoystick joystick;

void InitDB9() {
    joystick.init();
}

char GetDB9(char index, unsigned char *joy_map) {
    if (index < 0 || index >= joystick.count()) {
        return 0;
    }
    *joy_map = joystick.read(index);
    return 1;
}

void DB9SetLegacy(uint8_t on) {}

void DB9Update(uint8_t joy_num, uint8_t usbjoy) {}
