/*
 *  linux/include/asm-arm/arch-pxa/pxa-regs.h
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/config.h>

// FIXME hack so that SA-1111.h will work [cb]

#ifndef __ASSEMBLY__
typedef unsigned short  Word16 ;
typedef unsigned int    Word32 ;
typedef Word32          Word ;
typedef Word            Quad [4] ;
typedef void            *Address ;
typedef void            (*ExcpHndlr) (void) ;
#endif

/*
 * PXA Chip selects
 */

#define PXA_CS0_PHYS	0x00000000
#define PXA_CS1_PHYS	0x04000000
#define PXA_CS2_PHYS	0x08000000
#define PXA_CS3_PHYS	0x0C000000
#define PXA_CS4_PHYS	0x10000000
#define PXA_CS5_PHYS	0x14000000


/*
 * Personal Computer Memory Card International Association (PCMCIA) sockets
 */

#define PCMCIAPrtSp	0x04000000	/* PCMCIA Partition Space [byte]   */
#define PCMCIASp	(4*PCMCIAPrtSp)	/* PCMCIA Space [byte]             */
#define PCMCIAIOSp	PCMCIAPrtSp	/* PCMCIA I/O Space [byte]         */
#define PCMCIAAttrSp	PCMCIAPrtSp	/* PCMCIA Attribute Space [byte]   */
#define PCMCIAMemSp	PCMCIAPrtSp	/* PCMCIA Memory Space [byte]      */

#define PCMCIA0Sp	PCMCIASp	/* PCMCIA 0 Space [byte]           */
#define PCMCIA0IOSp	PCMCIAIOSp	/* PCMCIA 0 I/O Space [byte]       */
#define PCMCIA0AttrSp	PCMCIAAttrSp	/* PCMCIA 0 Attribute Space [byte] */
#define PCMCIA0MemSp	PCMCIAMemSp	/* PCMCIA 0 Memory Space [byte]    */

#define PCMCIA1Sp	PCMCIASp	/* PCMCIA 1 Space [byte]           */
#define PCMCIA1IOSp	PCMCIAIOSp	/* PCMCIA 1 I/O Space [byte]       */
#define PCMCIA1AttrSp	PCMCIAAttrSp	/* PCMCIA 1 Attribute Space [byte] */
#define PCMCIA1MemSp	PCMCIAMemSp	/* PCMCIA 1 Memory Space [byte]    */

#define _PCMCIA(Nb)	        	/* PCMCIA [0..1]                   */ \
                	(0x20000000 + (Nb)*PCMCIASp)
#define _PCMCIAIO(Nb)	_PCMCIA (Nb)	/* PCMCIA I/O [0..1]               */
#define _PCMCIAAttr(Nb)	        	/* PCMCIA Attribute [0..1]         */ \
                	(_PCMCIA (Nb) + 2*PCMCIAPrtSp)
#define _PCMCIAMem(Nb)	        	/* PCMCIA Memory [0..1]            */ \
                	(_PCMCIA (Nb) + 3*PCMCIAPrtSp)

#define _PCMCIA0	_PCMCIA (0)	/* PCMCIA 0                        */
#define _PCMCIA0IO	_PCMCIAIO (0)	/* PCMCIA 0 I/O                    */
#define _PCMCIA0Attr	_PCMCIAAttr (0)	/* PCMCIA 0 Attribute              */
#define _PCMCIA0Mem	_PCMCIAMem (0)	/* PCMCIA 0 Memory                 */

#define _PCMCIA1	_PCMCIA (1)	/* PCMCIA 1                        */
#define _PCMCIA1IO	_PCMCIAIO (1)	/* PCMCIA 1 I/O                    */
#define _PCMCIA1Attr	_PCMCIAAttr (1)	/* PCMCIA 1 Attribute              */
#define _PCMCIA1Mem	_PCMCIAMem (1)	/* PCMCIA 1 Memory                 */



/*
 * DMA Controller
 */

#define DCSR0		__REG(0x40000000)  /* DMA Control / Status Register for Channel 0 */
#define DCSR1		__REG(0x40000004)  /* DMA Control / Status Register for Channel 1 */
#define DCSR2		__REG(0x40000008)  /* DMA Control / Status Register for Channel 2 */
#define DCSR3		__REG(0x4000000c)  /* DMA Control / Status Register for Channel 3 */
#define DCSR4		__REG(0x40000010)  /* DMA Control / Status Register for Channel 4 */
#define DCSR5		__REG(0x40000014)  /* DMA Control / Status Register for Channel 5 */
#define DCSR6		__REG(0x40000018)  /* DMA Control / Status Register for Channel 6 */
#define DCSR7		__REG(0x4000001c)  /* DMA Control / Status Register for Channel 7 */
#define DCSR8		__REG(0x40000020)  /* DMA Control / Status Register for Channel 8 */
#define DCSR9		__REG(0x40000024)  /* DMA Control / Status Register for Channel 9 */
#define DCSR10		__REG(0x40000028)  /* DMA Control / Status Register for Channel 10 */
#define DCSR11		__REG(0x4000002c)  /* DMA Control / Status Register for Channel 11 */
#define DCSR12		__REG(0x40000030)  /* DMA Control / Status Register for Channel 12 */
#define DCSR13		__REG(0x40000034)  /* DMA Control / Status Register for Channel 13 */
#define DCSR14		__REG(0x40000038)  /* DMA Control / Status Register for Channel 14 */
#define DCSR15		__REG(0x4000003c)  /* DMA Control / Status Register for Channel 15 */

#define DCSR(x)		__REG2(0x40000000, (x) << 2)

#define DCSR_RUN	(1 << 31)	/* Run Bit (read / write) */
#define DCSR_NODESC	(1 << 30)	/* No-Descriptor Fetch (read / write) */
#define DCSR_STOPIRQEN	(1 << 29)	/* Stop Interrupt Enable (read / write) */
#define DCSR_REQPEND	(1 << 8)	/* Request Pending (read-only) */
#define DCSR_STOPSTATE	(1 << 3)	/* Stop State (read-only) */
#define DCSR_ENDINTR	(1 << 2)	/* End Interrupt (read / write) */
#define DCSR_STARTINTR	(1 << 1)	/* Start Interrupt (read / write) */
#define DCSR_BUSERR	(1 << 0)	/* Bus Error Interrupt (read / write) */

#define DINT		__REG(0x400000f0)  /* DMA Interrupt Register */

#define DRCMR0		__REG(0x40000100)  /* Request to Channel Map Register for DREQ 0 */
#define DRCMR1		__REG(0x40000104)  /* Request to Channel Map Register for DREQ 1 */
#define DRCMR2		__REG(0x40000108)  /* Request to Channel Map Register for I2S receive Request */
#define DRCMR3		__REG(0x4000010c)  /* Request to Channel Map Register for I2S transmit Request */
#define DRCMR4		__REG(0x40000110)  /* Request to Channel Map Register for BTUART receive Request */
#define DRCMR5		__REG(0x40000114)  /* Request to Channel Map Register for BTUART transmit Request. */
#define DRCMR6		__REG(0x40000118)  /* Request to Channel Map Register for FFUART receive Request */
#define DRCMR7		__REG(0x4000011c)  /* Request to Channel Map Register for FFUART transmit Request */
#define DRCMR8		__REG(0x40000120)  /* Request to Channel Map Register for AC97 microphone Request */
#define DRCMR9		__REG(0x40000124)  /* Request to Channel Map Register for AC97 modem receive Request */
#define DRCMR10		__REG(0x40000128)  /* Request to Channel Map Register for AC97 modem transmit Request */
#define DRCMR11		__REG(0x4000012c)  /* Request to Channel Map Register for AC97 audio receive Request */
#define DRCMR12		__REG(0x40000130)  /* Request to Channel Map Register for AC97 audio transmit Request */
#define DRCMR13		__REG(0x40000134)  /* Request to Channel Map Register for SSP receive Request */
#define DRCMR14		__REG(0x40000138)  /* Request to Channel Map Register for SSP transmit Request */
#define DRCMR15		__REG(0x4000013c)  /* Reserved */
#define DRCMR16		__REG(0x40000140)  /* Reserved */
#define DRCMR17		__REG(0x40000144)  /* Request to Channel Map Register for ICP receive Request */
#define DRCMR18		__REG(0x40000148)  /* Request to Channel Map Register for ICP transmit Request */
#define DRCMR19		__REG(0x4000014c)  /* Request to Channel Map Register for STUART receive Request */
#define DRCMR20		__REG(0x40000150)  /* Request to Channel Map Register for STUART transmit Request */
#define DRCMR21		__REG(0x40000154)  /* Request to Channel Map Register for MMC receive Request */
#define DRCMR22		__REG(0x40000158)  /* Request to Channel Map Register for MMC transmit Request */
#define DRCMR23		__REG(0x4000015c)  /* Reserved */
#define DRCMR24		__REG(0x40000160)  /* Reserved */
#define DRCMR25		__REG(0x40000164)  /* Request to Channel Map Register for USB endpoint 1 Request */
#define DRCMR26		__REG(0x40000168)  /* Request to Channel Map Register for USB endpoint 2 Request */
#define DRCMR27		__REG(0x4000016C)  /* Request to Channel Map Register for USB endpoint 3 Request */
#define DRCMR28		__REG(0x40000170)  /* Request to Channel Map Register for USB endpoint 4 Request */
#define DRCMR29		__REG(0x40000174)  /* Reserved */
#define DRCMR30		__REG(0x40000178)  /* Request to Channel Map Register for USB endpoint 6 Request */
#define DRCMR31		__REG(0x4000017C)  /* Request to Channel Map Register for USB endpoint 7 Request */
#define DRCMR32		__REG(0x40000180)  /* Request to Channel Map Register for USB endpoint 8 Request */
#define DRCMR33		__REG(0x40000184)  /* Request to Channel Map Register for USB endpoint 9 Request */
#define DRCMR34		__REG(0x40000188)  /* Reserved */
#define DRCMR35		__REG(0x4000018C)  /* Request to Channel Map Register for USB endpoint 11 Request */
#define DRCMR36		__REG(0x40000190)  /* Request to Channel Map Register for USB endpoint 12 Request */
#define DRCMR37		__REG(0x40000194)  /* Request to Channel Map Register for USB endpoint 13 Request */
#define DRCMR38		__REG(0x40000198)  /* Request to Channel Map Register for USB endpoint 14 Request */
#define DRCMR39		__REG(0x4000019C)  /* Reserved */

#define DRCMRRXSADR	DRCMR2
#define DRCMRTXSADR	DRCMR3
#define DRCMRRXBTRBR	DRCMR4
#define DRCMRTXBTTHR	DRCMR5
#define DRCMRRXFFRBR	DRCMR6
#define DRCMRTXFFTHR	DRCMR7
#define DRCMRRXMCDR	DRCMR8
#define DRCMRRXMODR	DRCMR9
#define DRCMRTXMODR	DRCMR10
#define DRCMRRXPCDR	DRCMR11
#define DRCMRTXPCDR	DRCMR12
#define DRCMRRXSSDR	DRCMR13
#define DRCMRTXSSDR	DRCMR14
#define DRCMRRXICDR	DRCMR17
#define DRCMRTXICDR	DRCMR18
#define DRCMRRXSTRBR	DRCMR19
#define DRCMRTXSTTHR	DRCMR20
#define DRCMRRXMMC	DRCMR21
#define DRCMRTXMMC	DRCMR22

#define DRCMR_MAPVLD	(1 << 7)	/* Map Valid (read / write) */
#define DRCMR_CHLNUM	0x0f		/* mask for Channel Number (read / write) */

