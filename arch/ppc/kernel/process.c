/*
 * $Id: process.c,v 1.70 1999/01/07 16:28:59 cort Exp $
 *
 *  linux/arch/ppc/kernel/process.c
 *
 *  Derived from "arch/i386/kernel/process.c"
 *    Copyright (C) 1995  Linus Torvalds
 *
 *  Updated and modified by Cort Dougan (cort@cs.nmt.edu) and
 *  Paul Mackerras (paulus@cs.anu.edu.au)
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/user.h>
#include <linux/elf.h>
#include <linux/elf.h>
#include <linux/init.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/prom.h>

int dump_fpu(struct pt_regs *regs, elf_fpregset_t *fpregs);
void switch_to(struct task_struct *, struct task_struct *);

extern unsigned long _get_SP(void);
extern spinlock_t scheduler_lock;

struct task_struct *last_task_used_math = NULL;

#undef SHOW_TASK_SWITCHES 1
#undef CHECK_STACK 1

unsigned long
kernel_stack_top(struct task_struct *tsk)
{
	return ((unsigned long)tsk) + sizeof(union task_union);
}

unsigned long
task_top(struct task_struct *tsk)
{
	return ((unsigned long)tsk) + sizeof(struct task_struct);
}

static struct vm_area_struct init_mmap = INIT_MMAP;
static struct fs_struct init_fs = INIT_FS;
static struct file * init_fd_array[NR_OPEN] = { NULL, };
static struct files_struct init_files = INIT_FILES;
static struct signal_struct init_signals = INIT_SIGNALS;

struct mm_struct init_mm = INIT_MM;
union task_union init_task_union = { INIT_TASK };

/* only used to get secondary processor up */
struct task_struct *current_set[NR_CPUS] = {&init_task, };

int
dump_fpu(struct pt_regs *regs, elf_fpregset_t *fpregs)
{
#ifdef __SMP__
	if ( regs->msr & MSR_FP )
		smp_giveup_fpu(current);
#else
	if (last_task_used_math == current)
		giveup_fpu();
#endif	
	memcpy(fpregs, &current->tss.fpr[0], sizeof(*fpregs));
	return 1;
}

/* check to make sure the kernel stack is healthy */
int check_stack(struct task_struct *tsk)
{
	unsigned long stack_top = kernel_stack_top(tsk);
	unsigned long tsk_top = task_top(tsk);
	int ret = 0;

#if 0	
	/* check tss magic */
	if ( tsk->tss.magic != TSS_MAGIC )
	{
		ret |= 1;
		printk("tss.magic bad: %08x\n", tsk->tss.magic);
	}
#endif

	if ( !tsk )
		printk("check_stack(): tsk bad tsk %p\n",tsk);
	
	/* check if stored ksp is bad */
	if ( (tsk->tss.ksp > stack_top) || (tsk->tss.ksp < tsk_top) )
	{
		printk("stack out of bounds: %s/%d\n"
		       " tsk_top %08lx ksp %08lx stack_top %08lx\n",
		       tsk->comm,tsk->pid,
		       tsk_top, tsk->tss.ksp, stack_top);
		ret |= 2;
	}
	
	/* check if stack ptr RIGHT NOW is bad */
	if ( (tsk == current) && ((_get_SP() > stack_top ) || (_get_SP() < tsk_top)) )
	{
		printk("current stack ptr out of bounds: %s/%d\n"
		       " tsk_top %08lx sp %08lx stack_top %08lx\n",
		       current->comm,current->pid,
		       tsk_top, _get_SP(), stack_top);
		ret |= 4;
	}

#if 0	
	/* check amount of free stack */
	for ( i = (unsigned long *)task_top(tsk) ; i < kernel_stack_top(tsk) ; i++ )
	{
		if ( !i )
			printk("check_stack(): i = %p\n", i);
		if ( *i != 0 )
		{
			/* only notify if it's less than 900 bytes */
			if ( (i - (unsigned long *)task_top(tsk))  < 900 )
				printk("%d bytes free on stack\n",
				       i - task_top(tsk));
			break;
		}
	}
#endif

	if (ret)
	{
		panic("bad kernel stack");
	}
	return(ret);
}

void
switch_to(struct task_struct *prev, struct task_struct *new)
{
	struct thread_struct *new_tss, *old_tss;
	int s = _disable_interrupts();
#if CHECK_STACK
	check_stack(prev);
	check_stack(new);
#endif

#ifdef SHOW_TASK_SWITCHES
	printk("%s/%d -> %s/%d NIP %08lx cpu %d lock %x root %x/%x\n",
	       prev->comm,prev->pid,
	       new->comm,new->pid,new->tss.regs->nip,new->processor,
	       scheduler_lock.lock,new->fs->root,prev->fs->root);
#endif
#ifdef __SMP__
	/* avoid complexity of lazy save/restore of fpu
	 * by just saving it every time we switch out if
	 * this task used the fpu during the last quantum.
	 * 
	 * If it tries to use the fpu again, it'll trap and
	 * reload its fp regs.
	 *  -- Cort
	 */
	if ( prev->tss.regs->msr & MSR_FP )
		smp_giveup_fpu(prev);

	prev->last_processor = prev->processor;
	current_set[smp_processor_id()] = new;
#endif /* __SMP__ */
	new_tss = &new->tss;
	old_tss = &current->tss;
	_switch(old_tss, new_tss, new->mm->context);
	_enable_interrupts(s);
}

