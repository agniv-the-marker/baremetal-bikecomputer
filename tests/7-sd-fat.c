// fat32 test, mount the filesystem and write a GPX file!
// setup: create a preallocated file on the SD card's FAT32 partition:
//     dd if=/dev/zero of=/Volumes/<card>/RIDE.GPX bs=1M count=2
// pass: pull the card on your PC and confirm RIDE.GPX contains the GPX and imports to Strava

#include "rpi.h"
#include "emmc.h"
#include "fat32_min.h"

enum { FAT_PART_LBA = 2048 }; // from the MBR dump (part0 type 0x0c)

static const char TEST_GPX[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<gpx version=\"1.1\" creator=\"pi-bikecomputer\" "
    "xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
    "<trk><name>pi test</name><trkseg>\n"
    "<trkpt lat=\"37.42486\" lon=\"-122.16196\">"
    "<ele>11</ele><time>2026-06-06T21:52:57Z</time></trkpt>\n"
    "<trkpt lat=\"37.42490\" lon=\"-122.16199\">"
    "<ele>11</ele><time>2026-06-06T21:53:00Z</time></trkpt>\n"
    "</trkseg></trk></gpx>\n";

void notmain(void) {
    if (!emmc_init()) { printk("sd: emmc_init FAILED\n"); return; }
    printk("sd: emmc_init OK\n");

    fat32_t fs;
    if (!fat32_mount(&fs, FAT_PART_LBA)) { printk("fat: mount FAILED\n"); return; }
    printk("fat: mounted. bytes/sec=%d sec/clus=%d fat@%d clus@%d root=%d\n",
           fs.bytes_per_sec, fs.sec_per_clus, fs.fat_begin_lba,
           fs.cluster_begin_lba, fs.root_cluster);

    fat32_file_t f;
    if (!fat32_find(&fs, "RIDE.GPX", &f)) {
        printk("fat: RIDE.GPX NOT FOUND. create it on the card first (see top of file)\n");
        return;
    }
    printk("fat: found RIDE.GPX  first_cluster=%d  size=%d  chain_clusters=%d\n",
           f.first_cluster, f.size, fat32_chain_len(&fs, f.first_cluster));

    unsigned n = strlen(TEST_GPX);
    printk("fat: writing %d bytes of GPX...\n", n);
    if (!fat32_write_into(&fs, &f, (const uint8_t *)TEST_GPX, n)) {
        printk("fat: write FAILED (file too small?)\n");
        return;
    }
    printk("fat: wrote OK, new size=%d\n", f.size);

    // read back the first sector and print it.
    static uint8_t b[512];
    emmc_read(fs.cluster_begin_lba + (f.first_cluster - 2) * fs.sec_per_clus, b, 512);
    b[120] = 0;
    printk("fat: readback: <<<%s>>>\n", b);
    printk("fat: done! pull the card and check RIDE.GPX\n");
    while (1) {}
}
