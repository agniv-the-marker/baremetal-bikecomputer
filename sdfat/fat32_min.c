// Minimal safe FAT32 for preallocated-file logging.

#include "fat32_min.h"
#include "emmc.h"

#define EOC 0x0ffffff8u // >= this = end of cluster chain

// read 32 bit val from little endian
static uint32_t rd32(const uint8_t *p) {
    return p[0] | (p[1]<<8) | (p[2]<<16) | ((uint32_t)p[3]<<24);
}

// read 16 bit val from little endian
static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1]<<8); }

// convert cluster number to logical block address
static uint32_t cluster_to_lba(fat32_t *fs, uint32_t c) {
    return fs->cluster_begin_lba + (c - 2) * fs->sec_per_clus;
}

// read the FAT entry for cluster c -> next cluster.
static uint32_t fat_next(fat32_t *fs, uint32_t c) {
    static uint8_t b[512];
    uint32_t byte = c * 4;
    emmc_read(fs->fat_begin_lba + byte / 512, b, 512);
    return rd32(b + (byte % 512)) & 0x0fffffff;
}

// build an 8.3 directory name (11 bytes, space padded, uppercased) from "NAME.EXT"
static void to_83(const char *name, char out[11]) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int o = 0;
    const char *p = name;
    while (*p && *p != '.' && o < 8) {
        char c = *p++;
        if (c >= 'a' && c <= 'z') c -= 32;
        out[o++] = c;
    }
    while (*p && *p != '.') p++;
    if (*p == '.') {
        p++;
        int e = 8;
        while (*p && e < 11) {
            char c = *p++;
            if (c >= 'a' && c <= 'z') c -= 32;
            out[e++] = c;
        }
    }
}

int fat32_mount(fat32_t *fs, uint32_t part_lba) {
    static uint8_t b[512];
    if (emmc_read(part_lba, b, 512) != 512)
        return 0;
    fs->part_lba          = part_lba;
    fs->bytes_per_sec     = rd16(b + 11);
    fs->sec_per_clus      = b[13];
    uint16_t reserved     = rd16(b + 14);
    uint8_t  nfats        = b[16];
    fs->nsec_per_fat      = rd32(b + 36);
    fs->root_cluster      = rd32(b + 44);
    fs->fat_begin_lba     = part_lba + reserved;
    fs->cluster_begin_lba = fs->fat_begin_lba + (uint32_t)nfats * fs->nsec_per_fat;
    return fs->bytes_per_sec == 512 && fs->sec_per_clus > 0;
}

int fat32_find(fat32_t *fs, const char *name, fat32_file_t *f) {
    char target[11];
    to_83(name, target);

    static uint8_t b[512];
    uint32_t c = fs->root_cluster;
    while (c >= 2 && c < EOC) {
        uint32_t lba = cluster_to_lba(fs, c);
        for (unsigned s = 0; s < fs->sec_per_clus; s++) {
            emmc_read(lba + s, b, 512);
            for (unsigned e = 0; e < 512; e += 32) {
                uint8_t *d = b + e;
                if (d[0] == 0x00) return 0; // end of directory
                if (d[0] == 0xe5) continue; // deleted
                if (d[11] == 0x0f) continue; // long-filename entry
                if (memcmp(d, target, 11) == 0) {
                    f->first_cluster = ((uint32_t)rd16(d + 20) << 16) | rd16(d + 26);
                    f->size = rd32(d + 28);
                    f->dirent_lba = lba + s;
                    f->dirent_off = e;
                    return 1;
                }
            }
        }
        c = fat_next(fs, c);
    }
    return 0;
}

uint32_t fat32_chain_len(fat32_t *fs, uint32_t c) {
    uint32_t n = 0;
    while (c >= 2 && c < EOC) {
        n++;
        c = fat_next(fs, c);
        if (n > 4000000) break; // runaway guard
    }
    return n;
}

int fat32_write_into(fat32_t *fs, fat32_file_t *f, const uint8_t *data, uint32_t len) {
    uint32_t clus_bytes = (uint32_t)fs->sec_per_clus * 512;
    uint32_t need = (len + clus_bytes - 1) / clus_bytes;
    if (need == 0) need = 1;
    if (fat32_chain_len(fs, f->first_cluster) < need)
        return 0; // preallocated file too small

    static uint8_t sb[512];
    uint32_t c = f->first_cluster, off = 0;
    for (uint32_t i = 0; i < need; i++) {
        uint32_t lba = cluster_to_lba(fs, c);
        for (unsigned s = 0; s < fs->sec_per_clus && off < len; s++) {
            uint32_t n = len - off; if (n > 512) n = 512;
            for (uint32_t k = 0; k < 512; k++) sb[k] = (k < n) ? data[off + k] : 0;
            if (emmc_write(lba + s, sb, 512) != 512) return 0;
            off += n;
        }
        c = fat_next(fs, c);
    }

    // update only this file's size field in its directory entry.
    emmc_read(f->dirent_lba, sb, 512);
    sb[f->dirent_off + 28] = len & 0xff;
    sb[f->dirent_off + 29] = (len >> 8) & 0xff;
    sb[f->dirent_off + 30] = (len >> 16) & 0xff;
    sb[f->dirent_off + 31] = (len >> 24) & 0xff;
    emmc_write(f->dirent_lba, sb, 512);
    f->size = len;
    return 1;
}

uint32_t fat32_read(fat32_t *fs, fat32_file_t *f, uint8_t *buf, uint32_t maxlen) {
    uint32_t n = f->size < maxlen ? f->size : maxlen;
    static uint8_t sb[512];
    uint32_t c = f->first_cluster, off = 0;
    while (off < n && c >= 2 && c < EOC) {
        uint32_t lba = cluster_to_lba(fs, c);
        for (unsigned s = 0; s < fs->sec_per_clus && off < n; s++) {
            emmc_read(lba + s, sb, 512);
            uint32_t m = n - off; if (m > 512) m = 512;
            for (uint32_t k = 0; k < m; k++) buf[off + k] = sb[k];
            off += m;
        }
        c = fat_next(fs, c);
    }
    return off;
}