#define DDADR0		__REG(0x40000200)  /* DMA Descriptor Address Register Channel 0 */
#define DSADR0		__REG(0x40000204)  /* DMA Source Address Register Channel 0 */
#define DTADR0		__REG(0x40000208)  /* DMA Target Address Register Channel 0 */
#define DCMD0		__REG(0x4000020c)  /* DMA Command Address Register Channel 0 */
#define DDADR1		__REG(0x40000210)  /* DMA Descriptor Address Register Channel 1 */
#define DSADR1		__REG(0x40000214)  /* DMA Source Address Register Channel 1 */
#define DTADR1		__REG(0x40000218)  /* DMA Target Address Register Channel 1 */
#define DCMD1		__REG(0x4000021c)  /* DMA Command Address Register Channel 1 */
#define DDADR2		__REG(0x40000220)  /* DMA Descriptor Address Register Channel 2 */
#define DSADR2		__REG(0x40000224)  /* DMA Source Address Register Channel 2 */
#define DTADR2		__REG(0x40000228)  /* DMA Target Address Register Channel 2 */
#define DCMD2		__REG(0x4000022c)  /* DMA Command Address Register Channel 2 */
#define DDADR3		__REG(0x40000230)  /* DMA Descriptor Address Register Channel 3 */
#define DSADR3		__REG(0x40000234)  /* DMA Source Address Register Channel 3 */
#define DTADR3		__REG(0x40000238)  /* DMA Target Address Register Channel 3 */
#define DCMD3		__REG(0x4000023c)  /* DMA Command Address Register Channel 3 */
#define DDADR4		__REG(0x40000240)  /* DMA Descriptor Address Register Channel 4 */
#define DSADR4		__REG(0x40000244)  /* DMA Source Address Register Channel 4 */
#define DTADR4		__REG(0x40000248)  /* DMA Target Address Register Channel 4 */
#define DCMD4		__REG(0x4000024c)  /* DMA Command Address Register Channel 4 */
#define DDADR5		__REG(0x40000250)  /* DMA Descriptor Address Register Channel 5 */
#define DSADR5		__REG(0x40000254)  /* DMA Source Address Register Channel 5 */
#define DTADR5		__REG(0x40000258)  /* DMA Target Address Register Channel 5 */
#define DCMD5		__REG(0x4000025c)  /* DMA Command Address Register Channel 5 */
#define DDADR6		__REG(0x40000260)  /* DMA Descriptor Address Register Channel 6 */
#define DSADR6		__REG(0x40000264)  /* DMA Source Address Register Channel 6 */
#define DTADR6		__REG(0x40000268)  /* DMA Target Address Register Channel 6 */
#define DCMD6		__REG(0x4000026c)  /* DMA Command Address Register Channel 6 */
#define DDADR7		__REG(0x40000270)  /* DMA Descriptor Address Register Channel 7 */
#define DSADR7		__REG(0x40000274)  /* DMA Source Address Register Channel 7 */
#define DTADR7		__REG(0x40000278)  /* DMA Target Address Register Channel 7 */
#define DCMD7		__REG(0x4000027c)  /* DMA Command Address Register Channel 7 */
#define DDADR8		__REG(0x40000280)  /* DMA Descriptor Address Register Channel 8 */
#define DSADR8		__REG(0x40000284)  /* DMA Source Address Register Channel 8 */
#define DTADR8		__REG(0x40000288)  /* DMA Target Address Register Channel 8 */
#define DCMD8		__REG(0x4000028c)  /* DMA Command Address Register Channel 8 */
#define DDADR9		__REG(0x40000290)  /* DMA Descriptor Address Register Channel 9 */
#define DSADR9		__REG(0x40000294)  /* DMA Source Address Register Channel 9 */
#define DTADR9		__REG(0x40000298)  /* DMA Target Address Register Channel 9 */
#define DCMD9		__REG(0x4000029c)  /* DMA Command Address Register Channel 9 */
#define DDADR10		__REG(0x400002a0)  /* DMA Descriptor Address Register Channel 10 */
#define DSADR10		__REG(0x400002a4)  /* DMA Source Address Register Channel 10 */
#define DTADR10		__REG(0x400002a8)  /* DMA Target Address Register Channel 10 */
#define DCMD10		__REG(0x400002ac)  /* DMA Command Address Register Channel 10 */
#define DDADR11		__REG(0x400002b0)  /* DMA Descriptor Address Register Channel 11 */
#define DSADR11		__REG(0x400002b4)  /* DMA Source Address Register Channel 11 */
#define DTADR11		__REG(0x400002b8)  /* DMA Target Address Register Channel 11 */
#define DCMD11		__REG(0x400002bc)  /* DMA Command Address Register Channel 11 */
#define DDADR12		__REG(0x400002c0)  /* DMA Descriptor Address Register Channel 12 */
#define DSADR12		__REG(0x400002c4)  /* DMA Source Address Register Channel 12 */
#define DTADR12		__REG(0x400002c8)  /* DMA Target Address Register Channel 12 */
#define DCMD12		__REG(0x400002cc)  /* DMA Command Address Register Channel 12 */
#define DDADR13		__REG(0x400002d0)  /* DMA Descriptor Address Register Channel 13 */
#define DSADR13		__REG(0x400002d4)  /* DMA Source Address Register Channel 13 */
#define DTADR13		__REG(0x400002d8)  /* DMA Target Address Register Channel 13 */
#define DCMD13		__REG(0x400002dc)  /* DMA Command Address Register Channel 13 */
#define DDADR14		__REG(0x400002e0)  /* DMA Descriptor Address Register Channel 14 */
#define DSADR14		__REG(0x400002e4)  /* DMA Source Address Register Channel 14 */
#define DTADR14		__REG(0x400002e8)  /* DMA Target Address Register Channel 14 */
#define DCMD14		__REG(0x400002ec)  /* DMA Command Address Register Channel 14 */
#define DDADR15		__REG(0x400002f0)  /* DMA Descriptor Address Register Channel 15 */
#define DSADR15		__REG(0x400002f4)  /* DMA Source Address Register Channel 15 */
#define DTADR15		__REG(0x400002f8)  /* DMA Target Address Register Channel 15 */
#define DCMD15		__REG(0x400002fc)  /* DMA Command Address Register Channel 15 */

#define DDADR(x)	__REG2(0x40000200, (x) << 4)
#define DSADR(x)	__REG2(0x40000204, (x) << 4)
#define DTADR(x)	__REG2(0x40000208, (x) << 4)
#define DCMD(x)		__REG2(0x4000020c, (x) << 4)

#define DDADR_DESCADDR	0xfffffff0	/* Address of next descriptor (mask) */
#define DDADR_STOP	(1 << 0)	/* Stop (read / write) */

#define DCMD_INCSRCADDR	(1 << 31)	/* Source Address Increment Setting. */
#define DCMD_INCTRGADDR	(1 << 30)	/* Target Address Increment Setting. */
#define DCMD_FLOWSRC	(1 << 29)	/* Flow Control by the source. */
#define DCMD_FLOWTRG	(1 << 28)	/* Flow Control by the target. */
#define DCMD_STARTIRQEN	(1 << 22)	/* Start Interrupt Enable */
#define DCMD_ENDIRQEN	(1 << 21)	/* End Interrupt Enable */
#define DCMD_ENDIAN	(1 << 18)	/* Device Endian-ness. */
#define DCMD_BURST8	(1 << 16)	/* 8 byte burst */
#define DCMD_BURST16	(2 << 16)	/* 16 byte burst */
#define DCMD_BURST32	(3 << 16)	/* 32 byte burst */
#define DCMD_WIDTH1	(1 << 14)	/* 1 byte width */
#define DCMD_WIDTH2	(2 << 14)	/* 2 byte width (HalfWord) */
#define DCMD_WIDTH4	(3 << 14)	/* 4 byte width (Word) */
#define DCMD_LENGTH	0x01fff		/* length mask (max = 8K - 1) */

/* default combinations */
#define DCMD_RXPCDR	(DCMD_INCTRGADDR|DCMD_FLOWSRC|DCMD_BURST32|DCMD_WIDTH4)
#define DCMD_RXMCDR	(DCMD_INCTRGADDR|DCMD_FLOWSRC|DCMD_BURST32|DCMD_WIDTH4)
#define DCMD_TXPCDR	(DCMD_INCSRCADDR|DCMD_FLOWTRG|DCMD_BURST32|DCMD_WIDTH4)


/*
 * UARTs
 */

/* Full Function UART (FFUART) */
#define FFUART		FFRBR
#define FFRBR		__REG(0x40100000)  /* Receive Buffer Register (read only) */
#define FFTHR		__REG(0x40100000)  /* Transmit Holding Register (write only) */
#define FFIER		__REG(0x40100004)  /* Interrupt Enable Register (read/write) */
#define FFIIR		__REG(0x40100008)  /* Interrupt ID Register (read only) */
#define FFFCR		__REG(0x40100008)  /* FIFO Control Register (write only) */
#define FFLCR		__REG(0x4010000C)  /* Line Control Register (read/write) */
#define FFMCR		__REG(0x40100010)  /* Modem Control Register (read/write) */
#define FFLSR		__REG(0x40100014)  /* Line Status Register (read only) */
#define FFMSR		__REG(0x40100018)  /* Modem Status Register (read only) */
#define FFSPR		__REG(0x4010001C)  /* Scratch Pad Register (read/write) */
#define FFISR		__REG(0x40100020)  /* Infrared Selection Register (read/write) */
#define FFDLL		__REG(0x40100000)  /* Divisor Latch Low Register (DLAB = 1) (read/write) */
#define FFDLH		__REG(0x40100004)  /* Divisor Latch High Register (DLAB = 1) (read/write) */

/* Bluetooth UART (BTUART) */
#define BTUART		BTRBR
#define BTRBR		__REG(0x40200000)  /* Receive Buffer Register (read only) */
#define BTTHR		__REG(0x40200000)  /* Transmit Holding Register (write only) */
#define BTIER		__REG(0x40200004)  /* Interrupt Enable Register (read/write) */
#define BTIIR		__REG(0x40200008)  /* Interrupt ID Register (read only) */
#define BTFCR		__REG(0x40200008)  /* FIFO Control Register (write only) */
#define BTLCR		__REG(0x4020000C)  /* Line Control Register (read/write) */
#define BTMCR		__REG(0x40200010)  /* Modem Control Register (read/write) */
#define BTLSR		__REG(0x40200014)  /* Line Status Register (read only) */
#define BTMSR		__REG(0x40200018)  /* Modem Status Register (read only) */
#define BTSPR		__REG(0x4020001C)  /* Scratch Pad Register (read/write) */
#define BTISR		__REG(0x40200020)  /* Infrared Selection Register (read/write) */
#define BTDLL		__REG(0x40200000)  /* Divisor Latch Low Register (DLAB = 1) (read/write) */
#define BTDLH		__REG(0x40200004)  /* Divisor Latch High Register (DLAB = 1) (read/write) */

/* Standard UART (STUART) */
#define STUART		STRBR
#define STRBR		__REG(0x40700000)  /* Receive Buffer Register (read only) */
#define STTHR		__REG(0x40700000)  /* Transmit Holding Register (write only) */
#define STIER		__REG(0x40700004)  /* Interrupt Enable Register (read/write) */
#define STIIR		__REG(0x40700008)  /* Interrupt ID Register (read only) */
#define STFCR		__REG(0x40700008)  /* FIFO Control Register (write only) */
#define STLCR		__REG(0x4070000C)  /* Line Control Register (read/write) */
#define STMCR		__REG(0x40700010)  /* Modem Control Register (read/write) */
#define STLSR		__REG(0x40700014)  /* Line Status Register (read only) */
#define STMSR		__REG(0x40700018)  /* Reserved */
#define STSPR		__REG(0x4070001C)  /* Scratch Pad Register (read/write) */
#define STISR		__REG(0x40700020)  /* Infrared Selection Register (read/write) */
#define STDLL		__REG(0x40700000)  /* Divisor Latch Low Register (DLAB = 1) (read/write) */
#define STDLH		__REG(0x40700004)  /* Divisor Latch High Register (DLAB = 1) (read/write) */

