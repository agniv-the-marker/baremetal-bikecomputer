# gps

files: `gps_nmea.c/h`, gps read path is in the main file, also needed `staff-sw-uart.o`.

neo-6m module streams ascii nmea sentences at 9600 baud, 1 hz. read/parse/position+speed+time

## fuuuuuuuck my stupid chud life

so bcm2835 has 2 uarts (pl011 + mini uart) and both map to gpio 14/15 and so if we use this we can't actually debug with printk. 

so we use bit banged software uart (what we've been doing the whole time) over gpio16, but however you need to have the cpu on the entire time, which fucking SUCKS. 

i tried hard to do hardware-uart, needed to debug pl011 but it wouldn't seem to work. so that fucking sucked. 

anyway this is like the one thing i really wanted to do but failed for power saving but it still roughly worked or smth

## nmea parsing

bytes accumulate into line buffer until `\n`, there are two sentence types:

- GPRMC which has status, lat, long, speed (in knots!), course over ground (default to true), and the UTC date/time.
- GPGGA (fix data), altitude, fix quality, and satellite count

steps:

1. checksum, the trailing *hh is the xor of every byte between the $ and *, and we verify this
2. coordinates are ddmm.mmmm (degrees + minutes), convert to decimal degrees with deg + minutes/60, signed by the north/south/east/west fields

just splitting on commas and converting

## satellites

just need usually 5+ sattelites, and i have an antenna that does pretty well for this, but it meant i was outside in my pjs at noon trying to summon a satellite

## sources
- NMEA 0183 reference: https://gpsd.gitlab.io/gpsd/NMEA.html
- u-blox NEO-6 receiver description (sentence fields, UBX for the future power-save).
- BCM2835 datasheet: PL011 UART (p175), mini-UART (p8), GPIO ALT-function table
  (p102, the GPIO14/15 conflict).
- sw-UART: class `staff-sw-uart.o` (`sw-uart.h`).
