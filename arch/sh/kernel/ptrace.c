/*
 * Surely this doesn't work... (we need to design ptrace for SupreH)
 * linux/arch/sh/kernel/ptrace.c
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
 * Offset of eflags on child stack..
 */
#define EFL_OFFSET ((EFL-2)*4-sizeof(struct pt_regs))

/*
 * this routine will get a word off of the processes privileged stack. 
 * the offset is how far from the base addr as stored in the TSS.  
 * this routine assumes that all the privileged stacks are in our
 * data space.
 */   
static inline int get_stack_long(struct task_struct *task, int offset)
{
	unsigned char *stack;

	stack = (unsigned char *)task->thread.sp;
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

	stack = (unsigned char *) task->thread.sp;
	stack += offset;
	*(unsigned long *) stack = data;
	return 0;
}

static int putreg(struct task_struct *child,
	unsigned long regno, unsigned long value)
{
#if 0
	switch (regno >> 2) {
		case ORIG_EAX:
			return -EIO;
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
		case DS:
		case ES:
			if (value && (value & 3) != 3)
				return -EIO;
			value &= 0xffff;
			break;
		case SS:
		case CS:
			if ((value & 3) != 3)
				return -EIO;
			value &= 0xffff;
			break;
		case EFL:
			value &= FLAG_MASK;
			value |= get_stack_long(child, EFL_OFFSET) & ~FLAG_MASK;
	}
	if (regno > GS*4)
		regno -= 2*4;
	put_stack_long(child, regno - sizeof(struct pt_regs), value);
#endif
	return 0;
}

static unsigned long getreg(struct task_struct *child,
	unsigned long regno)
{
	unsigned long retval = ~0UL;

#if 0
	switch (regno >> 2) {
		case FS:
			retval = child->thread.fs;
			break;
		case GS:
			retval = child->thread.gs;
			break;
		case DS:
		case ES:
		case SS:
		case CS:
			retval = 0xffff;
			/* fall through */
		default:
			if (regno > GS*4)
				regno -= 2*4;
			regno = regno - sizeof(struct pt_regs);
			retval &= get_stack_long(child, regno);
	}
#endif
	return retval;
}

asmlinkage int sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	struct user * dummy = NULL;
	unsigned long flags;
	int i, ret;

	lock_kernel();
	ret = -EPERM;
	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->flags & PF_PTRACED)
			goto out;
		/* set the ptrace bit in the process flags. */
		current->flags |= PF_PTRACED;
		ret = 0;
		goto out;
	}
	ret = -ESRCH;
	read_lock(&tasklist_lock);
	child = find_task_by_pid(pid);
	read_unlock(&tasklist_lock);	/* FIXME!!! */
	if (!child)
		goto out;
	ret = -EPERM;
	if (pid == 1)		/* you may not mess with init */
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

		write_lock_irqsave(&tasklist_lock, flags);
		if (child->p_pptr != current) {
			REMOVE_LINKS(child);
			child->p_pptr = current;
			SET_LINKS(child);
		}
		write_unlock_irqrestore(&tasklist_lock, flags);

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
	/* when I and D space are separate, these will need to be fixed. */
		case PTRACE_PEEKTEXT: /* read word at location addr. */ 
		case PTRACE_PEEKDATA: {
			unsigned long tmp;
			int copied;

			copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
			ret = -EIO;
			if (copied != sizeof(tmp))
				goto out;
			ret = put_user(tmp,(unsigned long *) data);
			goto out;
		}

	/* read the word at location addr in the USER area. */
		case PTRACE_PEEKUSR: {
			unsigned long tmp;

			ret = -EIO;
			if ((addr & 3) || addr < 0 || 
			    addr > sizeof(struct user) - 3)
				goto out;

			tmp = 0;  /* Default return condition */
			if(addr < 17*sizeof(long))
				tmp = getreg(child, addr);
#if 0
			if(addr >= (long) &dummy->u_debugreg[0] &&
			   addr <= (long) &dummy->u_debugreg[7]){
				addr -= (long) &dummy->u_debugreg[0];
				addr = addr >> 2;
				tmp = child->thread.debugreg[addr];
			};
#endif
			ret = put_user(tmp,(unsigned long *) data);
			goto out;
		}

      /* when I and D space are separate, this will have to be fixed. */
		case PTRACE_POKETEXT: /* write the word at location addr. */
		case PTRACE_POKEDATA:
			ret = 0;
			if (access_process_vm(child, addr, &data, sizeof(data), 1) == sizeof(data))
				goto out;
			ret = -EIO;
			goto out;

		case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
			ret = -EIO;
			if ((addr & 3) || addr < 0 || 
			    addr > sizeof(struct user) - 3)
				goto out;

			if (addr < 17*sizeof(long)) {
				ret = putreg(child, addr, data);
				goto out;
			}

		  /* We need to be very careful here.  We implicitly
		     want to modify a portion of the task_struct, and we
		     have to be selective about what portions we allow someone
		     to modify. */
#if 0
		  if(addr >= (long) &dummy->u_debugreg[0] &&
		     addr <= (long) &dummy->u_debugreg[7]){

			  if(addr == (long) &dummy->u_debugreg[4]) return -EIO;
			  if(addr == (long) &dummy->u_debugreg[5]) return -EIO;
			  if(addr < (long) &dummy->u_debugreg[4] &&
			     ((unsigned long) data) >= TASK_SIZE-3) return -EIO;
			  
			  ret = -EIO;
			  if(addr == (long) &dummy->u_debugreg[7]) {
				  data &= ~DR_CONTROL_RESERVED;
				  for(i=0; i<4; i++)
					  if ((0x5f54 >> ((data >> (16 + 4*i)) & 0xf)) & 1)
						  goto out;
			  };

			  addr -= (long) &dummy->u_debugreg;
			  addr = addr >> 2;
			  child->thread.debugreg[addr] = data;
			  ret = 0;
			  goto out;
		  };
