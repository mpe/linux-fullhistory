/*
 *	linux/arch/x86_64/kernel/ioport.c
 *
 * This contains the io-permission bitmap code - written by obz, with changes
 * by Linus.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <asm/io.h>

/* Set EXTENT bits starting at BASE in BITMAP to value TURN_ON. */
static void set_bitmap(unsigned long *bitmap, short base, short extent, int new_value)
{
	unsigned long mask;
	unsigned long *bitmap_base = bitmap + (base / BITS_PER_LONG);
	unsigned long low_index = base & (BITS_PER_LONG-1);
	int length = low_index + extent;

	if (low_index != 0) {
		mask = (~0UL << low_index);
		if (length < BITS_PER_LONG)
			mask &= ~(~0UL << length);
		if (new_value)
			*bitmap_base++ |= mask;
		else
			*bitmap_base++ &= ~mask;
		length -= BITS_PER_LONG;
	}

	mask = (new_value ? ~0UL : 0UL);
	while (length >= BITS_PER_LONG) {
		*bitmap_base++ = mask;
		length -= BITS_PER_LONG;
	}

	if (length > 0) {
		mask = ~(~0UL << length);
		if (new_value)
			*bitmap_base++ |= mask;
		else
			*bitmap_base++ &= ~mask;
	}
}


/*
 * this changes the io permissions bitmap in the current task.
 */
asmlinkage long sys_ioperm(unsigned long from, unsigned long num, int turn_on)
{
	struct thread_struct * t = &current->thread;
	struct tss_struct *tss;

	if ((from + num <= from) || (from + num > IO_BITMAP_SIZE*32))
		return -EINVAL;
	if (turn_on && !capable(CAP_SYS_RAWIO))
		return -EPERM;

	/*
	 * If it's the first ioperm() call in this thread's lifetime, set the
	 * IO bitmap up. ioperm() is much less timing critical than clone(),
	 * this is why we delay this operation until now:
	 */
	if (!t->io_bitmap_ptr) { 
		t->io_bitmap_ptr = kmalloc(IO_BITMAP_BYTES, GFP_KERNEL);
		if (!t->io_bitmap_ptr)
			return -ENOMEM;

		memset(t->io_bitmap_ptr,0xff,IO_BITMAP_BYTES);
	}

	/*
	 * do it in the per-thread copy and in the TSS ...
	 */
	set_bitmap((unsigned long *) t->io_bitmap_ptr, from, num, !turn_on);
	tss = init_tss + get_cpu();
	if (tss->io_map_base != IO_BITMAP_OFFSET) {
		memcpy(tss->io_bitmap, t->io_bitmap_ptr, sizeof(tss->io_bitmap));
		tss->io_map_base = IO_BITMAP_OFFSET;
	} else {
		set_bitmap((unsigned long *) tss->io_bitmap, from, num, !turn_on);
	}
	put_cpu();
	return 0;
}

/*
 * sys_iopl has to be used when you want to access the IO ports
 * beyond the 0x3ff range: to get the full 65536 ports bitmapped
 * you'd need 8kB of bitmaps/process, which is a bit excessive.
 *
 * Here we just change the eflags value on the stack: we allow
 * only the super-user to do it. This depends on the stack-layout
 * on system-call entry - see also fork() and the signal handling
 * code.
 */

asmlinkage long sys_iopl(unsigned int level, struct pt_regs regs)
{
	unsigned int old = (regs.eflags >> 12) & 3;

	if (level > 3)
		return -EINVAL;
	/* Trying to gain more privileges? */
	if (level > old) {
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
	}
	regs.eflags = (regs.eflags &~ 0x3000UL) | (level << 12);
	return 0;
}

void eat_key(void)
{
	if (inb(0x60) & 1) 
		inb(0x64);
}

