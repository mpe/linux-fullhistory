/*
 *  linux/arch/arm/kernel/fiq.c
 *
 *  Copyright (C) 1998 Russell King
 *  FIQ support written by Philip Blundell <philb@gnu.org>, 1998.
 *
 *  FIQ support re-written by Russell King to be more generic
 *
 * We now properly support a method by which the FIQ handlers can
 * be stacked onto the vector.  We still do not support sharing
 * the FIQ vector itself.
 *
 * Operation is as follows:
 *  1. Owner A claims FIQ:
 *     - default_fiq relinquishes control.
 *  2. Owner A:
 *     - inserts code.
 *     - sets any registers,
 *     - enables FIQ.
 *  3. Owner B claims FIQ:
 *     - if owner A has a relinquish function.
 *       - disable FIQs.
 *       - saves any registers.
 *       - returns zero.
 *  4. Owner B:
 *     - inserts code.
 *     - sets any registers,
 *     - enables FIQ.
 *  5. Owner B releases FIQ:
 *     - Owner A is asked to reacquire FIQ:
 *	 - inserts code.
 *	 - restores saved registers.
 *	 - enables FIQ.
 *  6. Goto 3
 */
#include <linux/config.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/init.h>

#include <asm/fiq.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#define FIQ_VECTOR 0x1c

static unsigned long no_fiq_insn;

#ifdef CONFIG_CPU_32
static inline void unprotect_page_0(void)
{
	__asm__ __volatile__("mcr	p15, 0, %0, c3, c0" :
			: "r" (DOMAIN_USER_MANAGER |
			       DOMAIN_KERNEL_CLIENT |
			       DOMAIN_IO_CLIENT));
}

static inline void protect_page_0(void)
{
	set_fs(get_fs());
}
#else

#define unprotect_page_0()
#define protect_page_0()

#endif

/* Default reacquire function
 * - we always relinquish FIQ control
 * - we always reacquire FIQ control
 */
int fiq_def_op(void *ref, int relinquish)
{
	if (!relinquish) {
		unprotect_page_0();
		*(unsigned long *)FIQ_VECTOR = no_fiq_insn;
		protect_page_0();
		__flush_entry_to_ram(FIQ_VECTOR);
	}

	return 0;
}

static struct fiq_handler default_owner =
	{ NULL, "default", fiq_def_op, NULL };
static struct fiq_handler *current_fiq = &default_owner;

int get_fiq_list(char *buf)
{
	char *p = buf;

	if (current_fiq != &default_owner)
		p += sprintf(p, "FIQ:              %s\n",
			     current_fiq->name);

	return p - buf;
}

void set_fiq_handler(void *start, unsigned int length)
{
	unprotect_page_0();

	memcpy((void *)FIQ_VECTOR, start, length);

	protect_page_0();
#ifdef CONFIG_CPU_32
	processor.u.armv3v4._flush_cache_area(FIQ_VECTOR, FIQ_VECTOR + length, 1);
#endif
}

void set_fiq_regs(struct pt_regs *regs)
{
	/* not yet -
	 * this is temporary to get the floppy working
	 * again on RiscPC.  It *will* become more
	 * generic.
	 */
#ifdef CONFIG_ARCH_ACORN
	extern void floppy_fiqsetup(unsigned long len, unsigned long addr,
					     unsigned long port);
	floppy_fiqsetup(regs->ARM_r9, regs->ARM_r10, regs->ARM_fp);
#endif
}

void get_fiq_regs(struct pt_regs *regs)
{
	/* not yet */
}

int claim_fiq(struct fiq_handler *f)
{
	int ret = 0;

	if (current_fiq) {
		ret = -EBUSY;

		if (current_fiq->fiq_op != NULL)
			ret = current_fiq->fiq_op(current_fiq->dev_id, 1);
	}

	if (!ret) {
		f->next = current_fiq;
		current_fiq = f;
	}

	return ret;
}

void release_fiq(struct fiq_handler *f)
{
	if (current_fiq != f) {
		printk(KERN_ERR "%s FIQ trying to release %s FIQ\n",
		       f->name, current_fiq->name);
#ifdef CONFIG_DEBUG_ERRORS
		__backtrace();
#endif
		return;
	}

	do
		current_fiq = current_fiq->next;
	while (current_fiq->fiq_op(current_fiq->dev_id, 0));
}

__initfunc(void init_FIQ(void))
{
	no_fiq_insn = *(unsigned long *)FIQ_VECTOR;
	set_fs(get_fs());
}
