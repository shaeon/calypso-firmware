#ifndef JOYSTICK_SOURCE_H
#define JOYSTICK_SOURCE_H

#include <inttypes.h>

namespace calypso {
    // Source of the DB9 joystick state in MiST format (RIGHT, LEFT, DOWN, UP,
    // FIRE1, FIRE2 in bits 0..5, active high).
    class JoystickSource {
    public:
        virtual ~JoystickSource() = default;
        virtual bool init() = 0;
        // Number of DB9 ports this source reports
        virtual uint8_t count() const = 0;
        virtual uint8_t read(uint8_t index) = 0;
    };

    // For boards without DB9 ports on the MCU side.
    class NullJoystick : public JoystickSource {
    public:
        bool init() override { return true; }
        uint8_t count() const override { return 0; }
        uint8_t read(uint8_t index) override { return 0; }
    };
}

#endif //JOYSTICK_SOURCE_H
