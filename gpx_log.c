// GPX track logger.

#include "gpx_log.h"

// <= 2mb
#define CAP (1900 * 1024)

static char buf[CAP];
static unsigned len; // bytes of header+points (NOT including footer)
static unsigned npts;
static fat32_t *g_fs;
static fat32_file_t *g_file;

static const char HEADER[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<gpx version=\"1.1\" creator=\"pi-bikecomputer\" "
    "xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
    "<trk><name>Ride</name><trkseg>\n";
static const char FOOTER[] = "</trkseg></trk></gpx>\n";

static void put_s(const char *s)
{
    while (*s && len < CAP - 1)
        buf[len++] = *s++;
}
static void put_c(char c)
{
    if (len < CAP - 1)
        buf[len++] = c;
}
static void put_u(unsigned v)
{
    char t[12];
    int i = 0;
    if (!v)
    {
        put_c('0');
        return;
    }
    while (v)
    {
        t[i++] = '0' + v % 10;
        v /= 10;
    }
    while (i--)
        put_c(t[i]);
}
static void put_2(unsigned v)
{
    put_c('0' + (v / 10) % 10);
    put_c('0' + v % 10);
}
static void put_i(int v)
{
    if (v < 0)
    {
        put_c('-');
        v = -v;
    }
    put_u((unsigned)v);
}
static void put_f6(float v)
{ // 6 decimal places
    if (v < 0)
    {
        put_c('-');
        v = -v;
    }
    unsigned ip = (unsigned)v;
    put_u(ip);
    put_c('.');
    float fr = v - ip;
    for (int i = 0; i < 6; i++)
    {
        fr *= 10;
        int d = (int)fr;
        put_c('0' + d);
        fr -= d;
    }
}

void gpx_init(fat32_t *fs, fat32_file_t *file)
{
    g_fs = fs;
    g_file = file;
    len = 0;
    npts = 0;
    put_s(HEADER);
}

void gpx_add(float lat, float lon, float ele_m,
             int year, int mon, int day, int hh, int mm, int ss)
{
    if (len + 128 >= CAP)
        return; // buffer full
    put_s("<trkpt lat=\"");
    put_f6(lat);
    put_s("\" lon=\"");
    put_f6(lon);
    put_s("\">");
    put_s("<ele>");
    put_i((int)ele_m);
    put_s("</ele>");
    put_s("<time>");
    put_u((unsigned)year);
    put_c('-');
    put_2(mon);
    put_c('-');
    put_2(day);
    put_c('T');
    put_2(hh);
    put_c(':');
    put_2(mm);
    put_c(':');
    put_2(ss);
    put_c('Z');
    put_s("</time></trkpt>\n");
    npts++;
}

int gpx_flush(void)
{
    if (!g_fs || !g_file)
        return 0;
    unsigned save = len;
    put_s(FOOTER); // temporarily append footer
    int r = fat32_write_into(g_fs, g_file, (const uint8_t *)buf, len);
    len = save; // rewind: next add overwrites footer
    return r;
}

unsigned gpx_count(void) { return npts; }
