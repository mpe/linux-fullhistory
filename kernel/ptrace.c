/* ptrace.c */
/* By Ross Biro 1/23/92 */
/* edited by Linus Torvalds */

#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>

#include <asm/segment.h>
#include <asm/system.h>

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/* determines which flags the user has access to. */
/* 1 = access 0 = no access */
#define FLAG_MASK 0x00000dd9

/* set's the trap flag. */
#define TRAP_FLAG 0x100

/*
 * this is the number to subtract from the top of the stack. To find
 * the local frame.
 */
#define MAGICNUMBER 68

/* change a pid into a task struct. */
static inline struct task_struct * get_task(int pid)
{
	int i;

	for (i = 1; i < NR_TASKS; i++) {
		if (task[i] != NULL && (task[i]->pid == pid))
			return task[i];
	}
	return NULL;
}

/*
 * this routine will get a word off of the processes priviledged stack. 
 * the offset is how far from the base addr as stored in the TSS.  
 * this routine assumes that all the priviledged stacks are in our
 * data space.
 */   
static inline int get_stack_long(struct task_struct *task, int offset)
{
	unsigned char *stack;

	stack = (unsigned char *)task->tss.esp0;
	stack += offset;
	return (*((int *)stack));
}

/*
 * this routine will put a word on the processes priviledged stack. 
 * the offset is how far from the base addr as stored in the TSS.  
 * this routine assumes that all the priviledged stacks are in our
 * data space.
 */
static inline int put_stack_long(struct task_struct *task, int offset,
	unsigned long data)
{
	unsigned char * stack;

	stack = (unsigned char *) task->tss.esp0;
	stack += offset;
	*(unsigned long *) stack = data;
	return 0;
}

/*
 * This routine gets a long from any process space by following the page
 * tables. NOTE! You should check that the long isn't on a page boundary,
 * and that it is in the task area before calling this: this routine does
 * no checking.
 *
 * NOTE2! This uses "tsk->tss.cr3" even though we know it's currently always
 * zero. This routine shouldn't have to change when we make a better mm.
 */
static unsigned long get_long(struct task_struct * tsk,
	unsigned long addr)
{
	unsigned long page;

repeat:
	page = tsk->tss.cr3 + ((addr >> 20) & 0xffc);
	page = *(unsigned long *) page;
	if (page & PAGE_PRESENT) {
		page &= 0xfffff000;
		page += (addr >> 10) & 0xffc;
		page = *((unsigned long *) page);
	}
	if (!(page & PAGE_PRESENT)) {
		do_no_page(0,addr,tsk,0);
		goto repeat;
	}
	page &= 0xfffff000;
	page += addr & 0xfff;
	return *(unsigned long *) page;
}

/*
 * This routine puts a long into any process space by following the page
 * tables. NOTE! You should check that the long isn't on a page boundary,
 * and that it is in the task area before calling this: this routine does
 * no checking.
 */
static void put_long(struct task_struct * tsk, unsigned long addr,
	unsigned long data)
{
	unsigned long page, pte;

repeat:
	page = tsk->tss.cr3 + ((addr >> 20) & 0xffc);
	page = *(unsigned long *) page;
	if (page & PAGE_PRESENT) {
		page &= 0xfffff000;
		page += (addr >> 10) & 0xffc;
		pte = page;
		page = *((unsigned long *) page);
	}
	if (!(page & PAGE_PRESENT)) {
		do_no_page(PAGE_RW,addr,tsk,0);
		goto repeat;
	}
	if (!(page & PAGE_RW)) {
		do_wp_page(PAGE_RW | PAGE_PRESENT,addr,tsk,0);
		goto repeat;
	}
/* we're bypassing pagetables, so we have to set the dirty bit ourselves */
	*(unsigned long *) pte |= (PAGE_DIRTY|PAGE_COW);
	page &= 0xfffff000;
	page += addr & 0xfff;
	*(unsigned long *) page = data;
}

