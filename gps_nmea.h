#ifndef __GPS_NMEA_H__
#define __GPS_NMEA_H__

// nmea-0183 parser for neo-6m gps

#include "rpi.h"

typedef struct {
    int   has_fix;          // 1 when RMC status == 'A' (valid 2D/3D fix)
    int   sats;             // satellites in use (from GGA)
    float lat, lon;         // decimal degrees (+ = N / E)
    float speed_kmh;        // ground speed
    float course_deg;       // track made good
    float alt_m;            // altitude above mean sea level (from GGA)
    int   hh, mm, ss;       // UTC time
    int   day, month, year; // UTC date (year is full, e.g. 2026)

    // parser scratchwork
    char  _line[128];
    int   _len;
} gps_t;

// reset parser + fix state.
void gps_init(gps_t *g);

// feed one received byte. returns 1 when a complete, checksum-valid sentence was
// parsed (fields updated), 0 otherwise.
int gps_feed(gps_t *g, char c);

#endif