#define IER_DMAE	(1 << 7)	/* DMA Requests Enable */
#define IER_UUE		(1 << 6)	/* UART Unit Enable */
#define IER_NRZE	(1 << 5)	/* NRZ coding Enable */
#define IER_RTIOE	(1 << 4)	/* Receiver Time Out Interrupt Enable */
#define IER_MIE		(1 << 3)	/* Modem Interrupt Enable */
#define IER_RLSE	(1 << 2)	/* Receiver Line Status Interrupt Enable */
#define IER_TIE		(1 << 1)	/* Transmit Data request Interrupt Enable */
#define IER_RAVIE	(1 << 0)	/* Receiver Data Available Interrupt Enable */

#define IIR_FIFOES1	(1 << 7)	/* FIFO Mode Enable Status */
#define IIR_FIFOES0	(1 << 6)	/* FIFO Mode Enable Status */
#define IIR_TOD		(1 << 3)	/* Time Out Detected */
#define IIR_IID2	(1 << 2)	/* Interrupt Source Encoded */
#define IIR_IID1	(1 << 1)	/* Interrupt Source Encoded */
#define IIR_IP		(1 << 0)	/* Interrupt Pending (active low) */

#define FCR_ITL2	(1 << 7)	/* Interrupt Trigger Level */
#define FCR_ITL1	(1 << 6)	/* Interrupt Trigger Level */
#define FCR_RESETTF	(1 << 2)	/* Reset Transmitter FIFO */
#define FCR_RESETRF	(1 << 1)	/* Reset Receiver FIFO */
#define FCR_TRFIFOE	(1 << 0)	/* Transmit and Receive FIFO Enable */
#define FCR_ITL_1	(0)
#define FCR_ITL_8	(FCR_ITL1)
#define FCR_ITL_16	(FCR_ITL2)
#define FCR_ITL_32	(FCR_ITL2|FCR_ITL1)

#define LCR_DLAB	(1 << 7)	/* Divisor Latch Access Bit */
#define LCR_SB		(1 << 6)	/* Set Break */
#define LCR_STKYP	(1 << 5)	/* Sticky Parity */
#define LCR_EPS		(1 << 4)	/* Even Parity Select */
#define LCR_PEN		(1 << 3)	/* Parity Enable */
#define LCR_STB		(1 << 2)	/* Stop Bit */
#define LCR_WLS1	(1 << 1)	/* Word Length Select */
#define LCR_WLS0	(1 << 0)	/* Word Length Select */

#define LSR_FIFOE	(1 << 7)	/* FIFO Error Status */
#define LSR_TEMT	(1 << 6)	/* Transmitter Empty */
#define LSR_TDRQ	(1 << 5)	/* Transmit Data Request */
#define LSR_BI		(1 << 4)	/* Break Interrupt */
#define LSR_FE		(1 << 3)	/* Framing Error */
#define LSR_PE		(1 << 2)	/* Parity Error */
#define LSR_OE		(1 << 1)	/* Overrun Error */
#define LSR_DR		(1 << 0)	/* Data Ready */

#define MCR_LOOP	(1 << 4)
#define MCR_OUT2	(1 << 3)	/* force MSR_DCD in loopback mode */
#define MCR_OUT1	(1 << 2)	/* force MSR_RI in loopback mode */
#define MCR_RTS		(1 << 1)	/* Request to Send */
#define MCR_DTR		(1 << 0)	/* Data Terminal Ready */

#define MSR_DCD		(1 << 7)	/* Data Carrier Detect */
#define MSR_RI		(1 << 6)	/* Ring Indicator */
#define MSR_DSR		(1 << 5)	/* Data Set Ready */
#define MSR_CTS		(1 << 4)	/* Clear To Send */
#define MSR_DDCD	(1 << 3)	/* Delta Data Carrier Detect */
#define MSR_TERI	(1 << 2)	/* Trailing Edge Ring Indicator */
#define MSR_DDSR	(1 << 1)	/* Delta Data Set Ready */
#define MSR_DCTS	(1 << 0)	/* Delta Clear To Send */

/*
 * IrSR (Infrared Selection Register)
 */
#define STISR_RXPL      (1 << 4)        /* Receive Data Polarity */
#define STISR_TXPL      (1 << 3)        /* Transmit Data Polarity */
#define STISR_XMODE     (1 << 2)        /* Transmit Pulse Width Select */
#define STISR_RCVEIR    (1 << 1)        /* Receiver SIR Enable */
#define STISR_XMITIR    (1 << 0)        /* Transmitter SIR Enable */


/*
 * I2C registers
 */

#define IBMR		__REG(0x40301680)  /* I2C Bus Monitor Register - IBMR */
#define IDBR		__REG(0x40301688)  /* I2C Data Buffer Register - IDBR */
#define ICR		__REG(0x40301690)  /* I2C Control Register - ICR */
#define ISR		__REG(0x40301698)  /* I2C Status Register - ISR */
#define ISAR		__REG(0x403016A0)  /* I2C Slave Address Register - ISAR */

#define ICR_START	(1 << 0)	   /* start bit */
#define ICR_STOP	(1 << 1)	   /* stop bit */
#define ICR_ACKNAK	(1 << 2)	   /* send ACK(0) or NAK(1) */
#define ICR_TB		(1 << 3)	   /* transfer byte bit */
#define ICR_MA		(1 << 4)	   /* master abort */
#define ICR_SCLE	(1 << 5)	   /* master clock enable */
#define ICR_IUE		(1 << 6)	   /* unit enable */
#define ICR_GCD		(1 << 7)	   /* general call disable */
#define ICR_ITEIE	(1 << 8)	   /* enable tx interrupts */
#define ICR_IRFIE	(1 << 9)	   /* enable rx interrupts */
#define ICR_BEIE	(1 << 10)	   /* enable bus error ints */
#define ICR_SSDIE	(1 << 11)	   /* slave STOP detected int enable */
#define ICR_ALDIE	(1 << 12)	   /* enable arbitration interrupt */
#define ICR_SADIE	(1 << 13)	   /* slave address detected int enable */
#define ICR_UR		(1 << 14)	   /* unit reset */

#define ISR_RWM		(1 << 0)	   /* read/write mode */
#define ISR_ACKNAK	(1 << 1)	   /* ack/nak status */
#define ISR_UB		(1 << 2)	   /* unit busy */
#define ISR_IBB		(1 << 3)	   /* bus busy */
#define ISR_SSD		(1 << 4)	   /* slave stop detected */
#define ISR_ALD		(1 << 5)	   /* arbitration loss detected */
#define ISR_ITE		(1 << 6)	   /* tx buffer empty */
#define ISR_IRF		(1 << 7)	   /* rx buffer full */
#define ISR_GCAD	(1 << 8)	   /* general call address detected */
#define ISR_SAD		(1 << 9)	   /* slave address detected */
#define ISR_BED		(1 << 10)	   /* bus error no ACK/NAK */


/*
 * Serial Audio Controller
 */

/* FIXME: This clash with SA1111 defines */
#ifndef CONFIG_SA1111
#define SACR0		__REG(0x40400000)  /* Global Control Register */
#define SACR1		__REG(0x40400004)  /* Serial Audio I 2 S/MSB-Justified Control Register */
#define SASR0		__REG(0x4040000C)  /* Serial Audio I 2 S/MSB-Justified Interface and FIFO Status Register */
#define SAIMR		__REG(0x40400014)  /* Serial Audio Interrupt Mask Register */
#define SAICR		__REG(0x40400018)  /* Serial Audio Interrupt Clear Register */
#define SADIV		__REG(0x40400060)  /* Audio Clock Divider Register. */
#define SADR		__REG(0x40400080)  /* Serial Audio Data Register (TX and RX FIFO access Register). */
#endif


/*
 * AC97 Controller registers
 */

#define POCR		__REG(0x40500000)  /* PCM Out Control Register */
#define POCR_FEIE	(1 << 3)	/* FIFO Error Interrupt Enable */

#define PICR		__REG(0x40500004)  /* PCM In Control Register */
#define PICR_FEIE	(1 << 3)	/* FIFO Error Interrupt Enable */

#define MCCR		__REG(0x40500008)  /* Mic In Control Register */
#define MCCR_FEIE	(1 << 3)	/* FIFO Error Interrupt Enable */

#define GCR		__REG(0x4050000C)  /* Global Control Register */
#define GCR_CDONE_IE	(1 << 19)	/* Command Done Interrupt Enable */
#define GCR_SDONE_IE	(1 << 18)	/* Status Done Interrupt Enable */
#define GCR_SECRDY_IEN	(1 << 9)	/* Secondary Ready Interrupt Enable */
#define GCR_PRIRDY_IEN	(1 << 8)	/* Primary Ready Interrupt Enable */
#define GCR_SECRES_IEN	(1 << 5)	/* Secondary Resume Interrupt Enable */
#define GCR_PRIRES_IEN	(1 << 4)	/* Primary Resume Interrupt Enable */
#define GCR_ACLINK_OFF	(1 << 3)	/* AC-link Shut Off */
#define GCR_WARM_RST	(1 << 2)	/* AC97 Warm Reset */
#define GCR_COLD_RST	(1 << 1)	/* AC'97 Cold Reset (0 = active) */
#define GCR_GIE		(1 << 0)	/* Codec GPI Interrupt Enable */

#define POSR		__REG(0x40500010)  /* PCM Out Status Register */
#define POSR_FIFOE	(1 << 4)	/* FIFO error */

#define PISR		__REG(0x40500014)  /* PCM In Status Register */
#define PISR_FIFOE	(1 << 4)	/* FIFO error */

#define MCSR		__REG(0x40500018)  /* Mic In Status Register */
#define MCSR_FIFOE	(1 << 4)	/* FIFO error */

#define GSR		__REG(0x4050001C)  /* Global Status Register */
#define GSR_CDONE	(1 << 19)	/* Command Done */
#define GSR_SDONE	(1 << 18)	/* Status Done */
#define GSR_RDCS	(1 << 15)	/* Read Completion Status */
#define GSR_BIT3SLT12	(1 << 14)	/* Bit 3 of slot 12 */
#define GSR_BIT2SLT12	(1 << 13)	/* Bit 2 of slot 12 */
#define GSR_BIT1SLT12	(1 << 12)	/* Bit 1 of slot 12 */
#define GSR_SECRES	(1 << 11)	/* Secondary Resume Interrupt */
#define GSR_PRIRES	(1 << 10)	/* Primary Resume Interrupt */
#define GSR_SCR		(1 << 9)	/* Secondary Codec Ready */
#define GSR_PCR		(1 << 8)	/*  Primary Codec Ready */
#define GSR_MINT	(1 << 7)	/* Mic In Interrupt */
#define GSR_POINT	(1 << 6)	/* PCM Out Interrupt */
#define GSR_PIINT	(1 << 5)	/* PCM In Interrupt */
#define GSR_MOINT	(1 << 2)	/* Modem Out Interrupt */
#define GSR_MIINT	(1 << 1)	/* Modem In Interrupt */
#define GSR_GSCI	(1 << 0)	/* Codec GPI Status Change Interrupt */

