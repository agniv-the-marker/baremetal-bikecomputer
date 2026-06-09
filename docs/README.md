# design doc for bike computer

concept: bare metal gps bike computer on raspberry pi zero. want live ride stats, save gpx for strava and also display on map. 

validated w/ a 23 mile bike ride to alices!

roughly a peripherals/systems project. needed fat32/emmc (140e lab 16), the dma for power optimizations (240lx lab 17), the pmu for monitoring stuff (240lx lab 10). also reused concepts from the display lab.

## high level architecture

just a big ass cooperative while loop (no preemption/scheduling, just constant polling of gps). peripheral base is `0x20000000`

roughly, the main loop is reading a gps byte, sending it over software uart, parsing it (nmea parsing), sending over that data to the st7735 screen over spi, sending the gps data also to the ride state to calculate distance/speed/course/altitute/time/auto-pausing, saving it to gpx_log, and then flushing every 30 seconds. also have 2 buttons over gpio that toggle between subpages and zoom.

## references

mostly just bcm2835 arm peripherals and again class labs.