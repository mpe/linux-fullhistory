/* $Id: vaddrs.h,v 1.21 1996/10/07 03:03:02 davem Exp $ */
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
#define  IOBASE_VADDR   0xfe000000  /* Base for mapping pages */
#define  IOBASE_LEN     0x00300000  /* Length of the IO area */
#define  IOBASE_END     0xfe300000
#define  DVMA_VADDR     0xfff00000  /* Base area of the DVMA on suns */
#define  DVMA_LEN       0x00040000  /* Size of the DVMA address space */
#define  DVMA_END       0xfff40000

/* IOMMU Mapping area, must be on a 16MB boundary!  Note this
 * doesn't count the DVMA areas, the prom lives between the
 * iommu mapping area (for scsi transfer buffers) and the
 * dvma upper range (for lance packet ring buffers).
 */
#define  IOMMU_VADDR    0xff000000
#define  IOMMU_LEN      0x00c00000
#define  IOMMU_END      0xffc00000 /* KADB debugger vm starts here */

/* On the sun4/4c we don't need an IOMMU area, but we need a place
 * to reliably map locked down kernel data.  This includes the
 * task_struct and kernel stack pages of each process plus the
 * scsi buffers during dvma IO transfers, also the floppy buffers
 * during pseudo dma which runs with traps off (no faults allowed).
 * Some quick calculations yield:
 *       NR_TASKS <512> * (3 * PAGE_SIZE) == 0x600000
 * Subtract this from 0xc00000 and you get 0x927C0 of vm left
 * over to map SCSI dvma + floppy pseudo-dma buffers.  So be
 * careful if you change NR_TASKS or else there won't be enough
 * room for it all.
 */
#define  SUN4C_LOCK_VADDR  0xff000000
#define  SUN4C_LOCK_LEN    0x00c00000
#define  SUN4C_LOCK_END    0xffc00000

/* On sun4m machines we need per-cpu virtual areas */
#define  PERCPU_VADDR   0xffc00000  /* Base for per-cpu virtual mappings */
#define  PERCPU_ENTSIZE 0x00100000
#define  PERCPU_LEN     ((PERCPU_ENTSIZE*NCPUS))

/* per-cpu offsets */
#define  PERCPU_TBR_OFFSET      0x00000      /* %tbr, mainly used for identification. */
#define  PERCPU_KSTACK_OFFSET   0x01000      /* Beginning of kernel stack for this cpu */
#define  PERCPU_MBOX_OFFSET     0x03000      /* Prom SMP Mailbox */
#define  PERCPU_CPUID_OFFSET    0x04000      /* Per-cpu ID number. */
#define  PERCPU_ISALIVE_OFFSET  0x04004      /* Has CPU been initted yet? */
#define  PERCPU_ISIDLING_OFFSET 0x04008      /* Is CPU in idle loop spinning? */

#endif /* !(_SPARC_VADDRS_H) */

