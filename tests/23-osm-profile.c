// pmu profiler on the osm map render! measures cycles/d-cache misses and the instructions:
//   naive  = project every segment (cpx/cpy = float mul+div x4), then pixel-clip
//   culled = cheap lat/lon bounding-box reject FIRST, only project survivors
// pmu should tell us if the loop is compute-bound or stall-bound (eg if culling works!)

// --- caches OFF (libpi default) ---
// naive : drawn=1862  cyc=144407677  icache_miss=0  inst=6262894  IPC=0.4  cyc/seg=3731
// culled: drawn=1862  cyc=88590125  icache_miss=0  inst=3976565  IPC=0.4  cyc/seg=2289
// --- icache + branch prediction ON ---
// naive : drawn=1862  cyc=106406332  icache_miss=54  inst=6262894  IPC=0.5  cyc/seg=2749
// culled: drawn=1862  cyc=62669193  icache_miss=25  inst=3976565  IPC=0.6  cyc/seg=1619

// cyc/seg is what relaly matters! icache_miss tells us if we're stalling on instruction fetch
// so we know that this is a instruction-fetch-stall-bound problem

#include "rpi.h"
#include "emmc.h"
#include "fat32_min.h"
#include "osm_map.h"
#include "cycle-count.h"
#include "armv6-pmu.h"

extern float cosf(float);
enum
{
    FAT_PART_LBA = 2048,
    W = 128,
    H = 128,
    MAP_X0 = 2,
    MAP_X1 = W - 3,
    MAP_Y0 = 13,
    MAP_Y1 = H - 3
};
static fat32_t fs;
static uint8_t fb[W * H]; // dummy framebuffer (exercise the writes)

// same projection as draw_map_centered
static float c_clat, c_clon, c_mpp, c_coslat;
static int c_cx, c_cy;
static int cpx(float lat, float lon)
{
    (void)lat;
    return c_cx + (int)((lon - c_clon) * 111320.0f * c_coslat / c_mpp);
}
static int cpy(float lat, float lon)
{
    (void)lon;
    return c_cy - (int)((lat - c_clat) * 111320.0f / c_mpp);
}
static void plot(int x, int y)
{
    if (x >= 0 && x < W && y >= 10 && y < H)
        fb[y * W + x] = 1;
}
static void line(int x0, int y0, int x1, int y1)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1, dy = y1 > y0 ? y1 - y0 : y0 - y1, sy = y0 < y1 ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2, e2;
    for (;;)
    {
        plot(x0, y0);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = err;
        if (e2 > -dx)
        {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dy)
        {
            err += dx;
            y0 += sy;
        }
    }
}

static int render_naive(void)
{
    int drawn = 0;
    for (int i = 0; i < osm_nseg; i++)
    {
        float a, b, c, d;
        osm_get(i, &a, &b, &c, &d);
        int x1 = cpx(a, b), y1 = cpy(a, b), x2 = cpx(c, d), y2 = cpy(c, d);
        if ((x1 < MAP_X0 && x2 < MAP_X0) || (x1 > MAP_X1 && x2 > MAP_X1) ||
            (y1 < MAP_Y0 && y2 < MAP_Y0) || (y1 > MAP_Y1 && y2 > MAP_Y1))
            continue;
        line(x1, y1, x2, y2);
        drawn++;
    }
    return drawn;
}

static int render_culled(void)
{
    // lat/lon half-window for the view (a touch over half for margin)
    float dlat = (c_mpp * (MAP_X1 - MAP_X0) * 0.6f) / 111320.0f;
    float dlon = dlat / c_coslat;
    float la0 = c_clat - dlat, la1 = c_clat + dlat, lo0 = c_clon - dlon, lo1 = c_clon + dlon;
    int drawn = 0;
    for (int i = 0; i < osm_nseg; i++)
    {
        float a, b, c, d;
        osm_get(i, &a, &b, &c, &d);
        if ((a < la0 && c < la0) || (a > la1 && c > la1) || (b < lo0 && d < lo0) || (b > lo1 && d > lo1))
            continue; // cheap reject
        int x1 = cpx(a, b), y1 = cpy(a, b), x2 = cpx(c, d), y2 = cpy(c, d);
        if ((x1 < MAP_X0 && x2 < MAP_X0) || (x1 > MAP_X1 && x2 > MAP_X1) ||
            (y1 < MAP_Y0 && y2 < MAP_Y0) || (y1 > MAP_Y1 && y2 > MAP_Y1))
            continue;
        line(x1, y1, x2, y2);
        drawn++;
    }
    return drawn;
}

static void profile(const char *tag, int (*fn)(void))
{
    uint32_t c0 = cycle_cnt_read(), d0 = pmu_event_get(0), i0 = pmu_event_get(1);
    int drawn = fn();
    uint32_t cyc = cycle_cnt_read() - c0, dm = pmu_event_get(0) - d0, ins = pmu_event_get(1) - i0;
    // IPC x100 (printk has no float)
    int ipc100 = ins ? (int)((uint64_t)ins * 100 / cyc) : 0;
    printk("%s: drawn=%d  cyc=%d  icache_miss=%d  inst=%d  IPC=0.%d  cyc/seg=%d\n",
           tag, drawn, cyc, dm, ins, ipc100, osm_nseg ? cyc / osm_nseg : 0);
}

void notmain(void)
{
    if (!emmc_init() || !fat32_mount(&fs, FAT_PART_LBA))
    {
        printk("init FAIL\n");
        return;
    }
    if (!osm_load(&fs, "ROADS.BIN"))
    {
        printk("no ROADS.BIN\n");
        return;
    }
    printk("osm-profile: %d segments loaded (~%d KB)\n", osm_nseg, osm_nseg * 16 / 1024);

    // view: centered in the ride bbox, ~1.5km across
    c_clat = 37.4200f;
    c_clon = -122.1600f;
    c_coslat = cosf(c_clat * 0.0174532925f);
    c_mpp = 1500.0f / (MAP_X1 - MAP_X0);
    c_cx = (MAP_X0 + MAP_X1) / 2;
    c_cy = (MAP_Y0 + MAP_Y1) / 2;

    // libpi runs with the L1 D-cache OFF, so DMA doesn't need validation.
    pmu_enable0(0x0); // PMN0 = I-cache miss
    pmu_enable1(0x7); // PMN1 = instructions executed

    printk("--- caches OFF (libpi default) ---\n");
    profile("naive ", render_naive);
    profile("culled", render_culled);

    caches_enable(); // branch prediction + I-cache. no DMA coherence issue, as
                     // DMA never writes code. D-cache stays off.
    printk("--- icache + branch prediction ON ---\n");
    profile("naive ", render_naive);
    profile("culled", render_culled);

    while (1)
    {
    }
}
