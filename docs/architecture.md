# architecture specifics

`bikecomputer.c` is just a sexy while loop. ai generated summary:

```
            bikecomputer.c   (app: main loop, ride state, UI pages, GPX lifecycle)
            /     |      |        \            \
   gps_nmea  ride/auto  gpx_log   display       dma_irq  (DMA-done IRQ -> wfi)
   (parse)   -pause     (RAM GPX)  abstraction       |
      |                    |       d_*/dcol_t        |
  sw-UART(staff)        fat32_min   /    \        (used by st7735_flush_dma
   GPIO16              (safe write) st7735  ssd1306   and emmc mode 3)
                          |        (TFT)   (OLED, fallback)
                        emmc.c (+DMA)       |
                        (Arasan SDHCI)    i2c(staff)
   route.c / osm_map.c read overlays via fat32_min.
   power.c = wfi() + IRQ-wake helpers.   cycle-count(staff) = PMU for SYS page.
```

## main loop

1. read the gps `sw_uart_get8_timeout(GPIO16)` (uses `on_gps_byte()` and `gps_feed()`).
2. if we have the data, do `ride_on_fix()` to get the distance/max speed/check for auto-pause/breadcrumbs/`gpx_add`, and update the screen every second with `show_ui()`. 
3. check buttons, gpio21 is cycling the page on short and on long it does save + force pause, and gpio26 toggles map zoom
4. `show_ui()` changes the shown fields and re-renders when something has changed
5. gpx auto flush every 30 seconds, long press just flushes immediately

## display

all drawing goes through `d_*` primitives and `dcol_t` color types, since i had two monitors and was debugging with both of them lol. but it defaults to 1 to use the st7735 instead of old 0 for the lab with the ssd1306. 

## defaults

other defaults are the `USE_FRAME_DMA` which is defaulted to yes to send data over the dma, and also `GPS_HW_UART` which is off because i think gpio15 might be dead? and also `HW_WFI` which is also off because it was the way to test it. i have emmc dma mode on since that was an early win, but its just per block right now rather than whole/whole+irq.

## build system

again i just asked claude to touch the makefiles for me:

- `make` builds `PROGS` (default `bikecomputer.c`) + `COMMON_SRC` (all driver
  modules) + `STAFF_OBJS` (sw-uart, hw-spi, pl011). `make PROGS=tests/X.c` for a
  test. Headers resolve via `-I.` and `-I./sdfat`, so source location doesn't matter.
- **Adding a driver:** add its `.c` to `COMMON_SRC` in the Makefile (every program links it). A new staff `.o` goes in `STAFF_OBJS`.
- **Dev vs standalone:** the Pi boots `kernel.img`. Dev = `bootloader.bin` (serial flashing); standalone = `bikecomputer.bin`. Flashing needs a Pi power-cycle.

## memory tracking

the `gpx_log` is about 2 mb, `osm_map` is about 2mb (see `ROADS.BIN`), `route` is about 3mb (`ROUTE.GPX` for reading). note this is because we have 128 blocks so we can't actually exceed 2mb really.

the st7736 framebuffer is 32kb and the dma has a shadow asw, and emmc/spi chunk buffers, which is chill for us on the pi 

## notes

`fat32_write_into` writes only into preallocated file clusters and never touches the fat, so logging/dma has no risk of bricking the boot card!

`gpx_log` writes the header once and the closing footer only at flush so the on card file is actually always complete

`dma_irq_init(0)` is called by the logger, dont want a periodic irq because itd jitter the uart and probably fuck up gps.

`DEST_DREQ=1<<6`, `SRC_DREQ=1<<10`, `INTEN=1<<0`, `PERMAP` at 16. eMMC=11, SPI TX=6, SPI RX=7. fucked it earlier

cache is disabled so we dont need to enable cleaning/invalidating around dma.

`4` for tx/sd and `5` spi rx-drain, not free.

`spi_n_init` wants raw chip select, not `SPI_CE0/CE1`

- GPIO16 = GPS sw-UART RX
- GPIO21/26 = buttons
- GPIO14/15 = hardware UART/serial co

## dead stuff

dont need `ssd1306-display-driver`, dont need `st7789` dead panels, dont need `bmp280` since dead sensor batch, the `GPS_HW_UART` PL011 path seems screwed, probably GPIO15 is broken?
