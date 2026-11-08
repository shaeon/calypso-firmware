#ifndef SHIFT_REGISTER_JOYSTICK_H
#define SHIFT_REGISTER_JOYSTICK_H

#include <inttypes.h>
#include "hardware/pio.h"
#include "JoystickSource.h"

namespace calypso {
    // Two DB9 joysticks read through the Neptuno shift register chain
    // (LOAD, SCK, DATA), driven from a PIO state machine. Same frame layout
    // and decoding as ZXMicroJack's mist-firmware-rp2040 (jamma.c, DB9 mode).
    //
    // The lines are shared with the FPGA: MiST cores must leave them as
    // inputs. release() hands them back for native cores.
    class ShiftRegisterJoystick : public JoystickSource {
    private:
        static constexpr uint32_t PIO_FREQ_HZ = 4000000;   // 1 MHz SCK
        static constexpr uint32_t READ_INTERVAL_US = 2000;

        PIO m_pio;
        int m_sm;
        uint m_offset;
        uint8_t m_load;     // SCK must be m_load + 1
        uint8_t m_data;
        bool m_pending;
        uint64_t m_lastRequest;
        uint8_t m_state[2];

        void poll();
    public:
        ShiftRegisterJoystick(PIO pio, uint8_t load, uint8_t sck, uint8_t data);
        bool init() override;
        uint8_t count() const override { return 2; }
        uint8_t read(uint8_t index) override;
        void release();
        // Decoding of one RX word, exposed for testing
        static void decode(uint32_t raw, uint8_t state[2]);
    };
}

#endif //SHIFT_REGISTER_JOYSTICK_H
