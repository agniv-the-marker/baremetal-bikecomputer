# storage

hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh

files: `sdfat/emmc.{c,h}`, `sdfat/hal.h`, `sdfat/fat32_min.{c,h}`, `gpx_log.{c,h}`.

## emmc

this is jsut ported from cs140e lab 16 bro, so thankfully i dont really need to do anything. however, its worthwhile to note we really dont need to do that much. specifically, we dont actually need to mutate the fat.

specifically we really only need to mount/find/verify capacity via chain_len/write_into/read things. 

thus precreating a ride.gpx (2mb of just 0s) once and overwriting it is easiest. 

heres a singular trackpoint that we care about:
```xml
<trkpt lat="37.4275" lon="-122.1697"><ele>30</ele><time>2026-06-07T15:52:56Z</time></trkpt>
```

## source
- SD Physical Layer + SD Host Controller Simplified Specs:
  https://www.sdcard.org/downloads/pls/ (the CMD/ACMD flow, host registers).
- BCM2835 datasheet EMMC section + errata (https://elinux.org/BCM2835_datasheet_errata).
- Microsoft FAT32 spec; struct layouts from the cs140e `16-fat32-readonly` lab.
- GPX 1.1 schema: https://www.topografix.com/gpx.asp.
