// Local road base-map loader. See osm_map.h.
#include "osm_map.h"

enum { MAX_SEG = 120000 }; // ~1.9MB buffer; covers a big metro bbox
#define RAW (8 + MAX_SEG * 16) // header + segments
static uint8_t raw[RAW];
int osm_nseg = 0;

static int32_t rd32s(const uint8_t *p) {
    return (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24));
}

int osm_load(fat32_t *fs, const char *name) {
    osm_nseg = 0;
    fat32_file_t f;
    if (!fat32_find(fs, name, &f) || f.size < 8)
        return 0;
    uint32_t n = fat32_read(fs, &f, raw, sizeof raw);
    if (n < 8 || raw[0] != 'R' || raw[1] != 'O' || raw[2] != 'A' || raw[3] != 'D')
        return 0;
    uint32_t nseg = (uint32_t)rd32s(raw + 4);
    uint32_t avail = (n - 8) / 16; // clamp to what we actually read
    if (nseg > avail)   nseg = avail;
    if (nseg > MAX_SEG) nseg = MAX_SEG;
    osm_nseg = (int)nseg;
    return osm_nseg;
}

void osm_get(int i, float *lat1, float *lon1, float *lat2, float *lon2) {
    const uint8_t *p = raw + 8 + (unsigned)i * 16;
    *lat1 = rd32s(p)      * 1e-6f;
    *lon1 = rd32s(p + 4)  * 1e-6f;
    *lat2 = rd32s(p + 8)  * 1e-6f;
    *lon2 = rd32s(p + 12) * 1e-6f;
}
