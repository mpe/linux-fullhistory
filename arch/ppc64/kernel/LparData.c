/* 
 * Copyright 2001 Mike Corrigan, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/threads.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/naca.h>
#include <asm/abs_addr.h>
#include <asm/iSeries/ItLpNaca.h>
#include <asm/lppaca.h>
#include <asm/iSeries/ItLpRegSave.h>
#include <asm/paca.h>
#include <asm/iSeries/HvReleaseData.h>
#include <asm/iSeries/LparMap.h>
#include <asm/iSeries/ItVpdAreas.h>
#include <asm/iSeries/ItIplParmsReal.h>
#include <asm/iSeries/ItExtVpdPanel.h>
#include <asm/iSeries/ItLpQueue.h>
#include <asm/iSeries/IoHriProcessorVpd.h>
#include <asm/iSeries/ItSpCommArea.h>

/* The LpQueue is used to pass event data from the hypervisor to
 * the partition.  This is where I/O interrupt events are communicated.
 */

/* May be filled in by the hypervisor so cannot end up in the BSS */
struct ItLpQueue xItLpQueue __attribute__((__section__(".data")));


/* The HvReleaseData is the root of the information shared between 
 * the hypervisor and Linux.  
 */

struct HvReleaseData hvReleaseData = {
	.xDesc = 0xc8a5d9c4,	/* "HvRD" ebcdic */
	.xSize = sizeof(struct HvReleaseData),
	.xVpdAreasPtrOffset = offsetof(struct naca_struct, xItVpdAreas),
	.xSlicNacaAddr = &naca,		/* 64-bit Naca address */
	.xMsNucDataOffset = 0x4800,	/* offset of LparMap within loadarea (see head.S) */
	.xTagsMode = 1,			/* tags inactive       */
	.xAddressSize = 0,		/* 64 bit              */
	.xNoSharedProcs = 0,		/* shared processors   */
	.xNoHMT = 0,			/* HMT allowed         */
	.xRsvd2 = 6,			/* TEMP: This allows non-GA driver */
	.xVrmIndex = 4,			/* We are v5r2m0               */
	.xMinSupportedPlicVrmIndex = 3,		/* v5r1m0 */
	.xMinCompatablePlicVrmIndex = 3,	/* v5r1m0 */
	.xVrmName = { 0xd3, 0x89, 0x95, 0xa4,	/* "Linux 2.4.64" ebcdic */
		0xa7, 0x40, 0xf2, 0x4b,
		0xf4, 0x4b, 0xf6, 0xf4 },
};

extern void system_reset_iSeries(void);
extern void machine_check_iSeries(void);
extern void data_access_iSeries(void);
extern void instruction_access_iSeries(void);
extern void hardware_interrupt_iSeries(void);
extern void alignment_iSeries(void);
extern void program_check_iSeries(void);
extern void fp_unavailable_iSeries(void);
extern void decrementer_iSeries(void);
extern void trap_0a_iSeries(void);
extern void trap_0b_iSeries(void);
extern void system_call_iSeries(void);
extern void single_step_iSeries(void);
extern void trap_0e_iSeries(void);
extern void performance_monitor_iSeries(void);
extern void data_access_slb_iSeries(void);
extern void instruction_access_slb_iSeries(void);
	
