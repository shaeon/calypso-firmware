// Neptuno+ stand-ins for upstream features that are not enabled on this board.
// The upstream sources that implement them are left out of the build.

#include <cstdio>
#include <inttypes.h>

extern "C" {
#include "mist-firmware/errors.h"

extern unsigned char Error;

// --- Firmware update from the SD card (src/mist/FirmwareUpdate.cxx) --------
// Its layout needs a 16 MB flash and it would accept Calypso images: off.
unsigned char CheckFirmware(char *name) {
    printf("Firmware update from SD is not available on Neptuno+\n");
    Error = ERROR_INVALID_DATA;
    return 0;
}

char *GetFirmwareVersion(char *name) {
    return nullptr;
}

void WriteFirmware(char *name) {}

// --- MIDI synthesizer (src/mist/midireceiver.cxx, midi_processor.cxx) ------
// Pending: audio pins and flash size to be confirmed on this board.
void midi_byte_processor(uint8_t value) {}
void midi_stop(void) {}
uint8_t midi_player_register() { return 0; }

// --- Tape player (src/mist/tap_processor.cxx) -------------------------------
// No free line towards the FPGA: Calypso's tape pins are the PS/2 mouse here.
uint8_t tap_player_register() { return 0; }
}
