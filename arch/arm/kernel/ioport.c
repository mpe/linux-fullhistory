/*
 * linux/arch/arm/kernel/ioport.c
 *
 * IO permission support for ARM.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ioport.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>

asmlinkage int sys_iopl(unsigned long turn_on)
{
	if (turn_on && !capable(CAP_SYS_RAWIO))
		return -EPERM;

	/*
	 * We only support an on_off approach
	 */
	modify_domain(DOMAIN_IO, turn_on ? DOMAIN_MANAGER : DOMAIN_CLIENT);

	return 0;
}
