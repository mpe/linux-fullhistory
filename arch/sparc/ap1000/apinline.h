  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/* inline utilities to support the AP1000 code */

#if 0
/* MMU bypass access */

static inline unsigned long phys_9_in(unsigned long paddr)
{
	unsigned long word;

	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
			     "=r" (word) :
			     "r" (paddr), "i" (0x29) :
			     "memory");
	return word;
}

static inline void phys_9_out(unsigned long paddr, unsigned long word)
{
	__asm__ __volatile__("sta %0, [%1] %2\n\t" : :
			     "r" (word), "r" (paddr), "i" (0x29) :
			     "memory");
}

static inline unsigned long phys_b_in(unsigned long paddr)
{
	unsigned long word;

	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
			     "=r" (word) :
			     "r" (paddr), "i" (0x2b) :
			     "memory");
	return word;
}

static inline void phys_b_out(unsigned long paddr, unsigned long word)
{
	__asm__ __volatile__("sta %0, [%1] %2\n\t" : :
			     "r" (word), "r" (paddr), "i" (0x2b) :
			     "memory");
}

static inline unsigned long phys_c_in(unsigned long paddr)
{
	unsigned long word;

	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
			     "=r" (word) :
			     "r" (paddr), "i" (0x2b) :
			     "memory");
	return word;
}

static inline void phys_c_out(unsigned long paddr, unsigned long word)
{
	__asm__ __volatile__("sta %0, [%1] %2\n\t" : :
			     "r" (word), "r" (paddr), "i" (0x2b) :
			     "memory");
}

#undef BIF_IN
#undef BIF_OUT
#undef DMA_IN
#undef DMA_OUT
#undef MSC_IN
#undef MSC_OUT
#undef MC_IN
#undef MC_OUT

#define BIF_IN(reg) phys_9_in(reg)
#define BIF_OUT(reg,v) phys_9_out(reg,v)
#define DMA_IN(reg) phys_9_in(reg)
#define DMA_OUT(reg,v) phys_9_out(reg,v)
#define MC_IN(reg) phys_b_in((reg) - MC_BASE0)
#define MC_OUT(reg,v) phys_b_out((reg) - MC_BASE0,v)
#define MSC_IN(reg) phys_c_in((reg) - MSC_BASE0)
#define MSC_OUT(reg,v) phys_c_out((reg) - MSC_BASE0,v)
#endif


