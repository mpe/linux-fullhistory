/* fpumacro.h: FPU related macros.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_FPUMACRO_H
#define _SPARC64_FPUMACRO_H

#include <asm/asi.h>
#include <asm/visasm.h>

struct fpustate {
	u32	regs[64];
};

#define FPUSTATE (struct fpustate *)(((unsigned long)current) + AOFF_task_fpregs)

extern __inline__ unsigned long fprs_read(void)
{
	unsigned long retval;

	__asm__ __volatile__("rd %%fprs, %0" : "=r" (retval));

	return retval;
}

extern __inline__ void fprs_write(unsigned long val)
{
	__asm__ __volatile__("wr %0, 0x0, %%fprs" : : "r" (val));
}

extern __inline__ void fpsave(unsigned long *fpregs,
			      unsigned long *fsr,
			      unsigned long *gsr)
{
	__asm__ __volatile__ ("
	wr	%%g0, %3, %%asi
	rd	%%gsr, %%g1
	membar	#LoadStore | #StoreStore
	stx	%%fsr, [%1]
	stx	%%g1, [%2]
	stda	%%f0, [%0] %%asi
	stda	%%f16, [%0 + 64] %%asi
	stda	%%f32, [%0 + 128] %%asi
	stda	%%f48, [%0 + 192] %%asi
	membar	#Sync
"	: /* No outputs */
	: "r" (fpregs), "r" (fsr), "r" (gsr), "i" (ASI_BLK_P)
	: "g1");
}

extern __inline__ void fpload(unsigned long *fpregs,
			      unsigned long *fsr,
			      unsigned long *gsr)
{
	__asm__ __volatile__ ("
	wr	%%g0, %3, %%asi
	membar	#StoreLoad | #LoadLoad
	ldda	[%0] %%asi, %%f0
	ldda	[%0 + 64] %%asi, %%f16
	ldda	[%0 + 128] %%asi, %%f32
	ldda	[%0 + 192] %%asi, %%f48
	ldx	[%1], %%fsr
	ldx	[%2], %%g1
	wr	%%g1, 0, %%gsr
	membar	#Sync
"	: /* No outputs */
	: "r" (fpregs), "r" (fsr), "r" (gsr), "i" (ASI_BLK_P)
	: "g1");
}

#endif /* !(_SPARC64_FPUMACRO_H) */