#define CAR		__REG(0x40500020)  /* CODEC Access Register */
#define CAR_CAIP	(1 << 0)	/* Codec Access In Progress */

#define PCDR		__REG(0x40500040)  /* PCM FIFO Data Register */
#define MCDR		__REG(0x40500060)  /* Mic-in FIFO Data Register */

#define MOCR		__REG(0x40500100)  /* Modem Out Control Register */
#define MOCR_FEIE	(1 << 3)	/* FIFO Error */

#define MICR		__REG(0x40500108)  /* Modem In Control Register */
#define MICR_FEIE	(1 << 3)	/* FIFO Error */

#define MOSR		__REG(0x40500110)  /* Modem Out Status Register */
#define MOSR_FIFOE	(1 << 4)	/* FIFO error */

#define MISR		__REG(0x40500118)  /* Modem In Status Register */
#define MISR_FIFOE	(1 << 4)	/* FIFO error */

#define MODR		__REG(0x40500140)  /* Modem FIFO Data Register */

#define PAC_REG_BASE	__REG(0x40500200)  /* Primary Audio Codec */
#define SAC_REG_BASE	__REG(0x40500300)  /* Secondary Audio Codec */
#define PMC_REG_BASE	__REG(0x40500400)  /* Primary Modem Codec */
#define SMC_REG_BASE	__REG(0x40500500)  /* Secondary Modem Codec */


/*
 * USB Device Controller
 */
#define UDC_RES1	__REG(0x40600004)  /* UDC Undocumented - Reserved1 */
#define UDC_RES2	__REG(0x40600008)  /* UDC Undocumented - Reserved2 */
#define UDC_RES3	__REG(0x4060000C)  /* UDC Undocumented - Reserved3 */

#define UDCCR		__REG(0x40600000)  /* UDC Control Register */
#define UDCCR_UDE	(1 << 0)	/* UDC enable */
#define UDCCR_UDA	(1 << 1)	/* UDC active */
#define UDCCR_RSM	(1 << 2)	/* Device resume */
#define UDCCR_RESIR	(1 << 3)	/* Resume interrupt request */
#define UDCCR_SUSIR	(1 << 4)	/* Suspend interrupt request */
#define UDCCR_SRM	(1 << 5)	/* Suspend/resume interrupt mask */
#define UDCCR_RSTIR	(1 << 6)	/* Reset interrupt request */
#define UDCCR_REM	(1 << 7)	/* Reset interrupt mask */

#define UDCCS0		__REG(0x40600010)  /* UDC Endpoint 0 Control/Status Register */
#define UDCCS0_OPR	(1 << 0)	/* OUT packet ready */
#define UDCCS0_IPR	(1 << 1)	/* IN packet ready */
#define UDCCS0_FTF	(1 << 2)	/* Flush Tx FIFO */
#define UDCCS0_DRWF	(1 << 3)	/* Device remote wakeup feature */
#define UDCCS0_SST	(1 << 4)	/* Sent stall */
#define UDCCS0_FST	(1 << 5)	/* Force stall */
#define UDCCS0_RNE	(1 << 6)	/* Receive FIFO no empty */
#define UDCCS0_SA	(1 << 7)	/* Setup active */

/* Bulk IN - Endpoint 1,6,11 */
#define UDCCS1		__REG(0x40600014)  /* UDC Endpoint 1 (IN) Control/Status Register */
#define UDCCS6		__REG(0x40600028)  /* UDC Endpoint 6 (IN) Control/Status Register */
#define UDCCS11		__REG(0x4060003C)  /* UDC Endpoint 11 (IN) Control/Status Register */

#define UDCCS_BI_TFS	(1 << 0)	/* Transmit FIFO service */
#define UDCCS_BI_TPC	(1 << 1)	/* Transmit packet complete */
#define UDCCS_BI_FTF	(1 << 2)	/* Flush Tx FIFO */
#define UDCCS_BI_TUR	(1 << 3)	/* Transmit FIFO underrun */
#define UDCCS_BI_SST	(1 << 4)	/* Sent stall */
#define UDCCS_BI_FST	(1 << 5)	/* Force stall */
#define UDCCS_BI_TSP	(1 << 7)	/* Transmit short packet */

/* Bulk OUT - Endpoint 2,7,12 */
#define UDCCS2		__REG(0x40600018)  /* UDC Endpoint 2 (OUT) Control/Status Register */
#define UDCCS7		__REG(0x4060002C)  /* UDC Endpoint 7 (OUT) Control/Status Register */
#define UDCCS12		__REG(0x40600040)  /* UDC Endpoint 12 (OUT) Control/Status Register */

#define UDCCS_BO_RFS	(1 << 0)	/* Receive FIFO service */
#define UDCCS_BO_RPC	(1 << 1)	/* Receive packet complete */
#define UDCCS_BO_DME	(1 << 3)	/* DMA enable */
#define UDCCS_BO_SST	(1 << 4)	/* Sent stall */
#define UDCCS_BO_FST	(1 << 5)	/* Force stall */
#define UDCCS_BO_RNE	(1 << 6)	/* Receive FIFO not empty */
#define UDCCS_BO_RSP	(1 << 7)	/* Receive short packet */

/* Isochronous IN - Endpoint 3,8,13 */
#define UDCCS3		__REG(0x4060001C)  /* UDC Endpoint 3 (IN) Control/Status Register */
#define UDCCS8		__REG(0x40600030)  /* UDC Endpoint 8 (IN) Control/Status Register */
#define UDCCS13		__REG(0x40600044)  /* UDC Endpoint 13 (IN) Control/Status Register */

#define UDCCS_II_TFS	(1 << 0)	/* Transmit FIFO service */
#define UDCCS_II_TPC	(1 << 1)	/* Transmit packet complete */
#define UDCCS_II_FTF	(1 << 2)	/* Flush Tx FIFO */
#define UDCCS_II_TUR	(1 << 3)	/* Transmit FIFO underrun */
#define UDCCS_II_TSP	(1 << 7)	/* Transmit short packet */

/* Isochronous OUT - Endpoint 4,9,14 */
#define UDCCS4		__REG(0x40600020)  /* UDC Endpoint 4 (OUT) Control/Status Register */
#define UDCCS9		__REG(0x40600034)  /* UDC Endpoint 9 (OUT) Control/Status Register */
#define UDCCS14		__REG(0x40600048)  /* UDC Endpoint 14 (OUT) Control/Status Register */

#define UDCCS_IO_RFS	(1 << 0)	/* Receive FIFO service */
#define UDCCS_IO_RPC	(1 << 1)	/* Receive packet complete */
#define UDCCS_IO_ROF	(1 << 3)	/* Receive overflow */
#define UDCCS_IO_DME	(1 << 3)	/* DMA enable */
#define UDCCS_IO_RNE	(1 << 6)	/* Receive FIFO not empty */
#define UDCCS_IO_RSP	(1 << 7)	/* Receive short packet */

/* Interrupt IN - Endpoint 5,10,15 */
#define UDCCS5		__REG(0x40600024)  /* UDC Endpoint 5 (Interrupt) Control/Status Register */
#define UDCCS10		__REG(0x40600038)  /* UDC Endpoint 10 (Interrupt) Control/Status Register */
#define UDCCS15		__REG(0x4060004C)  /* UDC Endpoint 15 (Interrupt) Control/Status Register */

#define UDCCS_INT_TFS	(1 << 0)	/* Transmit FIFO service */
#define UDCCS_INT_TPC	(1 << 1)	/* Transmit packet complete */
#define UDCCS_INT_FTF	(1 << 2)	/* Flush Tx FIFO */
#define UDCCS_INT_TUR	(1 << 3)	/* Transmit FIFO underrun */
#define UDCCS_INT_SST	(1 << 4)	/* Sent stall */
#define UDCCS_INT_FST	(1 << 5)	/* Force stall */
#define UDCCS_INT_TSP	(1 << 7)	/* Transmit short packet */

#define UFNRH		__REG(0x40600060)  /* UDC Frame Number Register High */
#define UFNRL		__REG(0x40600064)  /* UDC Frame Number Register Low */
#define UBCR2		__REG(0x40600068)  /* UDC Byte Count Reg 2 */
#define UBCR4		__REG(0x4060006c)  /* UDC Byte Count Reg 4 */
#define UBCR7		__REG(0x40600070)  /* UDC Byte Count Reg 7 */
#define UBCR9		__REG(0x40600074)  /* UDC Byte Count Reg 9 */
#define UBCR12		__REG(0x40600078)  /* UDC Byte Count Reg 12 */
#define UBCR14		__REG(0x4060007c)  /* UDC Byte Count Reg 14 */
#define UDDR0		__REG(0x40600080)  /* UDC Endpoint 0 Data Register */
#define UDDR1		__REG(0x40600100)  /* UDC Endpoint 1 Data Register */
#define UDDR2		__REG(0x40600180)  /* UDC Endpoint 2 Data Register */
#define UDDR3		__REG(0x40600200)  /* UDC Endpoint 3 Data Register */
#define UDDR4		__REG(0x40600400)  /* UDC Endpoint 4 Data Register */
#define UDDR5		__REG(0x406000A0)  /* UDC Endpoint 5 Data Register */
#define UDDR6		__REG(0x40600600)  /* UDC Endpoint 6 Data Register */
#define UDDR7		__REG(0x40600680)  /* UDC Endpoint 7 Data Register */
#define UDDR8		__REG(0x40600700)  /* UDC Endpoint 8 Data Register */
#define UDDR9		__REG(0x40600900)  /* UDC Endpoint 9 Data Register */
#define UDDR10		__REG(0x406000C0)  /* UDC Endpoint 10 Data Register */
#define UDDR11		__REG(0x40600B00)  /* UDC Endpoint 11 Data Register */
#define UDDR12		__REG(0x40600B80)  /* UDC Endpoint 12 Data Register */
#define UDDR13		__REG(0x40600C00)  /* UDC Endpoint 13 Data Register */
#define UDDR14		__REG(0x40600E00)  /* UDC Endpoint 14 Data Register */
#define UDDR15		__REG(0x406000E0)  /* UDC Endpoint 15 Data Register */

#define UICR0		__REG(0x40600050)  /* UDC Interrupt Control Register 0 */

#define UICR0_IM0	(1 << 0)	/* Interrupt mask ep 0 */
#define UICR0_IM1	(1 << 1)	/* Interrupt mask ep 1 */
#define UICR0_IM2	(1 << 2)	/* Interrupt mask ep 2 */
#define UICR0_IM3	(1 << 3)	/* Interrupt mask ep 3 */
#define UICR0_IM4	(1 << 4)	/* Interrupt mask ep 4 */
#define UICR0_IM5	(1 << 5)	/* Interrupt mask ep 5 */
#define UICR0_IM6	(1 << 6)	/* Interrupt mask ep 6 */
#define UICR0_IM7	(1 << 7)	/* Interrupt mask ep 7 */

#define UICR1		__REG(0x40600054)  /* UDC Interrupt Control Register 1 */

#define UICR1_IM8	(1 << 0)	/* Interrupt mask ep 8 */
#define UICR1_IM9	(1 << 1)	/* Interrupt mask ep 9 */
#define UICR1_IM10	(1 << 2)	/* Interrupt mask ep 10 */
#define UICR1_IM11	(1 << 3)	/* Interrupt mask ep 11 */
#define UICR1_IM12	(1 << 4)	/* Interrupt mask ep 12 */
#define UICR1_IM13	(1 << 5)	/* Interrupt mask ep 13 */
#define UICR1_IM14	(1 << 6)	/* Interrupt mask ep 14 */
#define UICR1_IM15	(1 << 7)	/* Interrupt mask ep 15 */

