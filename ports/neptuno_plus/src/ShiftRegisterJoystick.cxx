#include "ShiftRegisterJoystick.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "pico/time.h"
#include "joy_sr.pio.h"
#include <cstdio>

using namespace calypso;

ShiftRegisterJoystick::ShiftRegisterJoystick(PIO pio, uint8_t load, uint8_t sck, uint8_t data):
    m_pio(pio),
    m_sm(-1),
    m_offset(0),
    m_load(load),
    m_data(data),
    m_pending(false),
    m_lastRequest(0),
    m_state{0, 0} {
    // The PIO program drives LOAD and SCK as consecutive side-set pins
    (void) sck;
}

bool ShiftRegisterJoystick::init() {
    if (m_sm < 0) {
        if (!pio_can_add_program(m_pio, &joy_sr_program)) {
            printf("Joystick: no room for the PIO program\n");
            return false;
        }
        m_sm = pio_claim_unused_sm(m_pio, false);
        if (m_sm < 0) {
            printf("Joystick: no free state machine\n");
            return false;
        }
        m_offset = pio_add_program(m_pio, &joy_sr_program);
    }
    gpio_pull_up(m_data);
    const float clkdiv = (float) clock_get_hz(clk_sys) / PIO_FREQ_HZ;
    joy_sr_program_init(m_pio, m_sm, m_offset, clkdiv, m_load, m_data);
    pio_sm_clear_fifos(m_pio, m_sm);
    m_pending = false;
    m_state[0] = m_state[1] = 0;
    return true;
}

void ShiftRegisterJoystick::release() {
    if (m_sm < 0) return;
    pio_sm_set_enabled(m_pio, m_sm, false);
    pio_sm_clear_fifos(m_pio, m_sm);
    const uint8_t pins[] = {m_load, (uint8_t) (m_load + 1), m_data};
    for (uint8_t pin : pins) {
        gpio_init(pin);
        gpio_disable_pulls(pin);
    }
    m_pending = false;
}

// Byte layout per joystick (active low on the wire): bit 7 UP, 6 DOWN,
// 5 LEFT, 4 RIGHT, 3 FIRE1, 2 FIRE2. All six asserted means nothing is
// plugged in (same rule as ZXMicroJack's db9_translate).
static uint8_t translate(uint8_t d) {
    uint8_t ret = (d >> 4) | ((d & 0x08) << 1) | ((d & 0x04) << 3);
    return ret == 0x3f ? 0x00 : ret;
}

void ShiftRegisterJoystick::decode(uint32_t raw, uint8_t state[2]) {
    const uint32_t d = ~raw;
    state[0] = translate((d >> 16) & 0xff);
    state[1] = translate((d >> 8) & 0xff);
}

void ShiftRegisterJoystick::poll() {
    if (m_sm < 0) return;
    while (!pio_sm_is_rx_fifo_empty(m_pio, m_sm)) {
        decode(pio_sm_get(m_pio, m_sm), m_state);
        m_pending = false;
    }
    const uint64_t now = time_us_64();
    // A request that never completed (should not happen) is retried
    if (m_pending && now - m_lastRequest < 50000) return;
    if (now - m_lastRequest >= READ_INTERVAL_US && !pio_sm_is_tx_fifo_full(m_pio, m_sm)) {
        pio_sm_put(m_pio, m_sm, 0);
        m_pending = true;
        m_lastRequest = now;
    }
}

uint8_t ShiftRegisterJoystick::read(uint8_t index) {
    if (index == 0) {
        poll();
    }
    return index < 2 ? m_state[index] : 0;
}
