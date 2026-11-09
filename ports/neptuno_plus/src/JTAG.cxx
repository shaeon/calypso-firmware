// Neptuno+ implementation of calypso::JTAG (header: src/core/JTAG.h,
// unchanged). Replaces src/core/JTAG.cxx in this build.
//
// The upstream FPGA loader (src/mist/fpga.cxx) drives the class through
// startProgram() / programChunk() / programPostamble() / endProgram(). On
// Neptuno+ the RP2040 is not on the FPGA JTAG chain: it configures the
// Cyclone IV in Passive Serial, so these methods implement that instead.
// The constructor arguments are ignored; the pins come from Configuration.h.
//
// JTAG mode: selecting an empty RBF file (e.g. JTAGMODE.RBF, 0 bytes, as in
// ZXMicroJack's firmware) resets the FPGA and leaves it waiting for a
// configuration, releases DCLK/DATA0 and waits up to 60 s for a core loaded
// through JTAG (USB Blaster). The JTAG load then always starts from a freshly
// reset FPGA with the firmware idle. See jtagModeWait().
//
// Passive Serial: the RBF goes LSB first on DATA0, latched on the rising edge
// of DCLK. There is no minimum DCLK frequency, only minimum setup, hold and
// pulse widths, so a bit-banged loop is safe (a few tens of MHz at 250 MHz
// clk_sys, below the 66 MHz device limit).

#include "JTAG.h"
#include "Configuration.h"
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"
#include "pico/time.h"
#include <cstdio>

extern "C" {
#include "spi.h"
}

using namespace calypso;

namespace {
    constexpr uint8_t DCLK      = Configuration::GPIO_FPGA_DCLK;
    constexpr uint8_t DATA0     = Configuration::GPIO_FPGA_DATA0;
    constexpr uint8_t NCONFIG   = Configuration::GPIO_FPGA_NCONFIG;
    constexpr uint8_t NSTATUS   = Configuration::GPIO_FPGA_NSTATUS;
    constexpr uint8_t CONF_DONE = Configuration::GPIO_FPGA_CONF_DONE;

    constexpr uint32_t STATUS_TIMEOUT_US = 100000;
    constexpr uint32_t MAX_INIT_CLOCKS = 4096;
    constexpr uint32_t EXTRA_INIT_CLOCKS = 64;

    bool s_error = false;
    uint32_t s_bytes = 0;

    bool waitStatus(bool level) {
        const absolute_time_t deadline = make_timeout_time_us(STATUS_TIMEOUT_US);
        while (gpio_get(NSTATUS) != level) {
            if (time_reached(deadline)) return false;
            tight_loop_contents();
        }
        return true;
    }

    // Setup (tDSU) and DCLK high/low times are minimums: two cycles of
    // padding at 250 MHz keep every phase above 8 ns.
    #define PS_PAD() __asm volatile ("nop\n nop\n")

    void clockByte(uint8_t value) {
        const uint32_t dclkMask = 1u << DCLK;
        const uint32_t dataMask = 1u << DATA0;
        for (int bit = 0; bit < 8; bit++) {
            if (value & 1) {
                sio_hw->gpio_set = dataMask;
            } else {
                sio_hw->gpio_clr = dataMask;
            }
            value >>= 1;
            PS_PAD();
            sio_hw->gpio_set = dclkMask;
            PS_PAD();
            sio_hw->gpio_clr = dclkMask;
        }
    }
}

JTAG::JTAG(uint8_t tdo, uint8_t tdi, uint8_t tck, uint8_t tms):
    m_tdo(tdo), m_tdi(tdi), m_tck(tck), m_tms(tms) {}

int JTAG::scan() {
    return 1;
}

void JTAG::startProgram() {
    s_error = false;
    s_bytes = 0;

    // nSTATUS and CONF_DONE are open drain with external pull-ups: inputs only
    gpio_init(NSTATUS);
    gpio_set_dir(NSTATUS, GPIO_IN);
    gpio_init(CONF_DONE);
    gpio_set_dir(CONF_DONE, GPIO_IN);

    gpio_init(DCLK);
    gpio_put(DCLK, 0);
    gpio_set_dir(DCLK, GPIO_OUT);
    gpio_init(DATA0);
    gpio_put(DATA0, 0);
    gpio_set_dir(DATA0, GPIO_OUT);

    gpio_init(NCONFIG);
    gpio_put(NCONFIG, 1);
    gpio_set_dir(NCONFIG, GPIO_OUT);

    printf("PS: begin, nSTATUS=%d CONF_DONE=%d\n", gpio_get(NSTATUS), gpio_get(CONF_DONE));

    // nCONFIG low starts a reconfiguration; the device pulls nSTATUS low
    gpio_put(NCONFIG, 0);
    if (!waitStatus(false)) {
        printf("PS: nSTATUS did not go low\n");
        gpio_put(NCONFIG, 1);
        s_error = true;
        return;
    }
    sleep_us(10);                           // tCFG >= 500 ns
    gpio_put(NCONFIG, 1);

    // The device releases nSTATUS when it is ready for data
    if (!waitStatus(true)) {
        printf("PS: nSTATUS did not go high\n");
        s_error = true;
        return;
    }
    sleep_us(5);                            // tST2CK >= 2 us
}

