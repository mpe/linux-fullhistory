/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#include "linux/linkage.h"
#include "linux/slab.h"
#include "linux/shm.h"
#include "asm/uaccess.h"
#define __FRAME_OFFSETS
#include "asm/ptrace.h"
#include "asm/unistd.h"
#include "asm/prctl.h" /* XXX This should get the constants from libc */
#include "choose-mode.h"

asmlinkage long wrap_sys_shmat(int shmid, char __user *shmaddr, int shmflg)
{
	unsigned long raddr;

	return do_shmat(shmid, shmaddr, shmflg, &raddr) ?: (long) raddr;
}

#ifdef CONFIG_MODE_TT
extern int modify_ldt(int func, void *ptr, unsigned long bytecount);

long sys_modify_ldt_tt(int func, void *ptr, unsigned long bytecount)
{
	/* XXX This should check VERIFY_WRITE depending on func, check this
	 * in i386 as well.
	 */
	if (!access_ok(VERIFY_READ, ptr, bytecount))
		return -EFAULT;
	return(modify_ldt(func, ptr, bytecount));
}
#endif

#ifdef CONFIG_MODE_SKAS
extern int userspace_pid[];

long sys_modify_ldt_skas(int func, void *ptr, unsigned long bytecount)
{
	struct ptrace_ldt ldt;
        void *buf;
        int res, n;

        buf = kmalloc(bytecount, GFP_KERNEL);
        if(buf == NULL)
                return(-ENOMEM);

        res = 0;

        switch(func){
        case 1:
        case 0x11:
                res = copy_from_user(buf, ptr, bytecount);
                break;
        }

        if(res != 0){
                res = -EFAULT;
                goto out;
        }

	ldt = ((struct ptrace_ldt) { .func	= func,
				     .ptr	= buf,
				     .bytecount = bytecount });
#warning Need to look up userspace_pid by cpu
	res = ptrace(PTRACE_LDT, userspace_pid[0], 0, (unsigned long) &ldt);
        if(res < 0)
                goto out;

        switch(func){
        case 0:
        case 2:
                n = res;
                res = copy_to_user(ptr, buf, n);
                if(res != 0)
                        res = -EFAULT;
                else
                        res = n;
                break;
        }

 out:
        kfree(buf);
        return(res);
}
#endif

long sys_modify_ldt(int func, void *ptr, unsigned long bytecount)
{
        return(CHOOSE_MODE_PROC(sys_modify_ldt_tt, sys_modify_ldt_skas, func,
                                ptr, bytecount));
}

#ifdef CONFIG_MODE_TT
extern long arch_prctl(int code, unsigned long addr);

static long arch_prctl_tt(int code, unsigned long addr)
{
	unsigned long tmp;
	long ret;

	switch(code){
	case ARCH_SET_GS:
	case ARCH_SET_FS:
		ret = arch_prctl(code, addr);
		break;
	case ARCH_GET_FS:
	case ARCH_GET_GS:
		ret = arch_prctl(code, (unsigned long) &tmp);
		if(!ret)
			ret = put_user(tmp, &addr);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return(ret);
}
#endif

#ifdef CONFIG_MODE_SKAS

static long arch_prctl_skas(int code, unsigned long addr)
{
	long ret = 0;

	switch(code){
	case ARCH_SET_GS:
		current->thread.regs.regs.skas.regs[GS_BASE / sizeof(unsigned long)] = addr;
		break;
	case ARCH_SET_FS:
		current->thread.regs.regs.skas.regs[FS_BASE / sizeof(unsigned long)] = addr;
		break;
	case ARCH_GET_FS:
		ret = put_user(current->thread.regs.regs.skas.regs[GS / sizeof(unsigned long)], &addr);
	        break;
	case ARCH_GET_GS:
		ret = put_user(current->thread.regs.regs.skas.regs[FS / sizeof(unsigned \
long)], &addr);
	        break;
	default:
		ret = -EINVAL;
		break;
	}

	return(ret);
}
#endif

long sys_arch_prctl(int code, unsigned long addr)
{
	return(CHOOSE_MODE_PROC(arch_prctl_tt, arch_prctl_skas, code, addr));
}

long sys_clone(unsigned long clone_flags, unsigned long newsp,
	       void __user *parent_tid, void __user *child_tid)
{
	long ret;

	/* XXX: normal arch do here this pass, and also pass the regs to
	 * do_fork, instead of NULL. Currently the arch-independent code
	 * ignores these values, while the UML code (actually it's
	 * copy_thread) does the right thing. But this should change,
	 probably. */
	/*if (!newsp)
		newsp = UPT_SP(current->thread.regs);*/
	current->thread.forking = 1;
	ret = do_fork(clone_flags, newsp, NULL, 0, parent_tid, child_tid);
	current->thread.forking = 0;
	return(ret);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
