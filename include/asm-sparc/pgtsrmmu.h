/* pgtsrmmu.h:  SRMMU page table defines and code.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/page.h>  /* just in case */

#ifndef _SPARC_PGTSRMMU_H
#define _SPARC_PGTSRMMU_H

#define SRMMU_PAGE_TABLE_SIZE 0x100 /* 64 entries, 4 bytes a piece */
#define SRMMU_PMD_TABLE_SIZE  0x100 /* 64 entries, 4 bytes a piece */
#define SRMMU_PGD_TABLE_SIZE  0x400 /* 256 entries, 4 bytes a piece */

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define SRMMU_PMD_SHIFT       18
#define SRMMU_PMD_SIZE        (1UL << SRMMU_PMD_SHIFT)
#define SRMMU_PMD_MASK        (~(SRMMU_PMD_SIZE-1))
#define SRMMU_PMD_ALIGN(addr) (((addr)+SRMMU_PMD_SIZE-1)&SRMMU_PMD_MASK)

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define SRMMU_PGDIR_SHIFT       24
#define SRMMU_PGDIR_SIZE        (1UL << SRMMU_PGDIR_SHIFT)
#define SRMMU_PGDIR_MASK        (~(SRMMU_PGDIR_SIZE-1))
#define SRMMU_PGDIR_ALIGN(addr) (((addr)+SRMMU_PGDIR_SIZE-1)&SRMMU_PGDIR_MASK)

/*
 * Three-level on SRMMU.
 */

#define SRMMU_PTRS_PER_PTE    64
#define SRMMU_PTRS_PER_PMD    64
#define SRMMU_PTRS_PER_PGD    256

/* Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */
#define SRMMU_VMALLOC_OFFSET  (8*1024*1024)
#define SRMMU_VMALLOC_START ((high_memory + SRMMU_VMALLOC_OFFSET) & ~(SRMMU_VMALLOC_OFFSET-1))

/*
 * Sparc SRMMU page table fields.
 */

#define _SRMMU_PAGE_VALID      (SRMMU_ET_PTE)
#define _SRMMU_PMD_VALID       (SRMMU_ET_PTD)
#define _SRMMU_PGD_VALID       (SRMMU_ET_PTD)
#define _SRMMU_PAGE_WRITE_USR  (SRMMU_ACC_US_RDWR)
#define _SRMMU_PAGE_WRITE_KERN (SRMMU_ACC_S_RDWR)
#define _SRMMU_PAGE_EXEC       (SRMMU_ACC_US_RDEXEC)
#define _SRMMU_PAGE_RDONLY     (SRMMU_ACC_US_RDONLY)
#define _SRMMU_PAGE_NOREAD     (SRMMU_ACC_U_ACCDENIED)
#define _SRMMU_PAGE_NOCACHE    (~SRMMU_PTE_C_MASK)
#define _SRMMU_PAGE_PRIV       (SRMMU_ACC_S_RDWREXEC)
#define _SRMMU_PAGE_REF        (SRMMU_PTE_R_MASK)
#define _SRMMU_PAGE_DIRTY      (SRMMU_PTE_M_MASK)
#define _SRMMU_PAGE_COW        (SRMMU_ACC_U_RDONLY)
#define _SRMMU_PAGE_UNCOW      (SRMMU_ACC_US_RDWR)

/* We want the swapper not to swap out page tables, thus dirty and writable
 * so that the kernel can change the entries as needed. Also valid for
 * obvious reasons.
 */
#define _SRMMU_PAGE_TABLE     (_SRMMU_PAGE_VALID | _SRMMU_PAGE_WRITE_KERN | _SRMMU_PAGE_REF | _SRMMU_PAGE_DIRTY)
#define _SRMMU_PAGE_CHG_MASK  (_SRMMU_PAGE_REF | _SRMMU_PAGE_DIRTY | SRMMU_ET_PTE)
#define _SRMMU_PMD_CHG_MASK   (SRMMU_ET_PTD)
#define _SRMMU_PGD_CHG_MASK   (SRMMU_ET_PTD)

