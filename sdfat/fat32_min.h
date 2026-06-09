#ifndef __FAT32_MIN_H__
#define __FAT32_MIN_H__

// minimal, safe fat32 for logging into preallocated files.
// never allocate clusters or mutate the FAT!!! only need:
//  - read MBR/BPB/root-dir
//  - follow an existing file's cluster chain
//  - write into its existing data sectors
//  - update that one file's size field
// worst case is garbling the contents of the one preallocated file

#include "rpi.h"

typedef struct {
    uint32_t part_lba;          // partition start (from MBR)
    uint32_t fat_begin_lba;     // first FAT sector
    uint32_t cluster_begin_lba; // sector of cluster #2
    uint32_t nsec_per_fat;
    uint32_t root_cluster;
    uint16_t bytes_per_sec;     // 512
    uint8_t  sec_per_clus;
} fat32_t;

typedef struct {
    uint32_t first_cluster;
    uint32_t size;
    uint32_t dirent_lba;        // sector holding this file's 32-byte dir entry
    uint32_t dirent_off;        // byte offset of the entry within that sector
} fat32_file_t;

// mount the FAT32 partition at <part_lba>. returns 1 on success.
int fat32_mount(fat32_t *fs, uint32_t part_lba);

// find <name> ("RIDE.GPX") in the root directory. returns 1 + fills <f>.
int fat32_find(fat32_t *fs, const char *name, fat32_file_t *f);

// length (clusters) of the file's chain for capacity check before writing.
uint32_t fat32_chain_len(fat32_t *fs, uint32_t first_cluster);

// write <len> bytes into the file's EXISTING clusters and update its size.
// returns 1 on success, 0 if the preallocated file's chain is too small.
int fat32_write_into(fat32_t *fs, fat32_file_t *f, const uint8_t *data, uint32_t len);

// read up to <maxlen> bytes of the file (following its cluster chain) into <buf>.
// returns the number of bytes read (min(file size, maxlen)).
uint32_t fat32_read(fat32_t *fs, fat32_file_t *f, uint8_t *buf, uint32_t maxlen);

#endif
