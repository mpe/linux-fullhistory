#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H

#define MAX_DMA_ADDRESS		0xd0000000

#ifdef KERNEL_ARCH_DMA

static unsigned char arch_dma_setup;
unsigned char arch_dma_ctrl[8];
unsigned long arch_dma_addr[8];
unsigned long arch_dma_cnt[8];

static inline void arch_enable_dma(int dmanr)
{
  if (!(arch_dma_setup & (1 << dmanr))) {
    arch_dma_setup |= 1 << dmanr;
/*    dma_interrupt (16 + dmanr);*/
  }
  arch_dma_ctrl[dmanr] |= DMA_CR_E;
  switch (dmanr) {
    case 0: outb (arch_dma_ctrl[0], IOMD_IO0CR); break;
    case 1: outb (arch_dma_ctrl[1], IOMD_IO1CR); break;
    case 2: outb (arch_dma_ctrl[2], IOMD_IO2CR); break;
    case 3: outb (arch_dma_ctrl[3], IOMD_IO3CR); break;
    case 4: outb (arch_dma_ctrl[4], IOMD_SD0CR); break;
    case 5: outb (arch_dma_ctrl[5], IOMD_SD1CR); break;
  }
}

static inline void arch_disable_dma(int dmanr)
{
  arch_dma_ctrl[dmanr] &= ~DMA_CR_E;
  switch (dmanr) {
    case 0: outb (arch_dma_ctrl[0], IOMD_IO0CR); break;
    case 1: outb (arch_dma_ctrl[1], IOMD_IO1CR); break;
    case 2: outb (arch_dma_ctrl[2], IOMD_IO2CR); break;
    case 3: outb (arch_dma_ctrl[3], IOMD_IO3CR); break;
    case 4: outb (arch_dma_ctrl[4], IOMD_SD0CR); break;
    case 5: outb (arch_dma_ctrl[5], IOMD_SD1CR); break;
  }
}

static inline void arch_set_dma_addr(int dmanr, unsigned int addr)
{
  arch_dma_setup &= ~dmanr;
  arch_dma_addr[dmanr] = addr;
}

static inline void arch_set_dma_count(int dmanr, unsigned int count)
{
  arch_dma_setup &= ~dmanr;
  arch_dma_cnt[dmanr] = count;
}

static inline void arch_set_dma_mode(int dmanr, char mode)
{
  switch (mode) {
  case DMA_MODE_READ:
    arch_dma_ctrl[dmanr] |= DMA_CR_D;
    break;
  case DMA_MODE_WRITE:
    arch_dma_ctrl[dmanr] &= ~DMA_CR_D;
    break;
  }
}

static inline int arch_dma_count (int dmanr)
{
  return arch_dma_cnt[dmanr];
}
#endif

/* enable/disable a specific DMA channel */
extern void enable_dma(unsigned int dmanr);

static __inline__ void disable_dma(unsigned int dmanr)
{
    switch(dmanr) {
    	case 1:  break;
	case 2:  disable_irq(64); break;
	default: printk(dma_str, "disable_dma", dmanr); break;
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

