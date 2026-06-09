// button cycles screen betweeen stats/map page
//
// Wire: OLED (VCC->3V3, GND->GND, SDA->GPIO2, SCL->GPIO3).
//       Button between GPIO21 (pin 40) and GND (pressed reads LOW)
//
// Pass: each button press flips the page between STATS and MAP, a live counter
//       on the stats page keeps ticking so you can see it's running.

#include "rpi.h"
#include "i2c.h"
#include "ssd1306-display-driver.h"

enum { BTN_PIN = 21 };

typedef enum { PAGE_STATS = 0, PAGE_MAP = 1, PAGE_COUNT = 2 } page_t;

static void oled_str(int x, int y, const char *s, int sz) {
    for (; *s; s++) {
        ssd1306_display_draw_character_size(x, y, *s, COLOR_WHITE, sz, sz);
        x += 6 * sz;
    }
}

static void draw_stats(unsigned tick) {
    oled_str(0, 0, "STATS", 1);
    ssd1306_display_draw_horizontal_line(0, 127, 10, COLOR_WHITE);
    oled_str(0, 16, "SPEED --.- mph", 1);
    oled_str(0, 28, "DIST  --.-- mi", 1);
    oled_str(0, 40, "TIME 00:00:00", 1);
    // live heartbeat so we can tell the loop is running
    char buf[16];
    snprintk(buf, sizeof buf, "t=%d", tick);
    oled_str(0, 54, buf, 1);
}

static void draw_map(void) {
    oled_str(0, 0, "MAP", 1);
    ssd1306_display_draw_horizontal_line(0, 127, 10, COLOR_WHITE);
    // placeholder map frame
    ssd1306_display_draw_horizontal_line(0, 127, 12, COLOR_WHITE);
    ssd1306_display_draw_horizontal_line(0, 127, 63, COLOR_WHITE);
    ssd1306_display_draw_vertical_line(12, 63, 0, COLOR_WHITE);
    ssd1306_display_draw_vertical_line(12, 63, 127, COLOR_WHITE);
    oled_str(36, 34, "(no fix)", 1);
}

static void render(page_t page, unsigned tick) {
    ssd1306_display_clear();
    if (page == PAGE_STATS) draw_stats(tick);
    else                    draw_map();
    ssd1306_display_show();
}

void notmain(void) {
    delay_ms(100);
    i2c_init_clk_div(1500);
    delay_ms(100);
    ssd1306_display_init();
    delay_ms(100);

    gpio_set_input(BTN_PIN);
    gpio_set_pullup(BTN_PIN); // idle HIGH, pressed pulls to GND (LOW)

    page_t page = PAGE_STATS;
    int prev_pressed = 0;
    unsigned tick = 0;
    render(page, tick);

    // ~15ms poll loop: debounce by sampling; re-render on press or every ~0.5s.
    unsigned ms = 0;
    while (1) {
        int pressed = !gpio_read(BTN_PIN); // pressed = LOW
        if (pressed && !prev_pressed) { // falling edge (new press)
            page = (page + 1) % PAGE_COUNT;
            printk("button: -> page %s\n", page == PAGE_STATS ? "STATS" : "MAP");
            render(page, tick);
        }
        prev_pressed = pressed;

        delay_ms(15);
        ms += 15;
        if (ms >= 500) { // live refresh
            ms = 0;
            tick++;
            if (page == PAGE_STATS)
                render(page, tick);
        }
    }
}