#define SRMMU_PAGE_NONE       __pgprot(_SRMMU_PAGE_VALID | _SRMMU_PAGE_REF)
#define SRMMU_PAGE_SHARED     __pgprot(_SRMMU_PAGE_VALID | _SRMMU_PAGE_WRITE_USR | _SRMMU_PAGE_REF)
#define SRMMU_PAGE_COPY       __pgprot(_SRMMU_PAGE_VALID | _SRMMU_PAGE_REF | _SRMMU_PAGE_COW)
#define SRMMU_PAGE_READONLY   __pgprot(_SRMMU_PAGE_VALID | _SRMMU_PAGE_REF | SRMMU_ACC_US_RDONLY)
#define SRMMU_PAGE_KERNEL     __pgprot(_SRMMU_PAGE_VALID | _SRMMU_PAGE_PRIV | SRMMU_PTE_C_MASK)
#define SRMMU_PAGE_INVALID    __pgprot(SRMMU_ET_INVALID)

#define _SRMMU_PAGE_NORMAL(x) __pgprot(_SRMMU_PAGE_VALID | _SRMMU_PAGE_REF | (x))

/* SRMMU Register addresses */
#define SRMMU_CTRL_REG           0x00000000
#define SRMMU_CTXTBL_PTR         0x00000100
#define SRMMU_CTX_REG            0x00000200
#define SRMMU_FAULT_STATUS       0x00000300
#define SRMMU_FAULT_ADDR         0x00000400
#define SRMMU_AFAULT_STATUS      0x00000500
#define SRMMU_AFAULT_ADDR        0x00000600

/* The SRMMU control register fields:
 * -------------------------------------------------------------------
 * | IMPL  |  VERS  |    SysControl | PSO | Resv | No Fault | Enable |
 * -------------------------------------------------------------------
 * 31    28 27    24 23            8   7    6   2      1        0
 *
 * IMPL:  Indicates the implementation of this SRMMU, read-only.
 * VERS:  The version of this implementation, again read-only.
 * SysControl:  This is an implementation specific field, the SRMMU
 *              specification does not define anything for this field.
 * PSO: This determines whether the memory model as seen by the CPU
 *      is Partial Store Order (PSO=1) or Total Store Ordering (PSO=0).
 * Resv: Don't touch these bits ;)
 * No Fault: If zero, any fault acts as expected where the fault status
 *           and address registers are updated and a trap hits the CPU.
 *           When this bit is one, on any fault other than in ASI 9, the
 *           MMU updates the status and address fault registers but does
 *           not signal the CPU with a trap.  This is useful to beat
 *           race conditions in low-level code when we have to throw
 *           a register window onto the stack in a spill/fill handler
 *           on multiprocessors.
 * Enable: If one the MMU is doing translations, if zero the addresses
 *         given to the bus are pure physical.
 */

#define SRMMU_CTREG_IMPL_MASK        0xf0000000
#define SRMMU_CTREG_IMPL_SHIFT       28
#define SRMMU_CTREG_VERS_MASK        0x0f000000
#define SRMMU_CTREG_VERS_SHIFT       24
#define SRMMU_CTREG_SYSCNTRL_MASK    0x00ffff00
#define SRMMU_CTREG_SYSCNTRL_SHIFT   8
#define SRMMU_CTREG_PSO_MASK         0x00000080
#define SRMMU_CTREG_PSO_SHIFT        7
#define SRMMU_CTREG_RESV_MASK        0x0000007c
#define SRMMU_CTREG_RESV_SHIFT       2
#define SRMMU_CTREG_NOFAULT_MASK     0x00000002
#define SRMMU_CTREG_NOFAULT_SHIFT    1
#define SRMMU_CTREG_ENABLE_MASK      0x00000001
#define SRMMU_CTREG_ENABLE_SHIFT     0

/* Get the MMU control register */
extern inline unsigned int srmmu_get_mmureg(void)
{
        register unsigned int retval;
	__asm__ __volatile__("lda [%%g0] %1, %0\n\t" :
			     "=r" (retval) :
			     "i" (ASI_M_MMUREGS));
	return retval;
}

