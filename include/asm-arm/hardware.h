/*
 * linux/include/asm-arm/hardware.h
 *
 * Copyright (C) 1996 Russell King
 *
 * Common hardware definitions
 */

#ifndef __ASM_HARDWARE_H
#define __ASM_HARDWARE_H

#include <asm/arch/hardware.h>

/*
 * Use these macros to read/write the IOC.  All it does is perform the actual
 * read/write.
 */
#ifdef HAS_IOC
#ifndef __ASSEMBLER__
#define __IOC(offset)	(IOC_BASE + (offset >> 2))
#else
#define __IOC(offset)	offset
#endif

#define IOC_CONTROL	__IOC(0x00)
#define IOC_KARTTX	__IOC(0x04)
#define IOC_KARTRX	__IOC(0x04)

#define IOC_IRQSTATA	__IOC(0x10)
#define IOC_IRQREQA	__IOC(0x14)
#define IOC_IRQCLRA	__IOC(0x14)
#define IOC_IRQMASKA	__IOC(0x18)

#define IOC_IRQSTATB	__IOC(0x20)
#define IOC_IRQREQB	__IOC(0x24)
#define IOC_IRQMASKB	__IOC(0x28)

#define IOC_FIQSTAT	__IOC(0x30)
#define IOC_FIQREQ	__IOC(0x34)
#define IOC_FIQMASK	__IOC(0x38)

#define IOC_T0CNTL	__IOC(0x40)
#define IOC_T0LTCHL	__IOC(0x40)
#define IOC_T0CNTH	__IOC(0x44)
#define IOC_T0LTCHH	__IOC(0x44)
#define IOC_T0GO	__IOC(0x48)
#define IOC_T0LATCH	__IOC(0x4c)

#define IOC_T1CNTL	__IOC(0x50)
#define IOC_T1LTCHL	__IOC(0x50)
#define IOC_T1CNTH	__IOC(0x54)
#define IOC_T1LTCHH	__IOC(0x54)
#define IOC_T1GO	__IOC(0x58)
#define IOC_T1LATCH	__IOC(0x5c)

#define IOC_T2CNTL	__IOC(0x60)
#define IOC_T2LTCHL	__IOC(0x60)
#define IOC_T2CNTH	__IOC(0x64)
#define IOC_T2LTCHH	__IOC(0x64)
#define IOC_T2GO	__IOC(0x68)
#define IOC_T2LATCH	__IOC(0x6c)

#define IOC_T3CNTL	__IOC(0x70)
#define IOC_T3LTCHL	__IOC(0x70)
#define IOC_T3CNTH	__IOC(0x74)
#define IOC_T3LTCHH	__IOC(0x74)
#define IOC_T3GO	__IOC(0x78)
#define IOC_T3LATCH	__IOC(0x7c)
#endif

#ifdef HAS_MEMC
#define VDMA_ALIGNMENT	PAGE_SIZE
#define VDMA_XFERSIZE	16
#define VDMA_INIT	0
#define VDMA_START	1
#define VDMA_END	2

#define video_set_dma(start,end,offset)				\
do {								\
	memc_write (VDMA_START, (start >> 2));			\
	memc_write (VDMA_END, (end - VDMA_XFERSIZE) >> 2);	\
	memc_write (VDMA_INIT, (offset >> 2));			\
} while (0)
#endif

#ifdef HAS_IOMD
#ifndef __ASSEMBLER__
#define __IOMD(offset)	(IOMD_BASE + (offset >> 2))
#else
#define __IOMD(offset)	offset
#endif

#define IOMD_CONTROL	__IOMD(0x000)
#define IOMD_KARTTX	__IOMD(0x004)
#define IOMD_KARTRX	__IOMD(0x004)
#define IOMD_KCTRL	__IOMD(0x008)

#define IOMD_IRQSTATA	__IOMD(0x010)
#define IOMD_IRQREQA	__IOMD(0x014)
#define IOMD_IRQCLRA	__IOMD(0x014)
#define IOMD_IRQMASKA	__IOMD(0x018)

