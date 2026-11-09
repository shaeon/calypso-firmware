// Recovery when no MiST core is recognised (Neptuno+).
//
// Upstream mist_loop() (src/mist/compat.c) switches to LEGACY_MODE when the
// core type is CORE_TYPE_UNKNOWN and from then on stops calling
// user_io_poll(), the function that notices a core loaded through JTAG. The
// core type is only read again by user_io_detect_core_type(), which nothing
// calls in that mode, so the firmware stays there for good: no OSD, nothing.
//
// It happens when loading cores through JTAG: user_io_poll() restarts the MCU
// (MCUReset) on the first read that looks like a valid core id. With the FPGA
// still reconfiguring that read can be noise, the MCU boots before the JTAG
// load ends, fpga_init() gives up after 100 ms and the core type is left as
// unknown.
//
// compat.c is built unchanged with mist_loop renamed to upstream_mist_loop
// (see CMakeLists.txt). This wrapper keeps reading the core id while the type
// is unknown; once a valid MiST id is seen several times in a row it does what
// upstream does for a JTAG load: keep the core (USB_LOAD_VAR) and restart.

#include <cstdio>
#include "pico/time.h"

extern "C" {
#include "spi.h"
#include "compat.h"
#include "mist/hardware.h"

int upstream_mist_loop();
unsigned char user_io_core_type();
}

namespace {
    constexpr unsigned char CORE_TYPE_UNKNOWN = 0x55;   // mist-firmware user_io.h
    constexpr uint64_t POLL_INTERVAL_US = 50000;
    constexpr int STABLE_READS = 10;                    // 0.5 s of valid ids

    uint64_t s_nextPoll = 0;
    int s_validReads = 0;
    unsigned char s_lastId = 0;
    bool s_announced = false;

    void watchForCore() {
        const uint64_t now = time_us_64();
        if (now < s_nextPoll) return;
        s_nextPoll = now + POLL_INTERVAL_US;

        EnableIO();
        const unsigned char id = SPI(0xff);
        DisableIO();

        if (!s_announced) {
            printf("CoreWatch: no MiST core recognised, waiting for one (id %02x)\n", id);
            s_announced = true;
        }

        // Same test upstream uses for a valid core id; it must also be stable
        if ((id & 0xe0) == 0xa0 && (s_validReads == 0 || id == s_lastId)) {
            s_lastId = id;
            if (++s_validReads >= STABLE_READS) {
                printf("CoreWatch: MiST core found (id %02x), restarting to adopt it\n", id);
                USB_LOAD_VAR = USB_LOAD_VALUE;
                MCUReset();
                for (;;);
            }
        } else {
            s_validReads = 0;
        }
    }
}

extern "C" int mist_loop() {
    const int result = upstream_mist_loop();
    if (user_io_core_type() == CORE_TYPE_UNKNOWN) {
        watchForCore();
    } else {
        s_validReads = 0;
        s_announced = false;
    }
    return result;
}
