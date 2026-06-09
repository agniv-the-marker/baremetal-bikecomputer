#ifndef __ST7735_H__
#define __ST7735_H__
// st7736 1.44'' 128 x 128 spi tft driver!
// pins LED/SCK/SDA/A0/RESET/CS/GND/VCC, where A0 = DC.
//
// adapted from st7789.{c,h}, since same RGB565-framebuffer model,
//
// transports: hardware SPI0 (st7735_init) or bit-banged GPIO (st7735_init_bitbang).

#include "rpi.h"
#include "spi.h"

enum
{
    ST7735_W = 128,
    ST7735_H = 128,
    ST7735_NPIX = ST7735_W * ST7735_H,
};

typedef struct
{
    int bitbang;              // 0 = hardware SPI0, 1 = bit-banged GPIO
    spi_t spi;                // hardware-SPI mode
    unsigned clk, mosi;       // bit-bang mode pins
    unsigned dc;              // A0 / data-command GPIO (low=command, high=data)
    unsigned rst;             // reset GPIO
    unsigned bb_us;           // bit-bang per-phase hold (us)
    unsigned xstart, ystart;  // panel GRAM offset (1.44" red board: 2,3)
    uint8_t madctl;           // memory access ctrl (orientation + RGB/BGR)
    int invert;               // 1 = INVON, 0 = INVOFF
    int flip180;              // 1 = rotate the framebuffer 180 deg at flush time
    uint16_t fb[ST7735_NPIX]; // RGB565 framebuffer
} st7735_t;

static inline uint16_t st7735_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

enum
{
    ST_C_BLACK = 0x0000,
    ST_C_WHITE = 0xffff,
    ST_C_RED = 0xf800,
    ST_C_GREEN = 0x07e0,
    ST_C_BLUE = 0x001f,
    ST_C_YELLOW = 0xffe0,
    ST_C_CYAN = 0x07ff,
    ST_C_MAGENTA = 0xf81f,
    ST_C_GRAY = 0x8410,
};

// chip-select for st7735_init(): 0 => CE0/GPIO8, 1 => CE1/GPIO7 (raw number).
enum
{
    ST7735_CE0 = 0,
    ST7735_CE1 = 1
};

// hardware SPI0. <dc>=A0, <rst>=RESET, CS goes to the chosen CE pin.
void st7735_init(st7735_t *d, unsigned chip_select, unsigned dc, unsigned rst,
                 unsigned clock_divider);

// bit-banged GPIO bus (any two pins for clk/data).
void st7735_init_bitbang(st7735_t *d, unsigned clk, unsigned mosi,
                         unsigned dc, unsigned rst, unsigned bb_us);

void st7735_invert(st7735_t *d, int on);
void st7735_fb_fill(st7735_t *d, uint16_t color);
void st7735_pixel(st7735_t *d, int x, int y, uint16_t color);
void st7735_fill_rect(st7735_t *d, int x, int y, int w, int h, uint16_t color);
// 5x7 font (standard_ascii_font), scaled sx/sy, into the framebuffer.
void st7735_char(st7735_t *d, int x, int y, char ch, uint16_t color, int sx, int sy);
void st7735_flush(st7735_t *d);
// DMA the framebuffer to the panel over SPI0 (TX DREQ-paced) while the CPU wfi()s
// on the DMA-completion IRQ. falls back to a busy-wait if dma_irq isn't set up.
void st7735_flush_dma(st7735_t *d);
void st7735_display_on(st7735_t *d);  // DISPON  (0x29)
void st7735_display_off(st7735_t *d); // DISPOFF (0x28), blanks panel (power)

#endif
