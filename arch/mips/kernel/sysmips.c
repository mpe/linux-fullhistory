/*
 * MIPS specific syscalls
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 by Ralf Baechle
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
#include <asm/segment.h>
#include <asm/sysmips.h>

static inline size_t
strnlen_user(const char *s, size_t count)
{
	return strnlen(s, count);
}

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

	vma = find_vma(current, address);
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
	int	flags, len, retval = -EINVAL;

	lock_kernel();
	switch(cmd)
	{
	case SETNAME:
		retval = -EPERM;
		if (!suser())
			goto out;
		name = (char *) arg1;
		len = get_max_hostname((unsigned long)name);
		retval = len;
		if (len < 0)
			goto out;
		len = strnlen_user(name, retval);
		retval = -EINVAL;
		if (len == 0 || len > __NEW_UTS_LEN)
			goto out;
		memcpy_fromfs(system_utsname.nodename, name, len);
		system_utsname.nodename[len] = '\0';
		retval = 0;
		goto out;
	case MIPS_ATOMIC_SET:
		p = (int *) arg1;
		retval = -EINVAL;
		if(verify_area(VERIFY_WRITE, p, sizeof(*p)))
			goto out;
		save_flags(flags);
		cli();
		retval = *p;
		*p = arg2;
		restore_flags(flags);
		goto out;
	case MIPS_FIXADE:
		if (arg1)
			current->tss.mflags |= MF_FIXADE;
		else
			current->tss.mflags |= MF_FIXADE;
		retval = 0;
		goto out;
	case FLUSH_CACHE:
		sys_cacheflush(0, ~0, BCACHE);
		retval = 0;
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
