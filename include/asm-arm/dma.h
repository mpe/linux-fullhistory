#ifndef __ASM_ARM_DMA_H
#define __ASM_ARM_DMA_H

#include <asm/irq.h>

#define MAX_DMA_CHANNELS	14
#define DMA_0			8
#define DMA_1			9
#define DMA_2			10
#define DMA_3			11
#define DMA_S0			12
#define DMA_S1			13

#define DMA_MODE_READ		0x44
#define DMA_MODE_WRITE		0x48

extern const char dma_str[];

#include <asm/arch/dma.h>

/* These are in kernel/dma.c: */
/* reserve a DMA channel */
extern int request_dma(unsigned int dmanr, const char * device_id);
/* release it again */
extern void free_dma(unsigned int dmanr);

#endif /* _ARM_DMA_H */

