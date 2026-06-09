// verify the whole-transfer DMA flush, one DREQ-paced DMA moves an entire multi-block payload

#include "rpi.h"
#include "emmc.h"
#include "fat32_min.h"
#include "cycle-count.h"

enum { FAT_PART_LBA = 2048, N = 16384 }; // 16KB = 32 blocks

static fat32_t fs;
static uint8_t out[N], back[N];

// write N bytes via the given DMA <mode>, read back via PIO, compare. returns
// cycles spent in the write (CPU cost) or 0 on failure.
static uint32_t trial(const char *tag, int mode) {
    fat32_file_t f;
    if (!fat32_find(&fs, "RIDE.GPX", &f)) { printk("%s: RIDE.GPX missing\n", tag); return 0; }
    for (int i = 0; i < N; i++) { out[i] = (uint8_t)(i * 5 + mode * 37 + 1); back[i] = 0; }

    emmc_dma_set_mode(mode);
    uint32_t t = cycle_cnt_read();
    int w = fat32_write_into(&fs, &f, out, N);
    uint32_t cyc = cycle_cnt_read() - t;
    emmc_dma_set_mode(0);

    if (!w) { printk("%s: write FAILED\n", tag); return 0; }
    if (!fat32_find(&fs, "RIDE.GPX", &f)) return 0;
    uint32_t r = fat32_read(&fs, &f, back, N);

    int mism = 0, first = -1;
    for (int i = 0; i < N; i++) if (out[i] != back[i]) { mism++; if (first < 0) first = i; }
    if (r >= N && mism == 0) { printk("%s: PASS  write_cyc=%d\n", tag, cyc); return cyc ? cyc : 1; }
    printk("%s: FAIL r=%d mism=%d first@%d\n", tag, r, mism, first);
    return 0;
}

void notmain(void) {
    printk("13-dma-chain: emmc_init...\n");
    if (!emmc_init() || !fat32_mount(&fs, FAT_PART_LBA)) { printk("init FAILED\n"); return; }
    cycle_cnt_init();

    uint32_t c1 = trial("mode1 per-block", 1);
    uint32_t c2 = trial("mode2 whole-xfer", 2);

    if (c1 && c2)
        printk("13-dma-chain: BOTH PASS. per-block=%d cyc  whole=%d cyc  (%d pct of per-block)\n",
               c1, c2, (int)((uint64_t)c2 * 100 / c1));
    else
        printk("13-dma-chain: mode2 not safe -> keep emmc_dma_enable(1) (per-block).\n");
    while (1) {}
}