/* Set the MMU control register */
extern inline void srmmu_set_mmureg(unsigned long regval)
{
	__asm__ __volatile__("sta %0, [%%g0] %1\n\t" : :
			     "r" (regval), "i" (ASI_M_MMUREGS) : "memory");

	return;
}

/* The SRMMU Context Table Pointer Register:
 * ---------------------------------
 * |  Context Table Pointer | Resv |
 * ---------------------------------
 * 31                      2 1    0
 *
 * This is where the MMU goes to in physical RAM to fetch the
 * elements in the context table.  The non-Resv bits of this
 * address appear in bits 6-35 of the physical bus during miss
 * processing, then indexed by the value in the Context Register.
 * This table must be aligned on a boundary equal to the size of
 * the table, we provide a nice macro for doing this based upon
 * the significant bits in the context register.
 */
#define SRMMU_CTP_ADDR_MASK          0xfffffffc
#define SRMMU_CTP_ADDR_PADDR_SHIFT   0x4
#define SRMMU_CTP_RESV_MASK          0x00000003

#define SRMMU_SIGBITS_TO_ALIGNMENT(numbits)  ((1 << (numbits + 2)))


/* Set the address of the context table.  You pass this routine
 * the physical address, we do the magic shifting for you.
 */
extern inline void srmmu_set_ctable_ptr(unsigned long paddr)
{
	unsigned long ctp;

	ctp = (paddr >> SRMMU_CTP_ADDR_PADDR_SHIFT);
	ctp &= SRMMU_CTP_ADDR_MASK;

	__asm__ __volatile__("sta %0, [%1] %2\n\t" : :
			     "r" (ctp), "r" (SRMMU_CTXTBL_PTR),
			     "i" (ASI_M_MMUREGS) :
			     "memory");
	return;
}


/* Get the address of the context table.  We return the physical
 * address of the table, again we do the shifting here.
 */
extern inline unsigned long srmmu_get_ctable_ptr(void)
{
	register unsigned int retval;

	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
			     "=r" (retval) :
			     "r" (SRMMU_CTXTBL_PTR),
			     "i" (ASI_M_MMUREGS));

	retval &= SRMMU_CTP_ADDR_MASK;
	retval = (retval << SRMMU_CTP_ADDR_PADDR_SHIFT);
	return retval;
}

/* Set the context on an SRMMU */
extern inline void srmmu_set_context(int context)
{
	__asm__ __volatile__("sta %0, [%1] %2\n\t" : :
			     "r" (context), "r" (SRMMU_CTX_REG),
			     "i" (ASI_M_MMUREGS) : "memory");
	return;
}

/* Get the context on an SRMMU */
extern inline int srmmu_get_context(void)
{
	register int retval;
	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
			     "=r" (retval) :
			     "r" (SRMMU_CTX_REG),
			     "i" (ASI_M_MMUREGS));
	return retval;
}

/* SRMMU diagnostic register:
 * --------------------------------------------------------
 * |   Virtual Address   |   PDC entry   | DiagReg | Resv |
 * --------------------------------------------------------
 * 31                  12 11            4 3       2 1    0
 *
 * An SRMMU implementation has the choice of providing this register
 * and I don't know much about it.
 */

#define SRMMU_DIAG_VADDR_MASK        0xfffff000
#define SRMMU_DIAG_PDC_MASK          0x00000ff0
#define SRMMU_DIAG_REG_MASK          0x0000000c
#define SRMMU_DIAG_RESV_MASK         0x00000003

