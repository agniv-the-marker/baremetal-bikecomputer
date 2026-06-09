// dma vs cpu benchmark with pmu cycle counter
// print out:
//   cpu      = cycles the CPU spends doing memcpy(size)        (grows with size)
//   dma_kick = cycles the CPU spends to START the DMA          (~flat)
//   dma_all  = kick + busy-wait for completion                 (reference)

// dma-bench: size      cpu_memcpy   dma_kick   dma_all   (cyc)   ok
// dma-bench: 256       4704         659        2224      1
// dma-bench: 1024      17457        664        3277      1
// dma-bench: 4096      69328        664        10335     1
// dma-bench: 16384     276586       662        36550     1
// dma-bench: 65536     1105541      953        166741    1

#include "rpi.h"
#include "cycle-count.h"
#include "dma-impl.h"

enum { NMAX = 65536 };
static uint8_t src[NMAX], dst[NMAX];

static void fill(unsigned n)   { for (unsigned i = 0; i < n; i++) src[i] = (uint8_t)(i*7 + 1); }
static int  ok(unsigned n)     { for (unsigned i = 0; i < n; i++) if (dst[i] != src[i]) return 0; return 1; }

void notmain(void) {
    cycle_cnt_init();
    dma_ch_t *dma = dma_init(4);

    printk("dma-bench: size      cpu_memcpy   dma_kick   dma_all   (cyc)   ok\n");

    unsigned sizes[] = { 256, 1024, 4096, 16384, 65536 };
    for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        unsigned n = sizes[s];
        fill(n);

        // CPU memcpy
        memset(dst, 0, n);
        uint32_t t = cycle_cnt_read();
        memcpy(dst, src, n);
        uint32_t cpu = cycle_cnt_read() - t;
        int cpu_ok = ok(n);

        // DMA, measure kick (CPU cost) separately from the transfer wait
        memset(dst, 0, n);
        cb_t cb = cb_mk(bus(dst), bus(src), n);
        t = cycle_cnt_read();
        dma_initiate(dma, &cb); // CPU just kicks it off...
        uint32_t kick = cycle_cnt_read() - t;
        t = cycle_cnt_read();
        dma_wait(dma, 1 << 26); // ...transfer happens here
        uint32_t all = kick + (cycle_cnt_read() - t);
        int dma_ok = ok(n);

        printk("dma-bench: %d\t%d\t%d\t%d\t%d\n",
               n, cpu, kick, all, cpu_ok && dma_ok);
    }
    while (1) {}
}
