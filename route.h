#ifndef __ROUTE_H__
#define __ROUTE_H__

// Load a planned route (ROUTE.GPX) into a downsampled lat/lon polyline 
// for drawing under the live breadcrumb on the MAP.
#include "rpi.h"
#include "fat32_min.h"

enum { ROUTE_MAX = 1024 };
extern int   route_n;                 // 0 = no route loaded
extern float route_lat[ROUTE_MAX], route_lon[ROUTE_MAX];

// find+read <name> from the SD, parse its <trkpt lat lon> points
// returns route_n (0 if not found / no points).
int route_load(fat32_t *fs, const char *name);

#endif
