/*
 * IA32 helper functions
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/ia32.h>

extern unsigned long *ia32_gdt_table, *ia32_tss;

extern void die_if_kernel (char *str, struct pt_regs *regs, long err);

/*
 * Setup IA32 GDT and TSS 
 */
void
ia32_gdt_init(void)
{
	unsigned long gdt_and_tss_page;

	/* allocate two IA-32 pages of memory: */
	gdt_and_tss_page = __get_free_pages(GFP_KERNEL,
					    (IA32_PAGE_SHIFT < PAGE_SHIFT)
					    ? 0 : (IA32_PAGE_SHIFT + 1) - PAGE_SHIFT);
	ia32_gdt_table = (unsigned long *) gdt_and_tss_page;
	ia32_tss = (unsigned long *) (gdt_and_tss_page + IA32_PAGE_SIZE);

	/* Zero the gdt and tss */
	memset((void *) gdt_and_tss_page, 0, 2*IA32_PAGE_SIZE);

	/* CS descriptor in IA-32 format */
	ia32_gdt_table[4] = IA32_SEG_DESCRIPTOR(0L, 0xBFFFFFFFL, 0xBL, 1L,
						3L, 1L, 1L, 1L, 1L);

	/* DS descriptor in IA-32 format */
	ia32_gdt_table[5] = IA32_SEG_DESCRIPTOR(0L, 0xBFFFFFFFL, 0x3L, 1L,
						3L, 1L, 1L, 1L, 1L);
}

/*
 * Handle bad IA32 interrupt via syscall 
 */
void
ia32_bad_interrupt (unsigned long int_num, struct pt_regs *regs)
{
	siginfo_t siginfo;

	die_if_kernel("Bad IA-32 interrupt", regs, int_num);

	siginfo.si_signo = SIGTRAP;
	siginfo.si_errno = int_num;	/* XXX is it legal to abuse si_errno like this? */
	siginfo.si_code = TRAP_BRKPT;
	force_sig_info(SIGTRAP, &siginfo, current);
}