#define USIR0		__REG(0x40600058)  /* UDC Status Interrupt Register 0 */

#define USIR0_IR0	(1 << 0)	/* Interrup request ep 0 */
#define USIR0_IR1	(1 << 1)	/* Interrup request ep 1 */
#define USIR0_IR2	(1 << 2)	/* Interrup request ep 2 */
#define USIR0_IR3	(1 << 3)	/* Interrup request ep 3 */
#define USIR0_IR4	(1 << 4)	/* Interrup request ep 4 */
#define USIR0_IR5	(1 << 5)	/* Interrup request ep 5 */
#define USIR0_IR6	(1 << 6)	/* Interrup request ep 6 */
#define USIR0_IR7	(1 << 7)	/* Interrup request ep 7 */

#define USIR1		__REG(0x4060005C)  /* UDC Status Interrupt Register 1 */

#define USIR1_IR8	(1 << 0)	/* Interrup request ep 8 */
#define USIR1_IR9	(1 << 1)	/* Interrup request ep 9 */
#define USIR1_IR10	(1 << 2)	/* Interrup request ep 10 */
#define USIR1_IR11	(1 << 3)	/* Interrup request ep 11 */
#define USIR1_IR12	(1 << 4)	/* Interrup request ep 12 */
#define USIR1_IR13	(1 << 5)	/* Interrup request ep 13 */
#define USIR1_IR14	(1 << 6)	/* Interrup request ep 14 */
#define USIR1_IR15	(1 << 7)	/* Interrup request ep 15 */


/*
 * Fast Infrared Communication Port
 */

#define ICCR0		__REG(0x40800000)  /* ICP Control Register 0 */
#define ICCR1		__REG(0x40800004)  /* ICP Control Register 1 */
#define ICCR2		__REG(0x40800008)  /* ICP Control Register 2 */
#define ICDR		__REG(0x4080000c)  /* ICP Data Register */
#define ICSR0		__REG(0x40800014)  /* ICP Status Register 0 */
#define ICSR1		__REG(0x40800018)  /* ICP Status Register 1 */

#define ICCR0_AME       (1 << 7)           /* Address match enable */
#define ICCR0_TIE       (1 << 6)           /* Transmit FIFO interrupt enable */
#define ICCR0_RIE       (1 << 5)           /* Receive FIFO interrupt enable */
#define ICCR0_RXE       (1 << 4)           /* Receive enable */
#define ICCR0_TXE       (1 << 3)           /* Transmit enable */
#define ICCR0_TUS       (1 << 2)           /* Transmit FIFO underrun select */
#define ICCR0_LBM       (1 << 1)           /* Loopback mode */
#define ICCR0_ITR       (1 << 0)           /* IrDA transmission */

#define ICSR0_FRE       (1 << 5)           /* Framing error */
#define ICSR0_RFS       (1 << 4)           /* Receive FIFO service request */
#define ICSR0_TFS       (1 << 3)           /* Transnit FIFO service request */
#define ICSR0_RAB       (1 << 2)           /* Receiver abort */
#define ICSR0_TUR       (1 << 1)           /* Trunsmit FIFO underun */
#define ICSR0_EIF       (1 << 0)           /* End/Error in FIFO */

#define ICSR1_ROR       (1 << 6)           /* Receiver FIFO underrun  */
#define ICSR1_CRE       (1 << 5)           /* CRC error */
#define ICSR1_EOF       (1 << 4)           /* End of frame */
#define ICSR1_TNF       (1 << 3)           /* Transmit FIFO not full */
#define ICSR1_RNE       (1 << 2)           /* Receive FIFO not empty */
#define ICSR1_TBY       (1 << 1)           /* Tramsmiter busy flag */
#define ICSR1_RSY       (1 << 0)           /* Recevier synchronized flag */


/*
 * Real Time Clock
 */

#define RCNR		__REG(0x40900000)  /* RTC Count Register */
#define RTAR		__REG(0x40900004)  /* RTC Alarm Register */
#define RTSR		__REG(0x40900008)  /* RTC Status Register */
#define RTTR		__REG(0x4090000C)  /* RTC Timer Trim Register */

#define RTSR_HZE	(1 << 3)	/* HZ interrupt enable */
#define RTSR_ALE	(1 << 2)	/* RTC alarm interrupt enable */
#define RTSR_HZ		(1 << 1)	/* HZ rising-edge detected */
#define RTSR_AL		(1 << 0)	/* RTC alarm detected */


/*
 * OS Timer & Match Registers
 */

#define OSMR0		__REG(0x40A00000)  /* */
#define OSMR1		__REG(0x40A00004)  /* */
#define OSMR2		__REG(0x40A00008)  /* */
#define OSMR3		__REG(0x40A0000C)  /* */
#define OSCR		__REG(0x40A00010)  /* OS Timer Counter Register */
#define OSSR		__REG(0x40A00014)  /* OS Timer Status Register */
#define OWER		__REG(0x40A00018)  /* OS Timer Watchdog Enable Register */
#define OIER		__REG(0x40A0001C)  /* OS Timer Interrupt Enable Register */

#define OSSR_M3		(1 << 3)	/* Match status channel 3 */
#define OSSR_M2		(1 << 2)	/* Match status channel 2 */
#define OSSR_M1		(1 << 1)	/* Match status channel 1 */
#define OSSR_M0		(1 << 0)	/* Match status channel 0 */

#define OWER_WME	(1 << 0)	/* Watchdog Match Enable */

#define OIER_E3		(1 << 3)	/* Interrupt enable channel 3 */
#define OIER_E2		(1 << 2)	/* Interrupt enable channel 2 */
#define OIER_E1		(1 << 1)	/* Interrupt enable channel 1 */
#define OIER_E0		(1 << 0)	/* Interrupt enable channel 0 */


/*
 * Pulse Width Modulator
 */

#define PWM_CTRL0	__REG(0x40B00000)  /* PWM 0 Control Register */
#define PWM_PWDUTY0	__REG(0x40B00004)  /* PWM 0 Duty Cycle Register */
#define PWM_PERVAL0	__REG(0x40B00008)  /* PWM 0 Period Control Register */

#define PWM_CTRL1	__REG(0x40C00000)  /* PWM 1Control Register */
#define PWM_PWDUTY1	__REG(0x40C00004)  /* PWM 1 Duty Cycle Register */
#define PWM_PERVAL1	__REG(0x40C00008)  /* PWM 1 Period Control Register */


/*
 * Interrupt Controller
 */

#define ICIP		__REG(0x40D00000)  /* Interrupt Controller IRQ Pending Register */
#define ICMR		__REG(0x40D00004)  /* Interrupt Controller Mask Register */
#define ICLR		__REG(0x40D00008)  /* Interrupt Controller Level Register */
#define ICFP		__REG(0x40D0000C)  /* Interrupt Controller FIQ Pending Register */
#define ICPR		__REG(0x40D00010)  /* Interrupt Controller Pending Register */
#define ICCR		__REG(0x40D00014)  /* Interrupt Controller Control Register */


/*
 * General Purpose I/O
 */

#define GPLR0		__REG(0x40E00000)  /* GPIO Pin-Level Register GPIO<31:0> */
#define GPLR1		__REG(0x40E00004)  /* GPIO Pin-Level Register GPIO<63:32> */
#define GPLR2		__REG(0x40E00008)  /* GPIO Pin-Level Register GPIO<80:64> */

#define GPDR0		__REG(0x40E0000C)  /* GPIO Pin Direction Register GPIO<31:0> */
#define GPDR1		__REG(0x40E00010)  /* GPIO Pin Direction Register GPIO<63:32> */
#define GPDR2		__REG(0x40E00014)  /* GPIO Pin Direction Register GPIO<80:64> */

#define GPSR0		__REG(0x40E00018)  /* GPIO Pin Output Set Register GPIO<31:0> */
#define GPSR1		__REG(0x40E0001C)  /* GPIO Pin Output Set Register GPIO<63:32> */
#define GPSR2		__REG(0x40E00020)  /* GPIO Pin Output Set Register GPIO<80:64> */

#define GPCR0		__REG(0x40E00024)  /* GPIO Pin Output Clear Register GPIO<31:0> */
#define GPCR1		__REG(0x40E00028)  /* GPIO Pin Output Clear Register GPIO <63:32> */
#define GPCR2		__REG(0x40E0002C)  /* GPIO Pin Output Clear Register GPIO <80:64> */

#define GRER0		__REG(0x40E00030)  /* GPIO Rising-Edge Detect Register GPIO<31:0> */
#define GRER1		__REG(0x40E00034)  /* GPIO Rising-Edge Detect Register GPIO<63:32> */
#define GRER2		__REG(0x40E00038)  /* GPIO Rising-Edge Detect Register GPIO<80:64> */

#define GFER0		__REG(0x40E0003C)  /* GPIO Falling-Edge Detect Register GPIO<31:0> */
#define GFER1		__REG(0x40E00040)  /* GPIO Falling-Edge Detect Register GPIO<63:32> */
#define GFER2		__REG(0x40E00044)  /* GPIO Falling-Edge Detect Register GPIO<80:64> */

#define GEDR0		__REG(0x40E00048)  /* GPIO Edge Detect Status Register GPIO<31:0> */
#define GEDR1		__REG(0x40E0004C)  /* GPIO Edge Detect Status Register GPIO<63:32> */
#define GEDR2		__REG(0x40E00050)  /* GPIO Edge Detect Status Register GPIO<80:64> */

#define GAFR0_L		__REG(0x40E00054)  /* GPIO Alternate Function Select Register GPIO<15:0> */
#define GAFR0_U		__REG(0x40E00058)  /* GPIO Alternate Function Select Register GPIO<31:16> */
#define GAFR1_L		__REG(0x40E0005C)  /* GPIO Alternate Function Select Register GPIO<47:32> */
#define GAFR1_U		__REG(0x40E00060)  /* GPIO Alternate Function Select Register GPIO<63:48> */
#define GAFR2_L		__REG(0x40E00064)  /* GPIO Alternate Function Select Register GPIO<79:64> */
#define GAFR2_U		__REG(0x40E00068)  /* GPIO Alternate Function Select Register GPIO 80 */

/* More handy macros.  The argument is a literal GPIO number. */

#define GPIO_bit(x)	(1 << ((x) & 0x1f))
#define GPLR(x)		__REG2(0x40E00000, ((x) & 0x60) >> 3)
#define GPDR(x)		__REG2(0x40E0000C, ((x) & 0x60) >> 3)
#define GPSR(x)		__REG2(0x40E00018, ((x) & 0x60) >> 3)
#define GPCR(x)		__REG2(0x40E00024, ((x) & 0x60) >> 3)
#define GRER(x)		__REG2(0x40E00030, ((x) & 0x60) >> 3)
#define GFER(x)		__REG2(0x40E0003C, ((x) & 0x60) >> 3)
#define GEDR(x)		__REG2(0x40E00048, ((x) & 0x60) >> 3)
#define GAFR(x)		__REG2(0x40E00054, ((x) & 0x70) >> 2)

/* GPIO alternate function assignments */

