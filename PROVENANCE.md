# provenance

a decent amount of claude code, past files from the class, and some new custom stuff.

## new for this project

asked claude to summarize these guys

`bikecomputer.c` was written in an old version by myself before i asked claude to handle it.

| File | What it is |
| --- | --- |
| `bikecomputer.c` | the whole application: main loop, ride state + strava-style auto-pause, 3-page color ui, display abstraction (`d_*`/`dcol_t`/`USE_TFT`), buttons, gpx lifecycle, SYS/pmu page, parked hw-uart path |
| `gps_nmea.{c,h}` | nmea 0183 parser (RMC/GGA, checksum, `ddmm.mmmm`→decimal, knots→km/h) |
| `gpx_log.{c,h}` | always-valid gpx builder (header once, footer rewritten at every flush) |
| `st7735.{c,h}` | st7735 128×128 color spi driver adapted from the `st7789.c` below, with adafruit "144greentab" init values + a software flip180. includes the dma frame push (`st7735_flush_dma`) |
| `st7789.{c,h}` | st7789 240×240 spi driver (written here, panels were a dead batch, rip) |
| `bmp280.{c,h}` | bmp280 i2c driver (bosch compensation from the datasheet, sensor batch dead, also rip) |
| `route.{c,h}` | planned-route (`ROUTE.GPX`) loader/decimator |
| `osm_map.{c,h}` | road base-map (`ROADS.BIN`) loader |
| `power.{c,h}` | wfi + masked-irq wake helpers (arm-timer and gpio-edge) |
| `dma_irq.{c,h}` | dma-completion irq → wfi support |
| `sdfat/fat32_min.{c,h}` | the safe preallocated-file fat32 writer (struct layouts learned from the cs140e fat32 lab, the write-into-existing-clusters design is this project's) |
| `sdfat/hal.h` | shim mapping the ported `emmc.c` onto cs240lx libpi |
| `sdfat/emmc.c` (dma path) | the dma-driven data path (`dma_xfer`, modes 1/2/3, dreq pacing) added *into* the ported driver is new here, the sd bring-up below is ported |
| `tools/osm2bin.py` | pc-side overpass → `ROADS.BIN` converter |
| `tests/*.c` | all the bring-up/verify/benchmark programs |
| the docs (`README`, `docs/`, this file) | makefile/BUILD.md are fully clauded |

## ported from class labs, modified to fit

remember, sd card, dma, displays.

| File | Origin | Changes |
| --- | --- | --- |
| `sdfat/emmc.{c,h}` (bring-up) | cs140e lab 16 | `#undef printk/assert`, `hal.h` shim, retries/delays, + the new dma path |
| `sdfat/dma-impl.h` | lab 17 dma stuff (ty max!) | used as-is, the emmc dreq/permap control blocks are built in `emmc.c`, not here |
| `ssd1306-display-driver.{c,h}` | lab 15 oled driver | retargeted as the `USE_TFT=0` fallback |
| `standard-ascii-font.c` | lab 15 / adafruit-gfx 5×7 font table | shared by the oled and st7735 text renderers |

## staff / libpi, prebuilt, linked, not in this repo

- `staff-sw-uart.o` software uart (the gps rx bit-banger)
- `staff-hw-spi.o` hardware spi0 (drives the st7735)
- `pl011-uart.o` hardware pl011 uart (for the parked hw-uart gps path)
- `libpi` gpio, i2c, timer, interrupts, printk, cycle counter (lab 10 pmu), boot/start code

## how this was built (ai disclosure)

pair programming w/ claude code go brrrr

i had the concept/scope, and all the hardware was researched by me before i bought it and grabbed it. i made the design calls for the sd card stuff, the uart stuff, caching, culling, on how to measure this stuff, map stuff and parsing was done, and i rode my bike :P

claude handled a decent amount of code clean up and debugging, and made it easy to write tests and build system.

general loop of: decide + wire + flash + measure + ride, claude types fast!