struct ItLpNaca itLpNaca = {
	.xDesc = 0xd397d581,		/* "LpNa" ebcdic */
	.xSize = 0x0400,		/* size of ItLpNaca */
	.xIntHdlrOffset = 0x0300,	/* offset to int array */
	.xMaxIntHdlrEntries = 19,	/* # ents */
	.xPrimaryLpIndex = 0,		/* Part # of primary */
	.xServiceLpIndex = 0,		/* Part # of serv */
	.xLpIndex = 0,			/* Part # of me */
	.xMaxLpQueues = 0,		/* # of LP queues */
	.xLpQueueOffset = 0x100,	/* offset of start of LP queues */
	.xPirEnvironMode = 0,		/* Piranha stuff */
	.xPirConsoleMode = 0,
	.xPirDasdMode = 0,
	.xLparInstalled = 0,
	.xSysPartitioned = 0,
	.xHwSyncedTBs = 0,
	.xIntProcUtilHmt = 0,
	.xSpVpdFormat = 0,
	.xIntProcRatio = 0,
	.xPlicVrmIndex = 0,		/* VRM index of PLIC */
	.xMinSupportedSlicVrmInd = 0,	/* min supported SLIC */
	.xMinCompatableSlicVrmInd = 0,	/* min compat SLIC */
	.xLoadAreaAddr = 0,		/* 64-bit addr of load area */
	.xLoadAreaChunks = 0,		/* chunks for load area */
	.xPaseSysCallCRMask = 0,	/* PASE mask */
	.xSlicSegmentTablePtr = 0,	/* seg table */
	.xOldLpQueue = { 0 }, 		/* Old LP Queue */
	.xInterruptHdlr = {
		(u64)system_reset_iSeries,	/* 0x100 System Reset */
		(u64)machine_check_iSeries,	/* 0x200 Machine Check */
		(u64)data_access_iSeries,	/* 0x300 Data Access */
		(u64)instruction_access_iSeries, /* 0x400 Instruction Access */
		(u64)hardware_interrupt_iSeries, /* 0x500 External */
		(u64)alignment_iSeries,		/* 0x600 Alignment */
		(u64)program_check_iSeries,	/* 0x700 Program Check */
		(u64)fp_unavailable_iSeries,	/* 0x800 FP Unavailable */
		(u64)decrementer_iSeries,	/* 0x900 Decrementer */
		(u64)trap_0a_iSeries,		/* 0xa00 Trap 0A */
		(u64)trap_0b_iSeries,		/* 0xb00 Trap 0B */
		(u64)system_call_iSeries,	/* 0xc00 System Call */
		(u64)single_step_iSeries,	/* 0xd00 Single Step */
		(u64)trap_0e_iSeries,		/* 0xe00 Trap 0E */
		(u64)performance_monitor_iSeries,/* 0xf00 Performance Monitor */
		0,				/* int 0x1000 */
		0,				/* int 0x1010 */
		0,				/* int 0x1020 CPU ctls */
		(u64)hardware_interrupt_iSeries, /* SC Ret Hdlr */
		(u64)data_access_slb_iSeries,	/* 0x380 D-SLB */
		(u64)instruction_access_slb_iSeries /* 0x480 I-SLB */
	}
};
EXPORT_SYMBOL(itLpNaca);

/* May be filled in by the hypervisor so cannot end up in the BSS */
struct ItIplParmsReal xItIplParmsReal __attribute__((__section__(".data"))); 

/* May be filled in by the hypervisor so cannot end up in the BSS */
struct ItExtVpdPanel xItExtVpdPanel __attribute__((__section__(".data")));
EXPORT_SYMBOL(xItExtVpdPanel);

#define maxPhysicalProcessors 32

struct IoHriProcessorVpd xIoHriProcessorVpd[maxPhysicalProcessors] = {
	{
		.xInstCacheOperandSize = 32,
		.xDataCacheOperandSize = 32,
		.xProcFreq     = 50000000,
		.xTimeBaseFreq = 50000000,
		.xPVR = 0x3600
	}
};
	
/* Space for Main Store Vpd 27,200 bytes */
/* May be filled in by the hypervisor so cannot end up in the BSS */
u64    xMsVpd[3400] __attribute__((__section__(".data")));

/* Space for Recovery Log Buffer */
/* May be filled in by the hypervisor so cannot end up in the BSS */
u64    xRecoveryLogBuffer[32] __attribute__((__section__(".data")));

struct SpCommArea xSpCommArea = {
	.xDesc = 0xE2D7C3C2,
	.xFormat = 1,
};

/* The LparMap data is now located at offset 0x6000 in head.S
 * It was put there so that the HvReleaseData could address it
 * with a 32-bit offset as required by the iSeries hypervisor
 *
 * The Naca has a pointer to the ItVpdAreas.  The hypervisor finds
 * the Naca via the HvReleaseData area.  The HvReleaseData has the
 * offset into the Naca of the pointer to the ItVpdAreas.
 */