void show_regs(struct pt_regs * regs)
{
	int i;

	printk("NIP: %08lX XER: %08lX LR: %08lX REGS: %p TRAP: %04lx\n",
	       regs->nip, regs->xer, regs->link, regs,regs->trap);
	printk("MSR: %08lx EE: %01x PR: %01x FP: %01x ME: %01x IR/DR: %01x%01x\n",
	       regs->msr, regs->msr&MSR_EE ? 1 : 0, regs->msr&MSR_PR ? 1 : 0,
	       regs->msr & MSR_FP ? 1 : 0,regs->msr&MSR_ME ? 1 : 0,
	       regs->msr&MSR_IR ? 1 : 0,
	       regs->msr&MSR_DR ? 1 : 0);
	printk("TASK = %p[%d] '%s' mm->pgd %p ",
	       current, current->pid, current->comm, current->mm->pgd);
	printk("Last syscall: %ld ", current->tss.last_syscall);
	printk("\nlast math %p", last_task_used_math);
	
#ifdef __SMP__	
	printk(" CPU: %d last CPU: %d", current->processor,current->last_processor);
#endif /* __SMP__ */
	
	printk("\n");
	for (i = 0;  i < 32;  i++)
	{
		long r;
		if ((i % 8) == 0)
		{
			printk("GPR%02d: ", i);
		}

		if ( __get_user(r, &(regs->gpr[i])) )
		    goto out;
		printk("%08lX ", r);
		if ((i % 8) == 7)
		{
			printk("\n");
		}
	}
out:
}

void instruction_dump (unsigned long *pc)
{
	int i;

	if((((unsigned long) pc) & 3))
                return;

	printk("Instruction DUMP:");
	for(i = -3; i < 6; i++)
		printk("%c%08lx%c",i?' ':'<',pc[i],i?' ':'>');
	printk("\n");
}

void exit_thread(void)
{
	if (last_task_used_math == current)
		last_task_used_math = NULL;
}

void flush_thread(void)
{
	if (last_task_used_math == current)
		last_task_used_math = NULL;
}

void
release_thread(struct task_struct *t)
{
}

/*
 * Copy a thread..
 */
int
copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
	    struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs * childregs;
	
	/* Copy registers */
	childregs = ((struct pt_regs *)
		     ((unsigned long)p + sizeof(union task_union)
		      - STACK_FRAME_OVERHEAD)) - 2;
	*childregs = *regs;
	if ((childregs->msr & MSR_PR) == 0)
		childregs->gpr[2] = (unsigned long) p;	/* `current' in new task */
	childregs->gpr[3] = 0;  /* Result from fork() */
	p->tss.ksp = (unsigned long) childregs - STACK_FRAME_OVERHEAD;
	p->tss.regs = childregs;	
	if (usp >= (unsigned long) regs) {
		/* Stack is in kernel space - must adjust */
		childregs->gpr[1] = (unsigned long)(childregs + 1);
	} else {
		/* Provided stack is in user space */
		childregs->gpr[1] = usp;
	}
	p->tss.last_syscall = -1;
	  
	/*
	 * copy fpu info - assume lazy fpu switch now always
	 *  -- Cort
	 */
#ifdef __SMP__
	if ( regs->msr & MSR_FP )
		smp_giveup_fpu(current);
#else	
	if ( last_task_used_math == current )
		giveup_fpu();
#endif	  

	memcpy(&p->tss.fpr, &current->tss.fpr, sizeof(p->tss.fpr));
	p->tss.fpscr = current->tss.fpscr;
	childregs->msr &= ~MSR_FP;

#ifdef __SMP__
	if ( (p->pid != 0) || !(clone_flags & CLONE_PID) )
		p->tss.smp_fork_ret = 1;
	p->last_processor = NO_PROC_ID;
#endif /* __SMP__ */
	return 0;
}

/*
 * XXX ld.so expects the auxiliary table to start on
 * a 16-byte boundary, so we have to find it and
 * move it up. :-(
 */
static inline void shove_aux_table(unsigned long sp)
{
	int argc;
	char *p;
	unsigned long e;
	unsigned long aux_start, offset;

	if (__get_user(argc, (int *)sp))
		return;
	sp += sizeof(int) + (argc + 1) * sizeof(char *);
	/* skip over the environment pointers */
	do {
		if (__get_user(p, (char **)sp))
			return;
		sp += sizeof(char *);
	} while (p != NULL);
	aux_start = sp;
	/* skip to the end of the auxiliary table */
	do {
		if (__get_user(e, (unsigned long *)sp))
			return;
		sp += 2 * sizeof(unsigned long);
	} while (e != AT_NULL);
	offset = ((aux_start + 15) & ~15) - aux_start;
	if (offset != 0) {
		do {
			sp -= sizeof(unsigned long);
			if (__get_user(e, (unsigned long *)sp)
			    || __put_user(e, (unsigned long *)(sp + offset)))
				return;
		} while (sp > aux_start);
	}
}

