/*
 * drivers/sbus/audio/amd7930.h
 *
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@noc.rutgers.edu)
 *
 * Definitions for the AMD79C30 Digital Subscriber Controller which is
 * used as an audio chip in sun4c architecture machines. The
 * information in this file is based on Advanced Micro Devices
 * Publication 09893, Rev G, Amendment /0, Final (a.k.a. the data
 * sheet).
 */

#ifndef _AMD7930_H_
#define _AMD7930_H_

#include <linux/types.h>

/* Register interface presented to the CPU by the amd7930. */
struct amd7930
{
	__volatile__ __u8 cr;		/* Command Register (W) */
#define ir cr				/* Interrupt Register (R) */
	__volatile__ __u8 dr;		/* Data Register (R/W) */
	__volatile__ __u8 dsr1;		/* D-channel Status Register 1 (R) */
	__volatile__ __u8 der;		/* D-channel Error Register (R) */
	__volatile__ __u8 dctb;		/* D-channel Transmit Buffer (W) */
#define dcrb dctb			/* D-channel Receive Buffer (R) */
	__volatile__ __u8 bbtb;		/* Bb-channel Transmit Buffer (W) */
#define bbrb bbtb			/* Bb-channel Receive Buffer (R) */
	__volatile__ __u8 bctb;		/* Bc-channel Transmit Buffer (W) */
#define bcrb bctb			/* Bc-channel Receive Buffer (R) */
	__volatile__ __u8 dsr2;		/* D-channel Status Register 2 (R) */
};


/* Indirect registers in the Main Audio Processor. */
struct amd7930_map {
	__u16	x[8];
	__u16	r[8];
	__u16	gx;
	__u16	gr;
	__u16	ger;
	__u16	stgr;
	__u16	ftgr;
	__u16	atgr;
	__u8	mmr1;
	__u8	mmr2;
};


/* The amd7930 has "indirect registers" which are accessed by writing
 * the register number into the Command Register and then reading or
 * writing values from the Data Register as appropriate. We define the
 * AMR_* macros to be the indirect register numbers and AM_* macros to
 * be bits in whatever register is referred to.
 */

/* Initialization */
#define	AMR_INIT			0x21
#define		AM_INIT_ACTIVE			0x01
#define		AM_INIT_DATAONLY		0x02
#define		AM_INIT_POWERDOWN		0x03
#define		AM_INIT_DISABLE_INTS		0x04
#define AMR_INIT2			0x20
#define		AM_INIT2_ENABLE_POWERDOWN	0x20
#define		AM_INIT2_ENABLE_MULTIFRAME	0x10

/* Line Interface Unit */
#define	AMR_LIU_LSR			0xA1
#define	AMR_LIU_LPR			0xA2
#define	AMR_LIU_LMR1			0xA3
#define	AMR_LIU_LMR2			0xA4
#define	AMR_LIU_2_4			0xA5
#define	AMR_LIU_MF			0xA6
#define	AMR_LIU_MFSB			0xA7
#define	AMR_LIU_MFQB			0xA8

/* Multiplexor */
#define	AMR_MUX_MCR1			0x41
#define	AMR_MUX_MCR2			0x42
#define	AMR_MUX_MCR3			0x43
#define		AM_MUX_CHANNEL_B1		0x01
#define		AM_MUX_CHANNEL_B2		0x02
#define		AM_MUX_CHANNEL_Ba		0x03
#define		AM_MUX_CHANNEL_Bb		0x04
#define		AM_MUX_CHANNEL_Bc		0x05
#define		AM_MUX_CHANNEL_Bd		0x06
#define		AM_MUX_CHANNEL_Be		0x07
#define		AM_MUX_CHANNEL_Bf		0x08
#define	AMR_MUX_MCR4			0x44
#define		AM_MUX_MCR4_ENABLE_INTS		0x08
#define		AM_MUX_MCR4_REVERSE_Bb		0x10
#define		AM_MUX_MCR4_REVERSE_Bc		0x20
#define	AMR_MUX_1_4			0x45

/* Main Audio Processor */
#define	AMR_MAP_X			0x61
#define	AMR_MAP_R			0x62
#define	AMR_MAP_GX			0x63
#define	AMR_MAP_GR			0x64
#define	AMR_MAP_GER			0x65
#define	AMR_MAP_STGR			0x66
#define	AMR_MAP_FTGR_1_2		0x67
#define	AMR_MAP_ATGR_1_2		0x68
#define	AMR_MAP_MMR1			0x69
#define		AM_MAP_MMR1_ALAW		0x01
#define		AM_MAP_MMR1_GX			0x02
#define		AM_MAP_MMR1_GR			0x04
#define		AM_MAP_MMR1_GER			0x08
#define		AM_MAP_MMR1_X			0x10
#define		AM_MAP_MMR1_R			0x20
#define		AM_MAP_MMR1_STG			0x40
#define		AM_MAP_MMR1_LOOPBACK		0x80
#define	AMR_MAP_MMR2			0x6A
#define		AM_MAP_MMR2_AINB		0x01
#define		AM_MAP_MMR2_LS			0x02
#define		AM_MAP_MMR2_ENABLE_DTMF		0x04
#define		AM_MAP_MMR2_ENABLE_TONEGEN	0x08
#define		AM_MAP_MMR2_ENABLE_TONERING	0x10
#define		AM_MAP_MMR2_DISABLE_HIGHPASS	0x20
#define		AM_MAP_MMR2_DISABLE_AUTOZERO	0x40
#define	AMR_MAP_1_10			0x6B
#define	AMR_MAP_MMR3			0x6C
#define	AMR_MAP_STRA			0x6D
#define	AMR_MAP_STRF			0x6E
#define	AMR_MAP_PEAKX			0x70
#define	AMR_MAP_PEAKR			0x71
#define	AMR_MAP_15_16			0x72

/* Data Link Controller */
#define	AMR_DLC_FRAR_1_2_3		0x81
#define	AMR_DLC_SRAR_1_2_3		0x82
#define	AMR_DLC_TAR			0x83
#define	AMR_DLC_DRLR			0x84
#define	AMR_DLC_DTCR			0x85
#define	AMR_DLC_DMR1			0x86
#define	AMR_DLC_DMR2			0x87
#define	AMR_DLC_1_7			0x88
#define	AMR_DLC_DRCR			0x89
#define	AMR_DLC_RNGR1			0x8A
#define	AMR_DLC_RNGR2			0x8B
#define	AMR_DLC_FRAR4			0x8C
#define	AMR_DLC_SRAR4			0x8D
#define	AMR_DLC_DMR3			0x8E
#define	AMR_DLC_DMR4			0x8F
#define	AMR_DLC_12_15			0x90
#define	AMR_DLC_ASR			0x91
#define	AMR_DLC_EFCR			0x92

/* Peripheral Port */
#define	AMR_PP_PPCR1			0xC0
#define	AMR_PP_PPSR			0xC1
#define	AMR_PP_PPIER			0xC2
#define	AMR_PP_MTDR			0xC3
#define	AMR_PP_MRDR			0xC3
#define	AMR_PP_CITDR0			0xC4
#define	AMR_PP_CIRDR0			0xC4
#define	AMR_PP_CITDR1			0xC5
#define	AMR_PP_CIRDR1			0xC5
#define	AMR_PP_PPCR2			0xC8
#define	AMR_PP_PPCR3			0xC9

#endif