/* SRMMU Fault Status Register:
 * -----------------------------------------------------------
 * | Reserved          | EBE  |  L  |  AT  |  FT  | FAV | OW |
 * -----------------------------------------------------------
 *  31               18 17  10  9  8 7    5  4   2   1     0
 *
 * WARNING!!! On certain VERY BROKEN Viking Sun4d modules this register
 * is complete TOAST!  During a fault you cannot trust the values
 * contained in this register, you must calculate them yourself
 * by first using the trap program counter to decode the
 * instruction the code tried to execute (ie. load or store) and
 * the address they tried to access.  I think the Fault Virtual
 * Address register may be ok on these chips, but who knows. Grrr.
 *
 * Reserved:  These bits must be zero.
 * EBE: External bus error bits, implementation dependant (at least
 *      we know what the bits mean on sun4d Viking modules) ;)
 * L: The level in tree traversal at which the fault occured. The
 *    values are... 0 = context table
 *                  1 = level-1 page table
 *                  2 = level-2 page table
 *                  3 = level-3 page table
 * AT: Access type field. This is decoded as follows...
 *     0 -- Load from user data space
 *     1 -- Load from supervisor data space
 *     2 -- Read/Execute from user instruction space
 *     3 -- Read/Execute from supervisor instruction space
 *     4 -- Store to user data space
 *     5 -- Store to supervisor data space
 *     6 -- Store to user instruction space
 *     7 -- Store to supervisor instruction space (emacs does this)
 *     On the Viking --  TOAST!
 * FT:  This is the fault type field.  It is used to determine what was
 *      wrong in the attempted translation. It can be one of...
 *     0 -- None
 *     1 -- Invalid address error
 *     2 -- Protection violation error
 *     3 -- Priviledge violation error
 *     4 -- Translation error (your tables are fucked up)
 *     5 -- Bus access error (you lose)
 *     6 -- Internal error (might as well have a Viking)
 *     7 -- Reserved (don't touch)
 * FAV: Fault Address Valid bit.  When set to one the fault address
 *      register contents are valid.  It need not be valid for text
 *      faults as the trapped PC tells us this anyway.
 * OW: The Overwrite Bit, if set to one, this register has been
 *     written to more than once by the hardware since software did
 *     a read.  This mean multiple faults have occurred and you have
 *     to a manual page table tree traversal to continue the handling
 *     of the first fault. And on the Viking module....
 *
 * The Fault Address Register is just a 32-bit register representing the
 * virtual address which caused the fault.  It's validity is determined
 * by the following equation:
 * if(module==VIKING || FSR.FAV==0) forget_it();
 * It's ok for the FAV to be invalid for a text fault because we can
 * use the trapped program counter, however for a data fault we are SOL.
 * I'll probably have to write a workaround for this situation too ;-(
 */

#define SRMMU_FSR_RESV_MASK      0xfffc0000  /* Reserved bits */
#define SRMMU_FSR_EBE_MASK       0x0003fc00  /* External Bus Error bits */
#define SRMMU_FSR_EBE_BERR       0x00000400  /* Bus Error */
#define SRMMU_FSR_EBE_BTIMEO     0x00000800  /* Bus Time Out */
#define SRMMU_FSR_EBE_UNCOR      0x00001000  /* Uncorrectable Error */
#define SRMMU_FSR_EBE_UNDEF      0x00002000  /* Undefined Error */
#define SRMMU_FSR_EBE_PARITY     0x00004000  /* Parity error */
#define SRMMU_FSR_EBE_TPARITY    0x00006000  /* Tsunami parity error */
#define SRMMU_FSR_EBE_SBUF       0x00008000  /* Store Buffer error */
#define SRMMU_FSR_EBE_CSA        0x00010000  /* Control space access error (bad ASI) */
#define SRMMU_FSR_EBE_EMRT       0x00020000  /* Viking Emergency Response Team */
#define SRMMU_FSR_L_MASK         0x00000300  /* Fault level bits */
#define SRMMU_FSR_L_CTABLE       0x00000000  /* Context table level flt/err */
#define SRMMU_FSR_L_ONE          0x00000100  /* Level1 ptable flt/err */
#define SRMMU_FSR_L_TWO          0x00000200  /* Level2 ptable flt/err */
#define SRMMU_FSR_L_THREE        0x00000300  /* Level3 ptable flt/err */
#define SRMMU_FSR_AT_MASK        0x000000e0  /* Access Type bits */
#define SRMMU_FSR_AT_LUD         0x00000000  /* Load from User Data space */
#define SRMMU_FSR_AT_LSD         0x00000020  /* What I'll need after writing this code */
#define SRMMU_FSR_AT_RXUI        0x00000040  /* Read/Execute from user text */
#define SRMMU_FSR_AT_RXSI        0x00000060  /* Read/Execute from supv text */
#define SRMMU_FSR_AT_SUD         0x00000080  /* Store to user data space */
#define SRMMU_FSR_AT_SSD         0x000000a0  /* Store to supv data space */
#define SRMMU_FSR_AT_SUI         0x000000c0  /* Store to user text */
#define SRMMU_FSR_AT_SSI         0x000000e0  /* Store to supv text */
#define SRMMU_FSR_FT_MASK        0x0000001c  /* Fault Type bits */
#define SRMMU_FSR_FT_NONE        0x00000000  /* No fault occurred */
#define SRMMU_FSR_FT_IADDR       0x00000002  /* Invalid address */
#define SRMMU_FSR_FT_PROT        0x00000004  /* Protection violation */
#define SRMMU_FSR_FT_PRIV        0x00000008  /* Privilege violation */
#define SRMMU_FSR_FT_TRANS       0x0000000a  /* Translation error */
#define SRMMU_FSR_FT_BACC        0x0000000c  /* Bus Access error */
#define SRMMU_FSR_FT_IACC        0x0000000e  /* Internal error */
#define SRMMU_FSR_FT_RESV        0x00000010  /* Reserved, should not get this */
#define SRMMU_FSR_FAV_MASK       0x00000002  /* Fault Address Valid bits */
#define SRMMU_FSR_OW_MASK        0x00000001  /* SFSR OverWritten bits */

