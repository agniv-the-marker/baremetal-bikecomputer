// bike computer! gps (sw-uart, gpio16) -> ride state + gpx log (sd, dma) -> st7735 tft.
//
// wiring:
//   tft (spi0): SCK->11, SDA->10, CS->8, A0/DC->25, RST->24, LED+VCC->3v3
//   gps:        TXD->16, VCC->5v (neo-6m breakout has a regulator, its tx is 3.3v)
//   buttons:    21 (short=page / long=save), 26 (map zoom), each -> gnd
//   sd:         the boot card itself, needs a preallocated RIDE.GPX
//
// logging auto-starts on first fix, strava-style auto-pause. long press writes a
// complete valid RIDE.GPX (pull the card any time), logging continues after.

#include "rpi.h"
#include "i2c.h"
#include "ssd1306-display-driver.h"
#include "sw-uart.h"
#include "gps_nmea.h"
#include "emmc.h"
#include "fat32_min.h"
#include "gpx_log.h"
#include "power.h"
#include "route.h"
#include "osm_map.h"
#include "st7735.h"
#include "cycle-count.h"    // PMU cycle counter for the SYS diagnostics page
#include "pl011-uart.h"     // hardware UART for GPS (GPS_HW_UART path)
#include "rpi-interrupts.h" // disable_interrupts() (mask CPSR for the wfi trick)
#include "dma_irq.h"        // DMA-completion IRQ -> wfi during the SPI frame push

// USE_FRAME_DMA: push the framebuffer over SPI0 DMA while the cpu wfi()s
// (33.2M -> 3.84M cyc/blit measured).
#ifndef USE_FRAME_DMA
#define USE_FRAME_DMA 1
#endif

// GPS_HW_UART: 1 = pl011 hardware uart (gps txd -> gpio15), cpu sleeps on the
// 16-byte rx fifo. PARKED at 0: gpio15 reads no signal even as a plain input
// (tests/19), probably a dead pin.
#ifndef GPS_HW_UART
#define GPS_HW_UART 0
#endif

// HW_WFI: 1 = sleep on a timer irq between fifo drains (only for the hw-uart path).
#ifndef HW_WFI
#define HW_WFI 0
#endif

// --- display ---

// USE_TFT=1: color st7735 (128x128), 0: mono ssd1306 oled (128x64) fallback.
#ifndef USE_TFT
#define USE_TFT 1
#endif

#if USE_TFT
static st7735_t tft;
typedef uint16_t dcol_t;
enum
{
    DISP_W = ST7735_W,
    DISP_H = ST7735_H
};
#define DCOL_WHITE ST_C_WHITE
#define DCOL_GRAY ST_C_GRAY
#define DCOL_RED ST_C_RED
#define DCOL_GREEN ST_C_GREEN
#define DCOL_CYAN ST_C_CYAN
#define DCOL_YELLOW ST_C_YELLOW
static void d_init(void)
{
    st7735_init(&tft, ST7735_CE0, 25, 24, 16);
    tft.flip180 = 1;
}
static void d_clear(void) { st7735_fb_fill(&tft, ST_C_BLACK); }
#if USE_FRAME_DMA
static void d_show(void) { st7735_flush_dma(&tft); } // DMA push, CPU wfi's
#else
static void d_show(void) { st7735_flush(&tft); } // CPU PIO blit
#endif
static void d_pixel(int x, int y, dcol_t c) { st7735_pixel(&tft, x, y, c); }
static void d_fillrect(int x, int y, int w, int h, dcol_t c) { st7735_fill_rect(&tft, x, y, w, h, c); }
static void d_char(int x, int y, char ch, dcol_t c, int sx, int sy) { st7735_char(&tft, x, y, ch, c, sx, sy); }
static void d_on(void) { st7735_display_on(&tft); }
static void d_off(void) { st7735_display_off(&tft); }
#else
typedef int dcol_t;
enum
{
    DISP_W = 128,
    DISP_H = 64
};
#define DCOL_WHITE COLOR_WHITE
#define DCOL_GRAY COLOR_WHITE
#define DCOL_RED COLOR_WHITE
#define DCOL_GREEN COLOR_WHITE
#define DCOL_CYAN COLOR_WHITE
#define DCOL_YELLOW COLOR_WHITE
static void d_init(void) { ssd1306_display_init(); }
static void d_clear(void) { ssd1306_display_clear(); }
static void d_show(void) { ssd1306_display_show(); }
static void d_pixel(int x, int y, dcol_t c) { ssd1306_display_draw_pixel(x, y, c); }
static void d_fillrect(int x, int y, int w, int h, dcol_t c) { ssd1306_display_draw_fill_rect(x, y, w, h, c); }
static void d_char(int x, int y, char ch, dcol_t c, int sx, int sy) { ssd1306_display_draw_character_size(x, y, ch, c, sx, sy); }
static void d_on(void) { ssd1306_display_on(); }
static void d_off(void) { ssd1306_display_off(); }
#endif

