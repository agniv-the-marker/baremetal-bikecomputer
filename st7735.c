// ST7735 1.44" 128x128 SPI TFT driver. See st7735.h. init values are the
// Adafruit ST7735R "144greentab" set
// https://github.com/radiolab81/iRadio/blob/299a0fdcb03836a231466526693dd437eda6fcf1/display/st7735/src/tft_st7735.cpp

#include "st7735.h"

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
    ST_FRMCTR1 = 0xb1,
    ST_FRMCTR2 = 0xb2,
    ST_FRMCTR3 = 0xb3,
    ST_INVCTR = 0xb4,
    ST_PWCTR1 = 0xc0,
    ST_PWCTR2 = 0xc1,
    ST_PWCTR3 = 0xc2,
    ST_PWCTR4 = 0xc3,
    ST_PWCTR5 = 0xc4,
    ST_VMCTR1 = 0xc5,
    ST_GMCTRP1 = 0xe0,
    ST_GMCTRN1 = 0xe1,
};

enum
{
    CHUNK = 4096
};
static uint8_t tx_buf[CHUNK];
static uint8_t rx_buf[CHUNK];

// bit-bang one byte, MSB first, SPI MODE 0 (clk idles LOW, sample on rising edge)
static void bb_byte(st7735_t *d, uint8_t b)
{
    unsigned us = d->bb_us;
    for (int i = 7; i >= 0; i--)
    {
        gpio_write(d->clk, 0);
        gpio_write(d->mosi, (b >> i) & 1);
        if (us)
            delay_us(us);
        gpio_write(d->clk, 1);
        if (us)
            delay_us(us);
    }
}

static void send(st7735_t *d, int is_data, const uint8_t *buf, unsigned n)
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

static void cmd(st7735_t *d, uint8_t c) { send(d, 0, &c, 1); }
static void cmd_data(st7735_t *d, uint8_t c, const uint8_t *data, unsigned n)
{
    send(d, 0, &c, 1);
    if (n)
        send(d, 1, data, n);
}

static void set_window(st7735_t *d, int x0, int y0, int x1, int y1)
{
    x0 += d->xstart;
    x1 += d->xstart;
    y0 += d->ystart;
    y1 += d->ystart;
    cmd_data(d, ST_CASET, (uint8_t[]){0, (uint8_t)x0, 0, (uint8_t)x1}, 4);
    cmd_data(d, ST_RASET, (uint8_t[]){0, (uint8_t)y0, 0, (uint8_t)y1}, 4);
}

static void do_init(st7735_t *d)
{
    gpio_set_output(d->dc);
    gpio_set_output(d->rst);
    gpio_write(d->dc, 0);

    gpio_write(d->rst, 1);
    delay_ms(50);
    gpio_write(d->rst, 0);
    delay_ms(50);
    gpio_write(d->rst, 1);
    delay_ms(120);

    cmd(d, ST_SWRESET);
    delay_ms(150);
    cmd(d, ST_SLPOUT);
    delay_ms(500);

    cmd_data(d, ST_FRMCTR1, (uint8_t[]){0x01, 0x2c, 0x2d}, 3);
    cmd_data(d, ST_FRMCTR2, (uint8_t[]){0x01, 0x2c, 0x2d}, 3);
    cmd_data(d, ST_FRMCTR3, (uint8_t[]){0x01, 0x2c, 0x2d, 0x01, 0x2c, 0x2d}, 6);
    cmd_data(d, ST_INVCTR, (uint8_t[]){0x07}, 1);
    cmd_data(d, ST_PWCTR1, (uint8_t[]){0xa2, 0x02, 0x84}, 3);
    cmd_data(d, ST_PWCTR2, (uint8_t[]){0xc5}, 1);
    cmd_data(d, ST_PWCTR3, (uint8_t[]){0x0a, 0x00}, 2);
    cmd_data(d, ST_PWCTR4, (uint8_t[]){0x8a, 0x2a}, 2);
    cmd_data(d, ST_PWCTR5, (uint8_t[]){0x8a, 0xee}, 2);
    cmd_data(d, ST_VMCTR1, (uint8_t[]){0x0e}, 1);

    cmd(d, d->invert ? ST_INVON : ST_INVOFF);
    cmd_data(d, ST_MADCTL, (uint8_t[]){d->madctl}, 1);
    cmd_data(d, ST_COLMOD, (uint8_t[]){0x05}, 1); // 16 bits/pixel

    cmd_data(d, ST_GMCTRP1, (uint8_t[]){0x02, 0x1c, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2d, 0x29, 0x25, 0x2b, 0x39, 0x00, 0x01, 0x03, 0x10}, 16);
    cmd_data(d, ST_GMCTRN1, (uint8_t[]){0x03, 0x1d, 0x07, 0x06, 0x2e, 0x2c, 0x29, 0x2d, 0x2e, 0x2e, 0x37, 0x3f, 0x00, 0x00, 0x02, 0x10}, 16);

    cmd(d, ST_NORON);
    delay_ms(10);
    cmd(d, ST_DISPON);
    delay_ms(100);

    st7735_fb_fill(d, ST_C_BLACK);
    st7735_flush(d);
}

