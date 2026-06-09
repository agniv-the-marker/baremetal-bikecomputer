// sd card test, read the MBR (sector 0) and print the partition table.

#include "rpi.h"
#include "emmc.h"

void notmain(void) {
    printk("sd: emmc_init...\n");
    if (!emmc_init()) {
        printk("sd: emmc_init FAILED\n");
        return;
    }
    printk("sd: emmc_init OK\n");

    uint8_t sec[512];
    int r = emmc_read(0, sec, 512);
    printk("sd: read sector 0 -> %d (expect 512)\n", r);
    printk("sd: boot sig = %x %x (expect 55 aa)\n", sec[510], sec[511]);

    for (int i = 0; i < 4; i++) {
        uint8_t *p = sec + 446 + i * 16;
        uint8_t type = p[4];
        uint32_t lba  = p[8] | (p[9]<<8) | (p[10]<<16) | ((uint32_t)p[11]<<24);
        uint32_t nsec = p[12] | (p[13]<<8) | (p[14]<<16) | ((uint32_t)p[15]<<24);
        printk("sd: part%d  type=%x  lba=%d  nsec=%d\n", i, type, lba, nsec);
    }
    printk("sd: done (FAT32 partition is the one with type 0xb or 0xc)\n");
    while (1) {}
}