static void d_hline(int x0, int x1, int y, dcol_t c) { d_fillrect(x0, y, x1 - x0 + 1, 1, c); }

extern float sinf(float), cosf(float), sqrtf(float), atan2f(float, float);

enum
{
    GPS_RX = 16,
    GPS_TX = 20,
    GPS_BAUD = 9600,
    BTN_PIN = 21,
    ZOOM_PIN = 26,                // 2nd button (pin 37 -> GND pin 39), cycles map zoom
    MAX_PTS = 512,                // breadcrumb ring buffer
    FAT_PART_LBA = 2048,          // FAT32 partition start (from the MBR)
    GPX_FLUSH_SECS = 30,          // auto-flush cadence
    SCREEN_TIMEOUT_US = 20000000, // OLED-fallback only, sleep after 20s idle
};

typedef enum
{
    PAGE_STATS = 0,
    PAGE_MAP = 1,
    PAGE_SYS = 2,
    PAGE_COUNT = 3
} page_t;

// imperial units + Pacific time.
#define KMH_TO_MPH 0.621371f
#define KM_TO_MI 0.621371f
#define M_TO_FT 3.28084f
#define TZ_OFFSET (-7) // PDT = UTC-7

// Strava-style auto-pause. pause below PAUSE_KMH sustained for PAUSE_HOLD_S,
// resume above RESUME_KMH. moving time excludes paused stretches.
#define PAUSE_KMH 2.0f
#define RESUME_KMH 3.5f
#define PAUSE_HOLD_S 10

// ---- ride state ----

typedef enum
{
    ST_ACQUIRING = 0,
    ST_RIDING,
    ST_ENDED
} ride_state_t;
static ride_state_t rstate = ST_ACQUIRING; // auto-start on first fix

#define HEAD_MIN_KMH 4.0f     // course-over-ground only valid while moving
static float map_heading = 0; // held GPS course for heading-up map

// map zoom = meters across the draw width. ZOOM_PIN cycles these.
static const float ZOOM_LEVELS[] = {600.0f, 1500.0f, 3000.0f, 6000.0f};
enum
{
    N_ZOOM = sizeof(ZOOM_LEVELS) / sizeof(ZOOM_LEVELS[0])
};
static int zoom_idx = 1; // default 1.5km

static float total_km = 0, max_kmh = 0;
static float prev_lat, prev_lon;
static int have_prev = 0;
static float bcl_lat, bcl_lon; // last breadcrumb point
static int have_bc = 0;
static int start_secs = -1, cur_secs = 0; // GPS UTC seconds-of-day
static int last_secs = -1;                // for moving-time dt
static int moving_secs = 0;               // accumulated moving time
static int moving = 1;                    // auto-pause sub-state
static int below_since = -1;              // when speed first dropped low
static float bc_lat[MAX_PTS], bc_lon[MAX_PTS];
static int bc_n = 0, bc_head = 0; // ring buffer

// ---- SD GPX logging ----

static int sd_ok = 0; // SD + RIDE.GPX available
static fat32_t fs;
static fat32_file_t gfile;
static int manual_pause = 0;     // long-press force-pause toggle
static int last_log_sec = -1;    // 1 Hz trackpoint throttle
static int last_flush_sec = -1;  // auto-flush cadence
static uint32_t saved_until = 0; // show "SAVED" until this usec

// ---- SYS diagnostics counters (PAGE_SYS) ----

static uint32_t g_loops = 0;      // main-loop iterations
static uint32_t g_sentences = 0;  // NMEA sentences parsed
static uint32_t g_flushes = 0;    // GPX flushes to SD
static uint32_t g_render_cyc = 0; // PMU cycles for the last full render (+SPI push)
static uint32_t boot_us = 0;

// ---- power: display sleep + render-on-change ----

static int disp_on = 1;
static uint32_t last_activity_us = 0;  // last button/movement -> screen timeout
static uint32_t last_sig = 0xffffffff; // last rendered UI signature

static float deg2rad(float d) { return d * 0.0174532925f; }

