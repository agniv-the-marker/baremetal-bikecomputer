#ifndef __ST7789_H__
#define __ST7789_H__
// st7789 240x240 spi tft driver!
// each display has its own rgb565 framebuffer in RAM, drawing routines write into fbuffer
// and st7789_flush() streams it to the panel.
// 
// - hardware SPI0 (fast): st7789_init()
// - bit-banged GPIO SPI (any two pins): st7789_init_bitbang()

#include "rpi.h"
#include "spi.h"

enum {
    ST7789_W = 240,
    ST7789_H = 240,
    ST7789_NPIX = ST7789_W * ST7789_H,
};

typedef struct {
    int bitbang;        // 0 = hardware SPI0, 1 = bit-banged GPIO
    spi_t spi;          // hardware-SPI mode
    unsigned clk, mosi; // bit-bang mode pins
    unsigned dc;        // data/command GPIO (low=command, high=data)
    unsigned rst;       // reset GPIO
    unsigned bb_us;     // bit-bang: microseconds to hold each clock phase (slower = safer)
    uint16_t fb[ST7789_NPIX];   // RGB565 framebuffer
} st7789_t;

// build an RGB565 pixel (high color order is handled at flush time).
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

// a few handy colors.
enum {
    C_BLACK = 0x0000,
    C_WHITE = 0xffff,
    C_RED   = 0xf800,
    C_GREEN = 0x07e0,
    C_BLUE  = 0x001f,
    C_YELLOW= 0xffe0,
    C_CYAN  = 0x07ff,
    C_GRAY  = 0x8410,
};

// chip-select values for st7789_init(). spi_n_init() wants the raw chip number
// (0 => CE0/GPIO8, 1 => CE1/GPIO7), NOT the SPI_CE0/SPI_CE1 macros from spi.h.
enum { ST7789_CE0 = 0, ST7789_CE1 = 1 };

// initialize a display on hardware SPI0. <chip_select> is ST7789_CE0/CE1;
// <dc>/<rst> are the data-command and reset GPIO pins. Runs hardware reset +
// the ST7789 power-on sequence.
void st7789_init(st7789_t *d, unsigned chip_select, unsigned dc, unsigned rst,
                 unsigned clock_divider);

// initialize a display on a bit-banged GPIO bus. <clk>/<mosi> are the clock and
// data pins (driven as plain outputs). <bb_us> is the per-clock-phase hold time
// in microseconds (0 = as fast as gpio_write goes; larger = slower/safer over
// long wires/HATs). Same reset + init sequence.
void st7789_init_bitbang(st7789_t *d, unsigned clk, unsigned mosi,
                         unsigned dc, unsigned rst, unsigned bb_us);

// fill the entire framebuffer with one color (does not touch the panel).
void st7789_fb_fill(st7789_t *d, uint16_t color);

// push the whole framebuffer to the panel.
void st7789_flush(st7789_t *d);

// toggle display color inversion.
void st7789_invert(st7789_t *d, int on);

#endif
