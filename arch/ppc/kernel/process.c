/*
 *  linux/arch/ppc/kernel/process.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted for PowerPC by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu) 
 */

/*
 * This file handles the architecture-dependent parts of process handling..
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
#include <linux/a.out.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>

#include <asm/ppc_machine.h>

/*
 * Initial task structure. Make this a per-architecture thing,
 * because different architectures tend to have different
 * alignment requirements and potentially different initial
 * setup.
 */
static unsigned long init_kernel_stack[1024] = { STACK_MAGIC, };
unsigned long init_user_stack[1024] = { STACK_MAGIC, };
static struct vm_area_struct init_mmap = INIT_MMAP;
static struct fs_struct init_fs = INIT_FS;
static struct files_struct init_files = INIT_FILES;
static struct signal_struct init_signals = INIT_SIGNALS;

struct mm_struct init_mm = INIT_MM;
struct task_struct init_task = INIT_TASK;


int dump_fpu(void);
void hard_reset_now(void);
void switch_to(struct task_struct *, struct task_struct *);
void copy_thread(int,unsigned long,unsigned long,struct task_struct *,
		 struct pt_regs *);
void print_backtrace(unsigned long *);

int
dump_fpu(void)
{
  return (1);
}


/* check to make sure the kernel stack is healthy */
int check_stack(struct task_struct *tsk)
{
  extern unsigned long init_kernel_stack[PAGE_SIZE/sizeof(long)];
  int ret = 0;
  int i;
  
  /* skip check in init_kernel_task -- swapper */
  if ( tsk->kernel_stack_page == (unsigned long)&init_kernel_stack )
    return;
  /* check bounds on stack -- above/below kstack page */
  if ( (tsk->tss.ksp-1 & KERNEL_STACK_MASK) != tsk->kernel_stack_page )
  {
    printk("check_stack(): not in bounds %s/%d ksp %x/%x\n",
	   tsk->comm,tsk->pid,tsk->tss.ksp,tsk->kernel_stack_page);
    ret |= 1;
  }

  /* check for magic on kstack */
  if ( *(unsigned long *)(tsk->kernel_stack_page) != STACK_MAGIC)
  {
    printk("check_stack(): no magic %s/%d ksp %x/%x magic %x\n",
	   tsk->comm,tsk->pid,tsk->tss.ksp,tsk->kernel_stack_page,
	   *(unsigned long *)(tsk->kernel_stack_page));
    ret |= 2;
  }

#ifdef KERNEL_STACK_BUFFER
  /* check extra padding page under kernel stack */
  for ( i = PAGE_SIZE/sizeof(long) ; i >= 1; i--)
  {
    struct pt_regs *regs;
    
    if ( *((unsigned long *)(tsk->kernel_stack_page)-1) )
    {
      printk("check_stack(): padding touched %s/%d ksp %x/%x value %x/%d\n",
	     tsk->comm,tsk->pid,tsk->tss.ksp,tsk->kernel_stack_page,
	     *(unsigned long *)(tsk->kernel_stack_page-i),i*sizeof(long));
      regs = (struct pt_regs *)(tsk->kernel_stack_page-(i*sizeof(long)));
      printk("marker %x trap %x\n", regs->marker,regs->trap);
      print_backtrace((unsigned long *)(tsk->tss.ksp));
      
      ret |= 4;
      break;
    }
  }
#endif
  
#if 0
  if (ret)
    panic("bad stack");
#endif
  return(ret);
}


void
switch_to(struct task_struct *prev, struct task_struct *new)
{
	struct pt_regs *regs;
	struct thread_struct *new_tss, *old_tss;
	int s = _disable_interrupts();
	regs = (struct pt_regs *)(new->tss.ksp);
#if 1
	check_stack(prev);
	check_stack(new);
#endif
 	/* if a process has used fp 15 times, then turn
	   on the fpu for good otherwise turn it on with the fp
	   exception handler as needed.
	   skip this for kernel tasks.
	            -- Cort */
	if ( (regs->msr & MSR_FP)&&(regs->msr & MSR_PR)&&(new->tss.fp_used < 15) )
	{
#if 0
	  printk("turning off fpu: %s/%d fp_used %d\n",
		 new->comm,new->pid,new->tss.fp_used);
#endif
	  regs->msr = regs->msr & ~MSR_FP;
	}
#if 0
	printk("%s/%d -> %s/%d\n",prev->comm,prev->pid,new->comm,new->pid);
#endif
	new_tss = &new->tss;
	old_tss = &current->tss;
	current_set[0] = new;
	_switch(old_tss, new_tss);
	_enable_interrupts(s);
}

asmlinkage int sys_debug(unsigned long r3)
{
	lock_kernel();
	if (!strcmp(current->comm,"crashme"))
		printk("sys_debug(): r3 (syscall) %d\n", r3);
	unlock_kernel();
	return 0;
}

asmlinkage int sys_idle(void)
{
	int ret = -EPERM;

	lock_kernel();
	if (current->pid != 0)
		goto out;

	/* endless idle loop with no priority at all */
	current->counter = -100;
	for (;;) {
		schedule();
	}
	ret = 0;
out:
	unlock_kernel();
	return ret;
}

