// Firmware update from the SD card on Neptuno+.
//
// The upstream updater (src/mist/FirmwareUpdate.cxx) is compiled unchanged,
// with its entry points renamed to upstream_* (see CMakeLists.txt). These are
// the functions the MiST menu calls; they add two checks before handing over:
//
// - The flash must hold the upstream layout: image in the first 8 MB, staging
//   copy at 0x10800000, i.e. a 16 MB part. On a smaller part that address
//   aliases onto the running firmware and the update would erase it.
// - The file must be a Neptuno+ build (version "NPP-..."), not a Calypso one,
//   which shares the same file format.

#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include "hardware/flash.h"
#include "hardware/sync.h"

extern "C" {
#include "mist-firmware/errors.h"

extern unsigned char Error;

unsigned char upstream_CheckFirmware(char *name);
void upstream_WriteFirmware(char *name);
char *upstream_GetFirmwareVersion(char *name);
}

static constexpr uint32_t NEEDED_FLASH = 16u * 1024 * 1024;
static constexpr const char *BOARD_PREFIX = "NPP-";

// JEDEC ID: manufacturer, memory type, capacity (log2 of the size in bytes)
uint32_t neptuno_flash_size() {
    uint8_t tx[4] = {0x9f, 0, 0, 0};
    uint8_t rx[4] = {0};
    uint32_t irq = save_and_disable_interrupts();
    flash_do_cmd(tx, rx, sizeof(tx));
    restore_interrupts(irq);
    if (rx[3] < 16 || rx[3] > 28) return 0;
    return 1u << rx[3];
}

static bool updateAllowed(char *name) {
    const uint32_t size = neptuno_flash_size();
    if (size < NEEDED_FLASH) {
        printf("Upgrade: flash is %lu KB, the update needs %lu KB\n",
            (unsigned long) (size / 1024), (unsigned long) (NEEDED_FLASH / 1024));
        return false;
    }
    const char *version = upstream_GetFirmwareVersion(name);
    if (!version || strncmp(version, BOARD_PREFIX, strlen(BOARD_PREFIX)) != 0) {
        printf("Upgrade: %s is not a Neptuno+ firmware (version %s)\n",
            name, version ? version : "unknown");
        return false;
    }
    return true;
}

extern "C" {

unsigned char CheckFirmware(char *name) {
    if (!updateAllowed(name)) {
        Error = ERROR_INVALID_DATA;
        return 0;
    }
    return upstream_CheckFirmware(name);
}

void WriteFirmware(char *name) {
    // Checked again: never trust that CheckFirmware ran first
    if (!updateAllowed(name)) return;
    upstream_WriteFirmware(name);
}

char *GetFirmwareVersion(char *name) {
    return upstream_GetFirmwareVersion(name);
}

}