/*
 * Set up a thread for executing a new program
 */
void start_thread(struct pt_regs *regs, unsigned long nip, unsigned long sp)
{
	set_fs(USER_DS);
	regs->nip = nip;
	regs->gpr[1] = sp;
	regs->msr = MSR_USER;
	shove_aux_table(sp);
	if (last_task_used_math == current)
		last_task_used_math = 0;
	current->tss.fpscr = 0;
}

asmlinkage int sys_clone(int p1, int p2, int p3, int p4, int p5, int p6,
			 struct pt_regs *regs)
{
	unsigned long clone_flags = p1;
	int res;
	lock_kernel();
	res = do_fork(clone_flags, regs->gpr[1], regs);
	/*
	 * only parent returns here, child returns to either
	 * syscall_ret_1() or kernel_thread()
	 * -- Cort
	 */
#ifdef __SMP__
	/* When we clone the idle task we keep the same pid but
	 * the return value of 0 for both causes problems.
	 * -- Cort
	 */
	if ((current->pid == 0) && (current == &init_task))
		res = 1;
#endif /* __SMP__ */
	unlock_kernel();
	return res;
}

asmlinkage int sys_fork(int p1, int p2, int p3, int p4, int p5, int p6,
			struct pt_regs *regs)
{

	int res;
	
	lock_kernel();
	res = do_fork(SIGCHLD, regs->gpr[1], regs);
	/* only parent returns here */
#ifdef __SMP__
	/* When we clone the idle task we keep the same pid but
	 * the return value of 0 for both causes problems.
	 * -- Cort
	 */
	if ((current->pid == 0) && (current == &init_task))
		res = 1;
#endif /* __SMP__ */
	unlock_kernel();
	return res;
}

asmlinkage int sys_execve(unsigned long a0, unsigned long a1, unsigned long a2,
			  unsigned long a3, unsigned long a4, unsigned long a5,
			  struct pt_regs *regs)
{
	int error;
	char * filename;
	lock_kernel();
	filename = getname((char *) a0);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
#ifdef __SMP__	  
	if ( regs->msr & MSR_FP )
		smp_giveup_fpu(current);
#else	  
	if ( last_task_used_math == current )
		giveup_fpu();
#endif	
	error = do_execve(filename, (char **) a1, (char **) a2, regs);
	putname(filename);
out:
	unlock_kernel();

	return error;
}

void
print_backtrace(unsigned long *sp)
{
	int cnt = 0;
	unsigned long i;

	printk("Call backtrace: ");
	while (sp) {
		if (__get_user( i, &sp[1] ))
			break;
		if (cnt++ % 7 == 0)
			printk("\n");
		printk("%08lX ", i);
		if (cnt > 32) break;
		if (__get_user(sp, (unsigned long **)sp))
			break;
	}
	printk("\n");
}

#if 0
/*
 * Low level print for debugging - Cort
 */
__initfunc(int ll_printk(const char *fmt, ...))
{
        va_list args;
	char buf[256];
        int i;

        va_start(args, fmt);
        i=vsprintf(buf,fmt,args);
	ll_puts(buf);
        va_end(args);
        return i;
}

int lines = 24, cols = 80;
int orig_x = 0, orig_y = 0;

void puthex(unsigned long val)
{
	unsigned char buf[10];
	int i;
	for (i = 7;  i >= 0;  i--)
	{
		buf[i] = "0123456789ABCDEF"[val & 0x0F];
		val >>= 4;
	}
	buf[8] = '\0';
	prom_print(buf);
}

__initfunc(void ll_puts(const char *s))
{
	int x,y;
	char *vidmem = (char *)/*(_ISA_MEM_BASE + 0xB8000) */0xD00B8000;
	char c;
	extern int mem_init_done;

	if ( mem_init_done ) /* assume this means we can printk */
	{
		printk(s);
		return;
	}

#if 0	
	if ( have_of )
	{
		prom_print(s);
		return;
	}
#endif

	/*
	 * can't ll_puts on chrp without openfirmware yet.
	 * vidmem just needs to be setup for it.
	 * -- Cort
	 */
	if ( ! is_prep )
		return;
	x = orig_x;
	y = orig_y;

	while ( ( c = *s++ ) != '\0' ) {
		if ( c == '\n' ) {
			x = 0;
			if ( ++y >= lines ) {
				/*scroll();*/
				/*y--;*/
				y = 0;
			}
		} else {
			vidmem [ ( x + cols * y ) * 2 ] = c; 
			if ( ++x >= cols ) {
				x = 0;
				if ( ++y >= lines ) {
					/*scroll();*/
					/*y--;*/
					y = 0;
				}
			}
		}
	}

	orig_x = x;
	orig_y = y;
}
#endif
