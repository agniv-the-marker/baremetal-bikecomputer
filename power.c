// Low-power helpers.

#include "power.h"
#include "rpi-armtimer.h"
#include "rpi-interrupts.h"
#include "gpio.h" // gpio_int_falling_edge, gpio_event_clear

void power_wake_timer_init(uint32_t approx_us)
{
    dev_barrier();
    PUT32(arm_timer_Control, 0); // stop
    // /256 prescale: timer clock ~= 250MHz/256 ~= 0.977 MHz, ~1 tick/us.
    PUT32(arm_timer_Load, approx_us);
    PUT32(arm_timer_Control,
          RPI_ARMTIMER_CTRL_32BIT | RPI_ARMTIMER_CTRL_PRESCALE_256 |
              RPI_ARMTIMER_CTRL_INT_ENABLE | RPI_ARMTIMER_CTRL_ENABLE);
    // route the timer IRQ to the core so it wakes WFI
    // we DON'T enable_interrupts(), so CPSR.I stays masked and no handler runs.
    PUT32(IRQ_Enable_Basic, RPI_BASIC_ARM_TIMER_IRQ);
    dev_barrier();
}

void power_wake_ack(void)
{
    dev_barrier();
    PUT32(arm_timer_IRQClear, 1);
    dev_barrier();
}

void power_wake_gpio_falling_init(unsigned pin)
{
    dev_barrier();
    gpio_int_falling_edge(pin);
    gpio_event_clear(pin);
    // gpio bank 0 interrupts are on irq line 49, so bit 17, enable so edge wakes wfi
    PUT32(IRQ_Enable_2, 1 << (49 - 32));
    dev_barrier();
}

void power_wake_gpio_ack(unsigned pin)
{
    dev_barrier();
    gpio_event_clear(pin); // write-1-to-clear the event
    dev_barrier();
}
