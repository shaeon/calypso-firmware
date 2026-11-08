#include "PioUartStdio.h"
#include "pico/stdio.h"
#include "pico/stdio/driver.h"
#include "pico/time.h"
#include "pio_uart_tx.pio.h"

using namespace calypso;

static PIO s_pio = nullptr;
static int s_sm = -1;
static uint32_t s_baud = 115200;

static void pio_uart_out_chars(const char *buf, int length) {
    if (s_sm < 0) return;
    for (int i = 0; i < length; i++) {
        pio_sm_put_blocking(s_pio, s_sm, (uint32_t) (uint8_t) buf[i]);
    }
}

static stdio_driver_t pio_uart_driver = {
    .out_chars = pio_uart_out_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = PICO_STDIO_DEFAULT_CRLF
#endif
};

bool PioUartStdio::init(PIO pio, uint8_t txPin, uint32_t baud) {
    if (s_sm >= 0) return true;
    if (!pio_can_add_program(pio, &pio_uart_tx_program)) return false;
    int sm = pio_claim_unused_sm(pio, false);
    if (sm < 0) return false;
    uint offset = pio_add_program(pio, &pio_uart_tx_program);
    pio_uart_tx_program_init(pio, sm, offset, txPin, baud);
    s_pio = pio;
    s_sm = sm;
    s_baud = baud;
    stdio_set_driver_enabled(&pio_uart_driver, true);
    return true;
}

void PioUartStdio::drain() {
    if (s_sm < 0) return;
    while (!pio_sm_is_tx_fifo_empty(s_pio, s_sm)) tight_loop_contents();
    // Last word still shifting out: 10 bits
    sleep_us(10 * 1000000 / s_baud + 50);
}