// great-circle distance between two lat/lon points (the standard formula:
// a = sin^2(dlat/2) + cos(la1)cos(la2)sin^2(dlon/2), d = 2R*atan2(sqrt(a),sqrt(1-a))).
// https://www.movable-type.co.uk/scripts/latlong.html
static float haversine_km(float la1, float lo1, float la2, float lo2)
{
    float dlat = deg2rad(la2 - la1), dlon = deg2rad(lo2 - lo1);
    float a = sinf(dlat / 2) * sinf(dlat / 2) + cosf(deg2rad(la1)) * cosf(deg2rad(la2)) * sinf(dlon / 2) * sinf(dlon / 2);
    return 6371.0f * 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
}

static void bc_push(float lat, float lon)
{
    bc_lat[bc_head] = lat;
    bc_lon[bc_head] = lon;
    bc_head = (bc_head + 1) % MAX_PTS;
    if (bc_n < MAX_PTS)
        bc_n++;
}

// called on each valid fix. auto-start, moving time w/ auto-pause, distance, breadcrumb.
static void ride_on_fix(const gps_t *g)
{
    cur_secs = g->hh * 3600 + g->mm * 60 + g->ss;

    if (rstate == ST_ACQUIRING)
    { // auto-start
        rstate = ST_RIDING;
        start_secs = cur_secs;
        last_secs = cur_secs;
        moving = 1;
        below_since = -1;
    }
    if (rstate != ST_RIDING)
        return;

    int dt = cur_secs - last_secs;
    if (dt < 0)
        dt += 86400; // midnight wrap
    if (dt > 10)
        dt = 0; // big gap (lost fix): don't count
    last_secs = cur_secs;

    if (g->speed_kmh > max_kmh)
        max_kmh = g->speed_kmh;
    if (g->speed_kmh > HEAD_MIN_KMH)
        map_heading = g->course_deg; // for heading-up map

    // auto-pause, sustained low speed, so paused. clear stop above resume thresh.
    if (g->speed_kmh < PAUSE_KMH)
    {
        if (below_since < 0)
            below_since = cur_secs;
        int low = cur_secs - below_since;
        if (low < 0)
            low += 86400;
        if (low >= PAUSE_HOLD_S)
            moving = 0;
    }
    else if (g->speed_kmh > RESUME_KMH)
    {
        moving = 1;
        below_since = -1;
    }

    if (moving)
        moving_secs += dt; // moving time excludes pauses

    if (have_prev)
    {
        float d = haversine_km(prev_lat, prev_lon, g->lat, g->lon);
        if (d > 0.002f && d < 1.0f)
        {                  // 2m..1km gate: < filters gps jitter
            total_km += d; // when stopped, > drops teleport glitches
            prev_lat = g->lat;
            prev_lon = g->lon;
        }
    }
    else
    {
        prev_lat = g->lat;
        prev_lon = g->lon;
        have_prev = 1;
    }

    // breadcrumb only every ~6m of movement so the map stays clean.
    if (!have_bc)
    {
        bc_push(g->lat, g->lon);
        bcl_lat = g->lat;
        bcl_lon = g->lon;
        have_bc = 1;
    }
    else if (haversine_km(bcl_lat, bcl_lon, g->lat, g->lon) > 0.006f)
    {
        bc_push(g->lat, g->lon);
        bcl_lat = g->lat;
        bcl_lon = g->lon;
    }

    // 1 trackpoint/sec while recording, auto-flush so the card is never >30s stale.
    if (sd_ok && !manual_pause && moving)
    {
        if (cur_secs != last_log_sec)
        {
            gpx_add(g->lat, g->lon, g->alt_m,
                    g->year, g->month, g->day, g->hh, g->mm, g->ss);
            last_log_sec = cur_secs;
        }
        int since = (last_flush_sec < 0) ? 999 : cur_secs - last_flush_sec;
        if (since < 0)
            since += 86400;
        if (since >= GPX_FLUSH_SECS)
        {
            gpx_flush();
            g_flushes++;
            last_flush_sec = cur_secs;
        }
    }
}

// fix lost, break the trail so re-acquisition doesn't draw a wild jump.
static void ride_fix_lost(void)
{
    have_prev = 0;
    have_bc = 0;
    last_secs = -1; // resync dt on next fix
}

// long-press, flush a complete valid gpx NOW + toggle manual hold. not an end!
static void ride_save_toggle(void)
{
    if (sd_ok)
    {
        gpx_flush();
        g_flushes++;
        last_flush_sec = cur_secs;
        saved_until = timer_get_usec() + 2000000; // flash "SAVED" ~2s
    }
    manual_pause = !manual_pause;
}

// ---- drawing ----

