// NEO-6M NMEA parser.

#include "gps_nmea.h"

void gps_init(gps_t *g) {
    memset(g, 0, sizeof *g);
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// parse "[-+]ddd[.ddd]" -> float
static float str2f(const char *s) {
    if (!s || !*s) return 0;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    float v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    if (*s == '.') {
        s++;
        float f = 0.1f;
        while (*s >= '0' && *s <= '9') { v += (*s - '0') * f; f *= 0.1f; s++; }
    }
    return neg ? -v : v;
}

static int str2i(const char *s) {
    int v = 0;
    if (!s) return 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v;
}

// NMEA lat "ddmm.mmmm" / lon "dddmm.mmmm" + hemisphere -> decimal degrees.
// the formula is: decimal_degrees = degrees + minutes/60
static float parse_coord(const char *s, char hemi) {
    if (!s || !*s) return 0;
    float raw = str2f(s);              // 3725.6789
    int deg = (int)(raw / 100.0f);     // 37
    float min = raw - deg * 100.0f;    // 25.6789
    float val = deg + min / 60.0f;
    if (hemi == 'S' || hemi == 'W') val = -val;
    return val;
}

// parse a complete sentence (without CR/LF). modifies <line> in place.
static int parse_line(gps_t *g, char *line, int len) {
    if (len < 7 || line[0] != '$') return 0;

    int star = -1;
    for (int i = 0; i < len; i++) if (line[i] == '*') { star = i; break; }
    if (star < 0 || star + 2 >= len) return 0;

    int cs = 0;
    for (int i = 1; i < star; i++) cs ^= (unsigned char)line[i];
    int hi = hexval(line[star + 1]), lo = hexval(line[star + 2]);
    if (hi < 0 || lo < 0 || cs != ((hi << 4) | lo)) return 0;   // bad checksum

    line[star] = 0; // cut off "*HH"

    // split fields on ',' . f[0] = talker+type, "GPRMC".
    char *f[24];
    int nf = 0;
    f[nf++] = line + 1;
    for (char *q = line + 1; *q; q++)
        if (*q == ',') { *q = 0; if (nf < 24) f[nf++] = q + 1; }

    const char *type = f[0] + 2; // skip talker id ("GP"/"GN"/...)

    if (nf >= 10 && strcmp(type, "RMC") == 0) {
        const char *tm = f[1];
        if (strlen(tm) >= 6) {
            g->hh = (tm[0]-'0')*10 + (tm[1]-'0');
            g->mm = (tm[2]-'0')*10 + (tm[3]-'0');
            g->ss = (tm[4]-'0')*10 + (tm[5]-'0');
        }
        g->has_fix = (f[2][0] == 'A');
        if (g->has_fix) {
            g->lat = parse_coord(f[3], f[4][0]);
            g->lon = parse_coord(f[5], f[6][0]);
            g->speed_kmh  = str2f(f[7]) * 1.852f;   // knots -> km/h
            g->course_deg = str2f(f[8]);
        }
        const char *dt = f[9];
        if (strlen(dt) >= 6) {
            g->day   = (dt[0]-'0')*10 + (dt[1]-'0');
            g->month = (dt[2]-'0')*10 + (dt[3]-'0');
            g->year  = 2000 + (dt[4]-'0')*10 + (dt[5]-'0');
        }
        return 1;
    }

    if (nf >= 10 && strcmp(type, "GGA") == 0) {
        g->sats  = str2i(f[7]);
        g->alt_m = str2f(f[9]);
        if (str2i(f[6]) > 0) { // fix quality > 0 => valid position
            g->lat = parse_coord(f[2], f[3][0]);
            g->lon = parse_coord(f[4], f[5][0]);
        }
        return 1;
    }
    return 0;
}

int gps_feed(gps_t *g, char c) {
    if (c == '$') { // start of sentence
        g->_len = 0;
        g->_line[g->_len++] = c;
        return 0;
    }
    if (c == '\r' || c == '\n') { // end of sentence
        if (g->_len > 0) {
            int r = parse_line(g, g->_line, g->_len);
            g->_len = 0;
            return r;
        }
        return 0;
    }
    if (g->_len > 0 && g->_len < (int)sizeof(g->_line) - 1)
        g->_line[g->_len++] = c; // accumulate (only after a '$')
    return 0;
}
