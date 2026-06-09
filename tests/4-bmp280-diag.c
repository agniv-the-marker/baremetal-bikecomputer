// bmp280 raw register diagnostic over serial
// init OK but all reads fail, temp channel is like. negative terrible degrees
// want to check if pressure channel is alive because only important one is pressure for altitude.

#include "rpi.h"
#include "i2c.h"

#define ADDR 0x76

static int rd(uint8_t reg, uint8_t *buf, unsigned n) {
    if (i2c_write(ADDR, &reg, 1) < 0) return 0;
    return i2c_read(ADDR, buf, n) >= 0;
}

static void wr(uint8_t reg, uint8_t v) { uint8_t b[2] = { reg, v }; i2c_write(ADDR, b, 2); }

void notmain(void) {
    delay_ms(100);
    i2c_init_clk_div(1500);
    delay_ms(100);

    uint8_t id = 0; rd(0xD0, &id, 1);
    printk("bmp-diag: id=0x%x (expect 0x58)\n", id);

    // normal mode, temp x1 + press x16 (more pressure resolution), fastest standby.
    wr(0xF4, 0x37); // osrs_t=001, osrs_p=101(x16), mode=11(normal)
    wr(0xF5, 0x00); // t_standby 0.5ms, filter off
    delay_ms(100);
    uint8_t cm = 0, cfg = 0; rd(0xF4, &cm, 1); rd(0xF5, &cfg, 1);
    printk("bmp-diag: ctrl_meas=0x%x (expect 0x37)  config=0x%x\n", cm, cfg);

    while (1) {
        uint8_t st = 0; rd(0xF3, &st, 1); // status: bit3=measuring
        uint8_t d[6] = {0}; rd(0xF7, d, 6); // press[3], temp[3]
        int32_t adc_P = ((uint32_t)d[0] << 12) | ((uint32_t)d[1] << 4) | (d[2] >> 4);
        int32_t adc_T = ((uint32_t)d[3] << 12) | ((uint32_t)d[4] << 4) | (d[5] >> 4);
        printk("bmp-diag: status=0x%x  adc_T=0x%x  adc_P=0x%x\n", st, adc_T, adc_P);
        delay_ms(1000);
    }
}
