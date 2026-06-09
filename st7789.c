// ST7789 240x240 SPI TFT driver.
#include "st7789.h"

// ST7789 command opcodes
// https://github.com/pimoroni/pimoroni-pico/blob/main/drivers/st7789/st7789.cpp
enum
{
    ST_SWRESET = 0x01,
    ST_SLPOUT = 0x11,
    ST_NORON = 0x13,
    ST_INVOFF = 0x20,
    ST_INVON = 0x21,
    ST_DISPON = 0x29,
    ST_CASET = 0x2a,
    ST_RASET = 0x2b,
    ST_RAMWR = 0x2c,
    ST_MADCTL = 0x36,
    ST_COLMOD = 0x3a,
    // power / gamma config (the "full" init some panels require)
    ST_PORCTRL = 0xb2,
    ST_GCTRL = 0xb7,
    ST_VCOMS = 0xbb,
    ST_LCMCTRL = 0xc0,
    ST_VDVVRHEN = 0xc2,
    ST_VRHS = 0xc3,
    ST_VDVS = 0xc4,
    ST_FRCTRL2 = 0xc6,
    ST_PWCTRL1 = 0xd0,
    ST_PVGAM = 0xe0,
    ST_NVGAM = 0xe1,
};

// chunked SPI transfer, spi_n_transfer wants rx and tx of equal size, so we
// stream out of a fixed scratch pair rather than allocating a 115KB rx buffer.
enum
{
    CHUNK = 4096
};
static uint8_t tx_buf[CHUNK];
static uint8_t rx_buf[CHUNK];

// bit-bang one byte, MSB first, SPI MODE 3
// https://github.com/solinnovay/Python_ST7789/blob/master/ST7789/ST7789.py
static void bb_byte(st7789_t *d, uint8_t b)
{
    unsigned us = d->bb_us;
    for (int i = 7; i >= 0; i--)
    {
        gpio_write(d->clk, 0); // leading falling edge
        gpio_write(d->mosi, (b >> i) & 1);
        if (us)
            delay_us(us);      // data setup
        gpio_write(d->clk, 1); // trailing rising edge
        if (us)
            delay_us(us);
    }
}

// send <n> bytes with the data/command line at <is_data>, via whichever transport
static void send(st7789_t *d, int is_data, const uint8_t *buf, unsigned n)
{
    gpio_write(d->dc, is_data);
    if (d->bitbang)
    {
        for (unsigned i = 0; i < n; i++)
            bb_byte(d, buf[i]);
        return;
    }
    for (unsigned off = 0; off < n;)
    {
        unsigned m = n - off;
        if (m > CHUNK)
            m = CHUNK;
        for (unsigned i = 0; i < m; i++)
            tx_buf[i] = buf[off + i];
        spi_n_transfer(d->spi, rx_buf, tx_buf, m);
        off += m;
    }
}

static void cmd(st7789_t *d, uint8_t c)
{
    send(d, 0, &c, 1);
}

static void cmd_data(st7789_t *d, uint8_t c, const uint8_t *data, unsigned n)
{
    send(d, 0, &c, 1);
    if (n)
        send(d, 1, data, n);
}

// set the GRAM address window to the full 240x240 panel.
static void set_full_window(st7789_t *d)
{
    uint8_t cols[4] = {0x00, 0x00, 0x00, ST7789_W - 1}; // 0 .. 239
    uint8_t rows[4] = {0x00, 0x00, 0x00, ST7789_H - 1}; // 0 .. 239
    cmd_data(d, ST_CASET, cols, 4);
    cmd_data(d, ST_RASET, rows, 4);
}

