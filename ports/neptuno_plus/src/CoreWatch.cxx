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
// Adopting a JTAG-loaded core without restarting the MCU: upstream calls
// MCUReset() to pick up a core loaded through JTAG. Restarting the RP2040
// puts every pin back to its reset state (input with pull-down), nCONFIG
// included, until main() sets them again. On Neptuno+ that window resets the
// FPGA: the second JTAG load "configures" but the core never starts.
// ZXMicroJack's firmware does not restart there (its MCUReset() is empty,
// so it hangs in the for(;;) that follows) and the FPGA keeps the core.
// Here MCUReset() is replaced: when USB_LOAD_VAR asks to keep the core, it
// jumps back to mist_loop() and runs the same adoption sequence as the boot
// (detect, development ARC, fpga_init) without touching the pins. Any other
// MCUReset() (e.g. the MiST reset key combo) still restarts the MCU.
//
// compat.c is built unchanged with mist_loop renamed to upstream_mist_loop
// (see CMakeLists.txt). This wrapper keeps reading the core id while the type
// is unknown; once a valid MiST id is seen several times in a row it does what
// upstream does for a JTAG load: keep the core (USB_LOAD_VAR) and restart.

#include <cstdio>
#include <csetjmp>
#include <cstring>
#include "pico/time.h"
#include "hardware/gpio.h"
#include "Configuration.h"

extern "C" {
#include "spi.h"
#include "compat.h"
#include "mist/hardware.h"

int upstream_mist_loop();
void upstream_MCUReset();
unsigned char user_io_core_type();
void user_io_detect_core_type();
void user_io_reset();
char user_io_create_config_name(char *s, const char *ext, char flags);
void user_io_set_core_mod(int64_t mod);
int64_t arc_open(const char *fname);
char *arc_get_rbfname();
unsigned char fpga_init(const char *name);
void set_legacy_mode(uint8_t mode);
void DB9SetLegacy(uint8_t on);
void StateReset();
void CloseMenu();
}

namespace {
    constexpr unsigned char CORE_TYPE_UNKNOWN = 0x55;   // mist-firmware user_io.h
    constexpr uint64_t POLL_INTERVAL_US = 50000;
    constexpr int STABLE_READS = 10;                    // 0.5 s of valid ids

    uint64_t s_nextPoll = 0;
    int s_validReads = 0;
    unsigned char s_lastId = 0;
    bool s_announced = false;
    bool s_adoptRequested = false;

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
                printf("CoreWatch: MiST core found (id %02x)\n", id);
                s_validReads = 0;
                s_adoptRequested = true;
                return;
            }
        } else {
            s_validReads = 0;
        }
    }
}

namespace {
    // Diagnostics: log every change of the FPGA configuration lines and of
    // the core id, with a timestamp. A JTAG load shows up as CONF_DONE going
    // low and back high; the core id shows what user_io sees meanwhile.
    constexpr uint64_t MONITOR_INTERVAL_US = 2000;
    uint64_t s_nextMonitor = 0;
    int s_lastPins = -1;
    int s_lastCoreId = -1;
    unsigned char s_lastType = 0;

    int readPins() {
        return (gpio_get(Configuration::GPIO_FPGA_NCONFIG) ? 4 : 0) |
               (gpio_get(Configuration::GPIO_FPGA_NSTATUS) ? 2 : 0) |
               (gpio_get(Configuration::GPIO_FPGA_CONF_DONE) ? 1 : 0);
    }

    void monitor() {
        const uint64_t now = time_us_64();
        if (now < s_nextMonitor) return;
        s_nextMonitor = now + MONITOR_INTERVAL_US;
        const unsigned long ms = (unsigned long) (now / 1000);

        const int pins = readPins();
        if (pins != s_lastPins) {
            printf("[%lu ms] FPGA lines: nCONFIG=%d nSTATUS=%d CONF_DONE=%d\n",
                ms, (pins >> 2) & 1, (pins >> 1) & 1, pins & 1);
            s_lastPins = pins;
        }

        EnableIO();
        const int id = SPI(0xff);
        DisableIO();
        if (id != s_lastCoreId) {
            printf("[%lu ms] core id read: %02x\n", ms, id);
            s_lastCoreId = id;
        }

        const unsigned char type = user_io_core_type();
        if (type != s_lastType) {
            printf("[%lu ms] firmware core type: %02x\n", ms, type);
            s_lastType = type;
        }
    }
}

namespace {
    jmp_buf s_loopJump;
    volatile bool s_jumpValid = false;
    char s_configName[256];

    // Same sequence mist_init() (compat.c) runs at boot when USB_LOAD_VAR is
    // set, plus what the menu does after loading a core.
    void adoptCore() {
        printf("Adopting the core in the FPGA without restarting the MCU\n");
        USB_LOAD_VAR = USB_LOAD_VALUE;
        user_io_reset();

        int64_t mod = -1;
        user_io_detect_core_type();
        if (user_io_core_type() != CORE_TYPE_UNKNOWN &&
            !user_io_create_config_name(s_configName, "ARC", 1 /* CONFIG_ROOT */)) {
            printf("Load development ARC: %s\n", s_configName);
            mod = (char) arc_open(s_configName);   // same narrowing as compat.c
        }
        // USB_LOAD_VAR is set, so fpga_init() does not configure the FPGA: it
        // detects and initialises the core that is there, and clears the flag.
        if (mod < 0 || !strlen(arc_get_rbfname())) {
            fpga_init(nullptr);
        } else {
            user_io_set_core_mod(mod);
            strncpy(s_configName, arc_get_rbfname(), sizeof(s_configName) - 5);
            s_configName[sizeof(s_configName) - 5] = 0;
            strcat(s_configName, ".RBF");
            fpga_init(s_configName);
        }
        DB9SetLegacy(0);
        set_legacy_mode(user_io_core_type() == CORE_TYPE_UNKNOWN ? LEGACY_MODE : MIST_MODE);
        StateReset();
        CloseMenu();
        printf("Core adopted, type %02x\n", user_io_core_type());
    }
}

extern "C" void MCUReset() {
    if (USB_LOAD_VAR == USB_LOAD_VALUE && s_jumpValid) {
        printf("MCUReset: new core loaded through JTAG, adopting it without restarting\n");
        s_jumpValid = false;
        longjmp(s_loopJump, 1);
    }
    upstream_MCUReset();
    for (;;);
}

extern "C" int mist_loop() {
    if (setjmp(s_loopJump)) {
        // Back from MCUReset() inside upstream_mist_loop()
        adoptCore();
        s_validReads = 0;
        s_announced = false;
        return 0;
    }
#ifdef NEPTUNO_DEBUG_UART_PIN
    monitor();
#endif
    s_jumpValid = true;
    const int result = upstream_mist_loop();
    s_jumpValid = false;
    if (user_io_core_type() == CORE_TYPE_UNKNOWN) {
        watchForCore();
        if (s_adoptRequested) {
            s_adoptRequested = false;
            adoptCore();
            s_announced = false;
        }
    } else {
        s_validReads = 0;
        s_announced = false;
    }
    return result;
}
