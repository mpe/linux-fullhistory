/* ptrace.c */
/* By Ross Biro 1/23/92 */
/*
 * Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/i387.h>
#include <asm/debugreg.h>

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/* determines which flags the user has access to. */
/* 1 = access 0 = no access */
#define FLAG_MASK 0x44dd5UL

/* set's the trap flag. */
#define TRAP_FLAG 0x100UL

/*
 * eflags and offset of eflags on child stack..
 */
#define EFLAGS offsetof(struct pt_regs, eflags)
#define EFL_OFFSET ((int)(EFLAGS-sizeof(struct pt_regs)))

/*
 * this routine will get a word off of the processes privileged stack. 
 * the offset is how far from the base addr as stored in the TSS.  
 * this routine assumes that all the privileged stacks are in our
 * data space.
 */   
static inline unsigned long get_stack_long(struct task_struct *task, int offset)
{
	unsigned char *stack;

	stack = (unsigned char *)task->thread.rsp0;
	stack += offset;
	return (*((unsigned long *)stack));
}

/*
 * this routine will put a word on the processes privileged stack. 
 * the offset is how far from the base addr as stored in the TSS.  
 * this routine assumes that all the privileged stacks are in our
 * data space.
 */
static inline long put_stack_long(struct task_struct *task, int offset,
	unsigned long data)
{
	unsigned char * stack;

	stack = (unsigned char *) task->thread.rsp0;
	stack += offset;
	*(unsigned long *) stack = data;
	return 0;
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure the single step bit is not set.
 */
void ptrace_disable(struct task_struct *child)
{ 
	long tmp;

	tmp = get_stack_long(child, EFL_OFFSET) & ~TRAP_FLAG;
	put_stack_long(child, EFL_OFFSET, tmp);
}

static int putreg(struct task_struct *child,
	unsigned long regno, unsigned long value)
{
	unsigned long tmp; 
	switch (regno >> 2) {
		// XXX: add 64bit setting. 
		case FS:
			if (value && (value & 3) != 3)
				return -EIO;
			child->thread.fs = value;
			return 0;
		case GS:
			if (value && (value & 3) != 3)
				return -EIO;
			child->thread.gs = value;
			return 0;
		case EFLAGS:
			value &= FLAG_MASK;
			tmp = get_stack_long(child, EFL_OFFSET); 
			tmp &= ~FLAG_MASK; 
			value |= tmp;
			break;
	}
	/* assumption about sizes... */
	if (regno > GS*4)
		regno -= 2*4;
	/* This has to be changes to put_stack_64() */
	/* Hmm, with 32 bit applications being around... this will be
	   rather funny */
	put_stack_long(child, regno - sizeof(struct pt_regs), value);
	return 0;
}

static unsigned long getreg(struct task_struct *child,
	unsigned long regno)
{
	switch (regno >> 3) {
		case FS:
			return child->thread.fs;
		case GS:
			return child->thread.gs;
		default:
			regno = regno - sizeof(struct pt_regs);
			return get_stack_long(child, regno);
	}

}

asmlinkage long sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	struct user * dummy = NULL;
	long i, ret;

	/* This lock_kernel fixes a subtle race with suid exec */
	lock_kernel();
	ret = -EPERM;
	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->ptrace & PT_PTRACED)
			goto out;
		/* set the ptrace bit in the process flags. */
		current->ptrace |= PT_PTRACED;
		ret = 0;
		goto out;
	}
	ret = -ESRCH;
	read_lock(&tasklist_lock);
	child = find_task_by_pid(pid);
	if (child)
		get_task_struct(child);
	read_unlock(&tasklist_lock);
	if (!child)
		goto out;

	ret = -EPERM;
	if (pid == 1)		/* you may not mess with init */
		goto out_tsk;

	if (request == PTRACE_ATTACH) {
		ret = ptrace_attach(child);
		goto out_tsk;
	}
	ret = -ESRCH;
	if (!(child->ptrace & PT_PTRACED))
		goto out_tsk;
	if (child->state != TASK_STOPPED) {
		if (request != PTRACE_KILL)
			goto out_tsk;
	}
	if (child->p_pptr != current)
		goto out_tsk;
	switch (request) {
	/* when I and D space are separate, these will need to be fixed. */
	case PTRACE_PEEKTEXT: /* read word at location addr. */ 
	case PTRACE_PEEKDATA: {
		unsigned long tmp;
		int copied;

		copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
		ret = -EIO;
		if (copied != sizeof(tmp))
			break;
		ret = put_user(tmp,(unsigned long *) data);
		break;
	}

	/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR: {
		unsigned long tmp;

		ret = -EIO;
		if ((addr & 3) || addr < 0 || 
		    addr > sizeof(struct user) - 3)
			break;

		tmp = 0;  /* Default return condition */
		if(addr < 20*sizeof(long))
			tmp = getreg(child, addr);
		if(addr >= (long) &dummy->u_debugreg[0] &&
		   addr <= (long) &dummy->u_debugreg[7]){
			addr -= (long) &dummy->u_debugreg[0];
			addr = addr >> 2;
			tmp = child->thread.debugreg[addr];
		}
		ret = put_user(tmp,(unsigned long *) data);
		break;
	}

	/* when I and D space are separate, this will have to be fixed. */
	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		ret = 0;
		if (access_process_vm(child, addr, &data, sizeof(data), 1) == sizeof(data))
			break;
		ret = -EIO;
		break;

	case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
		ret = -EIO;
		if ((addr & 3) || addr < 0 || 
		    addr > sizeof(struct user) - 3)
			break;

		if (addr < 20*sizeof(long)) {
			ret = putreg(child, addr, data);
			break;
		}
		/* We need to be very careful here.  We implicitly
		   want to modify a portion of the task_struct, and we
		   have to be selective about what portions we allow someone
		   to modify. */

		  ret = -EIO;
		  if(addr >= (long) &dummy->u_debugreg[0] &&
		     addr <= (long) &dummy->u_debugreg[7]){

			  if(addr == (long) &dummy->u_debugreg[4]) break;
			  if(addr == (long) &dummy->u_debugreg[5]) break;
			  if(addr < (long) &dummy->u_debugreg[4] &&
			     ((unsigned long) data) >= TASK_SIZE-3) break;
			  
			  if(addr == (long) &dummy->u_debugreg[7]) {
				  data &= ~DR_CONTROL_RESERVED;
				  for(i=0; i<4; i++)
					  if ((0x5454 >> ((data >> (16 + 4*i)) & 0xf)) & 1)
						  goto out_tsk;
			  }

			  addr -= (long) &dummy->u_debugreg;
			  addr = addr >> 2;
			  child->thread.debugreg[addr] = data;
			  ret = 0;
		  }
		  break;

	case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
	case PTRACE_CONT: { /* restart after signal. */
		long tmp;

		ret = -EIO;
		if ((unsigned long) data > _NSIG)
			break;
		if (request == PTRACE_SYSCALL) {
			set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		}
		else {
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		}
		child->exit_code = data;
	/* make sure the single step bit is not set. */
		tmp = get_stack_long(child, EFL_OFFSET);
		tmp &= ~TRAP_FLAG;
		put_stack_long(child, EFL_OFFSET,tmp);
		wake_up_process(child);
		ret = 0;
		break;
	}

/*
 * make the child exit.  Best I can do is send it a sigkill. 
 * perhaps it should be put in the status that it wants to 
 * exit.
 */
	case PTRACE_KILL: {
		long tmp;

		ret = 0;
		if (child->state == TASK_ZOMBIE)	/* already dead */
			break;
		child->exit_code = SIGKILL;
		/* make sure the single step bit is not set. */
		tmp = get_stack_long(child, EFL_OFFSET) & ~TRAP_FLAG;
		put_stack_long(child, EFL_OFFSET, tmp);
		wake_up_process(child);
		break;
	}

	case PTRACE_SINGLESTEP: {  /* set the trap flag. */
		long tmp;

		ret = -EIO;
		if ((unsigned long) data > _NSIG)
			break;
		clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		if ((child->ptrace & PT_DTRACE) == 0) {
			/* Spurious delayed TF traps may occur */
			child->ptrace |= PT_DTRACE;
		}
		tmp = get_stack_long(child, EFL_OFFSET) | TRAP_FLAG;
		put_stack_long(child, EFL_OFFSET, tmp);
		child->exit_code = data;
		/* give it a chance to run. */
		wake_up_process(child);
		ret = 0;
		break;
	}

	case PTRACE_DETACH:
		/* detach a process that was attached. */
		ret = ptrace_detach(child, data);
		break;

	case PTRACE_GETREGS: { /* Get all gp regs from the child. */
	  	if (!access_ok(VERIFY_WRITE, (unsigned *)data, FRAME_SIZE)) {
			ret = -EIO;
			break;
		}
		for ( i = 0; i < FRAME_SIZE; i += sizeof(long) ) {
			__put_user(getreg(child, i),(unsigned long *) data);
			data += sizeof(long);
		}
		ret = 0;
		break;
	}

	case PTRACE_SETREGS: { /* Set all gp regs in the child. */
		unsigned long tmp;
	  	if (!access_ok(VERIFY_READ, (unsigned *)data, FRAME_SIZE)) {
			ret = -EIO;
			break;
		}
		for ( i = 0; i < FRAME_SIZE; i += sizeof(long) ) {
			__get_user(tmp, (unsigned long *) data);
			putreg(child, i, tmp);
			data += sizeof(long);
		}
		ret = 0;
		break;
	}

	case PTRACE_GETFPREGS: { /* Get the child extended FPU state. */
		if (!access_ok(VERIFY_WRITE, (unsigned *)data,
			       sizeof(struct user_i387_struct))) {
			ret = -EIO;
			break;
		}
		if ( !child->used_math ) {
			/* Simulate an empty FPU. */
			set_fpu_cwd(child, 0x037f);
			set_fpu_swd(child, 0x0000);
			set_fpu_twd(child, 0xffff);
			set_fpu_mxcsr(child, 0x1f80);
		}
		ret = get_fpregs((struct user_i387_struct *)data, child);
		break;
	}

	case PTRACE_SETFPREGS: { /* Set the child extended FPU state. */
		if (!access_ok(VERIFY_READ, (unsigned *)data,
			       sizeof(struct user_i387_struct))) {
			ret = -EIO;
			break;
		}
		child->used_math = 1;
		ret = set_fpregs(child, (struct user_i387_struct *)data);
		break;
	}

	case PTRACE_SETOPTIONS: {
		if (data & PTRACE_O_TRACESYSGOOD)
			child->ptrace |= PT_TRACESYSGOOD;
		else
			child->ptrace &= ~PT_TRACESYSGOOD;
		ret = 0;
		break;
	}

	default:
		ret = -EIO;
		break;
	}
out_tsk:
	put_task_struct(child);
out:
	unlock_kernel();
	return ret;
}

asmlinkage void syscall_trace(struct pt_regs *regs)
{

#if 0
	printk("trace %s rip %lx rsp %lx rax %d origrax %d caller %lx tiflags %x ptrace %x\n",
	       current->comm,
	       regs->rip, regs->rsp, regs->rax, regs->orig_rax, __builtin_return_address(0),
	       current_thread_info()->flags, current->ptrace); 
#endif

	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return; 
	if (!(current->ptrace & PT_PTRACED))
		return;
	
	current->exit_code = SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD)
					? 0x80 : 0);
	preempt_disable();
	current->state = TASK_STOPPED;
	notify_parent(current, SIGCHLD);
	schedule();
	preempt_enable();
	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}
