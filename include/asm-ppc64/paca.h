#ifndef _PPC64_PACA_H
#define _PPC64_PACA_H

/*============================================================================
 *                                                         Header File Id
 * Name______________:	paca.h
 *
 * Description_______:
 *
 * This control block defines the PACA which defines the processor 
 * specific data for each logical processor on the system.  
 * There are some pointers defined that are utilized by PLIC.
 *
 * C 2001 PPC 64 Team, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */    
#include	<asm/types.h>

#define N_EXC_STACK    2

/*-----------------------------------------------------------------------------
 * Other Includes
 *-----------------------------------------------------------------------------
 */
#include	<asm/iSeries/ItLpPaca.h>
#include	<asm/iSeries/ItLpRegSave.h>
#include	<asm/iSeries/ItLpQueue.h>
#include	<asm/mmu.h>
#include	<asm/processor.h>

extern struct paca_struct paca[];
register struct paca_struct *local_paca asm("r13");
#define get_paca()	local_paca

struct task_struct;

/*============================================================================
 * Name_______:	paca
 *
 * Description:
 *
 *	Defines the layout of the paca.  
 *
 *	This structure is not directly accessed by PLIC or the SP except
 *	for the first two pointers that point to the ItLpPaca area and the
 *	ItLpRegSave area for this processor.  Both the ItLpPaca and
 *	ItLpRegSave objects are currently contained within the
 *	PACA but they do not need to be.
 *
 *============================================================================
 */
struct paca_struct {
/*=====================================================================================
 * CACHE_LINE_1 0x0000 - 0x007F
 *=====================================================================================
 */
	struct ItLpPaca *xLpPacaPtr;	/* Pointer to LpPaca for PLIC		0x00 */
	struct ItLpRegSave *xLpRegSavePtr; /* Pointer to LpRegSave for PLIC	0x08 */
	struct task_struct *xCurrent;	/* Pointer to current			0x10 */
	/* Note: the spinlock functions in arch/ppc64/lib/locks.c load lock_token and
	   xPacaIndex with a single lwz instruction, using the constant offset 24.
	   If you move either field, fix the spinlocks and rwlocks. */
	u16 lock_token;			/* Constant 0x8000, used in spinlocks	0x18 */
	u16 xPacaIndex;			/* Logical processor number		0x1A */
	u32 default_decr;		/* Default decrementer value		0x1c */	
	u64 xKsave;			/* Saved Kernel stack addr or zero	0x20 */
	struct ItLpQueue *lpQueuePtr;	/* LpQueue handled by this processor    0x28 */
	u64  xTOC;			/* Kernel TOC address			0x30 */
	STAB xStab_data;		/* Segment table information		0x38,0x40,0x48 */
	u8 *exception_sp;		/*                                      0x50 */
	u8 xProcEnabled;		/*                                      0x58 */
	u8 prof_enabled;		/* 1=iSeries profiling enabled          0x59 */
	u16 xHwProcNum;			/* Physical processor number		0x5a */
	u8 resv1[36];			/*					0x5c */

/*=====================================================================================
 * CACHE_LINE_2 0x0080 - 0x00FF
 *=====================================================================================
 */
	u64 spare1;			/*					0x00 */
	u64 spare2;			/*					0x08 */
	u64 spare3;			/*					0x10 */
	u64 spare4;		/*					0x18 */
	u64 next_jiffy_update_tb;	/* TB value for next jiffy update	0x20 */
	u32 lpEvent_count;		/* lpEvents processed			0x28 */
	u32 prof_multiplier;		/*					0x2C */
	u32 prof_counter;		/*					0x30 */
	u32 prof_shift;			/* iSeries shift for profile bucket size0x34 */
	u32 *prof_buffer;		/* iSeries profiling buffer		0x38 */
	u32 *prof_stext;		/* iSeries start of kernel text		0x40 */
	u32 prof_len;			/* iSeries length of profile buffer -1	0x48 */
	u8  yielded;                    /* 0 = this processor is running        0x4c */
	                                /* 1 = this processor is yielded             */
	u8  rsvd2[128-77];		/*					0x49 */

/*=====================================================================================
 * CACHE_LINE_3 0x0100 - 0x017F
 *=====================================================================================
 */
	u8		xProcStart;	/* At startup, processor spins until	0x100 */
  					/* xProcStart becomes non-zero. */
	u8		rsvd3[127];

/*=====================================================================================
 * CACHE_LINE_4-8  0x0180 - 0x03FF Contains ItLpPaca
 *=====================================================================================
 */
	struct ItLpPaca xLpPaca;	/* Space for ItLpPaca */

/*=====================================================================================
 * CACHE_LINE_9-16 0x0400 - 0x07FF Contains ItLpRegSave
 *=====================================================================================
 */
	struct ItLpRegSave xRegSav;	/* Register save for proc */

/*=====================================================================================
 * CACHE_LINE_17-18 0x0800 - 0x08FF Reserved
 *=====================================================================================
 */
	u64 xR1;			/* r1 save for RTAS calls */
	u64 xSavedMsr;			/* Old msr saved here by HvCall */
	u8 rsvd5[256-16];

/*=====================================================================================
 * CACHE_LINE_19-30 0x0900 - 0x0EFF Reserved
 *=====================================================================================
 */
	u64 slb_shadow[0x20];
	u64 dispatch_log;
	u8  rsvd6[0x500 - 0x8];

/*=====================================================================================
 * CACHE_LINE_31-32 0x0F00 - 0x0FFF Exception register save areas
 *=====================================================================================
 */
	u64 exgen[8];		/* used for most interrupts/exceptions */
	u64 exmc[8];		/* used for machine checks */
	u64 exslb[8];		/* used for SLB/segment table misses
				 * on the linear mapping */
	u64 exdsi[8];		/* used for linear mapping hash table misses */

/*=====================================================================================
 * Page 2 used as a stack when we detect a bad kernel stack pointer,
 * and early in SMP boots before relocation is enabled.
 *=====================================================================================
 */
	u8 guard[0x1000];
};

#endif /* _PPC64_PACA_H */
