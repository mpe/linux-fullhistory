#ifndef __ASM_SH_HITACHI_SE_H
#define __ASM_SH_HITACHI_SE_H

/*
 * linux/include/asm-sh/hitachi_se.h
 *
 * Copyright (C) 2000  Kazumoto Kojima
 *
 * Hitachi SolutionEngine support
 */

/* Box specific addresses.  */

#define PA_ROM		0x00000000	/* EPROM */
#define PA_ROM_SIZE	0x00400000	/* EPROM size 4M byte */
#define PA_FROM		0x01000000	/* EPROM */
#define PA_FROM_SIZE	0x00400000	/* EPROM size 4M byte */
#define PA_EXT1		0x04000000
#define PA_EXT1_SIZE	0x04000000
#define PA_EXT2		0x08000000
#define PA_EXT2_SIZE	0x04000000
#define PA_SDRAM	0x0c000000
#define PA_SDRAM_SIZE	0x04000000

#define PA_EXT4		0x12000000
#define PA_EXT4_SIZE	0x02000000
#define PA_EXT5		0x14000000
#define PA_EXT5_SIZE	0x04000000
#define PA_PCIC		0x18000000	/* MR-SHPC-01 PCMCIA */

#define PA_83902	0xb0000000	/* DP83902A */
#define PA_83902_IF	0xb0040000	/* DP83902A remote io port */
#define PA_83902_RST	0xb0080000	/* DP83902A reset port */

#define PA_SUPERIO	0xb0400000	/* SMC37C935A super io chip */
#define PA_DIPSW0	0xb0800000	/* Dip switch 5,6 */
#define PA_DIPSW1	0xb0800002	/* Dip switch 7,8 */
#define PA_LED		0xb0c00000	/* LED */
#define PA_BCR		0xb1400000	/* FPGA */

#define PA_MRSHPC	0xb83fffe0	/* MR-SHPC-01 PCMCIA controler */

#define BCR_ILCRA	(PA_BCR + 0)
#define BCR_ILCRB	(PA_BCR + 2)
#define BCR_ILCRC	(PA_BCR + 4)
#define BCR_ILCRD	(PA_BCR + 6)
#define BCR_ILCRE	(PA_BCR + 8)
#define BCR_ILCRF	(PA_BCR + 10)
#define BCR_ILCRG	(PA_BCR + 12)

#define IRQ_STNIC	10

#endif  /* __ASM_SH_HITACHI_SE_H */
