#ifndef __GPX_LOG_H__
#define __GPX_LOG_H__

// gpx track logger into preallocated sd file, keep one growing buffer
// on flush append the closing tags so you always have a valid GPX file.

#include "rpi.h"
#include "fat32_min.h"

// bind to a mounted fs + found file, reset buffer to a fresh GPX header.
void gpx_init(fat32_t *fs, fat32_file_t *file);

// append one trackpoint (date/time is UTC). silently ignored if buffer full.
void gpx_add(float lat, float lon, float ele_m,
             int year, int mon, int day, int hh, int mm, int ss);

// write the complete valid GPX (header+points+footer) to the SD file.
// returns 1 on success, 0 on failure (preallocated file too small).
int gpx_flush(void);

unsigned gpx_count(void); // trackpoints logged so far

#endif