void show_regs(struct pt_regs * regs)
{
}

void exit_thread(void)
{
}

void flush_thread(void)
{
}

void
release_thread(struct task_struct *t)
{
}

/*
 * Copy a thread..
 */
int copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
	struct task_struct * p, struct pt_regs * regs)
{
	int i;
	SEGREG *segs;
	struct pt_regs * childregs;
	
	/* Construct segment registers */
	segs = (SEGREG *)(p->tss.segs);
	for (i = 0;  i < 8;  i++)
	{
		segs[i].ks = 0;
		segs[i].kp = 1;
#if 0
		segs[i].vsid = i | (nr << 4);
#else
		segs[i].vsid = i | ((nr * 10000) << 4);		
#endif
	}
	if ((p->mm->context == 0) || (p->mm->count == 1))
	{
#if 0
	  p->mm->context = ((nr)<<4);
#else	  
	  p->mm->context = ((nr*10000)<<4);	  
#endif
	}
	
	/* Last 8 are shared with kernel & everybody else... */
	for (i = 8;  i < 16;  i++)
	{
		segs[i].ks = 0;
		segs[i].kp = 1;
		segs[i].vsid = i;
	}
	
	/* Copy registers */
	childregs = ((struct pt_regs *) (p->kernel_stack_page + PAGE_SIZE)) - 2;

	*childregs = *regs;	/* STRUCT COPY */
	childregs->gpr[3] = 0;  /* Result from fork() */
	p->tss.ksp = (unsigned long)(childregs);
	if (usp >= (unsigned long)regs)
	{ /* Stack is in kernel space - must adjust */
		childregs->gpr[1] = (long)(childregs+1);
	} else
	{ /* Provided stack is in user space */
		childregs->gpr[1] = usp;
	}
	p->tss.fp_used = 0;

	return 0;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
}


asmlinkage int sys_fork(int p1, int p2, int p3, int p4, int p5, int p6, struct pt_regs *regs)
{
	int ret;

	lock_kernel();
	ret = do_fork(SIGCHLD, regs->gpr[1], regs);
	unlock_kernel();
	return ret;
}

asmlinkage int sys_execve(unsigned long a0, unsigned long a1, unsigned long a2,
	unsigned long a3, unsigned long a4, unsigned long a5,
	struct pt_regs *regs)
{
	int error;
	char * filename;

	lock_kernel();
	/* getname does it's own verification of the address
	   when it calls get_max_filename() but
	   it will assume it's valid if get_fs() == KERNEL_DS
	   which is always true on the ppc so we check
	   it here
	   
	   this doesn't completely check any of these data structures,
	   it just makes sure that the 1st long is in a good area
	   and from there we assume that it's safe then
	   -- Cort
	   */
	/* works now since get_fs/set_fs work properly */
#if 0
	if ( verify_area(VERIFY_READ,(void *)a0,1)
	     && verify_area(VERIFY_READ,(void *)a1,1)
	     && verify_area(VERIFY_READ,(void *)a2,1)
	     )
	{
	  return -EFAULT;
	}
#endif
	error = getname((char *) a0, &filename);
	if (error)
		goto out;
	flush_instruction_cache();
	error = do_execve(filename, (char **) a1, (char **) a2, regs);
#if 0
if (error)
{	
printk("EXECVE - file = '%s', error = %d\n", filename, error);
}
#endif
	putname(filename);
out:
	unlock_kernel();
	return error;
}

asmlinkage int sys_clone(int p1, int p2, int p3, int p4, int p5, int p6, struct pt_regs *regs)
{
	unsigned long clone_flags = p1;
	int res;

	lock_kernel();
	res = do_fork(clone_flags, regs->gpr[1], regs);
	unlock_kernel();
	return res;
}

void
print_backtrace(unsigned long *sp)
{
#if 0
	int cnt = 0;
	printk("... Call backtrace:\n");
	while (verify_area(VERIFY_READ,sp,sizeof(long)) && *sp)
	{
		printk("%08X ", sp[1]);
		sp = (unsigned long *)*sp;
		if (++cnt == 8)
		{
			printk("\n");
		}
		if (cnt > 32) break;
	}
	printk("\n");
#endif
}

void
print_user_backtrace(unsigned long *sp)
{
#if 0
	int cnt = 0;
	printk("... [User] Call backtrace:\n");
	while (verify_area(VERIFY_READ,sp,sizeof(long)) && *sp)
	{
		printk("%08X ", sp[1]);
		sp = (unsigned long *)*sp;
		if (++cnt == 8)
		{
			printk("\n");
		}
		if (cnt > 16) break;
	}
	printk("\n");
#endif
}

void
print_kernel_backtrace(void)
{
#if 0
	unsigned long *_get_SP(void);
	print_backtrace(_get_SP());
#endif
}
inline void start_thread(struct pt_regs * regs,
                         unsigned long eip, unsigned long esp)
{
	regs->nip = eip;
	regs->gpr[1] = esp;
	regs->msr = MSR_USER;
	set_fs(USER_DS);
}

