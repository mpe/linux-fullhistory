/* ptrace.c */
/* By Ross Biro 1/23/92 */
/* edited by Linus Torvalds */

#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>

#include <asm/segment.h>
#include <asm/system.h>

#if 0
/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/* determines which flags the user has access to. */
/* 1 = access 0 = no access */
#define FLAG_MASK 0x00044dd5

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
 * this routine will get a word off of the processes privileged stack. 
 * the offset is how far from the base addr as stored in the TSS.  
 * this routine assumes that all the privileged stacks are in our
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
 * this routine will put a word on the processes privileged stack. 
 * the offset is how far from the base addr as stored in the TSS.  
 * this routine assumes that all the privileged stacks are in our
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
 */
static unsigned long get_long(struct vm_area_struct * vma, unsigned long addr)
{
	pgd_t * pgdir;
	pte_t * pgtable;
	unsigned long page;

repeat:
	pgdir = PAGE_DIR_OFFSET(vma->vm_task, addr);
	if (pgd_none(*pgdir)) {
		do_no_page(vma, addr, 0);
		goto repeat;
	}
	if (pgd_bad(*pgdir)) {
		printk("ptrace: bad page directory %08lx\n", pgd_val(*pgdir));
		pgd_clear(pgdir);
		return 0;
	}
	pgtable = (pte_t *) (PAGE_PTR(addr) + pgd_page(*pgdir));
	if (!pte_present(*pgtable)) {
		do_no_page(vma, addr, 0);
		goto repeat;
	}
	page = pte_page(*pgtable);
/* this is a hack for non-kernel-mapped video buffers and similar */
	if (page >= high_memory)
		return 0;
	page += addr & ~PAGE_MASK;
	return *(unsigned long *) page;
}

/*
 * This routine puts a long into any process space by following the page
 * tables. NOTE! You should check that the long isn't on a page boundary,
 * and that it is in the task area before calling this: this routine does
 * no checking.
 *
 * Now keeps R/W state of page so that a text page stays readonly
 * even if a debugger scribbles breakpoints into it.  -M.U-
 */
static void put_long(struct vm_area_struct * vma, unsigned long addr,
	unsigned long data)
{
	pgd_t *pgdir;
	pte_t *pgtable;
	unsigned long page;

repeat:
	pgdir = PAGE_DIR_OFFSET(vma->vm_task, addr);
	if (!pgd_present(*pgdir)) {
		do_no_page(vma, addr, 1);
		goto repeat;
	}
	if (pgd_bad(*pgdir)) {
		printk("ptrace: bad page directory %08lx\n", pgd_val(*pgdir));
		pgd_clear(pgdir);
		return;
	}
	pgtable = (pte_t *) (PAGE_PTR(addr) + pgd_page(*pgdir));
	if (!pte_present(*pgtable)) {
		do_no_page(vma, addr, 1);
		goto repeat;
	}
	page = pte_page(*pgtable);
	if (!pte_write(*pgtable)) {
		do_wp_page(vma, addr, 1);
		goto repeat;
	}
/* this is a hack for non-kernel-mapped video buffers and similar */
	if (page < high_memory) {
		page += addr & ~PAGE_MASK;
		*(unsigned long *) page = data;
	}
/* we're bypassing pagetables, so we have to set the dirty bit ourselves */
/* this should also re-instate whatever read-only mode there was before */
	*pgtable = pte_mkdirty(mk_pte(page, vma->vm_page_prot));
	invalidate();
}

static struct vm_area_struct * find_extend_vma(struct task_struct * tsk, unsigned long addr)
{
	struct vm_area_struct * vma;

	addr &= PAGE_MASK;
	vma = find_vma(tsk, addr);
	if (!vma)
		return NULL;
	if (vma->vm_start <= addr)
		return vma;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		return NULL;
	if (vma->vm_end - addr > tsk->rlim[RLIMIT_STACK].rlim_cur)
		return NULL;
	vma->vm_offset -= vma->vm_start - addr;
	vma->vm_start = addr;
	return vma;
}

/*
 * This routine checks the page boundaries, and that the offset is
 * within the task area. It then calls get_long() to read a long.
 */