#endif
		  ret = -EIO;
		  goto out;

		case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
		case PTRACE_CONT: { /* restart after signal. */
			long tmp;

			ret = -EIO;
			if ((unsigned long) data > _NSIG)
				goto out;
			if (request == PTRACE_SYSCALL)
				child->flags |= PF_TRACESYS;
			else
				child->flags &= ~PF_TRACESYS;
			child->exit_code = data;
	/* make sure the single step bit is not set. */
#if 0
			tmp = get_stack_long(child, EFL_OFFSET) & ~TRAP_FLAG;
			put_stack_long(child, EFL_OFFSET,tmp);
#endif
			wake_up_process(child);
			ret = 0;
			goto out;
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
				goto out;
			child->exit_code = SIGKILL;
	/* make sure the single step bit is not set. */
#if 0
			tmp = get_stack_long(child, EFL_OFFSET) & ~TRAP_FLAG;
			put_stack_long(child, EFL_OFFSET, tmp);
#endif
			wake_up_process(child);
			goto out;
		}

		case PTRACE_SINGLESTEP: {  /* set the trap flag. */
			long tmp;

			ret = -EIO;
			if ((unsigned long) data > _NSIG)
				goto out;
			child->flags &= ~PF_TRACESYS;
			if ((child->flags & PF_DTRACE) == 0) {
				/* Spurious delayed TF traps may occur */
				child->flags |= PF_DTRACE;
			}
#if 0
			tmp = get_stack_long(child, EFL_OFFSET) | TRAP_FLAG;
			put_stack_long(child, EFL_OFFSET, tmp);
#endif
			child->exit_code = data;
	/* give it a chance to run. */
			wake_up_process(child);
			ret = 0;
			goto out;
		}

		case PTRACE_DETACH: { /* detach a process that was attached. */
			long tmp;

			ret = -EIO;
			if ((unsigned long) data > _NSIG)
				goto out;
			child->flags &= ~(PF_PTRACED|PF_TRACESYS);
			child->exit_code = data;
			write_lock_irqsave(&tasklist_lock, flags);
			REMOVE_LINKS(child);
			child->p_pptr = child->p_opptr;
			SET_LINKS(child);
			write_unlock_irqrestore(&tasklist_lock, flags);
	/* make sure the single step bit is not set. */
#if 0
			tmp = get_stack_long(child, EFL_OFFSET) & ~TRAP_FLAG;
			put_stack_long(child, EFL_OFFSET, tmp);
#endif
			wake_up_process(child);
			ret = 0;
			goto out;
		}
#if 0
		case PTRACE_GETREGS: { /* Get all gp regs from the child. */
		  	if (!access_ok(VERIFY_WRITE, (unsigned *)data,
				       17*sizeof(long)))
			  {
			    ret = -EIO;
			    goto out;
			  }
			for ( i = 0; i < 17*sizeof(long); i += sizeof(long) )
			  {
			    __put_user(getreg(child, i),(unsigned long *) data);
			    data += sizeof(long);
			  }
			ret = 0;
			goto out;
		  };

		case PTRACE_SETREGS: { /* Set all gp regs in the child. */
			unsigned long tmp;
		  	if (!access_ok(VERIFY_READ, (unsigned *)data,
				       17*sizeof(long)))
			  {
			    ret = -EIO;
			    goto out;
			  }
			for ( i = 0; i < 17*sizeof(long); i += sizeof(long) )
			  {
			    __get_user(tmp, (unsigned long *) data);
			    putreg(child, i, tmp);
			    data += sizeof(long);
			  }
			ret = 0;
			goto out;
		  };

		case PTRACE_GETFPREGS: { /* Get the child FPU state. */
			if (!access_ok(VERIFY_WRITE, (unsigned *)data,
				       sizeof(struct user_i387_struct)))
			  {
			    ret = -EIO;
			    goto out;
			  }
			ret = 0;
			if ( !child->used_math ) {
			  /* Simulate an empty FPU. */
			  child->thread.i387.hard.cwd = 0xffff037f;
			  child->thread.i387.hard.swd = 0xffff0000;
			  child->thread.i387.hard.twd = 0xffffffff;
			}
				__copy_to_user((void *)data, &child->thread.i387.hard,
						sizeof(struct user_i387_struct));
			goto out;
		  };

		case PTRACE_SETFPREGS: { /* Set the child FPU state. */
			if (!access_ok(VERIFY_READ, (unsigned *)data,
				       sizeof(struct user_i387_struct)))
			  {
			    ret = -EIO;
			    goto out;
			  }
			child->used_math = 1;
			  __copy_from_user(&child->thread.i387.hard, (void *)data,
					   sizeof(struct user_i387_struct));
			ret = 0;
			goto out;
		  };
#endif
		default:
			ret = -EIO;
			goto out;
	}
out:
	unlock_kernel();
	return ret;
}

asmlinkage void syscall_trace(void)
{
	if ((current->flags & (PF_PTRACED|PF_TRACESYS))
			!= (PF_PTRACED|PF_TRACESYS))
		return;
	current->exit_code = SIGTRAP;
	current->state = TASK_STOPPED;
	notify_parent(current, SIGCHLD);
	schedule();
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
