/*
 * Kernel support for the ptrace() and syscall tracing interfaces.
 *
 * Copyright (C) 1999-2000 Hewlett-Packard Co
 * Copyright (C) 1999-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * Derived from the x86 and Alpha versions.  Most of the code in here
 * could actually be factored into a common set of routines.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/smp_lock.h>
#include <linux/user.h>

#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/ptrace_offsets.h>
#include <asm/rse.h>
#include <asm/system.h>
#include <asm/uaccess.h>

/*
 * Bits in the PSR that we allow ptrace() to change:
 *	be, up, ac, mfl, mfh (the user mask; five bits total)
 *	db (debug breakpoint fault; one bit)
 *	id (instruction debug fault disable; one bit)
 *	dd (data debug fault disable; one bit)
 *	ri (restart instruction; two bits)
 */
#define CR_IPSR_CHANGE_MASK 0x06a00100003eUL

/*
 * Collect the NaT bits for r1-r31 from sw->caller_unat and
 * sw->ar_unat and return a NaT bitset where bit i is set iff the NaT
 * bit of register i is set.
 */
long
ia64_get_nat_bits (struct pt_regs *pt, struct switch_stack *sw)
{
#	define GET_BITS(str, first, last, unat)						\
	({										\
		unsigned long bit = ia64_unat_pos(&str->r##first);			\
		unsigned long mask = ((1UL << (last - first + 1)) - 1) << first;	\
		(ia64_rotl(unat, first) >> bit) & mask;					\
	})
	unsigned long val;

	val  = GET_BITS(pt,  1,  3, sw->caller_unat);
	val |= GET_BITS(pt, 12, 15, sw->caller_unat);
	val |= GET_BITS(pt,  8, 11, sw->caller_unat);
	val |= GET_BITS(pt, 16, 31, sw->caller_unat);
	val |= GET_BITS(sw,  4,  7, sw->ar_unat);
	return val;

#	undef GET_BITS
}

/*
 * Store the NaT bitset NAT in pt->caller_unat and sw->ar_unat.
 */
void
ia64_put_nat_bits (struct pt_regs *pt, struct switch_stack *sw, unsigned long nat)
{
#	define PUT_BITS(str, first, last, nat)					\
	({									\
		unsigned long bit = ia64_unat_pos(&str->r##first);		\
		unsigned long mask = ((1UL << (last - first + 1)) - 1) << bit;	\
		(ia64_rotr(nat, first) << bit) & mask;				\
	})
	sw->caller_unat  = PUT_BITS(pt,  1,  3, nat);
	sw->caller_unat |= PUT_BITS(pt, 12, 15, nat);
	sw->caller_unat |= PUT_BITS(pt,  8, 11, nat);
	sw->caller_unat |= PUT_BITS(pt, 16, 31, nat);
	sw->ar_unat      = PUT_BITS(sw,  4,  7, nat);

#	undef PUT_BITS
}

#define IA64_MLI_TEMPLATE	0x2
#define IA64_MOVL_OPCODE	6

void
ia64_increment_ip (struct pt_regs *regs)
{
	unsigned long w0, w1, ri = ia64_psr(regs)->ri + 1;

	if (ri > 2) {
		ri = 0;
		regs->cr_iip += 16;
	} else if (ri == 2) {
		get_user(w0, (char *) regs->cr_iip + 0);
		get_user(w1, (char *) regs->cr_iip + 8);
		if (((w0 >> 1) & 0xf) == IA64_MLI_TEMPLATE && (w1 >> 60) == IA64_MOVL_OPCODE) {
			/*
			 * rfi'ing to slot 2 of an MLI bundle causes
			 * an illegal operation fault.  We don't want
			 * that to happen...  Note that we check the
			 * opcode only.  "movl" has a vc bit of 0, but
			 * since a vc bit of 1 is currently reserved,
			 * we might just as well treat it like a movl.
			 */
			ri = 0;
			regs->cr_iip += 16;
		}
	}
	ia64_psr(regs)->ri = ri;
}

void
ia64_decrement_ip (struct pt_regs *regs)
{
	unsigned long w0, w1, ri = ia64_psr(regs)->ri - 1;

	if (ia64_psr(regs)->ri == 0) {
		regs->cr_iip -= 16;
		ri = 2;
		get_user(w0, (char *) regs->cr_iip + 0);
		get_user(w1, (char *) regs->cr_iip + 8);
		if (((w0 >> 1) & 0xf) == IA64_MLI_TEMPLATE && (w1 >> 60) == IA64_MOVL_OPCODE) {
			/*
			 * rfi'ing to slot 2 of an MLI bundle causes
			 * an illegal operation fault.  We don't want
			 * that to happen...  Note that we check the
			 * opcode only.  "movl" has a vc bit of 0, but
			 * since a vc bit of 1 is currently reserved,
			 * we might just as well treat it like a movl.
			 */
			ri = 1;
		}
	}
	ia64_psr(regs)->ri = ri;
}

/*
 * This routine is used to read an rnat bits that are stored on the
 * kernel backing store.  Since, in general, the alignment of the user
 * and kernel are different, this is not completely trivial.  In
 * essence, we need to construct the user RNAT based on up to two
 * kernel RNAT values and/or the RNAT value saved in the child's
 * pt_regs.
 *
 * user rbs
 *
 * +--------+ <-- lowest address
 * | slot62 |
 * +--------+
 * |  rnat  | 0x....1f8
 * +--------+
 * | slot00 | \
 * +--------+ |
 * | slot01 | > child_regs->ar_rnat
 * +--------+ |
 * | slot02 | /				kernel rbs
 * +--------+ 				+--------+
 *	    <- child_regs->ar_bspstore	| slot61 | <-- krbs
 * +- - - - +				+--------+
 *					| slot62 |
 * +- - - - +				+--------+
 *					|  rnat	 |
 * +- - - - +				+--------+
 *   vrnat				| slot00 |
 * +- - - - +				+--------+
 *					=	 =
 *					+--------+
 *					| slot00 | \
 *					+--------+ |
 *					| slot01 | > child_stack->ar_rnat
 *					+--------+ |
 *					| slot02 | /
 *					+--------+
 *						  <--- child_stack->ar_bspstore
 *
 * The way to think of this code is as follows: bit 0 in the user rnat
 * corresponds to some bit N (0 <= N <= 62) in one of the kernel rnat
 * value.  The kernel rnat value holding this bit is stored in
 * variable rnat0.  rnat1 is loaded with the kernel rnat value that
 * form the upper bits of the user rnat value.
 *
 * Boundary cases:
 *
 * o when reading the rnat "below" the first rnat slot on the kernel
 *   backing store, rnat0/rnat1 are set to 0 and the low order bits
 *   are merged in from pt->ar_rnat.
 *
 * o when reading the rnat "above" the last rnat slot on the kernel
 *   backing store, rnat0/rnat1 gets its value from sw->ar_rnat.
 */
static unsigned long
get_rnat (struct pt_regs *pt, struct switch_stack *sw,
	  unsigned long *krbs, unsigned long *urnat_addr)
{
	unsigned long rnat0 = 0, rnat1 = 0, urnat = 0, *slot0_kaddr, kmask = ~0UL;
	unsigned long *kbsp, *ubspstore, *rnat0_kaddr, *rnat1_kaddr, shift;
	long num_regs;

	kbsp = (unsigned long *) sw->ar_bspstore;
	ubspstore = (unsigned long *) pt->ar_bspstore;
	/*
	 * First, figure out which bit number slot 0 in user-land maps
	 * to in the kernel rnat.  Do this by figuring out how many
	 * register slots we're beyond the user's backingstore and
	 * then computing the equivalent address in kernel space.
	 */
	num_regs = ia64_rse_num_regs(ubspstore, urnat_addr + 1);
	slot0_kaddr = ia64_rse_skip_regs(krbs, num_regs);
	shift = ia64_rse_slot_num(slot0_kaddr);
	rnat1_kaddr = ia64_rse_rnat_addr(slot0_kaddr);
	rnat0_kaddr = rnat1_kaddr - 64;

	if (ubspstore + 63 > urnat_addr) {
		/* some bits need to be merged in from pt->ar_rnat */
		kmask = ~((1UL << ia64_rse_slot_num(ubspstore)) - 1);
		urnat = (pt->ar_rnat & ~kmask);
	} 
	if (rnat0_kaddr >= kbsp) {
		rnat0 = sw->ar_rnat;
	} else if (rnat0_kaddr > krbs) {
		rnat0 = *rnat0_kaddr;
	}
	if (rnat1_kaddr >= kbsp) {
		rnat1 = sw->ar_rnat;
	} else if (rnat1_kaddr > krbs) {
		rnat1 = *rnat1_kaddr;
	}
	urnat |= ((rnat1 << (63 - shift)) | (rnat0 >> shift)) & kmask;
	return urnat;
}

/*
 * The reverse of get_rnat.
 */
static void
put_rnat (struct pt_regs *pt, struct switch_stack *sw,
	  unsigned long *krbs, unsigned long *urnat_addr, unsigned long urnat)
{
	unsigned long rnat0 = 0, rnat1 = 0, rnat = 0, *slot0_kaddr, kmask = ~0UL, mask;
	unsigned long *kbsp, *ubspstore, *rnat0_kaddr, *rnat1_kaddr, shift;
	long num_regs;

	kbsp = (unsigned long *) sw->ar_bspstore;
	ubspstore = (unsigned long *) pt->ar_bspstore;
	/*
	 * First, figure out which bit number slot 0 in user-land maps
	 * to in the kernel rnat.  Do this by figuring out how many
	 * register slots we're beyond the user's backingstore and
	 * then computing the equivalent address in kernel space.
	 */
	num_regs = (long) ia64_rse_num_regs(ubspstore, urnat_addr + 1);
	slot0_kaddr = ia64_rse_skip_regs(krbs, num_regs);
	shift = ia64_rse_slot_num(slot0_kaddr);
	rnat1_kaddr = ia64_rse_rnat_addr(slot0_kaddr);
	rnat0_kaddr = rnat1_kaddr - 64;

	if (ubspstore + 63 > urnat_addr) {
		/* some bits need to be place in pt->ar_rnat: */
		kmask = ~((1UL << ia64_rse_slot_num(ubspstore)) - 1);
		pt->ar_rnat = (pt->ar_rnat & kmask) | (rnat & ~kmask);
	} 
	/*
	 * Note: Section 11.1 of the EAS guarantees that bit 63 of an
	 * rnat slot is ignored. so we don't have to clear it here.
	 */
	rnat0 = (urnat << shift);
	mask = ~0UL << shift;
	if (rnat0_kaddr >= kbsp) {
		sw->ar_rnat = (sw->ar_rnat & ~mask) | (rnat0 & mask);
	} else if (rnat0_kaddr > krbs) {
		*rnat0_kaddr = ((*rnat0_kaddr & ~mask) | (rnat0 & mask));
	}

	rnat1 = (urnat >> (63 - shift));
	mask = ~0UL >> (63 - shift);
	if (rnat1_kaddr >= kbsp) {
		sw->ar_rnat = (sw->ar_rnat & ~mask) | (rnat1 & mask);
	} else if (rnat1_kaddr > krbs) {
		*rnat1_kaddr = ((*rnat1_kaddr & ~mask) | (rnat1 & mask));
	}
}

long
ia64_peek (struct pt_regs *regs, struct task_struct *child, unsigned long addr, long *val)
{
	unsigned long *bspstore, *krbs, krbs_num_regs, regnum, *rbs_end, *laddr;
	struct switch_stack *child_stack;
	struct pt_regs *child_regs;
	size_t copied;
	long ret;

	laddr = (unsigned long *) addr;
	child_regs = ia64_task_regs(child);
	child_stack = (struct switch_stack *) child_regs - 1;
	bspstore = (unsigned long *) child_regs->ar_bspstore;
	krbs = (unsigned long *) child + IA64_RBS_OFFSET/8;
	krbs_num_regs = ia64_rse_num_regs(krbs, (unsigned long *) child_stack->ar_bspstore);
	rbs_end = ia64_rse_skip_regs(bspstore, krbs_num_regs);
	if (laddr >= bspstore && laddr <= ia64_rse_rnat_addr(rbs_end)) {
		/*
		 * Attempt to read the RBS in an area that's actually
		 * on the kernel RBS => read the corresponding bits in
		 * the kernel RBS.
		 */
		if (ia64_rse_is_rnat_slot(laddr))
			ret = get_rnat(child_regs, child_stack, krbs, laddr);
		else {
			regnum = ia64_rse_num_regs(bspstore, laddr);
			laddr = ia64_rse_skip_regs(krbs, regnum);
			if (regnum >= krbs_num_regs) {
				ret = 0;
			} else {
				if  ((unsigned long) laddr >= (unsigned long) high_memory) {
					printk("yikes: trying to access long at %p\n", laddr);
					return -EIO;
				}
				ret = *laddr;
			}
		}
	} else {
		copied = access_process_vm(child, addr, &ret, sizeof(ret), 0);
		if (copied != sizeof(ret))
			return -EIO;
	}
	*val = ret;
	return 0;
}

long
ia64_poke (struct pt_regs *regs, struct task_struct *child, unsigned long addr, long val)
{
	unsigned long *bspstore, *krbs, krbs_num_regs, regnum, *rbs_end, *laddr;
	struct switch_stack *child_stack;
	struct pt_regs *child_regs;

	laddr = (unsigned long *) addr;
	child_regs = ia64_task_regs(child);
	child_stack = (struct switch_stack *) child_regs - 1;
	bspstore = (unsigned long *) child_regs->ar_bspstore;
	krbs = (unsigned long *) child + IA64_RBS_OFFSET/8;
	krbs_num_regs = ia64_rse_num_regs(krbs, (unsigned long *) child_stack->ar_bspstore);
	rbs_end = ia64_rse_skip_regs(bspstore, krbs_num_regs);
	if (laddr >= bspstore && laddr <= ia64_rse_rnat_addr(rbs_end)) {
		/*
		 * Attempt to write the RBS in an area that's actually
		 * on the kernel RBS => write the corresponding bits
		 * in the kernel RBS.
		 */
		if (ia64_rse_is_rnat_slot(laddr))
			put_rnat(child_regs, child_stack, krbs, laddr, val);
		else {
			regnum = ia64_rse_num_regs(bspstore, laddr);
			laddr = ia64_rse_skip_regs(krbs, regnum);
			if (regnum < krbs_num_regs) {
				*laddr = val;
			}
		}
	} else if (access_process_vm(child, addr, &val, sizeof(val), 1) != sizeof(val)) {
		return -EIO;
	}
	return 0;
}

/*
 * Synchronize (i.e, write) the RSE backing store living in kernel
 * space to the VM of the indicated child process.
 *
 * If new_bsp is non-zero, the bsp will (effectively) be updated to
 * the new value upon resumption of the child process.  This is
 * accomplished by setting the loadrs value to zero and the bspstore
 * value to the new bsp value.
 *
 * When new_bsp and force_loadrs_to_zero are both 0, the register
 * backing store in kernel space is written to user space and the
 * loadrs and bspstore values are left alone.
 *
 * When new_bsp is zero and force_loadrs_to_zero is 1 (non-zero),
 * loadrs is set to 0, and the bspstore value is set to the old bsp
 * value.  This will cause the stacked registers (r32 and up) to be
 * obtained entirely from the the child's memory space rather than
 * from the kernel.  (This makes it easier to write code for
 * modifying the stacked registers in multi-threaded programs.)
 *
 * Note:  I had originally written this function without the
 * force_loadrs_to_zero parameter; it was written so that loadrs would
 * always be set to zero.  But I had problems with certain system
 * calls apparently causing a portion of the RBS to be zeroed.  (I
 * still don't understand why this was happening.) Anyway, it'd
 * definitely less intrusive to leave loadrs and bspstore alone if
 * possible.
 */
static long
sync_kernel_register_backing_store (struct task_struct *child,
                                    long new_bsp,
                                    int force_loadrs_to_zero)
{
	unsigned long *krbs, bspstore, bsp, krbs_num_regs, rbs_end, addr, val;
	long ndirty, ret;
	struct pt_regs *child_regs;
	struct switch_stack *child_stack;

	ret = 0;
	child_regs = ia64_task_regs(child);
	child_stack = (struct switch_stack *) child_regs - 1;

	krbs = (unsigned long *) child + IA64_RBS_OFFSET/8;
	ndirty = ia64_rse_num_regs(krbs, krbs + (child_regs->loadrs >> 19));
	bspstore = child_regs->ar_bspstore;
	bsp = (long) ia64_rse_skip_regs((long *)bspstore, ndirty);
	krbs_num_regs = ia64_rse_num_regs(krbs, (unsigned long *) child_stack->ar_bspstore);
	rbs_end = (long) ia64_rse_skip_regs((long *)bspstore, krbs_num_regs);

	/* Return early if nothing to do */
	if (bsp == new_bsp)
		return 0;

	/* Write portion of backing store living on kernel stack to the child's VM. */
	for (addr = bspstore; addr < rbs_end; addr += 8) {
		ret = ia64_peek(child_regs, child, addr, &val);
		if (ret != 0)
			return ret;
		if (access_process_vm(child, addr, &val, sizeof(val), 1) != sizeof(val))
			return -EIO;
	}

	if (new_bsp != 0) {
		force_loadrs_to_zero = 1;
		bsp = new_bsp;
	}

	if (force_loadrs_to_zero) {
		child_regs->loadrs = 0;
		child_regs->ar_bspstore = bsp;
	}

	return ret;
}

static void
sync_thread_rbs (struct task_struct *child, int make_writable)
{
	struct task_struct *p;
	read_lock(&tasklist_lock);
	for_each_task(p) {
		if (p->mm == child->mm && p->state != TASK_RUNNING)
			sync_kernel_register_backing_store(p, 0, make_writable);
	}
	read_unlock(&tasklist_lock);
	child->thread.flags |= IA64_THREAD_KRBS_SYNCED;
}

/*
 * Ensure the state in child->thread.fph is up-to-date.
 */
static void
sync_fph (struct task_struct *child)
{
	if (ia64_psr(ia64_task_regs(child))->mfh && ia64_get_fpu_owner() == child) {
		ia64_save_fpu(&child->thread.fph[0]);
		child->thread.flags |= IA64_THREAD_FPH_VALID;
	}
	if (!(child->thread.flags & IA64_THREAD_FPH_VALID)) {
		memset(&child->thread.fph, 0, sizeof(child->thread.fph));
		child->thread.flags |= IA64_THREAD_FPH_VALID;
	}
}

asmlinkage long
sys_ptrace (long request, pid_t pid, unsigned long addr, unsigned long data,
	    long arg4, long arg5, long arg6, long arg7, long stack)
{
	struct pt_regs *regs = (struct pt_regs *) &stack;
	struct switch_stack *child_stack;
	struct pt_regs *child_regs;
	struct task_struct *child;
	unsigned long flags, regnum, *base;
	long ret;

	lock_kernel();
	ret = -EPERM;
	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->flags & PF_PTRACED)
			goto out;
		current->flags |= PF_PTRACED;
		ret = 0;
		goto out;
	}

	ret = -ESRCH;
	read_lock(&tasklist_lock);
	child = find_task_by_pid(pid);
	read_unlock(&tasklist_lock);
	if (!child)
		goto out;
	ret = -EPERM;
	if (pid == 1)		/* no messing around with init! */
		goto out;

	if (request == PTRACE_ATTACH) {
		if (child == current)
			goto out;
		if ((!child->dumpable ||
		    (current->uid != child->euid) ||
		    (current->uid != child->suid) ||
		    (current->uid != child->uid) ||
	 	    (current->gid != child->egid) ||
	 	    (current->gid != child->sgid) ||
	 	    (!cap_issubset(child->cap_permitted, current->cap_permitted)) ||
	 	    (current->gid != child->gid)) && !capable(CAP_SYS_PTRACE))
			goto out;
		/* the same process cannot be attached many times */
		if (child->flags & PF_PTRACED)
			goto out;
		child->flags |= PF_PTRACED;
		if (child->p_pptr != current) {
			unsigned long flags;

			write_lock_irqsave(&tasklist_lock, flags);
			REMOVE_LINKS(child);
			child->p_pptr = current;
			SET_LINKS(child);
			write_unlock_irqrestore(&tasklist_lock, flags);
		}
		send_sig(SIGSTOP, child, 1);
		ret = 0;
		goto out;
	}
	ret = -ESRCH;
	if (!(child->flags & PF_PTRACED))
		goto out;
	if (child->state != TASK_STOPPED) {
		if (request != PTRACE_KILL)
			goto out;
	}
	if (child->p_pptr != current)
		goto out;

	switch (request) {
	      case PTRACE_PEEKTEXT:
	      case PTRACE_PEEKDATA:		/* read word at location addr */
	        if (!(child->thread.flags & IA64_THREAD_KRBS_SYNCED)
		    && atomic_read(&child->mm->mm_users) > 1)
			sync_thread_rbs(child, 0);
		ret = ia64_peek(regs, child, addr, &data);
		if (ret == 0) {
			ret = data;
			regs->r8 = 0;	/* ensure "ret" is not mistaken as an error code */
		}
		goto out;

	      case PTRACE_POKETEXT:
	      case PTRACE_POKEDATA:		/* write the word at location addr */
	        if (!(child->thread.flags & IA64_THREAD_KRBS_SYNCED)
		    && atomic_read(&child->mm->mm_users) > 1)
			sync_thread_rbs(child, 1);
		ret = ia64_poke(regs, child, addr, data);
		goto out;

	      case PTRACE_PEEKUSR:		/* read the word at addr in the USER area */
		ret = -EIO;
		if ((addr & 0x7) != 0)
			goto out;

		if (addr < PT_CALLER_UNAT) {
			/* accessing fph */
			sync_fph(child);
			addr += (unsigned long) &child->thread.fph;
			ret = *(unsigned long *) addr;
		} else if (addr < PT_F9+16) {
			/* accessing switch_stack or pt_regs: */
			child_regs = ia64_task_regs(child);
			child_stack = (struct switch_stack *) child_regs - 1;
			ret = *(unsigned long *) ((long) child_stack + addr - PT_CALLER_UNAT);

			if (addr == PT_AR_BSP) {
				/* ret currently contains pt_regs.loadrs */
				unsigned long *rbs, *bspstore, ndirty;

				rbs = (unsigned long *) child + IA64_RBS_OFFSET/8;
				bspstore = (unsigned long *) child_regs->ar_bspstore;
				ndirty = ia64_rse_num_regs(rbs, rbs + (ret >> 19));
				ret = (unsigned long) ia64_rse_skip_regs(bspstore, ndirty);

				/*
				 * If we're in a system call, no ``cover'' was done.  So
				 * to make things uniform, we'll add the appropriate
				 * displacement onto bsp if we're in a system call.
				 *
				 * Note: It may be better to leave the system call case
				 * alone and subtract the amount of the cover for the
				 * non-syscall case.  That way the reported bsp value
				 * would actually be the correct bsp for the child
				 * process.
				 */
				if (!(child_regs->cr_ifs & (1UL << 63))) {
					ret = (unsigned long)
						ia64_rse_skip_regs((unsigned long *) ret,
						                   child_stack->ar_pfs & 0x7f);
				}
			} else if (addr == PT_CFM) {
				/* ret currently contains pt_regs.cr_ifs */
				if ((ret & (1UL << 63)) == 0)
					ret = child_stack->ar_pfs;
				ret &= 0x3fffffffffUL;		/* return only the CFM */
			}
		} else {
			if (!(child->thread.flags & IA64_THREAD_DBG_VALID)) {
				child->thread.flags |= IA64_THREAD_DBG_VALID;
				memset(child->thread.dbr, 0, sizeof child->thread.dbr);
				memset(child->thread.ibr, 0, sizeof child->thread.ibr);
			}
			if (addr >= PT_IBR) {
				regnum = (addr - PT_IBR) >> 3;
				base = &child->thread.ibr[0];
			} else {
				regnum = (addr - PT_DBR) >> 3;
				base = &child->thread.dbr[0];
			}
			if (regnum >= 8)
				goto out;
			ret = base[regnum];
		}
		regs->r8 = 0;	/* ensure "ret" is not mistaken as an error code */
		goto out;

	      case PTRACE_POKEUSR:	      /* write the word at addr in the USER area */
		ret = -EIO;
		if ((addr & 0x7) != 0)
			goto out;

		if (addr < PT_CALLER_UNAT) {
			/* accessing fph */
			sync_fph(child);
			addr += (unsigned long) &child->thread.fph;
			*(unsigned long *) addr = data;
		} else if (addr == PT_AR_BSPSTORE || addr == PT_CALLER_UNAT 
			|| addr == PT_KERNEL_FPSR || addr == PT_K_B0 || addr == PT_K_AR_PFS 
			|| (PT_K_AR_UNAT <= addr && addr <= PT_K_PR)) {
			/*
			 * Don't permit changes to certain registers.
			 * 
			 * We don't allow bspstore to be modified because doing
			 * so would mess up any modifications to bsp.  (See
			 * sync_kernel_register_backing_store for the details.)
			 */
			goto out;
		} else if (addr == PT_AR_BSP) {
			/* FIXME? Account for lack of ``cover'' in the syscall case */
			ret = sync_kernel_register_backing_store(child, data, 1);
			goto out;
		} else if (addr == PT_CFM) {
			child_regs = ia64_task_regs(child);
			child_stack = (struct switch_stack *) child_regs - 1;

			if (child_regs->cr_ifs & (1UL << 63)) {
				child_regs->cr_ifs = (child_regs->cr_ifs & ~0x3fffffffffUL)
				                   | (data & 0x3fffffffffUL);
			} else {
				child_stack->ar_pfs = (child_stack->ar_pfs & ~0x3fffffffffUL)
				                    | (data & 0x3fffffffffUL);
			}
		} else if (addr < PT_F9+16) {
			/* accessing switch_stack or pt_regs */
			child_regs = ia64_task_regs(child);
			child_stack = (struct switch_stack *) child_regs - 1;

			if (addr == PT_CR_IPSR)
				data = (data & CR_IPSR_CHANGE_MASK)
				     | (child_regs->cr_ipsr & ~CR_IPSR_CHANGE_MASK);
			
			*(unsigned long *) ((long) child_stack + addr - PT_CALLER_UNAT) = data;
		} else {
			if (!(child->thread.flags & IA64_THREAD_DBG_VALID)) {
				child->thread.flags |= IA64_THREAD_DBG_VALID;
				memset(child->thread.dbr, 0, sizeof child->thread.dbr);
				memset(child->thread.ibr, 0, sizeof child->thread.ibr);
			}

			if (addr >= PT_IBR) {
				regnum = (addr - PT_IBR) >> 3;
				base = &child->thread.ibr[0];
			} else {
				regnum = (addr - PT_DBR) >> 3;
				base = &child->thread.dbr[0];
			}
			if (regnum >= 8)
				goto out;
			if (regnum & 1) {
				/* force breakpoint to be effective only for user-level: */
				data &= ~(0x7UL << 56);
			}
			base[regnum] = data;
		}
		ret = 0;
		goto out;

	      case PTRACE_GETSIGINFO:
		ret = -EIO;
		if (!access_ok(VERIFY_WRITE, data, sizeof (siginfo_t))
		    || child->thread.siginfo == 0)
			goto out;
		copy_to_user((siginfo_t *) data, child->thread.siginfo, sizeof (siginfo_t));
		ret = 0;
		goto out;
		break;
	      case PTRACE_SETSIGINFO:
		ret = -EIO;
		if (!access_ok(VERIFY_READ, data, sizeof (siginfo_t))
		    || child->thread.siginfo == 0)
			goto out;
		copy_from_user(child->thread.siginfo, (siginfo_t *) data, sizeof (siginfo_t));
		ret = 0;
		goto out;
	      case PTRACE_SYSCALL:	/* continue and stop at next (return from) syscall */
	      case PTRACE_CONT:		/* restart after signal. */
		ret = -EIO;
		if (data > _NSIG)
			goto out;
		if (request == PTRACE_SYSCALL)
			child->flags |= PF_TRACESYS;
		else
			child->flags &= ~PF_TRACESYS;
		child->exit_code = data;

		/* make sure the single step/take-branch tra bits are not set: */
		ia64_psr(ia64_task_regs(child))->ss = 0;
		ia64_psr(ia64_task_regs(child))->tb = 0;

		/* Turn off flag indicating that the KRBS is sync'd with child's VM: */
		child->thread.flags &= ~IA64_THREAD_KRBS_SYNCED;

		wake_up_process(child);
		ret = 0;
		goto out;

	      case PTRACE_KILL:
		/*
		 * Make the child exit.  Best I can do is send it a
		 * sigkill.  Perhaps it should be put in the status
		 * that it wants to exit.
		 */
		if (child->state == TASK_ZOMBIE)		/* already dead */
			goto out;
		child->exit_code = SIGKILL;

		/* make sure the single step/take-branch tra bits are not set: */
		ia64_psr(ia64_task_regs(child))->ss = 0;
		ia64_psr(ia64_task_regs(child))->tb = 0;

		/* Turn off flag indicating that the KRBS is sync'd with child's VM: */
		child->thread.flags &= ~IA64_THREAD_KRBS_SYNCED;

		wake_up_process(child);
		ret = 0;
		goto out;

	      case PTRACE_SINGLESTEP:		/* let child execute for one instruction */
	      case PTRACE_SINGLEBLOCK:
		ret = -EIO;
		if (data > _NSIG)
			goto out;

		child->flags &= ~PF_TRACESYS;
		if (request == PTRACE_SINGLESTEP) {
			ia64_psr(ia64_task_regs(child))->ss = 1;
		} else {
			ia64_psr(ia64_task_regs(child))->tb = 1;
		}
		child->exit_code = data;

		/* Turn off flag indicating that the KRBS is sync'd with child's VM: */
		child->thread.flags &= ~IA64_THREAD_KRBS_SYNCED;

		/* give it a chance to run. */
		wake_up_process(child);
		ret = 0;
		goto out;

	      case PTRACE_DETACH:		/* detach a process that was attached. */
		ret = -EIO;
		if (data > _NSIG)
			goto out;

		child->flags &= ~(PF_PTRACED|PF_TRACESYS);
		child->exit_code = data;
		write_lock_irqsave(&tasklist_lock, flags);
		REMOVE_LINKS(child);
		child->p_pptr = child->p_opptr;
		SET_LINKS(child);
		write_unlock_irqrestore(&tasklist_lock, flags);

		/* make sure the single step/take-branch tra bits are not set: */
		ia64_psr(ia64_task_regs(child))->ss = 0;
		ia64_psr(ia64_task_regs(child))->tb = 0;

		/* Turn off flag indicating that the KRBS is sync'd with child's VM: */
		child->thread.flags &= ~IA64_THREAD_KRBS_SYNCED;

		wake_up_process(child);
		ret = 0;
		goto out;

	      default:
		ret = -EIO;
		goto out;
	}
  out:
	unlock_kernel();
	return ret;
}

void
syscall_trace (void)
{
	if ((current->flags & (PF_PTRACED|PF_TRACESYS)) != (PF_PTRACED|PF_TRACESYS))
		return;
	current->exit_code = SIGTRAP;
	set_current_state(TASK_STOPPED);
	notify_parent(current, SIGCHLD);
	schedule();
	/*
	 * This isn't the same as continuing with a signal, but it
	 * will do for normal use.  strace only continues with a
	 * signal if the stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}