static void str_c(int x, int y, const char *s, int sz, dcol_t col)
{
    for (; *s; s++)
    {
        d_char(x, y, *s, col, sz, sz);
        x += 6 * sz;
    }
}
static void str(int x, int y, const char *s, int sz) { str_c(x, y, s, sz, DCOL_WHITE); }
static void fmt1(char *b, unsigned n, float v)
{
    int neg = v < 0;
    if (neg)
        v = -v;
    int w = (int)v, t = (int)((v - w) * 10 + 0.5f);
    if (t == 10)
    {
        w++;
        t = 0;
    }
    snprintk(b, n, "%s%d.%d", neg ? "-" : "", w, t);
}
static void fmt5(char *b, unsigned n, float v)
{
    int neg = v < 0;
    if (neg)
        v = -v;
    int w = (int)v;
    int f = (int)((v - w) * 100000 + 0.5f);
    if (f >= 100000)
    {
        w++;
        f -= 100000;
    }
    snprintk(b, n, "%s%d.%d%d%d%d%d", neg ? "-" : "", w,
             (f / 10000) % 10, (f / 1000) % 10, (f / 100) % 10, (f / 10) % 10, f % 10);
}

static void draw_stats(const gps_t *g)
{
    char l[24], a[12], b[12];

    // top status bar. ride state + sats + Pacific wall clock (12h) + SD/SAVED.
    const char *st = manual_pause          ? "HOLD"
                     : rstate == ST_RIDING ? (moving ? "REC" : "PAUS")
                                           : "...";
    const char *sd = !sd_ok ? " noSD"
                            : (timer_get_usec() < saved_until ? " SAVED" : "");
    int lh = (g->hh + 24 + TZ_OFFSET) % 24; // UTC -> Pacific
    int h12 = lh % 12;
    if (h12 == 0)
        h12 = 12;
    char ap = lh < 12 ? 'a' : 'p';
    snprintk(l, sizeof l, "%s s%d %d:%d%d%c%s",
             st, g->sats, h12, g->mm / 10, g->mm % 10, ap, sd);
    dcol_t scol = manual_pause ? DCOL_CYAN : (moving ? DCOL_GREEN : DCOL_GRAY);
    str_c(0, 0, l, 1, scol); // state colored by mode
    d_hline(0, DISP_W - 1, 10, DCOL_GREEN);

    // BIG speed (size 4 ~ 32px tall) + mph label.
    fmt1(a, sizeof a, g->speed_kmh * KMH_TO_MPH);
    str_c(2, 16, a, 4, DCOL_YELLOW);
    str_c(98, 40, "mph", 1, DCOL_CYAN);
    d_hline(0, DISP_W - 1, 58, DCOL_GRAY);

    // distance (size 2) + moving time (size 2).
    fmt1(a, sizeof a, total_km * KM_TO_MI);
    snprintk(l, sizeof l, "%smi", a);
    str_c(2, 64, l, 2, DCOL_WHITE);

    int el = moving_secs;                                  // Strava-style moving time
    int eh = el / 3600, em = (el / 60) % 60, es = el % 60; // manual zero-pad (no %02d)
    snprintk(l, sizeof l, "%d:%d%d:%d%d", eh, em / 10, em % 10, es / 10, es % 10);
    str_c(2, 86, l, 2, DCOL_CYAN);

    // altitude + max speed (size 1).
    fmt1(a, sizeof a, g->alt_m * M_TO_FT);
    fmt1(b, sizeof b, max_kmh * KMH_TO_MPH);
    snprintk(l, sizeof l, "%sft  max %smph", a, b);
    str_c(2, 108, l, 1, DCOL_GRAY);

    // lat,lon (size 1).
    fmt5(a, sizeof a, g->lat);
    fmt5(b, sizeof b, g->lon);
    snprintk(l, sizeof l, "%s,%s", a, b);
    str_c(2, 118, l, 1, DCOL_GRAY);
}

static dcol_t pen = DCOL_WHITE; // current color for plot()/line()
static void plot(int x, int y)
{ // clipped to the map area
    if (x >= 0 && x < DISP_W && y >= 10 && y < DISP_H)
        d_pixel(x, y, pen);
}