/*
 * This routine checks the page boundaries, and that the offset is
 * within the task area. It then calls get_long() to read a long.
 */
static int read_long(struct task_struct * tsk, unsigned long addr,
	unsigned long * result)
{
	unsigned long low,high;

	if (addr > TASK_SIZE-4)
		return -EIO;
	if ((addr & 0xfff) > PAGE_SIZE-4) {
		low = get_long(tsk,addr & 0xfffffffc);
		high = get_long(tsk,(addr+4) & 0xfffffffc);
		switch (addr & 3) {
			case 1:
				low >>= 8;
				low |= high << 24;
				break;
			case 2:
				low >>= 16;
				low |= high << 16;
				break;
			case 3:
				low >>= 24;
				low |= high << 8;
				break;
		}
		*result = low;
	} else
		*result = get_long(tsk,addr);
	return 0;
}

/*
 * This routine checks the page boundaries, and that the offset is
 * within the task area. It then calls put_long() to write a long.
 */
static int write_long(struct task_struct * tsk, unsigned long addr,
	unsigned long data)
{
	unsigned long low,high;

	if (addr > TASK_SIZE-4)
		return -EIO;
	if ((addr & 0xfff) > PAGE_SIZE-4) {
		low = get_long(tsk,addr & 0xfffffffc);
		high = get_long(tsk,(addr+4) & 0xfffffffc);
		switch (addr & 3) {
			case 0: /* shouldn't happen, but safety first */
				low = data;
				break;
			case 1:
				low &= 0x000000ff;
				low |= data << 8;
				high &= 0xffffff00;
				high |= data >> 24;
				break;
			case 2:
				low &= 0x0000ffff;
				low |= data << 16;
				high &= 0xffff0000;
				high |= data >> 16;
				break;
			case 3:
				low &= 0x00ffffff;
				low |= data << 24;
				high &= 0xff000000;
				high |= data >> 8;
				break;
		}
		put_long(tsk,addr & 0xfffffffc,low);
		put_long(tsk,(addr+4) & 0xfffffffc,high);
	} else
		put_long(tsk,addr,data);
	return 0;
}