// shared reset + power-on sequence.
static void do_init(st7789_t *d)
{
    gpio_set_output(d->dc);
    gpio_set_output(d->rst);
    gpio_write(d->dc, 0);

    // hardware reset, high 100ms, low 100ms, high 100ms.
    gpio_write(d->rst, 1);
    delay_ms(100);
    gpio_write(d->rst, 0);
    delay_ms(100);
    gpio_write(d->rst, 1);
    delay_ms(100);

    delay_ms(10);
    cmd(d, ST_SLPOUT);
    delay_ms(150);

    cmd_data(d, ST_MADCTL, (uint8_t[]){0x00}, 1);
    cmd_data(d, ST_COLMOD, (uint8_t[]){0x05}, 1); // 16 bits/pixel

    cmd_data(d, ST_PORCTRL, (uint8_t[]){0x0c, 0x0c}, 2);
    cmd_data(d, ST_GCTRL, (uint8_t[]){0x35}, 1);
    cmd_data(d, ST_VCOMS, (uint8_t[]){0x1a}, 1);
    cmd_data(d, ST_LCMCTRL, (uint8_t[]){0x2c}, 1);
    cmd_data(d, ST_VDVVRHEN, (uint8_t[]){0x01}, 1);
    cmd_data(d, ST_VRHS, (uint8_t[]){0x0b}, 1);
    cmd_data(d, ST_VDVS, (uint8_t[]){0x20}, 1);
    cmd_data(d, ST_FRCTRL2, (uint8_t[]){0x0f}, 1);
    cmd_data(d, ST_PWCTRL1, (uint8_t[]){0xa4, 0xa1}, 2);

    cmd(d, ST_INVON);

    cmd_data(d, ST_PVGAM, (uint8_t[]){0x00, 0x19, 0x1e, 0x0a, 0x09, 0x15, 0x3d, 0x44, 0x51, 0x12, 0x03, 0x00, 0x3f, 0x3f}, 14);
    cmd_data(d, ST_NVGAM, (uint8_t[]){0x00, 0x18, 0x1e, 0x0a, 0x09, 0x25, 0x3f, 0x43, 0x52, 0x33, 0x03, 0x00, 0x3f, 0x3f}, 14);

    cmd(d, ST_DISPON);
    delay_ms(100);

    st7789_fb_fill(d, C_BLACK);
    st7789_flush(d);
}

// SPI0 register base (BCM2835, peripheral base 0x20000000).
#define SPI0_CS 0x20204000
// force SPI mode 3 (CPOL=1 bit3, CPHA=1 bit2).
static void spi_force_mode3(void)
{
    volatile void *cs = (volatile void *)SPI0_CS;
    dev_barrier();
    put32(cs, get32(cs) | (1u << 3) | (1u << 2));
    dev_barrier();
}

void st7789_init(st7789_t *d, unsigned chip_select, unsigned dc, unsigned rst,
                 unsigned clock_divider)
{
    d->bitbang = 0;
    d->bb_us = 0;
    d->dc = dc;
    d->rst = rst;
    d->spi = spi_n_init(chip_select, clock_divider);
    spi_force_mode3();
    do_init(d);
}

void st7789_init_bitbang(st7789_t *d, unsigned clk, unsigned mosi,
                         unsigned dc, unsigned rst, unsigned bb_us)
{
    d->bitbang = 1;
    d->bb_us = bb_us;
    d->clk = clk;
    d->mosi = mosi;
    d->dc = dc;
    d->rst = rst;
    gpio_set_output(clk);
    gpio_set_output(mosi);
    gpio_write(clk, 1); // idle HIGH (SPI mode 3)
    gpio_write(mosi, 0);
    do_init(d);
}

void st7789_invert(st7789_t *d, int on)
{
    cmd(d, on ? ST_INVON : ST_INVOFF);
}

void st7789_fb_fill(st7789_t *d, uint16_t color)
{
    for (unsigned i = 0; i < ST7789_NPIX; i++)
        d->fb[i] = color;
}

void st7789_flush(st7789_t *d)
{
    set_full_window(d);
    cmd(d, ST_RAMWR);

    gpio_write(d->dc, 1); // data
    if (d->bitbang)
    {
        for (unsigned i = 0; i < ST7789_NPIX; i++)
        {
            bb_byte(d, d->fb[i] >> 8); // high byte first
            bb_byte(d, d->fb[i] & 0xff);
        }
        return;
    }
    // hardware SPI: stream as big-endian RGB565, CHUNK bytes at a time.
    unsigned pix = 0;
    while (pix < ST7789_NPIX)
    {
        unsigned n = 0;
        while (n + 2 <= CHUNK && pix < ST7789_NPIX)
        {
            uint16_t c = d->fb[pix++];
            tx_buf[n++] = c >> 8;
            tx_buf[n++] = c & 0xff;
        }
        spi_n_transfer(d->spi, rx_buf, tx_buf, n);
    }
}