// integer bresenham. dotted=1 draws every other pixel, thick=1 adds 2 offset px.
// three distinguishable styles, roads dashed, route thin-solid, track thick-solid.
// https://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm
static void line(int x0, int y0, int x1, int y1, int dotted, int thick)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1, sy = y0 < y1 ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2, e2, k = 0;
    for (;;)
    {
        if (!dotted || (k++ & 1) == 0)
        {
            plot(x0, y0);
            if (thick)
            {
                plot(x0 + 1, y0);
                plot(x0, y0 + 1);
            }
        }
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

// arrow at (cx,cy) pointing along <heading_deg> (north-up: 0deg = up)
static void draw_arrow(int cx, int cy, float heading_deg)
{
    pen = DCOL_RED;
    float h = heading_deg * 0.0174532925f;
    float dx = sinf(h), dy = -cosf(h);                          // heading 0 -> (0,-1) = up
    float px = -dy, py = dx;                                    // perpendicular
    int tx = cx + (int)(dx * 7), ty = cy + (int)(dy * 7);       // tip
    int bx = cx + (int)(dx * 2), by = cy + (int)(dy * 2);       // arrowhead base
    line(cx, cy, tx, ty, 0, 1);                                 // shaft (thick)
    line(tx, ty, bx + (int)(px * 3), by + (int)(py * 3), 0, 0); // head left
    line(tx, ty, bx - (int)(px * 3), by - (int)(py * 3), 0, 0); // head right
}

// map draw area + shared projection from the combined lat/lon bounds.
enum
{
    MAP_X0 = 2,
    MAP_X1 = DISP_W - 3,
    MAP_Y0 = 13,
    MAP_Y1 = DISP_H - 3
};
static float m_mnla, m_mnlo, m_rla, m_rlo;
static int proj_x(float lon) { return MAP_X0 + (int)((lon - m_mnlo) / m_rlo * (MAP_X1 - MAP_X0)); }
static int proj_y(float lat) { return MAP_Y1 - (int)((lat - m_mnla) / m_rla * (MAP_Y1 - MAP_Y0)); }

// position-centered, fixed-zoom, NORTH-UP road map view. roads dashed + route thin
// + live track thick + you at center with an arrow rotating to gps heading.
// stays north-up so it's stable/legible, only the arrow turns.
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
    return c_cy - (int)((lat - c_clat) * 111320.0f / c_mpp); // north = up
}

static void draw_map_centered(const gps_t *g)
{
    const float VIEW_M = ZOOM_LEVELS[zoom_idx]; // meters across the draw width
    // header shows the zoom so the 2nd button's effect is visible, e.g. "MAP 1.5k".
    int t = (int)(VIEW_M / 100.0f + 0.5f); // width in tenths of a km
    char hdr[12] = {'M', 'A', 'P', ' ', 0};
    int hi = t / 10, k = 4;
    if (hi >= 10)
        hdr[k++] = '0' + (hi / 10) % 10;
    hdr[k++] = '0' + hi % 10;
    hdr[k++] = '.';
    hdr[k++] = '0' + t % 10;
    hdr[k++] = 'k';
    hdr[k] = 0;
    str(0, 0, hdr, 1);
    d_hline(0, DISP_W - 1, 9, DCOL_GREEN);

    c_clat = g->lat;
    c_clon = g->lon;
    c_mpp = VIEW_M / (MAP_X1 - MAP_X0);
    c_coslat = cosf(c_clat * 0.0174532925f);
    c_cx = (MAP_X0 + MAP_X1) / 2;
    c_cy = (MAP_Y0 + MAP_Y1) / 2;

    // cheap lat/lon bbox reject BEFORE projecting! pmu-profiled in tests/23,
    // -36% cyc for identical output. the loop is memory-stall-bound (dcache off)
    // so skipping work per off-screen segment is the whole win. 0.6 = half + margin.
    float dlat = (c_mpp * (MAP_X1 - MAP_X0) * 0.6f) / 111320.0f;
    float dlon = dlat / c_coslat;
    float la0 = c_clat - dlat, la1 = c_clat + dlat;
    float lo0 = c_clon - dlon, lo1 = c_clon + dlon;

    pen = DCOL_GRAY;
    for (int i = 0; i < osm_nseg; i++)
    { // roads (gray, dashed)
        float a, b, c, d;
        osm_get(i, &a, &b, &c, &d);
        if ((a < la0 && c < la0) || (a > la1 && c > la1) ||
            (b < lo0 && d < lo0) || (b > lo1 && d > lo1))
            continue; // bbox reject: skip projection
        int x1 = cpx(a, b), y1 = cpy(a, b), x2 = cpx(c, d), y2 = cpy(c, d);
        if ((x1 < MAP_X0 && x2 < MAP_X0) || (x1 > MAP_X1 && x2 > MAP_X1) ||
            (y1 < MAP_Y0 && y2 < MAP_Y0) || (y1 > MAP_Y1 && y2 > MAP_Y1))
            continue; // both ends off-screen: skip
        line(x1, y1, x2, y2, 1, 0);
    }
    pen = DCOL_CYAN;
    for (int i = 1; i < route_n; i++) // planned route (cyan, thin)
        line(cpx(route_lat[i - 1], route_lon[i - 1]), cpy(route_lat[i - 1], route_lon[i - 1]),
             cpx(route_lat[i], route_lon[i]), cpy(route_lat[i], route_lon[i]), 0, 0);
    pen = DCOL_YELLOW;
    for (int i = 1; i < bc_n; i++)
    { // live track (yellow, thick)
        int A = (bc_n < MAX_PTS) ? i - 1 : (bc_head + i - 1) % MAX_PTS;
        int B = (bc_n < MAX_PTS) ? i : (bc_head + i) % MAX_PTS;
        line(cpx(bc_lat[A], bc_lon[A]), cpy(bc_lat[A], bc_lon[A]),
             cpx(bc_lat[B], bc_lon[B]), cpy(bc_lat[B], bc_lon[B]), 0, 1);
    }
    draw_arrow(c_cx, c_cy, map_heading); // you + heading (red)
}

