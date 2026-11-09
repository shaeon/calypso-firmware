// Neptuno+ entry point. Same structure as src/main.cxx (Calypso), with the
// Neptuno+ hardware: no tape, no MIDI synth, no user button, joysticks
// through the shift register and the SD card on its own PIO-driven bus.
// When src/main.cxx changes upstream, check whether this file needs the same.

#include <cstdio>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "Configuration.h"
#include "SPIDevice.h"
#include "JTAG.h"
#include "SPISDCard.h"
#include "Service.h"
#include "PS2Keyboard.h"
#include "PS2Mouse.h"
#include "mist/MistService.h"
#include "USBService.h"
#include "Util.h"
#include "ShiftRegisterJoystick.h"
#ifdef NEPTUNO_DEBUG_UART_PIN
#include "PioUartStdio.h"
#endif

// Last: the MiST hardware header defines short macros that clash with the
// C++ standard headers pulled in above.
extern "C" {
#include "mist/hardware.h"
}

using namespace calypso;

// MiST SPI bus (user_io, data_io, OSD): hardware spi0
SPIDevice spi(spi0, Configuration::GPIO_SPI_SCK, Configuration::GPIO_SPI_MISO, Configuration::GPIO_SPI_MOSI);

// SD card: own bus, driven from a PIO (nullptr = PIO, see SPIDevice.cxx)
SPIDevice sdSpi(nullptr, Configuration::GPIO_SD_SCK, Configuration::GPIO_SD_MISO, Configuration::GPIO_SD_MOSI);
SPISDCard sdcard(sdSpi, Configuration::GPIO_SD_SEL, Configuration::GPIO_MIST_DMODE);

// FPGA configuration: the JTAG class of this port does Passive Serial
JTAG jtag(0, 0, 0, 0);

// Two DB9 joysticks through the shift register, on pio1 with the SD bus
ShiftRegisterJoystick joystick(pio1, Configuration::GPIO_JOY_LOAD,
    Configuration::GPIO_JOY_SCK, Configuration::GPIO_JOY_DATA);
static_assert(Configuration::GPIO_JOY_SCK == Configuration::GPIO_JOY_LOAD + 1,
    "joy_sr.pio drives LOAD and SCK as consecutive side-set pins");

uint8_t kbdBuffer[8];
CircularBuffer<uint8_t> kbdFifo(kbdBuffer, 8);
PIOContext kbdPio = {
    .sm = 0,
    .fifo = &kbdFifo
};
PS2Keyboard keyboard(kbdPio, kbdFifo, Configuration::GPIO_PS2_CLK1);

uint8_t mouseBuffer[8];
CircularBuffer<uint8_t> mouseFifo(mouseBuffer, 8);
PIOContext mousePio = {
    .sm = 1,
    .fifo = &mouseFifo
};
PS2Mouse mouse(mousePio, mouseFifo, Configuration::GPIO_PS2_CLK2);

PIOContext* pioContexts[] = {&kbdPio, &mousePio};

USBService usbService;

MistService mistService(Configuration::GPIO_MIST_USERIO, Configuration::GPIO_MIST_DATAIO,
    Configuration::GPIO_MIST_OSD, Configuration::GPIO_MIST_DMODE);

int main() {
    // Same clock as Calypso (PIO and SPI rates are derived from it)
    set_sys_clock_khz(250000, false);

    // DCLK and DATA0 must not float once the FPGA is configured, and after a
    // reboot of this MCU it may still hold a core (e.g. loaded through JTAG).
    // Drive them low before anything else, as the loader leaves them.
    gpio_init(Configuration::GPIO_FPGA_DCLK);
    gpio_put(Configuration::GPIO_FPGA_DCLK, 0);
    gpio_set_dir(Configuration::GPIO_FPGA_DCLK, GPIO_OUT);
    gpio_init(Configuration::GPIO_FPGA_DATA0);
    gpio_put(Configuration::GPIO_FPGA_DATA0, 0);
    gpio_set_dir(Configuration::GPIO_FPGA_DATA0, GPIO_OUT);

    // No UART here: its default pins are DCLK/DATA0 (see CMakeLists.txt)
    stdio_init_all();
#ifdef NEPTUNO_DEBUG_UART_PIN
    PioUartStdio::init(pio1, NEPTUNO_DEBUG_UART_PIN, 115200);
#endif

    printf("Neptuno+ firmware (calypso-firmware port)\n");
    printf("Reset: %s, development core kept: %s\n",
        watchdog_caused_reboot() ? "watchdog (MCUReset)" : "power-on/button",
        USB_LOAD_VAR == USB_LOAD_VALUE ? "yes" : "no");
    extern uint32_t neptuno_flash_size();
    printf("Flash: %lu KB (SD card update needs 16384 KB)\n",
        (unsigned long) (neptuno_flash_size() / 1024));
    printf("FPGA at boot: nCONFIG=%d nSTATUS=%d CONF_DONE=%d\n",
        gpio_get(Configuration::GPIO_FPGA_NCONFIG),
        gpio_get(Configuration::GPIO_FPGA_NSTATUS),
        gpio_get(Configuration::GPIO_FPGA_CONF_DONE));

    Service::registerService(&usbService);
    Service::registerService(&mistService);
    spi.init();
    if (!sdSpi.init()) {
        printf("Error initializing the SD card PIO SPI\n");
    }
    keyboard.init();
    mouse.init();

    printf("Initializing services...\n");
    for (int i = 0; i < Service::serviceCount; i++) {
        Service *s = Service::services[i];
        printf("Initializing service %s\n", s->name());
        if (!s->init()) {
            printf("Error initializing service %s", s->name());
        }
    }
    printf(" ...initialization done\n");
    printf("Neptuno+ firmware main loop starting\n");

    while (true) {
        for (int i = 0; i < Service::serviceCount; i++) {
            Service *s = Service::services[i];
            if (s->needsAttention()) {
                s->attention();
            }
        }
    }
}
