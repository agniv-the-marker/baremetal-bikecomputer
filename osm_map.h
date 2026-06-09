#ifndef __OSM_MAP_H__
#define __OSM_MAP_H__

// load ROADS.BIN from the SD card.

#include "rpi.h"
#include "fat32_min.h"

extern int osm_nseg;// 0 = no base map loaded

// load <name> ("ROADS.BIN") from the SD
// returns segment count (0 if absent).
int osm_load(fat32_t *fs, const char *name);

// segment i endpoints, in decimal degrees.
void osm_get(int i, float *lat1, float *lon1, float *lat2, float *lon2);

#endif