struct ItVpdAreas itVpdAreas = {
	.xSlicDesc = 0xc9a3e5c1,		/* "ItVA" */
	.xSlicSize = sizeof(struct ItVpdAreas),
	.xSlicVpdEntries = ItVpdMaxEntries,	/* # VPD array entries */
	.xSlicDmaEntries = ItDmaMaxEntries,	/* # DMA array entries */
	.xSlicMaxLogicalProcs = NR_CPUS * 2,	/* Max logical procs */
	.xSlicMaxPhysicalProcs = maxPhysicalProcessors,	/* Max physical procs */
	.xSlicDmaToksOffset = offsetof(struct ItVpdAreas, xPlicDmaToks),
	.xSlicVpdAdrsOffset = offsetof(struct ItVpdAreas, xSlicVpdAdrs),
	.xSlicDmaLensOffset = offsetof(struct ItVpdAreas, xPlicDmaLens),
	.xSlicVpdLensOffset = offsetof(struct ItVpdAreas, xSlicVpdLens),
	.xSlicMaxSlotLabels = 0,		/* max slot labels */
	.xSlicMaxLpQueues = 1,			/* max LP queues */
	.xPlicDmaLens = { 0 },			/* DMA lengths */
	.xPlicDmaToks = { 0 },			/* DMA tokens */
	.xSlicVpdLens = {			/* VPD lengths */
	        0,0,0,		        /*  0 - 2 */
		sizeof(xItExtVpdPanel), /*       3 Extended VPD   */
		sizeof(struct paca_struct),	/*       4 length of Paca  */
		0,			/*       5 */
		sizeof(struct ItIplParmsReal),/* 6 length of IPL parms */
		26992,			/*	 7 length of MS VPD */
		0,			/*       8 */
		sizeof(struct ItLpNaca),/*       9 length of LP Naca */
		0, 			/*	10 */
		256,			/*	11 length of Recovery Log Buf */
		sizeof(struct SpCommArea), /*   12 length of SP Comm Area */
		0,0,0,			/* 13 - 15 */
		sizeof(struct IoHriProcessorVpd),/* 16 length of Proc Vpd */
		0,0,0,0,0,0,		/* 17 - 22  */
		sizeof(struct ItLpQueue),/*     23 length of Lp Queue */
		0,0			/* 24 - 25 */
		},
	.xSlicVpdAdrs = {			/* VPD addresses */
		0,0,0,  		/*	 0 -  2 */
		&xItExtVpdPanel,        /*       3 Extended VPD */
		&paca[0],		/*       4 first Paca */
		0,			/*       5 */
		&xItIplParmsReal,	/*	 6 IPL parms */
		&xMsVpd,		/*	 7 MS Vpd */
		0,			/*       8 */
		&itLpNaca,		/*       9 LpNaca */
		0,			/*	10 */
		&xRecoveryLogBuffer,	/*	11 Recovery Log Buffer */
		&xSpCommArea,		/*	12 SP Comm Area */
		0,0,0,			/* 13 - 15 */
		&xIoHriProcessorVpd,	/*      16 Proc Vpd */
		0,0,0,0,0,0,		/* 17 - 22 */
		&xItLpQueue,		/*      23 Lp Queue */
		0,0
	}
};

struct msChunks msChunks;
EXPORT_SYMBOL(msChunks);

/* Depending on whether this is called from iSeries or pSeries setup
 * code, the location of the msChunks struct may or may not have
 * to be reloc'd, so we force the caller to do that for us by passing
 * in a pointer to the structure.
 */
unsigned long
msChunks_alloc(unsigned long mem, unsigned long num_chunks, unsigned long chunk_size)
{
	unsigned long offset = reloc_offset();
	struct msChunks *_msChunks = PTRRELOC(&msChunks);

	_msChunks->num_chunks  = num_chunks;
	_msChunks->chunk_size  = chunk_size;
	_msChunks->chunk_shift = __ilog2(chunk_size);
	_msChunks->chunk_mask  = (1UL<<_msChunks->chunk_shift)-1;

	mem = _ALIGN(mem, sizeof(msChunks_entry));
	_msChunks->abs = (msChunks_entry *)(mem + offset);
	mem += num_chunks * sizeof(msChunks_entry);

	return mem;
}
