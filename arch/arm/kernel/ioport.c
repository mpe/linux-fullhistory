/*
 * linux/arch/arm/kernel/ioport.c
 *
 * IO permission support for ARM.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/mm.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>

unsigned long
resource_fixup(struct pci_dev * dev, struct resource * res,
	       unsigned long start, unsigned long size)
{
	return start;
}

#ifdef CONFIG_CPU_32
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
#else
asmlinkage int sys_iopl(unsigned long turn_on)
{
	return -ENOSYS;
}
#endif
