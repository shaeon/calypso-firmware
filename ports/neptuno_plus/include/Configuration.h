#ifndef CONFIGURATION_H
#define CONFIGURATION_H

// Neptuno+ pin map. This header shadows src/core/Configuration.h for the
// upstream sources that include it through the include path (mist/spi.cxx)
// and for the files of this port.
//
// Taken from ZXMicroJack's mist-firmware-rp2040 (drivers/pins.h, "mistusb"
// target: ALTERA_FPGA + QMTECH + SD_DIRECT_MODE_GPIO=22) and checked against
// the middleboard schematic (edge2vgadb9midi).
//
// The second column is the middleboard SIGNAL NAME, which carries the FPGA
// pin of the board the middleboard was first drawn for (484-pin package).
// It is NOT the pin on the Neptuno+ FPGA (EP4CGX150DF27, 672 pins): there the
// same connector lines land on other pins, given by the Neptuno+ pinout.
//
//  GP  Signal Function
//  --  -----  -----------------------------------------------------------
//   0  L12    FPGA DCLK  (Passive Serial)   - NOT the debug UART here
//   1  R22    FPGA DATA0 (Passive Serial)   - NOT the debug UART here
//   2  AA8    MIDI synth I2S BCLK  -> FPGA
//   3  Y8     MIDI synth I2S LRCLK -> FPGA
//   4  AB8    MIDI synth I2S DATA  -> FPGA
//   5  AB7    (unused: MIDI bytes come from the core through user_io)
//   6  M5     SD MISO  \
//   7  L4     SD MOSI   |  SD card, shared with the FPGA (direct mode)
//   8  M6     SD SCK    |
//   9  L5     SD CS    /
//  10  -      FPGA nCONFIG (also the board reset line)
//  11  U1     PS/2 keyboard CLK  \  shared with the FPGA
//  12  T1     PS/2 keyboard DATA /
//  13  -      FPGA nSTATUS
//  14  P4     PS/2 mouse CLK     \  shared with the FPGA
//  15  N5     PS/2 mouse DATA    /
//  16  V8     MiST SPI MISO (spi0 RX)
//  17  W9     MiST SS user_io
//  18  W7     MiST SPI SCK  (spi0 SCK)
//  19  V7     MiST SPI MOSI (spi0 TX)
//  20  W5     MiST SS data_io
//  21  W6     MiST SS OSD
//  22  W4     MiST direct mode (SS4)
//  23  Y6     optional debug UART TX (-DNEPTUNO_DEBUG_UART_PIN=23)
//  24  Y9     (unused)
//  25  -      FPGA CONF_DONE  (input only!)
//  26  AA4    joystick shift register LOAD \
//  27  AB5    joystick shift register SCK   |  shared with the FPGA
//  28  AA6    joystick shift register DATA /
//  29  -      not connected

#include <inttypes.h>

class Configuration {
public:
    // MiST SPI (hardware spi0)
    static constexpr uint8_t GPIO_SPI_SCK  = 18;
    static constexpr uint8_t GPIO_SPI_MOSI = 19;
    static constexpr uint8_t GPIO_SPI_MISO = 16;

    static constexpr uint8_t GPIO_MIST_USERIO = 17;
    static constexpr uint8_t GPIO_MIST_DATAIO = 20;
    static constexpr uint8_t GPIO_MIST_OSD    = 21;
    static constexpr uint8_t GPIO_MIST_DMODE  = 22;

    // SD card (PIO SPI)
    static constexpr uint8_t GPIO_SD_MISO = 6;
    static constexpr uint8_t GPIO_SD_MOSI = 7;
    static constexpr uint8_t GPIO_SD_SCK  = 8;
    static constexpr uint8_t GPIO_SD_SEL  = 9;

    // FPGA Passive Serial configuration (Cyclone IV)
    static constexpr uint8_t GPIO_FPGA_DCLK      = 0;
    static constexpr uint8_t GPIO_FPGA_DATA0     = 1;
    static constexpr uint8_t GPIO_FPGA_NCONFIG   = 10;
    static constexpr uint8_t GPIO_FPGA_NSTATUS   = 13;
    static constexpr uint8_t GPIO_FPGA_CONF_DONE = 25;

    // PS/2 (DATA must be CLK + 1 for ps2.pio)
    static constexpr uint8_t GPIO_PS2_CLK1  = 11;
    static constexpr uint8_t GPIO_PS2_DATA1 = 12;
    static constexpr uint8_t GPIO_PS2_CLK2  = 14;
    static constexpr uint8_t GPIO_PS2_DATA2 = 15;

    // MIDI synthesizer output, standard I2S as on Calypso (BCLK = base,
    // LRCLK = base + 1, as pico_audio_i2s requires). On pio1, state machine 3:
    // the library claims a fixed one, and the SD bus, the joysticks and the
    // optional debug console take the lower ones (claimed at boot).
    static constexpr uint32_t MIDI_SAMPLE_FREQ = 24000;
    static constexpr uint8_t GPIO_I2S_BASE = 2;     // takes GP2 (BCLK) and GP3 (LRCLK)
    static constexpr uint8_t GPIO_I2S_DATA = 4;
    static constexpr uint8_t I2S_PIO_SM = 3;

    // Joystick shift register (SCK must be LOAD + 1)
    static constexpr uint8_t GPIO_JOY_LOAD = 26;
    static constexpr uint8_t GPIO_JOY_SCK  = 27;
    static constexpr uint8_t GPIO_JOY_DATA = 28;
};

#endif //CONFIGURATION_H
