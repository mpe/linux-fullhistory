/*
 * loadmmu.c: Setup cpu/cache specific function ptrs at boot time.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 * $Id: loadmmu.c,v 1.7 1998/03/27 08:53:41 ralf Exp $
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/bootinfo.h>
#include <asm/sgialib.h>

/* memory functions */
void (*clear_page)(unsigned long page);
void (*copy_page)(unsigned long to, unsigned long from);

/* Cache operations. */
void (*flush_cache_all)(void);
void (*flush_cache_mm)(struct mm_struct *mm);
void (*flush_cache_range)(struct mm_struct *mm, unsigned long start,
			  unsigned long end);
void (*flush_cache_page)(struct vm_area_struct *vma, unsigned long page);
void (*flush_cache_sigtramp)(unsigned long addr);
void (*flush_page_to_ram)(unsigned long page);

/* DMA cache operations. */
void (*dma_cache_wback_inv)(unsigned long start, unsigned long size);
void (*dma_cache_inv)(unsigned long start, unsigned long size);

/* TLB operations. */
void (*flush_tlb_all)(void);
void (*flush_tlb_mm)(struct mm_struct *mm);
void (*flush_tlb_range)(struct mm_struct *mm, unsigned long start,
			unsigned long end);
void (*flush_tlb_page)(struct vm_area_struct *vma, unsigned long page);

/* Miscellaneous. */
void (*load_pgd)(unsigned long pg_dir);
void (*pgd_init)(unsigned long page);
void (*update_mmu_cache)(struct vm_area_struct * vma,
			 unsigned long address, pte_t pte);

void (*show_regs)(struct pt_regs *);

void (*add_wired_entry)(unsigned long entrylo0, unsigned long entrylo1,
			unsigned long entryhi, unsigned long pagemask);

int (*user_mode)(struct pt_regs *);

asmlinkage void (*resume)(void *tsk);

extern void ld_mmu_r2300(void);
extern void ld_mmu_r4xx0(void);
extern void ld_mmu_r6000(void);
extern void ld_mmu_tfp(void);
extern void ld_mmu_andes(void);

__initfunc(void loadmmu(void))
{
	switch(mips_cputype) {
	case CPU_R2000:
	case CPU_R3000:
		printk("Loading R[23]00 MMU routines.\n");
		ld_mmu_r2300();
		break;

	case CPU_R4000PC:
	case CPU_R4000SC:
	case CPU_R4000MC:
	case CPU_R4200:
	case CPU_R4300:
	case CPU_R4400PC:
	case CPU_R4400SC:
	case CPU_R4400MC:
	case CPU_R4600:
	case CPU_R4640:
	case CPU_R4650:
	case CPU_R4700:
	case CPU_R5000:
	case CPU_R5000A:
	case CPU_NEVADA:
		printk("Loading R4000 MMU routines.\n");
		ld_mmu_r4xx0();
		break;

	case CPU_R6000:
	case CPU_R6000A:
		printk("Loading R6000 MMU routines.\n");
		ld_mmu_r6000();
		break;

	case CPU_R8000:
		printk("Loading TFP MMU routines.\n");
		ld_mmu_tfp();
		break;

	case CPU_R10000:
		printk("Loading R10000 MMU routines.\n");
		ld_mmu_andes();
		break;

	default:
		/* XXX We need an generic routine in the MIPS port
		 * XXX to jabber stuff onto the screen on all machines
		 * XXX before the console is setup.  The ARCS prom
		 * XXX routines look good for this, but only the SGI
		 * XXX code has a full library for that at this time.
		 */
		panic("Yeee, unsupported mmu/cache architecture.");
	}
}
