/*
 * linux/arch/arm/kernel/ioport.c
 *
 * Io-port support is not used for ARM
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ioport.h>

/* Set EXTENT bits starting at BASE in BITMAP to value TURN_ON. */
/*asmlinkage void set_bitmap(unsigned long *bitmap, short base, short extent, int new_value)
{
}*/

asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int turn_on)
{
	return -ENOSYS;
}

asmlinkage int sys_iopl(long ebx,long ecx,long edx,
	     long esi, long edi, long ebp, long eax, long ds,
	     long es, long fs, long gs, long orig_eax,
	     long eip,long cs,long eflags,long esp,long ss)
{
	return -ENOSYS;
}
