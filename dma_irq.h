#ifndef __DMA_IRQ_H__
#define __DMA_IRQ_H__

// shared dma completion interrupt support, so cpu can wfi during dma transfers
// each program must define its own interrupt_vector() function that calls dma_irq_isr()
// because the interrupt handler is shared!
// call the irq init once before using IRQ-driven DMA, then per transfer:
//     dma_irq_clear(ch); <start DMA on ch with TI.INTEN>; dma_irq_wait(ch, naps);

// install the vector table, enable DMA channel 4+5 IRQs at the controller, and
// enable IRQs. also arms a ~5ms ARM-timer as a backup wfi-wake so a wait can't hang
void dma_irq_init(int safety_timer);

// call from your interrupt_vector(), write-1-to-clear the INT flag on DMA ch 4/5, records
// completion, and acks the ARM-timer.
void dma_irq_isr(void);

// clear the recorded-done flag for channel <ch> before starting a transfer on it.
void dma_irq_clear(unsigned ch);

// wfi() until channel <ch>'s completion IRQ fired (or <max_naps> timer wakes pass).
// returns 1 if the DMA completed, 0 on timeout.
int dma_irq_wait(unsigned ch, int max_naps);

#endif