#define IOMD_IRQSTATB	__IOMD(0x020)
#define IOMD_IRQREQB	__IOMD(0x024)
#define IOMD_IRQMASKB	__IOMD(0x028)

#define IOMD_FIQSTAT	__IOMD(0x030)
#define IOMD_FIQREQ	__IOMD(0x034)
#define IOMD_FIQMASK	__IOMD(0x038)

#define IOMD_T0CNTL	__IOMD(0x040)
#define IOMD_T0LTCHL	__IOMD(0x040)
#define IOMD_T0CNTH	__IOMD(0x044)
#define IOMD_T0LTCHH	__IOMD(0x044)
#define IOMD_T0GO	__IOMD(0x048)
#define IOMD_T0LATCH	__IOMD(0x04c)

#define IOMD_T1CNTL	__IOMD(0x050)
#define IOMD_T1LTCHL	__IOMD(0x050)
#define IOMD_T1CNTH	__IOMD(0x054)
#define IOMD_T1LTCHH	__IOMD(0x054)
#define IOMD_T1GO	__IOMD(0x058)
#define IOMD_T1LATCH	__IOMD(0x05c)

#define IOMD_ROMCR0	__IOMD(0x080)
#define IOMD_ROMCR1	__IOMD(0x084)
#define IOMD_DRAMCR	__IOMD(0x088)
#define IOMD_VREFCR	__IOMD(0x08C)

#define IOMD_FSIZE	__IOMD(0x090)
#define IOMD_ID0	__IOMD(0x094)
#define IOMD_ID1	__IOMD(0x098)
#define IOMD_VERSION	__IOMD(0x09C)

#define IOMD_MOUSEX	__IOMD(0x0A0)
#define IOMD_MOUSEY	__IOMD(0x0A4)

#define IOMD_DMATCR	__IOMD(0x0C0)
#define IOMD_IOTCR	__IOMD(0x0C4)
#define IOMD_ECTCR	__IOMD(0x0C8)
#define IOMD_DMAEXT	__IOMD(0x0CC)

#define IOMD_IO0CURA	__IOMD(0x100)
#define IOMD_IO0ENDA	__IOMD(0x104)
#define IOMD_IO0CURB	__IOMD(0x108)
#define IOMD_IO0ENDB	__IOMD(0x10C)
#define IOMD_IO0CR	__IOMD(0x110)
#define IOMD_IO0ST	__IOMD(0x114)

#define IOMD_IO1CURA	__IOMD(0x120)
#define IOMD_IO1ENDA	__IOMD(0x124)
#define IOMD_IO1CURB	__IOMD(0x128)
#define IOMD_IO1ENDB	__IOMD(0x12C)
#define IOMD_IO1CR	__IOMD(0x130)
#define IOMD_IO1ST	__IOMD(0x134)

#define IOMD_IO2CURA	__IOMD(0x140)
#define IOMD_IO2ENDA	__IOMD(0x144)
#define IOMD_IO2CURB	__IOMD(0x148)
#define IOMD_IO2ENDB	__IOMD(0x14C)
#define IOMD_IO2CR	__IOMD(0x150)
#define IOMD_IO2ST	__IOMD(0x154)

#define IOMD_IO3CURA	__IOMD(0x160)
#define IOMD_IO3ENDA	__IOMD(0x164)
#define IOMD_IO3CURB	__IOMD(0x168)
#define IOMD_IO3ENDB	__IOMD(0x16C)
#define IOMD_IO3CR	__IOMD(0x170)
#define IOMD_IO3ST	__IOMD(0x174)

#define IOMD_SD0CURA	__IOMD(0x180)
#define IOMD_SD0ENDA	__IOMD(0x184)
#define IOMD_SD0CURB	__IOMD(0x188)
#define IOMD_SD0ENDB	__IOMD(0x18C)
#define IOMD_SD0CR	__IOMD(0x190)
#define IOMD_SD0ST	__IOMD(0x194)

