# display notes

files: `st7735.{c,h}`, `standard-ascii-font.c`, the display block + draw code in `bikecomputer.c`, `staff-hw-spi.o`

## panel

1.44 128x128 st7735r from cole, holy clutch, handled over spi0. has pins led/sck/sda/a0/reset/cs/gnd/vcc, a0=dc.

- need a 128x128 rgb565 framebuffer, draw into it, then `st7735_flush` streams over SPI in 4kb chunks wiht `spi_n_transfer`. 
- initialization, need to set frame-rate, power, and gamma. also need a gram offset with xstart=2 and ystart=3, and madctl/invert. 
- flip180 just flips it for my own sake lmao just stream the framebuffer in reverse
- `st7735_init` is hardware spi0 and bit bang fallback via `st7735_init_bitbang`

## pages

- stats: big yellow speed, distance/time, alt/max, lat/lon, ride state colored
  by mode (green REC / gray paused / cyan HOLD), green header rule.
- map: north-up, gray roads (dashed) / cyan planned route / yellow
  live track / red heading arrow + position marker + button 2 cycles zoom
  (600 m / 1.5 k / 3 k / 6 k), shown in the header.
- sys: the live PMU dashboard

uses `standard_ascii_font` from lab 15 scaled up to work

## sad displays

- i had ssd1306 oleds from the class, but you cant do dma dreq so no offloading! you want spi for this
- i also bought st7789s 240 x 240 but both panels were dead so rip
