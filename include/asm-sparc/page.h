/* page.h:  Various defines and such for MMU operations on the Sparc for
 *          the Linux kernel.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_PAGE_H
#define _SPARC_PAGE_H

#include <asm/asi.h>        /* for get/set segmap/pte routines */
#include <asm/contregs.h>   /* for switch_to_context */
#include <asm/head.h>       /* for KERNBASE */

#define PAGE_SHIFT   12             /* This is the virtual page... */
#define PAGE_OFFSET    KERNBASE
#define PAGE_SIZE    (1 << PAGE_SHIFT)

/* to mask away the intra-page address bits */
#define PAGE_MASK         (~(PAGE_SIZE-1))

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

/* The following structure is used to hold the physical
 * memory configuration of the machine.  This is filled in
 * probe_memory() and is later used by mem_init() to set up
 * mem_map[].  We statically allocate SPARC_PHYS_BANKS of
 * these structs, this is arbitrary.  The entry after the
 * last valid one has num_bytes==0.
 */

struct sparc_phys_banks {
  unsigned long base_addr;
  unsigned long num_bytes;
};

#define SPARC_PHYS_BANKS 32

extern struct sparc_phys_banks sp_banks[SPARC_PHYS_BANKS];

#define STRICT_MM_TYPECHECKS

#ifdef STRICT_MM_TYPECHECKS
/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)	((x).pte)
#define pmd_val(x)      ((x).pmd)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) } )
#define __pmd(x)        ((pmd_t) { (x) } )
#define __pgd(x)	((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

#else
/*
 * .. while these make it easier on the compiler
 */
typedef unsigned long pte_t;
typedef unsigned long pmd_t;
typedef unsigned long pgd_t;
typedef unsigned long pgprot_t;

#define pte_val(x)	(x)
#define pmd_val(x)      (x)
#define pgd_val(x)	(x)
#define pgprot_val(x)	(x)

#define __pte(x)	(x)
#define __pmd(x)        (x)
#define __pgd(x)	(x)
#define __pgprot(x)	(x)

#endif

/* The current va context is global and known, so all that is needed to
 * do an invalidate is flush the VAC on a sun4c or call the ASI flushing
 * routines on a SRMMU.
 */

extern void (*invalidate)(void);
extern void (*set_pte)(pte_t *ptep, pte_t entry);

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)  (((addr)+PAGE_SIZE-1)&PAGE_MASK)

/* We now put the free page pool mapped contiguously in high memory above
 * the kernel.
 */
#define MAP_NR(addr) ((((unsigned long)addr) - PAGE_OFFSET) >> PAGE_SHIFT)
#define MAP_PAGE_RESERVED (1<<15)


#endif /* !(__ASSEMBLY__) */

/* The rest is kind of funky because on the sparc, the offsets into the mmu 
 * entries are encoded in magic alternate address space tables. I will 
 * probably find some nifty inline assembly routines to do the equivalent. 
 * Much thought must go into this code.   (davem@caip.rutgers.edu)
 */

/* Bitfields within a Sparc sun4c PTE (page table entry). */

#define PTE_V     0x80000000   /* valid bit */
#define PTE_ACC   0x60000000   /* access bits */
#define PTE_W     0x40000000   /* writable bit */
#define PTE_P     0x20000000   /* privileged page */
#define PTE_NC    0x10000000   /* page is non-cacheable */
#define PTE_TYP   0x0c000000   /* page type field */
#define PTE_RMEM  0x00000000   /* type == on board real memory */
#define PTE_IO    0x04000000   /* type == i/o area */
#define PTE_VME16 0x08000000   /* type == 16-bit VME area */
#define PTE_VME32 0x0c000000   /* type == 32-bit VME area */
#define PTE_R     0x02000000   /* page has been referenced */
#define PTE_M     0x01000000   /* page has been modified */
#define PTE_RESV  0x00f80000   /* reserved bits */
#define PTE_PHYPG 0x0007ffff   /* phys pg number, sun4c only uses 16bits */

/* SRMMU defines */
/* The fields in an srmmu virtual address when it gets translated.
 *
 *  -------------------------------------------------------------
 *  |   INDEX 1   |   INDEX 2   |   INDEX 3   |   PAGE OFFSET   |
 *  -------------------------------------------------------------
 *  31          24 23        18  17         12  11              0
 */
#define SRMMU_IDX1_SHIFT      24
#define SRMMU_IDX1_MASK       0xff000000
#define SRMMU_IDX2_SHIFT      18
#define SRMMU_IDX2_MASK       0x00fc0000
#define SRMMU_IDX3_SHIFT      12
#define SRMMU_IDX3_MASK       0x0003f000

#define SRMMU_PGOFFSET_MASK   0x00000fff
/* The page table sizes for the various levels in bytes. */
#define SRMMU_LV1_PTSIZE      1024
#define SRMMU_LV2_PTSIZE      256
#define SRMMU_LV3_PTSIZE      256

/* Definition of the values in the ET field of PTD's and PTE's */
#define SRMMU_ET_INVALID      0x0
#define SRMMU_ET_PTD          0x1
#define SRMMU_ET_PTE          0x2
#define SRMMU_ET_RESV         0x3
#define SRMMU_ET_PTDBAD       0x3   /* Upward compatability my butt. */