static int read_long(struct task_struct * tsk, unsigned long addr,
	unsigned long * result)
{
	struct vm_area_struct * vma = find_extend_vma(tsk, addr);

	if (!vma)
		return -EIO;
	if ((addr & ~PAGE_MASK) > PAGE_SIZE-sizeof(long)) {
		unsigned long low,high;
		struct vm_area_struct * vma_high = vma;

		if (addr + sizeof(long) >= vma->vm_end) {
			vma_high = vma->vm_next;
			if (!vma_high || vma_high->vm_start != vma->vm_end)
				return -EIO;
		}
		low = get_long(vma, addr & ~(sizeof(long)-1));
		high = get_long(vma_high, (addr+sizeof(long)) & ~(sizeof(long)-1));
		switch (addr & (sizeof(long)-1)) {
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
		*result = get_long(vma, addr);
	return 0;
}

/*
 * This routine checks the page boundaries, and that the offset is
 * within the task area. It then calls put_long() to write a long.
 */
static int write_long(struct task_struct * tsk, unsigned long addr,
	unsigned long data)
{
	struct vm_area_struct * vma = find_extend_vma(tsk, addr);

	if (!vma)
		return -EIO;
	if ((addr & ~PAGE_MASK) > PAGE_SIZE-sizeof(long)) {
		unsigned long low,high;
		struct vm_area_struct * vma_high = vma;

		if (addr + sizeof(long) >= vma->vm_end) {
			vma_high = vma->vm_next;
			if (!vma_high || vma_high->vm_start != vma->vm_end)
				return -EIO;
		}
		low = get_long(vma, addr & ~(sizeof(long)-1));
		high = get_long(vma_high, (addr+sizeof(long)) & ~(sizeof(long)-1));
		switch (addr & (sizeof(long)-1)) {
			case 0: /* shouldn't happen, but safety first */
				low = data;
				break;
			case 1:
				low &= 0x000000ff;
				low |= data << 8;
				high &= ~0xff;
				high |= data >> 24;
				break;
			case 2:
				low &= 0x0000ffff;
				low |= data << 16;
				high &= ~0xffff;
				high |= data >> 16;
				break;
			case 3:
				low &= 0x00ffffff;
				low |= data << 24;
				high &= ~0xffffff;
				high |= data >> 8;
				break;
		}
		put_long(vma, addr & ~(sizeof(long)-1),low);
		put_long(vma_high, (addr+sizeof(long)) & ~(sizeof(long)-1),high);
	} else
		put_long(vma, addr, data);
	return 0;
}
#endif

asmlinkage int sys_ptrace(long request, long pid, long addr, long data)
{
#if 1
	return -ENOSYS;
#else
	struct task_struct *child;
	struct user * dummy;
	int i;


	dummy = NULL;

	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->flags & PF_PTRACED)
			return -EPERM;
		/* set the ptrace bit in the process flags. */
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
		if ((!child->dumpable ||
		    (current->uid != child->euid) ||
		    (current->uid != child->uid) ||
	 	    (current->gid != child->egid) ||
	 	    (current->gid != child->gid)) && !suser())
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
	if (child->state != TASK_STOPPED) {
		if (request != PTRACE_KILL)
			return -ESRCH;
	}
	if (child->p_pptr != current)
		return -ESRCH;

	switch (request) {
	/* when I and D space are separate, these will need to be fixed. */
		case PTRACE_PEEKTEXT: /* read word at location addr. */ 
		case PTRACE_PEEKDATA: {
			unsigned long tmp;
			int res;

			res = read_long(child, addr, &tmp);
			if (res < 0)
				return res;
			res = verify_area(VERIFY_WRITE, (void *) data, sizeof(long));
			if (!res)
				put_fs_long(tmp,(unsigned long *) data);
			return res;
		}

	/* read the word at location addr in the USER area. */
		case PTRACE_PEEKUSR: {
			unsigned long tmp;
			int res;

			if ((addr & 3) || addr < 0 || 
			    addr > sizeof(struct user) - 3)
				return -EIO;

			res = verify_area(VERIFY_WRITE, (void *) data, sizeof(long));
			if (res)
				return res;
			tmp = 0;  /* Default return condition */
			if(addr < 17*sizeof(long)) {
			  addr = addr >> 2; /* temporary hack. */

			  tmp = get_stack_long(child, sizeof(long)*addr - MAGICNUMBER);
			  if (addr == DS || addr == ES ||
			      addr == FS || addr == GS ||
			      addr == CS || addr == SS)
			    tmp &= 0xffff;
			};
			if(addr >= (long) &dummy->u_debugreg[0] &&
			   addr <= (long) &dummy->u_debugreg[7]){
				addr -= (long) &dummy->u_debugreg[0];
				addr = addr >> 2;
				tmp = child->debugreg[addr];
			};
			put_fs_long(tmp,(unsigned long *) data);
			return 0;
		}

      /* when I and D space are separate, this will have to be fixed. */
		case PTRACE_POKETEXT: /* write the word at location addr. */
		case PTRACE_POKEDATA:
			return write_long(child,addr,data);

		case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
			if ((addr & 3) || addr < 0 || 
			    addr > sizeof(struct user) - 3)
				return -EIO;

			addr = addr >> 2; /* temporary hack. */

			if (addr == ORIG_EAX)
				return -EIO;
			if (addr == DS || addr == ES ||
			    addr == FS || addr == GS ||
			    addr == CS || addr == SS) {
			    	data &= 0xffff;
			    	if (data && (data & 3) != 3)
					return -EIO;
			}
			if (addr == EFL) {   /* flags. */
				data &= FLAG_MASK;
				data |= get_stack_long(child, EFL*sizeof(long)-MAGICNUMBER)  & ~FLAG_MASK;
			}
		  /* Do not allow the user to set the debug register for kernel
		     address space */
		  if(addr < 17){
			  if (put_stack_long(child, sizeof(long)*addr-MAGICNUMBER, data))
				return -EIO;
			return 0;
			};

		  /* We need to be very careful here.  We implicitly
		     want to modify a portion of the task_struct, and we
		     have to be selective about what portions we allow someone
		     to modify. */

		  addr = addr << 2;  /* Convert back again */
		  if(addr >= (long) &dummy->u_debugreg[0] &&
		     addr <= (long) &dummy->u_debugreg[7]){

			  if(addr == (long) &dummy->u_debugreg[4]) return -EIO;
			  if(addr == (long) &dummy->u_debugreg[5]) return -EIO;
			  if(addr < (long) &dummy->u_debugreg[4] &&
			     ((unsigned long) data) >= 0xbffffffd) return -EIO;
			  
			  if(addr == (long) &dummy->u_debugreg[7]) {
				  data &= ~DR_CONTROL_RESERVED;
				  for(i=0; i<4; i++)
					  if ((0x5f54 >> ((data >> (16 + 4*i)) & 0xf)) & 1)
						  return -EIO;
			  };

			  addr -= (long) &dummy->u_debugreg;
			  addr = addr >> 2;
			  child->debugreg[addr] = data;
			  return 0;
		  };
		  return -EIO;

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
			tmp = get_stack_long(child, sizeof(long)*EFL-MAGICNUMBER) & ~TRAP_FLAG;
			put_stack_long(child, sizeof(long)*EFL-MAGICNUMBER,tmp);
			return 0;
		}

/*
 * make the child exit.  Best I can do is send it a sigkill. 
 * perhaps it should be put in the status that it wants to 
 * exit.
 */
		case PTRACE_KILL: {
			long tmp;

			child->state = TASK_RUNNING;
			child->exit_code = SIGKILL;
	/* make sure the single step bit is not set. */
			tmp = get_stack_long(child, sizeof(long)*EFL-MAGICNUMBER) & ~TRAP_FLAG;
			put_stack_long(child, sizeof(long)*EFL-MAGICNUMBER,tmp);
			return 0;
		}

		case PTRACE_SINGLESTEP: {  /* set the trap flag. */
			long tmp;

			if ((unsigned long) data > NSIG)
				return -EIO;
			child->flags &= ~PF_TRACESYS;
			tmp = get_stack_long(child, sizeof(long)*EFL-MAGICNUMBER) | TRAP_FLAG;
			put_stack_long(child, sizeof(long)*EFL-MAGICNUMBER,tmp);
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
			tmp = get_stack_long(child, sizeof(long)*EFL-MAGICNUMBER) & ~TRAP_FLAG;
			put_stack_long(child, sizeof(long)*EFL-MAGICNUMBER,tmp);
			return 0;
		}

		default:
			return -EIO;
	}
#endif
}

asmlinkage void syscall_trace(void)
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
