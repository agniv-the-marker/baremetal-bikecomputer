#ifndef __POWER_H__
#define __POWER_H__

// low power helpers! on arm1176 wfi wakes when interrupts are asserted
// even if cpsr irqs are masked! so halt the core and wake on periodic arm timer interrupts.

#include "rpi.h"

// halt the core until a wakeup event (ARM1176: mcr p15,0,Rd,c7,c0,4).
static inline void wfi(void) {
    asm volatile("mcr p15, 0, %0, c7, c0, 4" :: "r"(0) : "memory");
}

// arm a periodic ARM-timer interrupt (~approx_us) as a WFI wakeup source
void power_wake_timer_init(uint32_t approx_us);

// clear the timer's pending bit so the IRQ line de-asserts and WFI can sleep again.
void power_wake_ack(void);

// arm gpio falling-edge interrupt as a WFI wakeup source
// configures gpio event and enables the irq at controller,
// leaves cpsr irqs masked so no handler runs, core wakes when edge fires.
void power_wake_gpio_falling_init(unsigned pin);

// clear gpio event for pin so wfi can sleep again
void power_wake_gpio_ack(unsigned pin);

#endif
