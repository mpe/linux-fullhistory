/*
 * MIPS specific syscalls
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997 by Ralf Baechle
 *
 * $Id: sysmips.c,v 1.4 1998/05/07 15:20:05 ralf Exp $
 */
#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/utsname.h>

#include <asm/cachectl.h>
#include <asm/pgtable.h>
#include <asm/sysmips.h>
#include <asm/uaccess.h>

/*
 * How long a hostname can we get from user space?
 *  -EFAULT if invalid area or too long
 *  0 if ok
 *  >0 EFAULT after xx bytes
 */
static inline int
get_max_hostname(unsigned long address)
{
	struct vm_area_struct * vma;

	vma = find_vma(current->mm, address);
	if (!vma || vma->vm_start > address || !(vma->vm_flags & VM_READ))
		return -EFAULT;
	address = vma->vm_end - address;
	if (address > PAGE_SIZE)
		return 0;
	if (vma->vm_next && vma->vm_next->vm_start == vma->vm_end &&
	   (vma->vm_next->vm_flags & VM_READ))
		return 0;
	return address;
}

asmlinkage int
sys_sysmips(int cmd, int arg1, int arg2, int arg3)
{
	int	*p;
	char	*name;
	int	flags, tmp, len, retval;

	lock_kernel();
	switch(cmd)
	{
	case SETNAME:
		retval = -EPERM;
		if (!capable(CAP_SYS_ADMIN))
			goto out;

		name = (char *) arg1;
		len = strlen_user(name);

		retval = len;
		if (len < 0)
			goto out;

		retval = -EINVAL;
		if (len == 0 || len > __NEW_UTS_LEN)
			goto out;

		copy_from_user(system_utsname.nodename, name, len);
		system_utsname.nodename[len] = '\0';
		retval = 0;
		goto out;

	case MIPS_ATOMIC_SET:
		/* This is broken in case of page faults and SMP ...
		   Risc/OS fauls after maximum 20 tries with EAGAIN.  */
		p = (int *) arg1;
		retval = verify_area(VERIFY_WRITE, p, sizeof(*p));
		if (retval)
			goto out;
		save_and_cli(flags);
		retval = *p;
		*p = arg2;
		restore_flags(flags);
		goto out;

	case MIPS_FIXADE:
		tmp = current->tss.mflags & ~3;
		current->tss.mflags = tmp | (arg1 & 3);
		retval = 0;
		goto out;

	case FLUSH_CACHE:
		flush_cache_all();
		retval = 0;
		goto out;

	case MIPS_RDNVRAM:
		retval = -EIO;
		goto out;

	default:
		retval = -EINVAL;
		goto out;
	}

out:
	unlock_kernel();
	return retval;
}

/*
 * No implemented yet ...
 */
asmlinkage int
sys_cachectl(char *addr, int nbytes, int op)
{
	return -ENOSYS;
}
