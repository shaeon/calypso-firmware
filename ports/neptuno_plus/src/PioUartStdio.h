#ifndef PIO_UART_STDIO_H
#define PIO_UART_STDIO_H

#include <inttypes.h>
#include "hardware/pio.h"

namespace calypso {
    // Transmit-only debug console on a PIO state machine, registered as a
    // pico stdio driver. For boards whose default UART pins are taken
    // (Neptuno+: GP0/GP1 are the FPGA DCLK/DATA0).
    class PioUartStdio {
    public:
        static bool init(PIO pio, uint8_t txPin, uint32_t baud);
        // Wait until everything queued has left the pin
        static void drain();
    };
}

#endif //PIO_UART_STDIO_H