/* Read the Fault Status Register on the SRMMU */
extern inline unsigned int srmmu_get_fstatus(void)
{
	register unsigned int retval;

	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
			     "=r" (retval) :
			     "r" (SRMMU_FAULT_STATUS), "i" (ASI_M_MMUREGS));
	return retval;
}

/* Read the Fault Address Register on the SRMMU */
extern inline unsigned int srmmu_get_faddr(void)
{
	register unsigned int retval;

	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
			     "=r" (retval) :
			     "r" (SRMMU_FAULT_ADDR), "i" (ASI_M_MMUREGS));
	return retval;
}

/* SRMMU Asynchronous Fault Status Register:
 * -----------------------------------------
 * |  RESERVED |UCE|BTO|BERR|RSV|HFADDR|AFO|
 * -----------------------------------------
 *  31       13  12  11  10  9-8  7-4     0
 *
 * UCE: UnCorrectable Error
 * BTO: Bus TimeOut
 * BERR: Genreic Bus Error
 * HFADDR: High 4 bits of the faulting address
 * AFO: Asynchronous Fault Occurred
 */
#define SRMMU_AFSR_RESVMASK  0xffffe000
#define SRMMU_AFSR_UCE       0x00001000
#define SRMMU_AFSR_BTO       0x00000800
#define SRMMU_AFSR_BERR      0x00000400
#define SRMMU_AFSR_HFADDR    0x000000f0
#define SRMMU_AFSR_AFO       0x00000001

/* Read the asynchronous fault register */
extern inline unsigned int srmmu_get_afstatus(void)
{
	register unsigned int retval;

	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
			     "=r" (retval) :
			     "r" (SRMMU_AFAULT_STATUS), "i" (ASI_M_MMUREGS));
	return retval;
}

/* Read the Asynchronous Fault Address Register on the SRMMU */
extern inline unsigned int srmmu_get_afaddr(void)
{
	register unsigned int retval;

	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
			     "=r" (retval) :
			     "r" (SRMMU_AFAULT_ADDR), "i" (ASI_M_MMUREGS));
	return retval;
}


/* Flush the entire TLB cache on the SRMMU. */
extern inline void srmmu_flush_whole_tlb(void)
{
	__asm__ __volatile__("sta %%g0, [%0] %1\n\t": :
			     "r" (0x400),        /* Flush entire TLB!! */
			     "i" (ASI_M_FLUSH_PROBE) : "memory");

	return;
}

/* Probe for an entry in the page-tables of the SRMMU. */
extern inline unsigned long srmmu_hwprobe(unsigned long vaddr)
{
	unsigned long retval;

	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
			     "=r" (retval) :
			     "r" (vaddr | 0x400), "i" (ASI_M_FLUSH_PROBE));

	return retval;
}

#endif /* !(_SPARC_PGTSRMMU_H) */