void JTAG::programChunk(uint8_t *data, uint16_t length) {
    if (s_error) return;
    for (uint16_t i = 0; i < length; i++) {
        clockByte(data[i]);
    }
    s_bytes += length;
    // A CRC error is flagged by the device pulling nSTATUS low
    if (!gpio_get(NSTATUS)) {
        printf("PS: nSTATUS low after %lu bytes (CRC error?)\n", (unsigned long) s_bytes);
        s_error = true;
    }
}

void JTAG::programPostamble() {
    // An empty file is the JTAG mode request: no clocks into the waiting device
    if (s_error || s_bytes == 0) return;
    // Keep clocking until CONF_DONE rises, then a few more cycles for the
    // device to leave initialisation and enter user mode
    uint32_t clocks = 0;
    while (!gpio_get(CONF_DONE) && clocks < MAX_INIT_CLOCKS) {
        clockByte(0xff);
        clocks += 8;
    }
    for (uint32_t i = 0; i < EXTRA_INIT_CLOCKS; i += 8) {
        clockByte(0xff);
    }
}

namespace {
    constexpr uint32_t JTAG_MODE_TIMEOUT_MS = 60000;
    constexpr uint32_t JTAG_MODE_POLL_MS = 50;
    constexpr int JTAG_MODE_STABLE_READS = 10;      // 0.5 s of a valid core id

    // Waits for a core loaded through JTAG. Returns when a MiST core answers
    // with a stable id (the upstream caller, fpga_init(), then detects and
    // initialises it as usual) or after the timeout.
    void jtagModeWait() {
        // Same as ZXMicroJack's JTAGMODE: the device has just been reset by
        // startProgram() and waits for data; DCLK and DATA0 are released.
        gpio_init(DCLK);
        gpio_init(DATA0);

        printf("JTAG mode: FPGA reset, waiting up to %lu s for a core loaded through JTAG\n",
            (unsigned long) (JTAG_MODE_TIMEOUT_MS / 1000));
        printf("JTAG mode: nSTATUS=%d CONF_DONE=%d\n", gpio_get(NSTATUS), gpio_get(CONF_DONE));

        const absolute_time_t deadline = make_timeout_time_ms(JTAG_MODE_TIMEOUT_MS);
        int lastConfDone = gpio_get(CONF_DONE);
        int validReads = 0;
        unsigned char lastId = 0;
        while (!time_reached(deadline)) {
            sleep_ms(JTAG_MODE_POLL_MS);

            const int confDone = gpio_get(CONF_DONE);
            if (confDone != lastConfDone) {
                printf("JTAG mode: CONF_DONE=%d\n", confDone);
                lastConfDone = confDone;
            }

            EnableIO();
            const unsigned char id = SPI(0xff);
            DisableIO();
            if ((id & 0xe0) == 0xa0 && (validReads == 0 || id == lastId)) {
                lastId = id;
                if (++validReads >= JTAG_MODE_STABLE_READS) {
                    printf("JTAG mode: core found (id %02x)\n", id);
                    return;
                }
            } else {
                validReads = 0;
            }
        }
        printf("JTAG mode: no core after %lu s\n", (unsigned long) (JTAG_MODE_TIMEOUT_MS / 1000));
    }
}

void JTAG::endProgram() {
    if (!s_error && s_bytes == 0) {
        jtagModeWait();
        return;
    }
    const bool ok = !s_error && gpio_get(CONF_DONE) && gpio_get(NSTATUS);
    // DCLK and DATA0 must not float after configuration: leave them driven low
    gpio_put(DCLK, 0);
    gpio_put(DATA0, 0);
    printf("PS: %s, %lu bytes (nSTATUS=%d CONF_DONE=%d)\n", ok ? "configured" : "FAILED",
        (unsigned long) s_bytes, gpio_get(NSTATUS), gpio_get(CONF_DONE));
}
