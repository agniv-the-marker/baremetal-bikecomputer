#include "rpi.h"

enum {
    LED_PIN = 47,
    LED_ACTIVE_LOW = 0,
};

static void led_set(int on) {
    gpio_write(LED_PIN, LED_ACTIVE_LOW ? !on : on);
}

void notmain(void) {
    gpio_set_output(LED_PIN);

    // blink forever: 500ms on, 500ms off => 1 Hz.
    while (1) {
        led_set(1);
        delay_ms(500);
        led_set(0);
        delay_ms(500);
    }
}
