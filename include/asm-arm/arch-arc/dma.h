#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H

#define MAX_DMA_ADDRESS		0x03000000

#ifdef KERNEL_ARCH_DMA

static inline void arch_disable_dma (int dmanr)
{
    printk (dma_str, "arch_disable_dma", dmanr);
}

static inline void arch_enable_dma (int dmanr)
{
    printk (dma_str, "arch_enable_dma", dmanr);
}

static inline void arch_set_dma_addr (int dmanr, unsigned int addr)
{
    printk (dma_str, "arch_set_dma_addr", dmanr);
}

static inline void arch_set_dma_count (int dmanr, unsigned int count)
{
    printk (dma_str, "arch_set_dma_count", dmanr);
}

static inline void arch_set_dma_mode (int dmanr, char mode)
{
    printk (dma_str, "arch_set_dma_mode", dmanr);
}

static inline int arch_dma_count (int dmanr)
{
    printk (dma_str, "arch_dma_count", dmanr);
    return 0;
}

#endif

/* enable/disable a specific DMA channel */
extern void enable_dma(unsigned int dmanr);

static __inline__ void disable_dma(unsigned int dmanr)
{
    switch(dmanr) {
    case 0:  disable_irq(64); break;
    case 1:  break;
    default: printk (dma_str, "disable_dma", dmanr); break;
    }
}

/* Clear the 'DMA Pointer Flip Flop'.
 * Write 0 for LSB/MSB, 1 for MSB/LSB access.
 * Use this once to initialize the FF to a known state.
 * After that, keep track of it. :-)
 * --- In order to do that, the DMA routines below should ---
 * --- only be used while interrupts are disabled! ---
 */
#define clear_dma_ff(dmanr)

/* set mode (above) for a specific DMA channel */
extern void set_dma_mode(unsigned int dmanr, char mode);

/* Set only the page register bits of the transfer address.
 * This is used for successive transfers when we know the contents of
 * the lower 16 bits of the DMA current address register, but a 64k boundary
 * may have been crossed.
 */
static __inline__ void set_dma_page(unsigned int dmanr, char pagenr)
{
    printk (dma_str, "set_dma_page", dmanr);
}


/* Set transfer address & page bits for specific DMA channel.
 * Assumes dma flipflop is clear.
 */
extern void set_dma_addr(unsigned int dmanr, unsigned int addr);

/* Set transfer size for a specific DMA channel.
 */
extern void set_dma_count(unsigned int dmanr, unsigned int count);

/* Get DMA residue count. After a DMA transfer, this
 * should return zero. Reading this while a DMA transfer is
 * still in progress will return unpredictable results.
 * If called before the channel has been used, it may return 1.
 * Otherwise, it returns the number of _bytes_ left to transfer.
 *
 * Assumes DMA flip-flop is clear.
 */
extern int get_dma_residue(unsigned int dmanr);

#endif /* _ASM_ARCH_DMA_H */
