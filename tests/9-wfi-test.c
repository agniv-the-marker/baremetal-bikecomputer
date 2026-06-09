// enable wfi (wait for interrupt) functionality
// pass: blinks the ACTLED on each wake so you can SEE the core waking at the timer rate while halted.

#include "rpi.h"
#include "cycle-count.h"
#include "power.h"

enum { PERIOD_US = 250000 }; // ~0.25s -> ~4 wakes/s -> LED ~2 Hz

void notmain(void) {
    cycle_cnt_init();
    gpio_set_output(47);

    // baseline: busy loop for ~1s, count active cycles
    uint32_t c0 = cycle_cnt_read();
    uint32_t start = timer_get_usec();
    volatile unsigned it = 0;
    while (timer_get_usec() - start < 1000000) it++;
    uint32_t busy = cycle_cnt_read() - c0;
    printk("busy: ~%d active cyc/s (CPU full tilt), %d iters\n", busy, it);

    // WFI loop for ~1s, sleep between timer wakeups
    power_wake_timer_init(PERIOD_US);
    uint32_t awake = 0;
    unsigned wakes = 0;
    start = timer_get_usec();
    while (timer_get_usec() - start < 1000000) {
        wfi(); // <-- core halts here until timer
        uint32_t a = cycle_cnt_read();
        power_wake_ack();
        gpio_write(47, wakes & 1); // cycle the light
        awake += cycle_cnt_read() - a;
        wakes++;
    }
    printk("wfi:  %d wakes/s, ~%d awake cyc/s (of ~250M) -> CPU mostly halted\n",
           wakes, awake);
    printk("wfi-test: done (LED should have blinked ~2 Hz)\n");
    while (1) {}
}
