/* fpumacro.h: FPU related macros.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_FPUMACRO_H
#define _SPARC64_FPUMACRO_H

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

extern __inline__ void fpsave32(unsigned int *fpregs, unsigned long *fsr)
{
	__asm__ __volatile__ ("
	wr	%%g0, %2, %%asi
	stx	%%fsr, [%1]
	stda	%%f0, [%0] %%asi
	stda	%%f16, [%0 + 64] %%asi
	" : : "r" (fpregs), "r" (fsr), "i" (ASI_BLK_P));
}

extern __inline__ void fpload32(unsigned int *fpregs, unsigned long *fsr)
{
	__asm__ __volatile__ ("
	wr	%%g0, %2, %%asi
	ldda	[%0] %%asi, %%f0
	ldda	[%0 + 64] %%asi, %%f16
	ldx	[%1], %%fsr
	" : : "r" (fpregs), "r" (fsr), "i" (ASI_BLK_P));
}

extern __inline__ void fpsave64hi(unsigned int *fpregs, unsigned long *fsr)
{
	__asm__ __volatile__ ("
	wr	%%g0, %2, %%asi
	stx	%%fsr, [%1]
	stda	%%f32, [%0 + 128] %%asi
	stda	%%f48, [%0 + 192] %%asi
	" : : "r" (fpregs), "r" (fsr), "i" (ASI_BLK_P));
}

extern __inline__ void fpload64hi(unsigned int *fpregs, unsigned long *fsr)
{
	__asm__ __volatile__ ("
	wr	%%g0, %2, %%asi
	ldda	[%0 + 128] %%asi, %%f32
	ldda	[%0 + 192] %%asi, %%f48
	ldx	[%1], %%fsr
	" : : "r" (fpregs), "r" (fsr), "i" (ASI_BLK_P));
}

extern __inline__ void fpsave(unsigned int *fpregs, unsigned long *fsr)
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

extern __inline__ void fpload(unsigned int *fpregs, unsigned long *fsr)
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