#define GPIO1_RST		1	/* reset */
#define GPIO6_MMCCLK		6	/* MMC Clock */
#define GPIO7_48MHz		7	/* 48 MHz clock output */
#define GPIO8_MMCCS0		8	/* MMC Chip Select 0 */
#define GPIO9_MMCCS1		9	/* MMC Chip Select 1 */
#define GPIO10_RTCCLK		10	/* real time clock (1 Hz) */
#define GPIO11_3_6MHz		11	/* 3.6 MHz oscillator out */
#define GPIO12_32KHz		12	/* 32 kHz out */
#define GPIO13_MBGNT		13	/* memory controller grant */
#define GPIO14_MBREQ		14	/* alternate bus master request */
#define GPIO15_nCS_1		15	/* chip select 1 */
#define GPIO16_PWM0		16	/* PWM0 output */
#define GPIO17_PWM1		17	/* PWM1 output */
#define GPIO18_RDY		18	/* Ext. Bus Ready */
#define GPIO19_DREQ1		19	/* External DMA Request */
#define GPIO20_DREQ0		20	/* External DMA Request */
#define GPIO23_SCLK		23	/* SSP clock */
#define GPIO24_SFRM		24	/* SSP Frame */
#define GPIO25_STXD		25	/* SSP transmit */
#define GPIO26_SRXD		26	/* SSP receive */
#define GPIO27_SEXTCLK		27	/* SSP ext_clk */
#define GPIO28_BITCLK		28	/* AC97/I2S bit_clk */
#define GPIO29_SDATA_IN		29	/* AC97 Sdata_in0 / I2S Sdata_in */
#define GPIO30_SDATA_OUT	30	/* AC97/I2S Sdata_out */
#define GPIO31_SYNC		31	/* AC97/I2S sync */
#define GPIO32_SDATA_IN1	32	/* AC97 Sdata_in1 */
#define GPIO33_nCS_5		33	/* chip select 5 */
#define GPIO34_FFRXD		34	/* FFUART receive */
#define GPIO34_MMCCS0		34	/* MMC Chip Select 0 */
#define GPIO35_FFCTS		35	/* FFUART Clear to send */
#define GPIO36_FFDCD		36	/* FFUART Data carrier detect */
#define GPIO37_FFDSR		37	/* FFUART data set ready */
#define GPIO38_FFRI		38	/* FFUART Ring Indicator */
#define GPIO39_MMCCS1		39	/* MMC Chip Select 1 */
#define GPIO39_FFTXD		39	/* FFUART transmit data */
#define GPIO40_FFDTR		40	/* FFUART data terminal Ready */
#define GPIO41_FFRTS		41	/* FFUART request to send */
#define GPIO42_BTRXD		42	/* BTUART receive data */
#define GPIO43_BTTXD		43	/* BTUART transmit data */
#define GPIO44_BTCTS		44	/* BTUART clear to send */
#define GPIO45_BTRTS		45	/* BTUART request to send */
#define GPIO46_ICPRXD		46	/* ICP receive data */
#define GPIO46_STRXD		46	/* STD_UART receive data */
#define GPIO47_ICPTXD		47	/* ICP transmit data */
#define GPIO47_STTXD		47	/* STD_UART transmit data */
#define GPIO48_nPOE		48	/* Output Enable for Card Space */
#define GPIO49_nPWE		49	/* Write Enable for Card Space */
#define GPIO50_nPIOR		50	/* I/O Read for Card Space */
#define GPIO51_nPIOW		51	/* I/O Write for Card Space */
#define GPIO52_nPCE_1		52	/* Card Enable for Card Space */
#define GPIO53_nPCE_2		53	/* Card Enable for Card Space */
#define GPIO53_MMCCLK		53	/* MMC Clock */
#define GPIO54_MMCCLK		54	/* MMC Clock */
#define GPIO54_pSKTSEL		54	/* Socket Select for Card Space */
#define GPIO55_nPREG		55	/* Card Address bit 26 */
#define GPIO56_nPWAIT		56	/* Wait signal for Card Space */
#define GPIO57_nIOIS16		57	/* Bus Width select for I/O Card Space */
#define GPIO58_LDD_0		58	/* LCD data pin 0 */
#define GPIO59_LDD_1		59	/* LCD data pin 1 */
#define GPIO60_LDD_2		60	/* LCD data pin 2 */
#define GPIO61_LDD_3		61	/* LCD data pin 3 */
#define GPIO62_LDD_4		62	/* LCD data pin 4 */
#define GPIO63_LDD_5		63	/* LCD data pin 5 */
#define GPIO64_LDD_6		64	/* LCD data pin 6 */
#define GPIO65_LDD_7		65	/* LCD data pin 7 */
#define GPIO66_LDD_8		66	/* LCD data pin 8 */
#define GPIO66_MBREQ		66	/* alternate bus master req */
#define GPIO67_LDD_9		67	/* LCD data pin 9 */
#define GPIO67_MMCCS0		67	/* MMC Chip Select 0 */
#define GPIO68_LDD_10		68	/* LCD data pin 10 */
#define GPIO68_MMCCS1		68	/* MMC Chip Select 1 */
#define GPIO69_LDD_11		69	/* LCD data pin 11 */
#define GPIO69_MMCCLK		69	/* MMC_CLK */
#define GPIO70_LDD_12		70	/* LCD data pin 12 */
#define GPIO70_RTCCLK		70	/* Real Time clock (1 Hz) */
#define GPIO71_LDD_13		71	/* LCD data pin 13 */
#define GPIO71_3_6MHz		71	/* 3.6 MHz Oscillator clock */
#define GPIO72_LDD_14		72	/* LCD data pin 14 */
#define GPIO72_32kHz		72	/* 32 kHz clock */
#define GPIO73_LDD_15		73	/* LCD data pin 15 */
#define GPIO73_MBGNT		73	/* Memory controller grant */
#define GPIO74_LCD_FCLK		74	/* LCD Frame clock */
#define GPIO75_LCD_LCLK		75	/* LCD line clock */
#define GPIO76_LCD_PCLK		76	/* LCD Pixel clock */
#define GPIO77_LCD_ACBIAS	77	/* LCD AC Bias */
#define GPIO78_nCS_2		78	/* chip select 2 */
#define GPIO79_nCS_3		79	/* chip select 3 */
#define GPIO80_nCS_4		80	/* chip select 4 */

/* GPIO alternate function mode & direction */

#define GPIO_IN			0x000
#define GPIO_OUT		0x080
#define GPIO_ALT_FN_1_IN	0x100
#define GPIO_ALT_FN_1_OUT	0x180
#define GPIO_ALT_FN_2_IN	0x200
#define GPIO_ALT_FN_2_OUT	0x280
#define GPIO_ALT_FN_3_IN	0x300
#define GPIO_ALT_FN_3_OUT	0x380
#define GPIO_MD_MASK_NR		0x07f
#define GPIO_MD_MASK_DIR	0x080
#define GPIO_MD_MASK_FN		0x300

#define GPIO1_RTS_MD		( 1 | GPIO_ALT_FN_1_IN)
#define GPIO6_MMCCLK_MD		( 6 | GPIO_ALT_FN_1_OUT)
#define GPIO7_48MHz_MD		( 7 | GPIO_ALT_FN_1_OUT)
#define GPIO8_MMCCS0_MD		( 8 | GPIO_ALT_FN_1_OUT)
#define GPIO9_MMCCS1_MD		( 9 | GPIO_ALT_FN_1_OUT)
#define GPIO10_RTCCLK_MD	(10 | GPIO_ALT_FN_1_OUT)
#define GPIO11_3_6MHz_MD	(11 | GPIO_ALT_FN_1_OUT)
#define GPIO12_32KHz_MD		(12 | GPIO_ALT_FN_1_OUT)
#define GPIO13_MBGNT_MD		(13 | GPIO_ALT_FN_2_OUT)
#define GPIO14_MBREQ_MD		(14 | GPIO_ALT_FN_1_IN)
#define GPIO15_nCS_1_MD		(15 | GPIO_ALT_FN_2_OUT)
#define GPIO16_PWM0_MD		(16 | GPIO_ALT_FN_2_OUT)
#define GPIO17_PWM1_MD		(17 | GPIO_ALT_FN_2_OUT)
#define GPIO18_RDY_MD		(18 | GPIO_ALT_FN_1_IN)
#define GPIO19_DREQ1_MD		(19 | GPIO_ALT_FN_1_IN)
#define GPIO20_DREQ0_MD		(20 | GPIO_ALT_FN_1_IN)
#define GPIO23_SCLK_md		(23 | GPIO_ALT_FN_2_OUT)
#define GPIO24_SFRM_MD		(24 | GPIO_ALT_FN_2_OUT)
#define GPIO25_STXD_MD		(25 | GPIO_ALT_FN_2_OUT)
#define GPIO26_SRXD_MD		(26 | GPIO_ALT_FN_1_IN)
#define GPIO27_SEXTCLK_MD	(27 | GPIO_ALT_FN_1_IN)
#define GPIO28_BITCLK_AC97_MD	(28 | GPIO_ALT_FN_1_IN)
#define GPIO28_BITCLK_I2S_MD	(28 | GPIO_ALT_FN_2_IN)
#define GPIO29_SDATA_IN_AC97_MD	(29 | GPIO_ALT_FN_1_IN)
#define GPIO29_SDATA_IN_I2S_MD	(29 | GPIO_ALT_FN_2_IN)
#define GPIO30_SDATA_OUT_AC97_MD	(30 | GPIO_ALT_FN_2_OUT)
#define GPIO30_SDATA_OUT_I2S_MD	(30 | GPIO_ALT_FN_1_OUT)
#define GPIO31_SYNC_AC97_MD	(31 | GPIO_ALT_FN_2_OUT)
#define GPIO31_SYNC_I2S_MD	(31 | GPIO_ALT_FN_1_OUT)
#define GPIO32_SDATA_IN1_AC97_MD	(32 | GPIO_ALT_FN_1_IN)
#define GPIO33_nCS_5_MD		(33 | GPIO_ALT_FN_2_OUT)
#define GPIO34_FFRXD_MD		(34 | GPIO_ALT_FN_1_IN)
#define GPIO34_MMCCS0_MD	(34 | GPIO_ALT_FN_2_OUT)
#define GPIO35_FFCTS_MD		(35 | GPIO_ALT_FN_1_IN)
#define GPIO36_FFDCD_MD		(36 | GPIO_ALT_FN_1_IN)
#define GPIO37_FFDSR_MD		(37 | GPIO_ALT_FN_1_IN)
#define GPIO38_FFRI_MD		(38 | GPIO_ALT_FN_1_IN)
#define GPIO39_MMCCS1_MD	(39 | GPIO_ALT_FN_1_OUT)
#define GPIO39_FFTXD_MD		(39 | GPIO_ALT_FN_2_OUT)
#define GPIO40_FFDTR_MD		(40 | GPIO_ALT_FN_2_OUT)
#define GPIO41_FFRTS_MD		(41 | GPIO_ALT_FN_2_OUT)
#define GPIO42_BTRXD_MD		(42 | GPIO_ALT_FN_1_IN)
#define GPIO43_BTTXD_MD		(43 | GPIO_ALT_FN_2_OUT)
#define GPIO44_BTCTS_MD		(44 | GPIO_ALT_FN_1_IN)
#define GPIO45_BTRTS_MD		(45 | GPIO_ALT_FN_2_OUT)
#define GPIO46_ICPRXD_MD	(46 | GPIO_ALT_FN_1_IN)
#define GPIO46_STRXD_MD		(46 | GPIO_ALT_FN_2_IN)
#define GPIO47_ICPTXD_MD	(47 | GPIO_ALT_FN_2_OUT)
#define GPIO47_STTXD_MD		(47 | GPIO_ALT_FN_1_OUT)
#define GPIO48_nPOE_MD		(48 | GPIO_ALT_FN_2_OUT)
#define GPIO49_nPWE_MD		(49 | GPIO_ALT_FN_2_OUT)
#define GPIO50_nPIOR_MD		(50 | GPIO_ALT_FN_2_OUT)
#define GPIO51_nPIOW_MD		(51 | GPIO_ALT_FN_2_OUT)
#define GPIO52_nPCE_1_MD	(52 | GPIO_ALT_FN_2_OUT)
#define GPIO53_nPCE_2_MD	(53 | GPIO_ALT_FN_2_OUT)
#define GPIO53_MMCCLK_MD	(53 | GPIO_ALT_FN_1_OUT)
#define GPIO54_MMCCLK_MD	(54 | GPIO_ALT_FN_1_OUT)
#define GPIO54_pSKTSEL_MD	(54 | GPIO_ALT_FN_2_OUT)
#define GPIO55_nPREG_MD		(55 | GPIO_ALT_FN_2_OUT)
#define GPIO56_nPWAIT_MD	(56 | GPIO_ALT_FN_1_IN)
#define GPIO57_nIOIS16_MD	(57 | GPIO_ALT_FN_1_IN)
#define GPIO58_LDD_0_MD		(58 | GPIO_ALT_FN_2_OUT)
#define GPIO59_LDD_1_MD		(59 | GPIO_ALT_FN_2_OUT)
#define GPIO60_LDD_2_MD		(60 | GPIO_ALT_FN_2_OUT)
#define GPIO61_LDD_3_MD		(61 | GPIO_ALT_FN_2_OUT)
#define GPIO62_LDD_4_MD		(62 | GPIO_ALT_FN_2_OUT)
#define GPIO63_LDD_5_MD		(63 | GPIO_ALT_FN_2_OUT)
#define GPIO64_LDD_6_MD		(64 | GPIO_ALT_FN_2_OUT)
#define GPIO65_LDD_7_MD		(65 | GPIO_ALT_FN_2_OUT)
#define GPIO66_LDD_8_MD		(66 | GPIO_ALT_FN_2_OUT)
#define GPIO66_MBREQ_MD		(66 | GPIO_ALT_FN_1_IN)
#define GPIO67_LDD_9_MD		(67 | GPIO_ALT_FN_2_OUT)
#define GPIO67_MMCCS0_MD	(67 | GPIO_ALT_FN_1_OUT)
#define GPIO68_LDD_10_MD	(68 | GPIO_ALT_FN_2_OUT)
#define GPIO68_MMCCS1_MD	(68 | GPIO_ALT_FN_1_OUT)
#define GPIO69_LDD_11_MD	(69 | GPIO_ALT_FN_2_OUT)
#define GPIO69_MMCCLK_MD	(69 | GPIO_ALT_FN_1_OUT)
#define GPIO70_LDD_12_MD	(70 | GPIO_ALT_FN_2_OUT)
#define GPIO70_RTCCLK_MD	(70 | GPIO_ALT_FN_1_OUT)
#define GPIO71_LDD_13_MD	(71 | GPIO_ALT_FN_2_OUT)
#define GPIO71_3_6MHz_MD	(71 | GPIO_ALT_FN_1_OUT)
#define GPIO72_LDD_14_MD	(72 | GPIO_ALT_FN_2_OUT)
#define GPIO72_32kHz_MD		(72 | GPIO_ALT_FN_1_OUT)
#define GPIO73_LDD_15_MD	(73 | GPIO_ALT_FN_2_OUT)
#define GPIO73_MBGNT_MD		(73 | GPIO_ALT_FN_1_OUT)
#define GPIO74_LCD_FCLK_MD	(74 | GPIO_ALT_FN_2_OUT)
#define GPIO75_LCD_LCLK_MD	(75 | GPIO_ALT_FN_2_OUT)
#define GPIO76_LCD_PCLK_MD	(76 | GPIO_ALT_FN_2_OUT)
#define GPIO77_LCD_ACBIAS_MD	(77 | GPIO_ALT_FN_2_OUT)
#define GPIO78_nCS_2_MD		(78 | GPIO_ALT_FN_2_OUT)
#define GPIO79_nCS_3_MD		(79 | GPIO_ALT_FN_2_OUT)
#define GPIO80_nCS_4_MD		(80 | GPIO_ALT_FN_2_OUT)


