#ifndef _SPARC_VAC_OPS_H
#define _SPARC_VAC_OPS_H

/* vac-ops.h: Inline assembly routines to do operations on the Sparc
              VAC (virtual address cache).

   Copyright (C) 1994, David S. Miller (davem@caip.rutgers.edu)
*/

extern unsigned long *trapbase;
extern char end, etext, msgbuf;

extern void flush_vac_context(void);
extern void flush_vac_segment(unsigned int foo_segment);
extern void flush_vac_page(unsigned int foo_addr);

extern int vac_do_hw_vac_flushes, vac_size, vac_linesize;
extern int vac_entries_per_context, vac_entries_per_segment;
extern int vac_entries_per_page;

/* enable_vac() enables the virtual address cache. It returns 0 on
   success, 1 on failure.
*/

extern __inline__ int enable_vac(void)
{
  int success=0;

  __asm__ __volatile__("lduba [%1] 2, %0\n\t"
		       "or    %0, 0x10, %0\n\t"
		       "stba  %0, [%1] 2\n\t"
		       "or    %%g0, %%g0, %0" :
		       "=r" (success) :
		       "r" ((unsigned int) 0x40000000),
		       "0" (success));
  return success;
}

/* disable_vac() disables the virtual address cache. It returns 0 on
   success, 1 on failure.
*/

extern __inline__ int disable_vac(void)
{
  int success=0;

  __asm__ __volatile__("lduba [%1] 0x2, %0\n\t"
			"xor   %0, 0x10, %0\n\t"
			"stba  %0, [%1] 0x2\n\t"
			"or    %%g0, %%g0, %0" : 
		       "=r" (success) : 
		       "r" (0x40000000),
		       "0" (success));
  return success;
}

/* Various one-shot VAC entry flushes on the Sparc */

extern __inline__ void hw_flush_vac_context_entry(char* addr)
{
  __asm__ __volatile__("sta %%g0, [%0] 0x7" : : "r" (addr));
}

extern __inline__ void sw_flush_vac_context_entry(char* addr)
{
  __asm__ __volatile__("sta %%g0, [%0] 0xe" : : "r" (addr));
}

extern __inline__ void hw_flush_vac_segment_entry(char* addr)
{
  __asm__ __volatile__("sta %%g0, [%0] 0x5" : : "r" (addr));
}

extern __inline__ void sw_flush_vac_segment_entry(char* addr)
{
  __asm__ __volatile__("sta %%g0, [%0] 0xc" : : "r" (addr));
}

extern __inline__ void hw_flush_vac_page_entry(unsigned long* addr)
{
  __asm__ __volatile__("sta %%g0, [%0] 0x6" : : "r" (addr));
}

extern __inline__ void sw_flush_vac_page_entry(unsigned long* addr)
{
  __asm__ __volatile__("sta %%g0, [%0] 0xd" : : "r" (addr));
}
 
#endif /* !(_SPARC_VAC_OPS_H) */
