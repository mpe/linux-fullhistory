/*
 * IA-32 ELF support.
 *
 * Copyright (C) 1999 Arun Sharma <arun.sharma@intel.com>
 */
#include <linux/config.h>
#include <linux/posix_types.h>

#include <asm/signal.h>
#include <asm/ia32.h>

#define CONFIG_BINFMT_ELF32

/* Override some function names */
#undef start_thread
#define start_thread			ia32_start_thread
#define init_elf_binfmt			init_elf32_binfmt

#undef CONFIG_BINFMT_ELF
#ifdef CONFIG_BINFMT_ELF32
# define CONFIG_BINFMT_ELF		CONFIG_BINFMT_ELF32
#endif

#undef CONFIG_BINFMT_ELF_MODULE
#ifdef CONFIG_BINFMT_ELF32_MODULE
# define CONFIG_BINFMT_ELF_MODULE	CONFIG_BINFMT_ELF32_MODULE
#endif

void ia64_elf32_init(struct pt_regs *regs);
#define ELF_PLAT_INIT(_r)		ia64_elf32_init(_r)

#define setup_arg_pages(bprm)		ia32_setup_arg_pages(bprm)

/* Ugly but avoids duplication */
#include "../../../fs/binfmt_elf.c"

/* Global descriptor table */
unsigned long *ia32_gdt_table, *ia32_tss;

struct page *
put_shared_page(struct task_struct * tsk, struct page *page, unsigned long address)
{
	pgd_t * pgd;
	pmd_t * pmd;
	pte_t * pte;

	if (page_count(page) != 1)
		printk("mem_map disagrees with %p at %08lx\n", page, address);
	pgd = pgd_offset(tsk->mm, address);
	pmd = pmd_alloc(pgd, address);
	if (!pmd) {
		__free_page(page);
		force_sig(SIGKILL, tsk);
		return 0;
	}
	pte = pte_alloc(pmd, address);
	if (!pte) {
		__free_page(page);
		force_sig(SIGKILL, tsk);
		return 0;
	}
	if (!pte_none(*pte)) {
		pte_ERROR(*pte);
		__free_page(page);
		return 0;
	}
	flush_page_to_ram(page);
	set_pte(pte, pte_mkwrite(mk_pte(page, PAGE_SHARED)));
	/* no need for flush_tlb */
	return page;
}

void ia64_elf32_init(struct pt_regs *regs)
{
	int nr;

	put_shared_page(current, mem_map + MAP_NR(ia32_gdt_table), IA32_PAGE_OFFSET);
	if (PAGE_SHIFT <= IA32_PAGE_SHIFT)
		put_shared_page(current, mem_map + MAP_NR(ia32_tss), IA32_PAGE_OFFSET + PAGE_SIZE);

	nr = smp_processor_id();
	
	/* Do all the IA-32 setup here */

	current->thread.map_base = 0x40000000;

	/* CS descriptor */
	__asm__("mov ar.csd = %0" : /* no outputs */
		: "r" IA64_SEG_DESCRIPTOR(0L, 0xFFFFFL, 0xBL, 1L,
					  3L, 1L, 1L, 1L));
	/* SS descriptor */
	__asm__("mov ar.ssd = %0" : /* no outputs */
		: "r" IA64_SEG_DESCRIPTOR(0L, 0xFFFFFL, 0x3L, 1L,
					  3L, 1L, 1L, 1L));
	/* EFLAGS */
	__asm__("mov ar.eflag = %0" : /* no outputs */ : "r" (IA32_EFLAG));

	/* Control registers */
	__asm__("mov ar.cflg = %0"
		: /* no outputs */
		: "r" (((ulong) IA32_CR4 << 32) | IA32_CR0));
	__asm__("mov ar.fsr = %0"
		: /* no outputs */
		: "r" ((ulong)IA32_FSR_DEFAULT));
	__asm__("mov ar.fcr = %0"
		: /* no outputs */
		: "r" ((ulong)IA32_FCR_DEFAULT));
	__asm__("mov ar.fir = r0");
	__asm__("mov ar.fdr = r0");
	/* TSS */
	__asm__("mov ar.k1 = %0"
		: /* no outputs */
		: "r" IA64_SEG_DESCRIPTOR(IA32_PAGE_OFFSET + PAGE_SIZE,
					  0x1FFFL, 0xBL, 1L,
					  3L, 1L, 1L, 1L));

	/* Get the segment selectors right */
	regs->r16 = (__USER_DS << 16) |  (__USER_DS); /* ES == DS, GS, FS are zero */
	regs->r17 = (_TSS(nr) << 48) | (_LDT(nr) << 32)
		    | (__USER_DS << 16) | __USER_CS;

	/* Setup other segment descriptors - ESD, DSD, FSD, GSD */
	regs->r24 = IA64_SEG_DESCRIPTOR(0L, 0xFFFFFL, 0x3L, 1L, 3L, 1L, 1L, 1L);
	regs->r27 = IA64_SEG_DESCRIPTOR(0L, 0xFFFFFL, 0x3L, 1L, 3L, 1L, 1L, 1L);
	regs->r28 = IA64_SEG_DESCRIPTOR(0L, 0xFFFFFL, 0x3L, 1L, 3L, 1L, 1L, 1L);
	regs->r29 = IA64_SEG_DESCRIPTOR(0L, 0xFFFFFL, 0x3L, 1L, 3L, 1L, 1L, 1L);

	/* Setup the LDT and GDT */
	regs->r30 = ia32_gdt_table[_LDT(nr)];
	regs->r31 = IA64_SEG_DESCRIPTOR(0xc0000000L, 0x400L, 0x3L, 1L, 3L,
					1L, 1L, 1L);

       	/* Clear psr.ac */
	regs->cr_ipsr &= ~IA64_PSR_AC;

	regs->loadrs = 0;
}

#undef STACK_TOP
#define STACK_TOP ((IA32_PAGE_OFFSET/3) * 2)

int ia32_setup_arg_pages(struct linux_binprm *bprm)
{
	unsigned long stack_base;
	struct vm_area_struct *mpnt;
	int i;

	stack_base = STACK_TOP - MAX_ARG_PAGES*PAGE_SIZE;

	bprm->p += stack_base;
	if (bprm->loader)
		bprm->loader += stack_base;
	bprm->exec += stack_base;

	mpnt = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!mpnt) 
		return -ENOMEM; 
	
	{
		mpnt->vm_mm = current->mm;
		mpnt->vm_start = PAGE_MASK & (unsigned long) bprm->p;
		mpnt->vm_end = STACK_TOP;
		mpnt->vm_page_prot = PAGE_COPY;
		mpnt->vm_flags = VM_STACK_FLAGS;
		mpnt->vm_ops = NULL;
		mpnt->vm_pgoff = 0;
		mpnt->vm_file = NULL;
		mpnt->vm_private_data = 0;
		insert_vm_struct(current->mm, mpnt);
		current->mm->total_vm = (mpnt->vm_end - mpnt->vm_start) >> PAGE_SHIFT;
	} 

	for (i = 0 ; i < MAX_ARG_PAGES ; i++) {
		if (bprm->page[i]) {
			current->mm->rss++;
			put_dirty_page(current,bprm->page[i],stack_base);
		}
		stack_base += PAGE_SIZE;
	}
	
	return 0;
}