// whole-ride auto-scaled view, used when no base map / no fix.
static void draw_map_autoscale(void)
{
    str(0, 0, route_n ? "MAP +route" : "MAP", 1);
    d_hline(0, DISP_W - 1, 9, DCOL_GREEN);
    if (bc_n < 2 && route_n < 2)
    {
        str_c(20, 30, "acquiring...", 1, DCOL_GRAY);
        return;
    }

    // bounds over BOTH route and breadcrumb so they're shown to the same scale.
    float mnla = 1e9f, mxla = -1e9f, mnlo = 1e9f, mxlo = -1e9f;
    for (int i = 0; i < route_n; i++)
    {
        if (route_lat[i] < mnla)
            mnla = route_lat[i];
        if (route_lat[i] > mxla)
            mxla = route_lat[i];
        if (route_lon[i] < mnlo)
            mnlo = route_lon[i];
        if (route_lon[i] > mxlo)
            mxlo = route_lon[i];
    }
    for (int i = 0; i < bc_n; i++)
    {
        if (bc_lat[i] < mnla)
            mnla = bc_lat[i];
        if (bc_lat[i] > mxla)
            mxla = bc_lat[i];
        if (bc_lon[i] < mnlo)
            mnlo = bc_lon[i];
        if (bc_lon[i] > mxlo)
            mxlo = bc_lon[i];
    }
    m_mnla = mnla;
    m_mnlo = mnlo;
    m_rla = mxla - mnla;
    if (m_rla < 1e-6f)
        m_rla = 1e-6f;
    m_rlo = mxlo - mnlo;
    if (m_rlo < 1e-6f)
        m_rlo = 1e-6f;

    // planned route: cyan thin polyline (drawn first, so it sits underneath).
    pen = DCOL_CYAN;
    for (int i = 1; i < route_n; i++)
        line(proj_x(route_lon[i - 1]), proj_y(route_lat[i - 1]),
             proj_x(route_lon[i]), proj_y(route_lat[i]), 0, 0);

    // live track: yellow thick polyline + red current-position marker (on top).
    pen = DCOL_YELLOW;
    int px = 0, py = 0, first = 1;
    for (int i = 0; i < bc_n; i++)
    {
        int idx = (bc_n < MAX_PTS) ? i : (bc_head + i) % MAX_PTS; // oldest->newest
        int x = proj_x(bc_lon[idx]), y = proj_y(bc_lat[idx]);
        if (!first)
            line(px, py, x, y, 0, 1);
        px = x;
        py = y;
        first = 0;
    }
    if (!first)
        d_fillrect(px - 1, py - 1, 3, 3, DCOL_RED);
}

// MAP: use the road base-map (position-centered) when we have it AND a fix;
// otherwise the whole-ride auto-scaled trail.
static void draw_map(const gps_t *g)
{
    if (osm_nseg > 0 && g->has_fix)
        draw_map_centered(g);
    else
        draw_map_autoscale();
}