#define SPI0_CS 0x20204000

void st7735_init(st7735_t *d, unsigned chip_select, unsigned dc, unsigned rst,
                 unsigned clock_divider)
{
    d->bitbang = 0;
    d->bb_us = 0;
    d->dc = dc;
    d->rst = rst;
    d->xstart = 2;
    d->ystart = 3;
    d->madctl = 0xc8;
    d->invert = 0;
    d->flip180 = 0;
    d->spi = spi_n_init(chip_select, clock_divider);
    do_init(d);
}

void st7735_init_bitbang(st7735_t *d, unsigned clk, unsigned mosi,
                         unsigned dc, unsigned rst, unsigned bb_us)
{
    d->bitbang = 1;
    d->bb_us = bb_us;
    d->clk = clk;
    d->mosi = mosi;
    d->dc = dc;
    d->rst = rst;
    d->xstart = 2;
    d->ystart = 3;
    d->madctl = 0xc8;
    d->invert = 0;
    d->flip180 = 0;
    gpio_set_output(clk);
    gpio_set_output(mosi);
    gpio_write(clk, 0);
    gpio_write(mosi, 0); // idle LOW (SPI mode 0)
    do_init(d);
}

void st7735_invert(st7735_t *d, int on) { cmd(d, on ? ST_INVON : ST_INVOFF); }

// SPI0-DMA frame push

#include "dma_irq.h"
enum
{
    SPI0 = 0x20204000,
    SPI_CS = SPI0 + 0x00,
    SPI_FIFO = SPI0 + 0x04,
    SPI_FIFO_BUS = 0x7e204004,
    CS_TA = 1 << 7,
    CS_DMAEN = 1 << 8,
    CS_ADCS = 1 << 11,
    CS_CLR_TX = 1 << 4,
    CS_CLR_RX = 1 << 5,
    DMAB = 0x20007000,
    DMA_EN = DMAB + 0xff0,
    TXCH = 4,
    RXCH = 5,
    T_INTEN = 1 << 0,
    T_WAITR = 1 << 3,
    T_DDREQ = 1 << 6,
    T_DIGN = 1 << 7,
    T_SINC = 1 << 8,
    T_SDREQ = 1 << 10,
    PMAP_TX = 6 << 16,
    PMAP_RX = 7 << 16,
    D_ACTIVE = 1 << 0,
    D_RESET = 1u << 31,
};
typedef struct
{
    volatile uint32_t TI, SRC, DST, LEN, STR, NXT, p0, p1;
}
__attribute__((aligned(32))) dcb_t;
typedef struct
{
    volatile uint32_t CS, CBA, TI, SRC, DST, LEN, STR, NXT, DBG;
} dch_t;
#define DCH(ch) ((volatile dch_t *)(DMAB + (ch) * 0x100))
static dcb_t g_txcb __attribute__((aligned(32)));
static dcb_t g_rxcb __attribute__((aligned(32)));
static uint8_t g_shadow[ST7735_NPIX * 2]; // byte-swapped pixels for the DMA
static inline uint32_t b2(volatile void *p) { return ((uint32_t)(uintptr_t)p) | 0x40000000u; }