#define IOMD_SD1CURA	__IOMD(0x1A0)
#define IOMD_SD1ENDA	__IOMD(0x1A4)
#define IOMD_SD1CURB	__IOMD(0x1A8)
#define IOMD_SD1ENDB	__IOMD(0x1AC)
#define IOMD_SD1CR	__IOMD(0x1B0)
#define IOMD_SD1ST	__IOMD(0x1B4)

#define IOMD_CURSCUR	__IOMD(0x1C0)
#define IOMD_CURSINIT	__IOMD(0x1C4)

#define IOMD_VIDCUR	__IOMD(0x1D0)
#define IOMD_VIDEND	__IOMD(0x1D4)
#define IOMD_VIDSTART	__IOMD(0x1D8)
#define IOMD_VIDINIT	__IOMD(0x1DC)
#define IOMD_VIDCR	__IOMD(0x1E0)

#define IOMD_DMASTAT	__IOMD(0x1F0)
#define IOMD_DMAREQ	__IOMD(0x1F4)
#define IOMD_DMAMASK	__IOMD(0x1F8)

#define DMA_CR_C	0x80
#define DMA_CR_D	0x40
#define DMA_CR_E	0x20

#define DMA_ST_OFL	4
#define DMA_ST_INT	2
#define DMA_ST_AB	1
/*
 * IOC compatability
 */
#define IOC_CONTROL	IOMD_CONTROL
#define IOC_IRQSTATA	IOMD_IRQSTATA
#define IOC_IRQREQA	IOMD_IRQREQA
#define IOC_IRQCLRA	IOMD_IRQCLRA
#define IOC_IRQMASKA	IOMD_IRQMASKA

#define IOC_IRQSTATB	IOMD_IRQSTATB
#define IOC_IRQREQB	IOMD_IRQREQB
#define IOC_IRQMASKB	IOMD_IRQMASKB

#define IOC_FIQSTAT	IOMD_FIQSTAT
#define IOC_FIQREQ	IOMD_FIQREQ
#define IOC_FIQMASK	IOMD_FIQMASK

#define IOC_T0CNTL	IOMD_T0CNTL
#define IOC_T0LTCHL	IOMD_T0LTCHL
#define IOC_T0CNTH	IOMD_T0CNTH
#define IOC_T0LTCHH	IOMD_T0LTCHH
#define IOC_T0GO	IOMD_T0GO
#define IOC_T0LATCH	IOMD_T0LATCH

#define IOC_T1CNTL	IOMD_T1CNTL
#define IOC_T1LTCHL	IOMD_T1LTCHL
#define IOC_T1CNTH	IOMD_T1CNTH
#define IOC_T1LTCHH	IOMD_T1LTCHH
#define IOC_T1GO	IOMD_T1GO
#define IOC_T1LATCH	IOMD_T1LATCH

/*
 * DMA (MEMC) compatability
 */
#define HALF_SAM	vram_half_sam
#define VDMA_ALIGNMENT	(HALF_SAM * 2)
#define VDMA_XFERSIZE	(HALF_SAM)
#define VDMA_INIT	IOMD_VIDINIT
#define VDMA_START	IOMD_VIDSTART
#define VDMA_END	IOMD_VIDEND

#ifndef __ASSEMBLER__
extern unsigned int vram_half_sam;
#define video_set_dma(start,end,offset)				\
do {								\
	outl (SCREEN_START + start, VDMA_START);		\
	outl (SCREEN_START + end - VDMA_XFERSIZE, VDMA_END);	\
	if (offset >= end - VDMA_XFERSIZE)			\
		offset |= 0x40000000;				\
	outl (SCREEN_START + offset, VDMA_INIT);		\
} while (0)
#endif
#endif

#ifdef HAS_EXPMASK
#ifndef __ASSEMBLER__
#define __EXPMASK(offset)	(((volatile unsigned char *)EXPMASK_BASE)[offset])
#else
#define __EXPMASK(offset)	offset
#endif

#define	EXPMASK_STATUS	__EXPMASK(0x00)
#define EXPMASK_ENABLE	__EXPMASK(0x04)

#endif

#endif