/* Page table directory bits.
 *
 * ----------------
 * |  PTP    | ET |
 * ----------------
 * 31       2 1  0
 *
 * PTP:  The physical page table pointer.  This value appears on
 *       bits 35->6 on the physical address bus during translation.
 *
 * ET:   Entry type field.  Must be 1 for a PTD.
 */

#define SRMMU_PTD_PTP_SHIFT         0x2
#define SRMMU_PTD_PTP_MASK          0xfffffffc
#define SRMMU_PTD_PTP_PADDR_SHIFT   0x4
#define SRMMU_PTD_ET_SHIFT          0x0
#define SRMMU_PTD_ET_MASK           0x00000003

/* Page table entry bits.
 *
 * -------------------------------------------------
 * |  Physical Page Number  | C | M | R | ACC | ET |
 * -------------------------------------------------
 * 31                     8   7   6   5  4   2  1  0
 *
 * PPN: Physical page number, high order 24 bits of the 36-bit
 *      physical address, thus is you mask off all the non
 *      PPN bits you have the physical address of your page.
 *      No shifting necessary.
 *
 * C:   Whether the page is cacheable in the mmu TLB or not.  If
 *      not set the CPU cannot cache values to these addresses. For
 *      IO space translations this bit should be clear.
 *
 * M:   Modified.  This tells whether the page has been written to
 *      since the bit was last cleared.  NOTE: this does not include
 *      accesses via the ASI physical page pass through since that does
 *      not use the MMU.
 *
 * R:   References.  This tells whether the page has been referenced
 *      in any way shape or form since the last clearing of the bit.
 *      NOTE: this does not include accesses via the ASI physical page
 *      pass through since that does not use the MMU.
 *
 * ACC: Access permissions for this page.  This is further explained below
 *      with appropriate macros.
 */

#define SRMMU_PTE_PPN_SHIFT         0x8
#define SRMMU_PTE_PPN_MASK          0xffffff00
#define SRMMU_PTE_PPN_PADDR_SHIFT   0x4
#define SRMMU_PTE_C_SHIFT           0x7
#define SRMMU_PTE_C_MASK            0x00000080
#define SRMMU_PTE_M_SHIFT           0x6
#define SRMMU_PTE_M_MASK            0x00000040
#define SRMMU_PTE_R_SHIFT           0x5
#define SRMMU_PTE_R_MASK            0x00000020
#define SRMMU_PTE_ACC_SHIFT         0x2
#define SRMMU_PTE_ACC_MASK          0x0000001c
#define SRMMU_PTE_ET_SHIFT          0x0
#define SRMMU_PTE_ET_MASK           0x00000003

/* SRMMU pte access bits.
 *
 * BIT    USER ACCESS          SUPERVISOR ACCESS
 * ---    --------------       -----------------
 * 0x0    read only            read only
 * 0x1    read&write           read&write
 * 0x2    read&execute         read&execute
 * 0x3    read&write&execute   read&write&execute
 * 0x4    execute only         execute only
 * 0x5    read only            read&write
 * 0x6    ACCESS DENIED        read&execute
 * 0x7    ACCESS DENIED        read&write&execute
 *
 * All these values are shifted left two bits.
 */

#define SRMMU_ACC_US_RDONLY      0x00
#define SRMMU_ACC_US_RDWR        0x04
#define SRMMU_ACC_US_RDEXEC      0x08
#define SRMMU_ACC_US_RDWREXEC    0x0c
#define SRMMU_ACC_US_EXECONLY    0x10
#define SRMMU_ACC_U_RDONLY       0x14
#define SRMMU_ACC_S_RDWR         0x14
#define SRMMU_ACC_U_ACCDENIED    0x18
#define SRMMU_ACC_S_RDEXEC       0x18
#define SRMMU_ACC_U_ACCDENIED2   0x1c
#define SRMMU_ACC_S_RDWREXEC     0x1c

#ifndef __ASSEMBLY__

/* SUN4C pte, segmap, and context manipulation */
extern __inline__ unsigned long get_segmap(unsigned long addr)
{
  register unsigned long entry;

  __asm__ __volatile__("lduba [%1] %2, %0" : 
		       "=r" (entry) :
		       "r" (addr), "i" (ASI_SEGMAP));

  return (entry&0xff);
}

extern __inline__ void put_segmap(unsigned long addr, unsigned long entry)
{

  __asm__ __volatile__("stba %1, [%0] %2" : : "r" (addr), "r" (entry&0xff),
		       "i" (ASI_SEGMAP));

  return;
}

extern __inline__ unsigned long get_pte(unsigned long addr)
{
  register unsigned long entry;

  __asm__ __volatile__("lda [%1] %2, %0" : 
		       "=r" (entry) :
		       "r" (addr), "i" (ASI_PTE));
  return entry;
}

extern __inline__ void put_pte(unsigned long addr, unsigned long entry)
{
  __asm__ __volatile__("sta %1, [%0] %2" : :
		       "r" (addr), 
		       "r" (entry), "i" (ASI_PTE));

  return;
}

extern void (*switch_to_context)(int);

extern __inline__ int get_context(void)
{
  register int ctx;

  __asm__ __volatile__("lduba [%1] %2, %0" :
		       "=r" (ctx) :
		       "r" (AC_CONTEXT), "i" (ASI_CONTROL));

  return ctx;
}

typedef unsigned short mem_map_t;

#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* _SPARC_PAGE_H */