void st7735_flush_dma(st7735_t *d)
{
    if (d->bitbang)
    {
        st7735_flush(d);
        return;
    }
    set_window(d, 0, 0, ST7735_W - 1, ST7735_H - 1);
    cmd(d, ST_RAMWR);
    gpio_write(d->dc, 1); // data

    unsigned n = ST7735_NPIX, len = n * 2;
    for (unsigned i = 0; i < n; i++)
    { // fb -> shadow (hi byte first), +flip
        uint16_t c = d->fb[d->flip180 ? (n - 1 - i) : i];
        g_shadow[2 * i] = c >> 8;
        g_shadow[2 * i + 1] = c & 0xff;
    }

    dev_barrier();
    *(volatile uint32_t *)DMA_EN |= (1u << TXCH) | (1u << RXCH);
    PUT32(SPI_CS, CS_CLR_TX | CS_CLR_RX | CS_DMAEN | CS_ADCS);
    dev_barrier();

    g_rxcb.TI = PMAP_RX | T_SDREQ | T_DIGN | T_WAITR; // drain RX FIFO -> nowhere
    g_rxcb.SRC = SPI_FIFO_BUS;
    g_rxcb.DST = 0;
    g_rxcb.LEN = len;
    g_rxcb.STR = g_rxcb.NXT = g_rxcb.p0 = g_rxcb.p1 = 0;
    g_txcb.TI = PMAP_TX | T_SINC | T_DDREQ | T_WAITR | T_INTEN;
    g_txcb.SRC = b2(g_shadow);
    g_txcb.DST = SPI_FIFO_BUS;
    g_txcb.LEN = len;
    g_txcb.STR = g_txcb.NXT = g_txcb.p0 = g_txcb.p1 = 0;

    // DMA-mode SPI header, first FIFO write = [31:16]=byte length, [7:0]=CS|TA.
    PUT32(SPI_FIFO, (len << 16) | CS_TA);
    dev_barrier();

    DCH(RXCH)->CS = D_RESET;
    dev_barrier();
    DCH(RXCH)->CBA = b2(&g_rxcb);
    DCH(RXCH)->CS = D_ACTIVE; // start RX drain
    DCH(TXCH)->CS = D_RESET;
    dev_barrier();
    DCH(TXCH)->CBA = b2(&g_txcb);
    dma_irq_clear(TXCH);
    DCH(TXCH)->CS = D_ACTIVE; // start TX
    dev_barrier();

    if (!dma_irq_wait(TXCH, 4000))
    { // wfi until done (or busy-wait fallback)
        unsigned to = 4000000;
        while ((DCH(TXCH)->CS & D_ACTIVE) && --to)
        {
        }
    }
    dev_barrier();
    PUT32(SPI_CS, CS_CLR_TX | CS_CLR_RX); // drop DMAEN so CPU spi works after
    dev_barrier();
}

void st7735_display_on(st7735_t *d) { cmd(d, ST_DISPON); }
void st7735_display_off(st7735_t *d) { cmd(d, 0x28); } // DISPOFF

void st7735_fb_fill(st7735_t *d, uint16_t color)
{
    for (unsigned i = 0; i < ST7735_NPIX; i++)
        d->fb[i] = color;
}

void st7735_pixel(st7735_t *d, int x, int y, uint16_t color)
{
    if (x >= 0 && x < ST7735_W && y >= 0 && y < ST7735_H)
        d->fb[y * ST7735_W + x] = color;
}

// 5x7 ASCII font (shared standard_ascii_font: 5 bytes/char, bit0 = top row).
extern const unsigned char standard_ascii_font[];
void st7735_char(st7735_t *d, int x, int y, char ch, uint16_t color, int sx, int sy)
{
    for (int i = 0; i < 5; i++)
    {
        unsigned char col = standard_ascii_font[(unsigned char)ch * 5 + i];
        for (int j = 0; j < 8; j++, col >>= 1)
        {
            if (!(col & 1))
                continue;
            if (sx == 1 && sy == 1)
                st7735_pixel(d, x + i, y + j, color);
            else
                st7735_fill_rect(d, x + i * sx, y + j * sy, sx, sy, color);
        }
    }
}

void st7735_fill_rect(st7735_t *d, int x, int y, int w, int h, uint16_t color)
{
    for (int yy = y; yy < y + h; yy++)
    {
        if (yy < 0 || yy >= ST7735_H)
            continue;
        for (int xx = x; xx < x + w; xx++)
        {
            if (xx < 0 || xx >= ST7735_W)
                continue;
            d->fb[yy * ST7735_W + xx] = color;
        }
    }
}

void st7735_flush(st7735_t *d)
{
    set_window(d, 0, 0, ST7735_W - 1, ST7735_H - 1);
    cmd(d, ST_RAMWR);
    gpio_write(d->dc, 1);
    if (d->bitbang)
    {
        for (unsigned i = 0; i < ST7735_NPIX; i++)
        {
            uint16_t c = d->fb[d->flip180 ? (ST7735_NPIX - 1 - i) : i];
            bb_byte(d, c >> 8);
            bb_byte(d, c & 0xff);
        }
        return;
    }
    unsigned pix = 0;
    while (pix < ST7735_NPIX)
    {
        unsigned n = 0;
        while (n + 2 <= CHUNK && pix < ST7735_NPIX)
        {
            uint16_t c = d->fb[d->flip180 ? (ST7735_NPIX - 1 - pix) : pix];
            pix++;
            tx_buf[n++] = c >> 8;
            tx_buf[n++] = c & 0xff;
        }
        spi_n_transfer(d->spi, rx_buf, tx_buf, n);
    }
}