// SYS page: live systems dashboard. loop rate, pmu cost of a full frame
// (clear+draw+spi push), nmea/flush counters, sd-dma status.
// frame is ~3523k cyc at default zoom with frame-dma + cull + icache (was ~36M!).
static void draw_sys(const gps_t *g)
{
    char l[24];
    str_c(0, 0, "SYSTEM", 1, DCOL_GREEN);
    d_hline(0, DISP_W - 1, 10, DCOL_GREEN);

    uint32_t up = (timer_get_usec() - boot_us) / 1000000; // uptime s (wraps ~71m)
    uint32_t lps = up ? g_loops / up : 0;
    snprintk(l, sizeof l, "up    %ds", up);
    str(2, 16, l, 1);
    snprintk(l, sizeof l, "loop/s %d", lps);
    str(2, 28, l, 1);
    snprintk(l, sizeof l, "nmea  %d", g_sentences);
    str(2, 40, l, 1);
    snprintk(l, sizeof l, "fix   %s s%d", g->has_fix ? "yes" : "no", g->sats);
    str(2, 52, l, 1);
    snprintk(l, sizeof l, "flush %d", g_flushes);
    str(2, 64, l, 1);
    snprintk(l, sizeof l, "frame %dk cyc", g_render_cyc / 1000);
    str_c(2, 76, l, 1, DCOL_YELLOW);
    snprintk(l, sizeof l, "roads %d", osm_nseg);
    str(2, 88, l, 1);
    str_c(2, 100, sd_ok ? "SD log: DMA ON" : "SD log: OFF", 1,
          sd_ok ? DCOL_GREEN : DCOL_GRAY);
    str_c(2, 118, "btn1 page  hold=save", 1, DCOL_GRAY);
}

static void render(page_t page, const gps_t *g)
{
    uint32_t t = cycle_cnt_read();
    d_clear();
    if (page == PAGE_STATS)
        draw_stats(g);
    else if (page == PAGE_MAP)
        draw_map(g);
    else
        draw_sys(g);
    d_show();
    g_render_cyc = cycle_cnt_read() - t; // full-frame cost (incl. the SPI blit)
}

// hash of everything shown -> render-on-change (skip the blit if nothing visible
// changed, mostly matters when stopped).
static uint32_t ui_sig(page_t page, const gps_t *g)
{
    uint32_t s = (uint32_t)page * 2654435761u;
    s ^= (uint32_t)(g->speed_kmh * KMH_TO_MPH * 10);
    s ^= (uint32_t)moving_secs * 40503u;
    s ^= (uint32_t)(total_km * KM_TO_MI * 100) * 2246822519u;
    s ^= (uint32_t)g->sats << 4;
    s ^= (uint32_t)manual_pause << 9;
    s ^= (uint32_t)moving << 10;
    s ^= (uint32_t)bc_n << 13;
    s ^= (uint32_t)g->mm << 24; // clock minute
    if (page == PAGE_SYS)
        s ^= timer_get_usec() >> 20; // ~1Hz tick: live counters
    return s;
}

// render only if the display is on AND (forced or content changed).
static void show_ui(page_t page, const gps_t *g, int force)
{
    if (!disp_on)
        return;
    uint32_t s = ui_sig(page, g);
    if (!force && s == last_sig)
        return;
    last_sig = s;
    render(page, g);
}

// register activity; wake the panel if it was asleep.
static void note_activity(page_t page, const gps_t *g)
{
    last_activity_us = timer_get_usec();
    if (!disp_on)
    {
        d_on();
        disp_on = 1;
        last_sig = 0xffffffff;
        show_ui(page, g, 1);
    }
}

#if USE_TFT && USE_FRAME_DMA
// the irq vector (staff interrupts-asm leaves this symbol for us to define).
// only dma-completion irqs are ever enabled, so it just forwards.
void interrupt_vector(unsigned pc)
{
    (void)pc;
    dma_irq_isr();
}
#endif

// feed one gps byte to the parser
static void on_gps_byte(gps_t *g, char c, page_t page, unsigned *sent)
{
    if (!gps_feed(g, c))
        return;
    g_sentences++;
    if (g->has_fix)
    {
        ride_on_fix(g);
        if (moving)
            note_activity(page, g); // riding keeps the screen awake
    }
    else
    {
        ride_fix_lost(); // don't bridge across no-fix gaps
    }
    if (++(*sent) % 5 == 0)
        show_ui(page, g, 0); // ~1 Hz refresh tick
}

