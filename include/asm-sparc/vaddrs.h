/* $Id: vaddrs.h,v 1.15 1995/11/25 02:33:20 davem Exp $ */
#ifndef _SPARC_VADDRS_H
#define _SPARC_VADDRS_H

#include <asm/head.h>

/* asm-sparc/vaddrs.h:  Here will be define the virtual addresses at
 *                      which important I/O addresses will be mapped.
 *                      For instance the timer register virtual address
 *                      is defined here.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

/* I can see only one reason why we should have statically defined
 * mappings for devices and is the speedup improvements of not loading
 * a pointer and then the value in the assembly code
 */

#define  AUXIO_VADDR  (KERNBASE+0x6000)    /* Auxiliary IO register */
#define  TIMER_VADDR  (AUXIO_VADDR+0x1000) /* One page after AUXIO, length is
					    * 5 pages which is needed on sun4m.
					    */
#define  INTREG_VADDR (TIMER_VADDR+0x5000)

#define  IOBASE_VADDR   0xfe000000  /* Base for mapping pages */
#define  IOBASE_LEN     0x00100000  /* Length of the IO area */
#define  IOBASE_END     0xfe100000
#define  DVMA_VADDR     0xfff00000  /* Base area of the DVMA on suns */
#define  DVMA_LEN       0x00040000  /* Size of the DVMA address space */
#define  DVMA_END       0xfff40000

/* On sun4m machines we need per-cpu virtual areas */
#define  PERCPU_VADDR   0xff000000  /* Base for per-cpu virtual mappings */
#define  PERCPU_ENTSIZE 0x00100000
#define  PERCPU_LEN     ((PERCPU_ENTSIZE*NCPUS))

/* per-cpu offsets */
#define  PERCPU_TBR_OFFSET      0x00000      /* %tbr, mainly used for identification. */
#define  PERCPU_KSTACK_OFFSET   0x01000      /* Beginning of kernel stack for this cpu */
#define  PERCPU_MBOX_OFFSET     0x02000      /* Prom SMP Mailbox */
#define  PERCPU_CPUID_OFFSET    0x03000      /* Per-cpu ID number. */
#define  PERCPU_ISALIVE_OFFSET  0x03004      /* Has CPU been initted yet? */
#define  PERCPU_ISIDLING_OFFSET 0x03008      /* Is CPU in idle loop spinning? */

#endif /* !(_SPARC_VADDRS_H) */
