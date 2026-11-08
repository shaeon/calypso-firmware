// Neptuno+ implementation of calypso::SPIDevice (header: src/core/SPIDevice.h,
// unchanged). Replaces src/core/SPIDevice.cxx in this build.
//
// - With a hardware SPI block (the MiST bus on spi0) it behaves exactly like
//   the original.
// - Constructed with nullptr as the SPI block it drives the bus from a PIO
//   state machine: the Neptuno+ SD card pins (MISO GP6, MOSI GP7, SCK GP8)
//   do not match any RP2040 hardware SPI. SPI mode 0, which SD cards accept
//   as well as mode 3. Only one PIO bus is supported.

#include "SPIDevice.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include <cstdio>
#include "sd_spi.pio.h"

using namespace calypso;

// pio0 is fully used by the PS/2 program
static PIO const s_pio = pio1;
static int s_sm = -1;
static uint s_offset = 0;

static float clkdivFor(uint32_t baudrate) {
    // Four PIO cycles per bit; round up so SCK never exceeds the request
    const uint32_t sys = clock_get_hz(clk_sys);
    uint32_t div256 = (uint32_t) (((uint64_t) sys * 256 + (4ull * baudrate) - 1) / (4ull * baudrate));
    if (div256 < 256) div256 = 256;
    return div256 / 256.0f;
}

SPIDevice::SPIDevice(spi_inst_t* spiPeripheral, uint8_t sck, uint8_t miso, uint8_t mosi):
    m_spiPeripheral(spiPeripheral),
    m_sck(sck),
    m_miso(miso),
    m_mosi(mosi),
    m_baudrate(0) {}

bool SPIDevice::init() {
    if (m_spiPeripheral) {
        gpio_init(m_sck);
        gpio_set_function(m_sck, GPIO_FUNC_SPI);
        gpio_init(m_miso);
        gpio_set_function(m_miso, GPIO_FUNC_SPI);
        gpio_init(m_mosi);
        gpio_set_function(m_mosi, GPIO_FUNC_SPI);
        gpio_set_drive_strength(m_sck, GPIO_DRIVE_STRENGTH_12MA);
        gpio_set_drive_strength(m_miso, GPIO_DRIVE_STRENGTH_12MA);
        gpio_set_drive_strength(m_mosi, GPIO_DRIVE_STRENGTH_12MA);

        spi_init(m_spiPeripheral, DEFAULT_BAUDRATE);
        spi_set_format(m_spiPeripheral, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
        m_baudrate = DEFAULT_BAUDRATE;
        return true;
    }

    if (s_sm < 0) {
        if (!pio_can_add_program(s_pio, &sd_spi_program)) {
            printf("SPIDevice: no room for the PIO SPI program\n");
            return false;
        }
        s_sm = pio_claim_unused_sm(s_pio, false);
        if (s_sm < 0) {
            printf("SPIDevice: no free state machine\n");
            return false;
        }
        s_offset = pio_add_program(s_pio, &sd_spi_program);
    }
    gpio_pull_up(m_miso);
    gpio_set_drive_strength(m_sck, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(m_mosi, GPIO_DRIVE_STRENGTH_12MA);
    sd_spi_program_init(s_pio, s_sm, s_offset, clkdivFor(DEFAULT_BAUDRATE), m_sck, m_mosi, m_miso);
    m_baudrate = DEFAULT_BAUDRATE;
    return true;
}

void SPIDevice::setBaudrate(uint32_t speed) {
    if (m_spiPeripheral) {
        spi_set_baudrate(m_spiPeripheral, speed);
    } else if (s_sm >= 0) {
        // Only called between transfers, with the SM stalled on an empty FIFO
        pio_sm_set_clkdiv(s_pio, s_sm, clkdivFor(speed));
        pio_sm_clkdiv_restart(s_pio, s_sm);
    }
    m_baudrate = speed;
}

uint32_t SPIDevice::baudrate() const {
    return m_baudrate;
}

// Every byte out also clocks one in, and autopush stalls the SM when the RX
// FIFO is full, so the RX side is always drained.
static void pioTransfer(const uint8_t *src, uint8_t fill, uint8_t *dst, size_t len) {
    io_rw_8 *txfifo = (io_rw_8 *) &s_pio->txf[s_sm];
    io_rw_8 *rxfifo = (io_rw_8 *) &s_pio->rxf[s_sm];
    size_t txRemain = len;
    size_t rxRemain = len;
    while (txRemain || rxRemain) {
        if (txRemain && !pio_sm_is_tx_fifo_full(s_pio, s_sm)) {
            *txfifo = src ? *src++ : fill;
            --txRemain;
        }
        if (rxRemain && !pio_sm_is_rx_fifo_empty(s_pio, s_sm)) {
            uint8_t value = *rxfifo;
            if (dst) *dst++ = value;
            --rxRemain;
        }
    }
}

uint8_t SPIDevice::byte(uint8_t value) {
    uint8_t readValue;
    if (m_spiPeripheral) {
        spi_write_read_blocking(m_spiPeripheral, &value, &readValue, 1);
    } else {
        pioTransfer(&value, 0xff, &readValue, 1);
    }
    return readValue;
}

size_t SPIDevice::send(const uint8_t *buffer, size_t len) {
    if (m_spiPeripheral) {
        return spi_write_blocking(m_spiPeripheral, buffer, len);
    }
    pioTransfer(buffer, 0xff, nullptr, len);
    return len;
}

size_t SPIDevice::recv(uint8_t *buffer, size_t len) {
    if (m_spiPeripheral) {
        return spi_read_blocking(m_spiPeripheral, 0xff, buffer, len);
    }
    pioTransfer(nullptr, 0xff, buffer, len);
    return len;
}

size_t SPIDevice::sendrecv(const uint8_t *sendBuffer, uint8_t *recvBuffer, size_t len) {
    if (m_spiPeripheral) {
        return spi_write_read_blocking(m_spiPeripheral, sendBuffer, recvBuffer, len);
    }
    pioTransfer(sendBuffer, 0xff, recvBuffer, len);
    return len;
}

bool SPIDevice::isBusy() {
    // PIO transfers are blocking and wait for the last byte received
    return m_spiPeripheral ? spi_is_busy(m_spiPeripheral) : false;
}
