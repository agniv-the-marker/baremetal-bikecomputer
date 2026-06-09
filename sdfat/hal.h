// hal.h, emmc.c only needs c_delay_us/ms, dev_barrier, c_gpio_set_function,
// uart_puts, uart_put_hex, reboot

#ifndef __HAL_SHIM_H__
#define __HAL_SHIM_H__

#include "rpi.h"   // delay_us/ms, dev_barrier, gpio_set_function, printk, clean_reboot

static inline void c_delay_us(unsigned us) { delay_us(us); }
static inline void c_delay_ms(unsigned ms) { delay_ms(ms); }

static inline void c_gpio_set_function(unsigned pin, unsigned f) {
    gpio_set_function(pin, (gpio_func_t)f);
}

// emmc.c's uart_puts/uart_put_hex are defined here (before emmc.c re-#defines
// printk to a no-op), so they still reach the real serial console.
static inline void uart_puts(const char *s)   { printk("%s", s); }
static inline void uart_put_hex(unsigned x)   { printk("%x", x); }

static inline void reboot(void) { clean_reboot(); }

#endif