/*
 * Power Manager
 */

#define PMCR		__REG(0x40F00000)  /* Power Manager Control Register */
#define PSSR		__REG(0x40F00004)  /* Power Manager Sleep Status Register */
#define PSPR		__REG(0x40F00008)  /* Power Manager Scratch Pad Register */
#define PWER		__REG(0x40F0000C)  /* Power Manager Wake-up Enable Register */
#define PRER		__REG(0x40F00010)  /* Power Manager GPIO Rising-Edge Detect Enable Register */
#define PFER		__REG(0x40F00014)  /* Power Manager GPIO Falling-Edge Detect Enable Register */
#define PEDR		__REG(0x40F00018)  /* Power Manager GPIO Edge Detect Status Register */
#define PCFR		__REG(0x40F0001C)  /* Power Manager General Configuration Register */
#define PGSR0		__REG(0x40F00020)  /* Power Manager GPIO Sleep State Register for GP[31-0] */
#define PGSR1		__REG(0x40F00024)  /* Power Manager GPIO Sleep State Register for GP[63-32] */
#define PGSR2		__REG(0x40F00028)  /* Power Manager GPIO Sleep State Register for GP[84-64] */
#define RCSR		__REG(0x40F00030)  /* Reset Controller Status Register */

#define PSSR_RDH	(1 << 5)	/* Read Disable Hold */
#define PSSR_PH		(1 << 4)	/* Peripheral Control Hold */
#define PSSR_VFS	(1 << 2)	/* VDD Fault Status */
#define PSSR_BFS	(1 << 1)	/* Battery Fault Status */
#define PSSR_SSS	(1 << 0)	/* Software Sleep Status */

#define PCFR_DS		(1 << 3)	/* Deep Sleep Mode */
#define PCFR_FS		(1 << 2)	/* Float Static Chip Selects */
#define PCFR_FP		(1 << 1)	/* Float PCMCIA controls */
#define PCFR_OPDE	(1 << 0)	/* 3.6864 MHz oscillator power-down enable */

#define RCSR_GPR	(1 << 3)	/* GPIO Reset */
#define RCSR_SMR	(1 << 2)	/* Sleep Mode */
#define RCSR_WDR	(1 << 1)	/* Watchdog Reset */
#define RCSR_HWR	(1 << 0)	/* Hardware Reset */


/*
 * SSP Serial Port Registers
 */

#define SSCR0		__REG(0x41000000)  /* SSP Control Register 0 */
#define SSCR1		__REG(0x41000004)  /* SSP Control Register 1 */
#define SSSR		__REG(0x41000008)  /* SSP Status Register */
#define SSITR		__REG(0x4100000C)  /* SSP Interrupt Test Register */
#define SSDR		__REG(0x41000010)  /* (Write / Read) SSP Data Write Register/SSP Data Read Register */


/*
 * MultiMediaCard (MMC) controller
 */

#define MMC_STRPCL	__REG(0x41100000)  /* Control to start and stop MMC clock */
#define MMC_STAT	__REG(0x41100004)  /* MMC Status Register (read only) */
#define MMC_CLKRT	__REG(0x41100008)  /* MMC clock rate */
#define MMC_SPI		__REG(0x4110000c)  /* SPI mode control bits */
#define MMC_CMDAT	__REG(0x41100010)  /* Command/response/data sequence control */
#define MMC_RESTO	__REG(0x41100014)  /* Expected response time out */
#define MMC_RDTO	__REG(0x41100018)  /* Expected data read time out */
#define MMC_BLKLEN	__REG(0x4110001c)  /* Block length of data transaction */
#define MMC_NOB		__REG(0x41100020)  /* Number of blocks, for block mode */
#define MMC_PRTBUF	__REG(0x41100024)  /* Partial MMC_TXFIFO FIFO written */
#define MMC_I_MASK	__REG(0x41100028)  /* Interrupt Mask */
#define MMC_I_REG	__REG(0x4110002c)  /* Interrupt Register (read only) */
#define MMC_CMD		__REG(0x41100030)  /* Index of current command */
#define MMC_ARGH	__REG(0x41100034)  /* MSW part of the current command argument */
#define MMC_ARGL	__REG(0x41100038)  /* LSW part of the current command argument */
#define MMC_RES		__REG(0x4110003c)  /* Response FIFO (read only) */
#define MMC_RXFIFO	__REG(0x41100040)  /* Receive FIFO (read only) */
#define MMC_TXFIFO	__REG(0x41100044)  /* Transmit FIFO (write only) */


/*
 * Core Clock
 */

#define CCCR		__REG(0x41300000)  /* Core Clock Configuration Register */
#define CKEN		__REG(0x41300004)  /* Clock Enable Register */
#define OSCC		__REG(0x41300008)  /* Oscillator Configuration Register */

#define CCCR_N_MASK	0x0380		/* Run Mode Frequency to Turbo Mode Frequency Multiplier */
#define CCCR_M_MASK	0x0060		/* Memory Frequency to Run Mode Frequency Multiplier */
#define CCCR_L_MASK	0x001f		/* Crystal Frequency to Memory Frequency Multiplier */

#define CKEN16_LCD	(1 << 16)	/* LCD Unit Clock Enable */
#define CKEN14_I2C	(1 << 14)	/* I2C Unit Clock Enable */
#define CKEN13_FICP	(1 << 13)	/* FICP Unit Clock Enable */
#define CKEN12_MMC	(1 << 12)	/* MMC Unit Clock Enable */
#define CKEN11_USB	(1 << 11)	/* USB Unit Clock Enable */
#define CKEN8_I2S	(1 << 8)	/* I2S Unit Clock Enable */
#define CKEN7_BTUART	(1 << 7)	/* BTUART Unit Clock Enable */
#define CKEN6_FFUART	(1 << 6)	/* FFUART Unit Clock Enable */
#define CKEN5_STUART	(1 << 5)	/* STUART Unit Clock Enable */
#define CKEN3_SSP	(1 << 3)	/* SSP Unit Clock Enable */
#define CKEN2_AC97	(1 << 2)	/* AC97 Unit Clock Enable */
#define CKEN1_PWM1	(1 << 1)	/* PWM1 Clock Enable */
#define CKEN0_PWM0	(1 << 0)	/* PWM0 Clock Enable */

#define OSCC_OON	(1 << 1)	/* 32.768kHz OON (write-once only bit) */
#define OSCC_OOK	(1 << 0)	/* 32.768kHz OOK (read-only bit) */


/*
 * LCD
 */

#define LCCR0		__REG(0x44000000)  /* LCD Controller Control Register 0 */
#define LCCR1		__REG(0x44000004)  /* LCD Controller Control Register 1 */
#define LCCR2		__REG(0x44000008)  /* LCD Controller Control Register 2 */
#define LCCR3		__REG(0x4400000C)  /* LCD Controller Control Register 3 */
#define DFBR0		__REG(0x44000020)  /* DMA Channel 0 Frame Branch Register */
#define DFBR1		__REG(0x44000024)  /* DMA Channel 1 Frame Branch Register */
#define LCSR		__REG(0x44000038)  /* LCD Controller Status Register */
#define LIIDR		__REG(0x4400003C)  /* LCD Controller Interrupt ID Register */
#define TMEDRGBR	__REG(0x44000040)  /* TMED RGB Seed Register */
#define TMEDCR		__REG(0x44000044)  /* TMED Control Register */