extern "C" int sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;

	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->flags & PF_PTRACED)
			return -EPERM;
		/* set the ptrace bit in the proccess flags. */
		current->flags |= PF_PTRACED;
		return 0;
	}
	if (pid == 1)		/* you may not mess with init */
		return -EPERM;
	if (!(child = get_task(pid)))
		return -ESRCH;
	if (request == PTRACE_ATTACH) {
		if (child == current)
			return -EPERM;
		if ((!child->dumpable || (current->uid != child->euid) ||
	 	    (current->gid != child->egid)) && !suser())
			return -EPERM;
		/* the same process cannot be attached many times */
		if (child->flags & PF_PTRACED)
			return -EPERM;
		child->flags |= PF_PTRACED;
		if (child->p_pptr != current) {
			REMOVE_LINKS(child);
			child->p_pptr = current;
			SET_LINKS(child);
		}
		send_sig(SIGSTOP, child, 1);
		return 0;
	}
	if (!(child->flags & PF_PTRACED))
		return -ESRCH;
	if (child->state != TASK_STOPPED && request != PTRACE_DETACH)
		return -ESRCH;
	if (child->p_pptr != current)
		return -ESRCH;

	switch (request) {
	/* when I and D space are seperate, these will need to be fixed. */
		case PTRACE_PEEKTEXT: /* read word at location addr. */ 
		case PTRACE_PEEKDATA: {
			unsigned long tmp;
			int res;

			res = read_long(child, addr, &tmp);
			if (res < 0)
				return res;
			res = verify_area(VERIFY_WRITE, (void *) data, 4);
			if (!res)
				put_fs_long(tmp,(unsigned long *) data);
			return res;
		}

	/* read the word at location addr in the USER area. */
		case PTRACE_PEEKUSR: {
			unsigned long tmp;
			int res;

			addr = addr >> 2; /* temporary hack. */
			if (addr < 0 || addr >= 17)
				return -EIO;
			res = verify_area(VERIFY_WRITE, (void *) data, 4);
			if (res)
				return res;
			tmp = get_stack_long(child, 4*addr - MAGICNUMBER);
			put_fs_long(tmp,(unsigned long *) data);
			return 0;
		}

      /* when I and D space are seperate, this will have to be fixed. */
		case PTRACE_POKETEXT: /* write the word at location addr. */
		case PTRACE_POKEDATA:
			return write_long(child,addr,data);

		case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
			addr = addr >> 2; /* temproary hack. */
			if (addr < 0 || addr >= 17)
				return -EIO;
			if (addr == ORIG_EAX)
				return -EIO;
			if (addr == EFL) {   /* flags. */
				data &= FLAG_MASK;
				data |= get_stack_long(child, EFL*4-MAGICNUMBER)  & ~FLAG_MASK;
			}
			if (put_stack_long(child, 4*addr-MAGICNUMBER, data))
				return -EIO;
			return 0;

		case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
		case PTRACE_CONT: { /* restart after signal. */
			long tmp;

			if ((unsigned long) data > NSIG)
				return -EIO;
			if (request == PTRACE_SYSCALL)
				child->flags |= PF_TRACESYS;
			else
				child->flags &= ~PF_TRACESYS;
			child->exit_code = data;
			child->state = TASK_RUNNING;
	/* make sure the single step bit is not set. */
			tmp = get_stack_long(child, 4*EFL-MAGICNUMBER) & ~TRAP_FLAG;
			put_stack_long(child, 4*EFL-MAGICNUMBER,tmp);
			return 0;
		}

/*
 * make the child exit.  Best I can do is send it a sigkill. 
 * perhaps it should be put in the status that it want's to 
 * exit.
 */
		case PTRACE_KILL: {
			long tmp;

			child->state = TASK_RUNNING;
			child->exit_code = SIGKILL;
	/* make sure the single step bit is not set. */
			tmp = get_stack_long(child, 4*EFL-MAGICNUMBER) & ~TRAP_FLAG;
			put_stack_long(child, 4*EFL-MAGICNUMBER,tmp);
			return 0;
		}

		case PTRACE_SINGLESTEP: {  /* set the trap flag. */
			long tmp;

			if ((unsigned long) data > NSIG)
				return -EIO;
			child->flags &= ~PF_TRACESYS;
			tmp = get_stack_long(child, 4*EFL-MAGICNUMBER) | TRAP_FLAG;
			put_stack_long(child, 4*EFL-MAGICNUMBER,tmp);
			child->state = TASK_RUNNING;
			child->exit_code = data;
	/* give it a chance to run. */
			return 0;
		}

		case PTRACE_DETACH: { /* detach a process that was attached. */
			long tmp;

			if ((unsigned long) data > NSIG)
				return -EIO;
			child->flags &= ~(PF_PTRACED|PF_TRACESYS);
			child->state = TASK_RUNNING;
			child->exit_code = data;
			REMOVE_LINKS(child);
			child->p_pptr = child->p_opptr;
			SET_LINKS(child);
			/* make sure the single step bit is not set. */
			tmp = get_stack_long(child, 4*EFL-MAGICNUMBER) & ~TRAP_FLAG;
			put_stack_long(child, 4*EFL-MAGICNUMBER,tmp);
			return 0;
		}

		default:
			return -EIO;
	}
}

extern "C" void syscall_trace(void)
{
	if ((current->flags & (PF_PTRACED|PF_TRACESYS))
			!= (PF_PTRACED|PF_TRACESYS))
		return;
	current->exit_code = SIGTRAP;
	current->state = TASK_STOPPED;
	notify_parent(current);
	schedule();
	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code)
		current->signal |= (1 << (current->exit_code - 1));
	current->exit_code = 0;
}