void notmain(void)
{
#if GPS_HW_UART
    disable_interrupts();
#endif
    delay_ms(100);
#if !USE_TFT
    i2c_init_clk_div(1500); // OLED only, the ST7735 path uses SPI in d_init()
    delay_ms(100);
#endif
    // icache + branch prediction. ~-28% on everything, tests/23.
    caches_enable();
    d_init();
    printk("bikecomputer: display init OK\n");
    cycle_cnt_init(); // PMU cycle counter for the SYS page
#if USE_TFT && USE_FRAME_DMA
    dma_irq_init(0); // DMA-done IRQ only (no timer -> sw-UART safe)
#endif
    boot_us = timer_get_usec();
    delay_ms(50);

    // paint a first frame NOW, before the slow sd/route/osm loads. avoids a white
    // screen on boot (cheap st7735s sometimes don't latch a single post-init flush).
    gps_t g;
    gps_init(&g);
    page_t page = PAGE_STATS;
    disp_on = 1;
    last_activity_us = timer_get_usec();
    render(page, &g);

    gpio_set_input(BTN_PIN);
    gpio_set_pullup(BTN_PIN);
    gpio_set_input(ZOOM_PIN);
    gpio_set_pullup(ZOOM_PIN);

    // sd gpx logging (still a live computer if the card/file is absent).
    if (emmc_init() && fat32_mount(&fs, FAT_PART_LBA) &&
        fat32_find(&fs, "RIDE.GPX", &gfile))
    {
        gpx_init(&fs, &gfile);
        sd_ok = 1;
        printk("bikecomputer: SD logging ON (RIDE.GPX, %d clusters)\n",
               fat32_chain_len(&fs, gfile.first_cluster));
        // optional route overlay (ROUTE.GPX) + road base map (ROADS.BIN).
        // big multi-MB reads, keep them on the proven pio path.
        if (route_load(&fs, "ROUTE.GPX"))
            printk("bikecomputer: route overlay loaded (%d pts)\n", route_n);
        if (osm_load(&fs, "ROADS.BIN"))
            printk("bikecomputer: road base map loaded (%d segs)\n", osm_nseg);
        // dma only AFTER the big reads: from here on the gpx flush writes go dma.
        emmc_dma_enable(1);
    }
    else
    {
        printk("bikecomputer: SD logging OFF (no card / no RIDE.GPX)\n");
    }

#if GPS_HW_UART
    pl011_rx_only_init(GPS_BAUD); // GPS TXD -> GPIO15 (PL011 RXD); hardware FIFO
#if HW_WFI
    power_wake_timer_init(5000); // ~5ms tick: wake, drain the 16B FIFO, WFI again
#endif
#else
    sw_uart_t u = sw_uart_init(GPS_TX, GPS_RX, GPS_BAUD);
#endif
    show_ui(page, &g, 1); // refresh now that route/osm are loaded

    // main loop: poll gps byte -> buttons -> render-on-change.
    int prev_btn = 0;
    uint32_t press_start = 0;
    int long_done = 0;
    int prev_zoom = 0;
    unsigned sentences = 0;
    while (1)
    {
        g_loops++; // SYS page: loop-rate counter
#if GPS_HW_UART
        // hardware fills the RX FIFO while the CPU sleeps, just drain it on wake.
        while (pl011_has_data())
            on_gps_byte(&g, (char)pl011_get8(), page, &sentences);
#else
        int c = sw_uart_get8_timeout(&u, 20000); // 20ms (bit-banged, CPU busy)
        if (c >= 0)
            on_gps_byte(&g, (char)c, page, &sentences);
#endif

        int btn = !gpio_read(BTN_PIN); // pressed = LOW
        if (btn && !prev_btn)
        { // press begins
            press_start = timer_get_usec();
            long_done = 0;
            if (!disp_on)
            {
                note_activity(page, &g);
                long_done = 1;
            } // wake only, consume
        }
        else if (btn && prev_btn)
        { // held: fire long action once
            if (!long_done && (timer_get_usec() - press_start) > 1000000)
            {
                ride_save_toggle(); // save gpx now + hold toggle
                long_done = 1;
                // keep the panel ON and just show the new HOLD/REC state. (the old
                // deep-sleep DISPOFF here left the screen stuck white mid-ride, bad.)
                note_activity(page, &g);
                show_ui(page, &g, 1);
            }
        }
        else if (!btn && prev_btn)
        { // released
            if (!long_done)
            { // short press = next page
                page = (page + 1) % PAGE_COUNT;
                note_activity(page, &g);
                show_ui(page, &g, 1);
            }
        }
        prev_btn = btn;

        // 2nd button: cycle map zoom (press edge, active-low).
        int zb = !gpio_read(ZOOM_PIN);
        if (zb && !prev_zoom)
        {
            if (!disp_on)
            {
                note_activity(page, &g); // first press just wakes the screen
            }
            else
            {
                zoom_idx = (zoom_idx + 1) % N_ZOOM;
                note_activity(page, &g);
                show_ui(page, &g, 1);
            }
        }
        prev_zoom = zb;

#if !USE_TFT
        // oled only, sleep after inactivity. pointless on the tft as backlight is
        // hardwired to 3v3 so DISPOFF saves nothing and just shows a white panel.
        if (disp_on && (timer_get_usec() - last_activity_us) > SCREEN_TIMEOUT_US)
        {
            d_off();
            disp_on = 0;
        }
#endif

#if GPS_HW_UART && HW_WFI
        // halt the core until the ~5ms timer tick.
        wfi();
        power_wake_ack();
#endif
    }
}