#define FDADR0		__REG(0x44000200)  /* DMA Channel 0 Frame Descriptor Address Register */
#define FSADR0		__REG(0x44000204)  /* DMA Channel 0 Frame Source Address Register */
#define FIDR0		__REG(0x44000208)  /* DMA Channel 0 Frame ID Register */
#define LDCMD0		__REG(0x4400020C)  /* DMA Channel 0 Command Register */
#define FDADR1		__REG(0x44000210)  /* DMA Channel 1 Frame Descriptor Address Register */
#define FSADR1		__REG(0x44000214)  /* DMA Channel 1 Frame Source Address Register */
#define FIDR1		__REG(0x44000218)  /* DMA Channel 1 Frame ID Register */
#define LDCMD1		__REG(0x4400021C)  /* DMA Channel 1 Command Register */

#define LCCR0_ENB	(1 << 0)	/* LCD Controller enable */
#define LCCR0_CMS	(1 << 1)	/* Color = 0, Monochrome = 1 */
#define LCCR0_SDS	(1 << 2)	/* Single Panel = 0, Dual Panel = 1 */
#define LCCR0_LDM	(1 << 3)	/* LCD Disable Done Mask */
#define LCCR0_SFM	(1 << 4)	/* Start of frame mask */
#define LCCR0_IUM	(1 << 5)	/* Input FIFO underrun mask */
#define LCCR0_EFM	(1 << 6)	/* End of Frame mask */
#define LCCR0_PAS	(1 << 7)	/* Passive = 0, Active = 1 */
#define LCCR0_BLE	(1 << 8)	/* Little Endian = 0, Big Endian = 1 */
#define LCCR0_DPD	(1 << 9)	/* Double Pixel mode, 4 pixel value = 0, 8 pixle values = 1 */
#define LCCR0_DIS	(1 << 10)	/* LCD Disable */
#define LCCR0_QDM	(1 << 11)	/* LCD Quick Disable mask */
#define LCCR0_PDD	(0xff << 12)	/* Palette DMA request delay */
#define LCCR0_PDD_S	12
#define LCCR0_BM	(1 << 20) 	/* Branch mask */
#define LCCR0_OUM	(1 << 21)	/* Output FIFO underrun mask */

#define LCCR1_PPL       Fld (10, 0)      /* Pixels Per Line - 1 */
#define LCCR1_DisWdth(Pixel)            /* Display Width [1..800 pix.]  */ \
                        (((Pixel) - 1) << FShft (LCCR1_PPL))

#define LCCR1_HSW       Fld (6, 10)     /* Horizontal Synchronization     */
#define LCCR1_HorSnchWdth(Tpix)         /* Horizontal Synchronization     */ \
                                        /* pulse Width [1..64 Tpix]       */ \
                        (((Tpix) - 1) << FShft (LCCR1_HSW))

#define LCCR1_ELW       Fld (8, 16)     /* End-of-Line pixel clock Wait    */
                                        /* count - 1 [Tpix]                */
#define LCCR1_EndLnDel(Tpix)            /*  End-of-Line Delay              */ \
                                        /*  [1..256 Tpix]                  */ \
                        (((Tpix) - 1) << FShft (LCCR1_ELW))

#define LCCR1_BLW       Fld (8, 24)     /* Beginning-of-Line pixel clock   */
                                        /* Wait count - 1 [Tpix]           */
#define LCCR1_BegLnDel(Tpix)            /*  Beginning-of-Line Delay        */ \
                                        /*  [1..256 Tpix]                  */ \
                        (((Tpix) - 1) << FShft (LCCR1_BLW))


#define LCCR2_LPP       Fld (10, 0)     /* Line Per Panel - 1              */
#define LCCR2_DisHght(Line)             /*  Display Height [1..1024 lines] */ \
                        (((Line) - 1) << FShft (LCCR2_LPP))

#define LCCR2_VSW       Fld (6, 10)     /* Vertical Synchronization pulse  */
                                        /* Width - 1 [Tln] (L_FCLK)        */
#define LCCR2_VrtSnchWdth(Tln)          /*  Vertical Synchronization pulse */ \
                                        /*  Width [1..64 Tln]              */ \
                        (((Tln) - 1) << FShft (LCCR2_VSW))

#define LCCR2_EFW       Fld (8, 16)     /* End-of-Frame line clock Wait    */
                                        /* count [Tln]                     */
#define LCCR2_EndFrmDel(Tln)            /*  End-of-Frame Delay             */ \
                                        /*  [0..255 Tln]                   */ \
                        ((Tln) << FShft (LCCR2_EFW))

#define LCCR2_BFW       Fld (8, 24)     /* Beginning-of-Frame line clock   */
                                        /* Wait count [Tln]                */
#define LCCR2_BegFrmDel(Tln)            /*  Beginning-of-Frame Delay       */ \
                                        /*  [0..255 Tln]                   */ \
                        ((Tln) << FShft (LCCR2_BFW))

#if 0
#define LCCR3_PCD	(0xff)		/* Pixel clock divisor */
#define LCCR3_ACB	(0xff << 8)	/* AC Bias pin frequency */
#define LCCR3_ACB_S	8
#endif

#define LCCR3_API	(0xf << 16)	/* AC Bias pin trasitions per interrupt */
#define LCCR3_API_S	16
#define LCCR3_VSP	(1 << 20)	/* vertical sync polarity */
#define LCCR3_HSP	(1 << 21)	/* horizontal sync polarity */
#define LCCR3_PCP	(1 << 22)	/* pixel clock polarity */
#define LCCR3_OEP	(1 << 23)	/* output enable polarity */
#if 0
#define LCCR3_BPP	(7 << 24)	/* bits per pixel */
#define LCCR3_BPP_S	24
#endif
#define LCCR3_DPC	(1 << 27)	/* double pixel clock mode */


#define LCCR3_PCD       Fld (8, 0)      /* Pixel Clock Divisor */
#define LCCR3_PixClkDiv(Div)            /* Pixel Clock Divisor */ \
                        (((Div) << FShft (LCCR3_PCD)))


#define LCCR3_BPP       Fld (3, 24)     /* Bit Per Pixel */
#define LCCR3_Bpp(Bpp)                  /* Bit Per Pixel */ \
                        (((Bpp) << FShft (LCCR3_BPP)))

#define LCCR3_ACB       Fld (8, 8)      /* AC Bias */
#define LCCR3_Acb(Acb)                  /* BAC Bias */ \
                        (((Acb) << FShft (LCCR3_ACB)))

#define LCCR3_HorSnchH  (LCCR3_HSP*0)   /*  Horizontal Synchronization     */
                                        /*  pulse active High              */
#define LCCR3_HorSnchL  (LCCR3_HSP*1)   /*  Horizontal Synchronization     */

#define LCCR3_VrtSnchH  (LCCR3_VSP*0)   /*  Vertical Synchronization pulse */
                                        /*  active High                    */
#define LCCR3_VrtSnchL  (LCCR3_VSP*1)   /*  Vertical Synchronization pulse */
                                        /*  active Low                     */

#define LCSR_LDD	(1 << 0)	/* LCD Disable Done */
#define LCSR_SOF	(1 << 1)	/* Start of frame */
#define LCSR_BER	(1 << 2)	/* Bus error */
#define LCSR_ABC	(1 << 3)	/* AC Bias count */
#define LCSR_IUL	(1 << 4)	/* input FIFO underrun Lower panel */
#define LCSR_IUU	(1 << 5)	/* input FIFO underrun Upper panel */
#define LCSR_OU		(1 << 6)	/* output FIFO underrun */
#define LCSR_QD		(1 << 7)	/* quick disable */
#define LCSR_EOF	(1 << 8)	/* end of frame */
#define LCSR_BS		(1 << 9)	/* branch status */
#define LCSR_SINT	(1 << 10)	/* subsequent interrupt */

#define LDCMD_PAL	(1 << 26)	/* instructs DMA to load palette buffer */

#define LCSR_LDD	(1 << 0)	/* LCD Disable Done */
#define LCSR_SOF	(1 << 1)	/* Start of frame */
#define LCSR_BER	(1 << 2)	/* Bus error */
#define LCSR_ABC	(1 << 3)	/* AC Bias count */
#define LCSR_IUL	(1 << 4)	/* input FIFO underrun Lower panel */
#define LCSR_IUU	(1 << 5)	/* input FIFO underrun Upper panel */
#define LCSR_OU		(1 << 6)	/* output FIFO underrun */
#define LCSR_QD		(1 << 7)	/* quick disable */
#define LCSR_EOF	(1 << 8)	/* end of frame */
#define LCSR_BS		(1 << 9)	/* branch status */
#define LCSR_SINT	(1 << 10)	/* subsequent interrupt */

#define LDCMD_PAL	(1 << 26)	/* instructs DMA to load palette buffer */

/*
 * Memory controller
 */

#define MDCNFG		__REG(0x48000000)  /* SDRAM Configuration Register 0 */
#define MDREFR		__REG(0x48000004)  /* SDRAM Refresh Control Register */
#define MSC0		__REG(0x48000008)  /* Static Memory Control Register 0 */
#define MSC1		__REG(0x4800000C)  /* Static Memory Control Register 1 */
#define MSC2		__REG(0x48000010)  /* Static Memory Control Register 2 */
#define MECR		__REG(0x48000014)  /* Expansion Memory (PCMCIA/Compact Flash) Bus Configuration */
#define SXLCR		__REG(0x48000018)  /* LCR value to be written to SDRAM-Timing Synchronous Flash */
#define SXCNFG		__REG(0x4800001C)  /* Synchronous Static Memory Control Register */
#define SXMRS		__REG(0x48000024)  /* MRS value to be written to Synchronous Flash or SMROM */
#define MCMEM0		__REG(0x48000028)  /* Card interface Common Memory Space Socket 0 Timing */
#define MCMEM1		__REG(0x4800002C)  /* Card interface Common Memory Space Socket 1 Timing */
#define MCATT0		__REG(0x48000030)  /* Card interface Attribute Space Socket 0 Timing Configuration */
#define MCATT1		__REG(0x48000034)  /* Card interface Attribute Space Socket 1 Timing Configuration */
#define MCIO0		__REG(0x48000038)  /* Card interface I/O Space Socket 0 Timing Configuration */
#define MCIO1		__REG(0x4800003C)  /* Card interface I/O Space Socket 1 Timing Configuration */
#define MDMRS		__REG(0x48000040)  /* MRS value to be written to SDRAM */
#define BOOT_DEF	__REG(0x48000044)  /* Read-Only Boot-Time Register. Contains BOOT_SEL and PKG_SEL */

#define MDREFR_K2FREE	(1 << 25)	/* SDRAM Free-Running Control */
#define MDREFR_K1FREE	(1 << 24)	/* SDRAM Free-Running Control */
#define MDREFR_K0FREE	(1 << 23)	/* SDRAM Free-Running Control */
#define MDREFR_SLFRSH	(1 << 22)	/* SDRAM Self-Refresh Control/Status */
#define MDREFR_APD	(1 << 20)	/* SDRAM/SSRAM Auto-Power-Down Enable */
#define MDREFR_K2DB2	(1 << 19)	/* SDCLK2 Divide by 2 Control/Status */
#define MDREFR_K2RUN	(1 << 18)	/* SDCLK2 Run Control/Status */
#define MDREFR_K1DB2	(1 << 17)	/* SDCLK1 Divide by 2 Control/Status */
#define MDREFR_K1RUN	(1 << 16)	/* SDCLK1 Run Control/Status */
#define MDREFR_E1PIN	(1 << 15)	/* SDCKE1 Level Control/Status */
#define MDREFR_K0DB2	(1 << 14)	/* SDCLK0 Divide by 2 Control/Status */
#define MDREFR_K0RUN	(1 << 13)	/* SDCLK0 Run Control/Status */
#define MDREFR_E0PIN	(1 << 12)	/* SDCKE0 Level Control/Status */



