/* fpumacro.h: FPU related macros.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#ifndef _SPARC64_FPUMACRO_H
#define _SPARC64_FPUMACRO_H

extern __inline__ void fpsave32(unsigned long *fpregs, unsigned long *fsr)
{
	__asm__ __volatile__ ("
	wr	%%g0, %2, %%asi
	stx	%%fsr, [%1]
	stda	%%f0, [%0] %%asi
	stda	%%f16, [%0 + 64] %%asi
	" : : "r" (fpregs), "r" (fsr), "i" (ASI_BLK_P));
}

extern __inline__ void fpload32(unsigned long *fpregs, unsigned long *fsr)
{
	__asm__ __volatile__ ("
	wr	%%g0, %2, %%asi
	ldda	[%0] %%asi, %%f0
	ldda	[%0 + 64] %%asi, %%f16
	ldx	[%1], %%fsr
	" : : "r" (fpregs), "r" (fsr), "i" (ASI_BLK_P));
}

extern __inline__ void fpsave(unsigned long *fpregs, unsigned long *fsr)
{
	__asm__ __volatile__ ("
	wr	%%g0, %2, %%asi
	stx	%%fsr, [%1]
	stda	%%f0, [%0] %%asi
	stda	%%f16, [%0 + 64] %%asi
	stda	%%f32, [%0 + 128] %%asi
	stda	%%f48, [%0 + 192] %%asi
	" : : "r" (fpregs), "r" (fsr), "i" (ASI_BLK_P));
}

extern __inline__ void fpload(unsigned long *fpregs, unsigned long *fsr)
{
	__asm__ __volatile__ ("
	wr	%%g0, %2, %%asi
	ldda	[%0] %%asi, %%f0
	ldda	[%0 + 64] %%asi, %%f16
	ldda	[%0 + 128] %%asi, %%f32
	ldda	[%0 + 192] %%asi, %%f48
	ldx	[%1], %%fsr
	" : : "r" (fpregs), "r" (fsr), "i" (ASI_BLK_P));
}

#endif /* !(_SPARC64_FPUMACRO_H) */
