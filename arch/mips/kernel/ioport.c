/*
 * linux/arch/mips/kernel/ioport.c
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ioport.h>

#define IOTABLE_SIZE 32

typedef struct resource_entry_t {
	u_long from, num;
	const char *name;
	struct resource_entry_t *next;
} resource_entry_t;

/* Set EXTENT bits starting at BASE in BITMAP to value TURN_ON. */
static void set_bitmap(unsigned long *bitmap, short base, short extent, int new_value)
{
	int mask;
	unsigned long *bitmap_base = bitmap + (base >> 5);
	unsigned short low_index = base & 0x1f;
	int length = low_index + extent;

	if (low_index != 0) {
		mask = (~0 << low_index);
		if (length < 32)
				mask &= ~(~0 << length);
		if (new_value)
			*bitmap_base++ |= mask;
		else
			*bitmap_base++ &= ~mask;
		length -= 32;
	}

	mask = (new_value ? ~0 : 0);
	while (length >= 32) {
		*bitmap_base++ = mask;
		length -= 32;
	}

	if (length > 0) {
		mask = ~(~0 << length);
		if (new_value)
			*bitmap_base++ |= mask;
		else
			*bitmap_base++ &= ~mask;
	}
}

/*
 * this changes the io permissions bitmap in the current task.
 */
asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int turn_on)
{
	return -ENOSYS;
}

unsigned int *stack;

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
asmlinkage int sys_iopl(long ebx,long ecx,long edx,
	     long esi, long edi, long ebp, long eax, long ds,
	     long es, long fs, long gs, long orig_eax,
	     long eip,long cs,long eflags,long esp,long ss)
{
	return -ENOSYS;
}

/*
 * The workhorse function: find where to put a new entry
 */
static resource_entry_t *find_gap(resource_entry_t *root,
				  u_long from, u_long num)
{
	unsigned long flags;
	resource_entry_t *p;
	
	if (from > from+num-1)
		return NULL;
	save_flags(flags);
	cli();
	for (p = root; ; p = p->next) {
		if ((p != root) && (p->from+p->num-1 >= from)) {
			p = NULL;
			break;
		}
		if ((p->next == NULL) || (p->next->from > from+num-1))
			break;
	}
	restore_flags(flags);
	return p;
}
