// Load + parse a GPX route from the SD card.
#include "route.h"

int   route_n = 0;
float route_lat[ROUTE_MAX], route_lon[ROUTE_MAX];

#define RBUF (3 * 1024 * 1024)  // max route GPX we read (~25k trkpts)
static uint8_t rbuf[RBUF];

// Convert a string to a float.
static float str2f(const char *s) {
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    float v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    if (*s == '.') {
        s++; float f = 0.1f;
        while (*s >= '0' && *s <= '9') { v += (*s - '0') * f; f *= 0.1f; s++; }
    }
    return neg ? -v : v;
}

// find the next occurrence of <key> in [p,end)
// return pointer after it or NULL.
static char *find(char *p, char *end, const char *key) {
    unsigned kl = strlen(key);
    for (; p + kl <= end; p++)
        if (memcmp(p, key, kl) == 0)
            return p + kl;
    return NULL;
}

int route_load(fat32_t *fs, const char *name) {
    route_n = 0;
    fat32_file_t f;
    if (!fat32_find(fs, name, &f) || f.size == 0)
        return 0;

    uint32_t n = fat32_read(fs, &f, rbuf, RBUF);
    char *p = (char *)rbuf, *end = (char *)rbuf + n;

    // collect all trkpts into the array, downsampling if > ROUTE_MAX.
    while (route_n < ROUTE_MAX) {
        char *la = find(p, end, "lat=\"");
        if (!la) break;
        char *lo = find(la, end, "lon=\"");
        if (!lo) break;
        route_lat[route_n] = str2f(la);
        route_lon[route_n] = str2f(lo);
        route_n++;
        p = lo;
    }

    // if we hit the limit, we need to downsample the route
    if (route_n == ROUTE_MAX) {
        // count total trkpts
        int total = 0;
        for (char *q = (char *)rbuf; (q = find(q, end, "lat=\"")); ) total++;
        if (total > ROUTE_MAX) {
            int k = 0;
            char *q = (char *)rbuf;
            for (int i = 0; i < total && k < ROUTE_MAX; i++) {
                char *la = find(q, end, "lat=\"");
                char *lo = la ? find(la, end, "lon=\"") : NULL;
                if (!lo) break;
                if ((long)i * ROUTE_MAX / total == k) { // keep evenly spaced
                    route_lat[k] = str2f(la);
                    route_lon[k] = str2f(lo);
                    k++;
                }
                q = lo;
            }
            route_n = k;
        }
    }
    return route_n;
}
