/*
 *  linux/arch/x86-64/kernel/process.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 * 
 *  X86-64 port
 *	Andi Kleen.
 * 
 *  $Id: process.c,v 1.38 2002/01/15 10:08:03 ak Exp $
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#define __KERNEL_SYSCALLS__
#include <stdarg.h>

#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/interrupt.h>
#include <linux/config.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/slab.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/ldt.h>
#include <asm/processor.h>
#include <asm/i387.h>
#include <asm/desc.h>
#include <asm/mmu_context.h>
#include <asm/pda.h>
#include <asm/prctl.h>
#include <asm/kdebug.h>

#include <linux/irq.h>

asmlinkage extern void ret_from_fork(void);

unsigned long kernel_thread_flags = CLONE_VM;

int hlt_counter;

/*
 * Powermanagement idle function, if any..
 */
void (*pm_idle)(void);

/*
 * Power off function, if any
 */
void (*pm_power_off)(void);

void disable_hlt(void)
{
	hlt_counter++;
}

void enable_hlt(void)
{
	hlt_counter--;
}

/*
 * We use this if we don't have any better
 * idle routine..
 */
void default_idle(void)
{
	if (!hlt_counter) {
		__cli();
		if (!need_resched())
			safe_halt();
		else
			__sti();
	}
}

/*
 * On SMP it's slightly faster (but much more power-consuming!)
 * to poll the ->need_resched flag instead of waiting for the
 * cross-CPU IPI to arrive. Use this option with caution.
 */
static void poll_idle (void)
{
	int oldval;

	__sti();

	/*
	 * Deal with another CPU just having chosen a thread to
	 * run here:
	 */
	oldval = test_and_clear_thread_flag(TIF_NEED_RESCHED);

	if (!oldval) {
		set_thread_flag(TIF_POLLING_NRFLAG); 
		asm volatile(
			"2:"
			"testl %0,%1;"
			"rep; nop;"
			"je 2b;"
			: :
			"i" (_TIF_NEED_RESCHED), 
			"m" (current_thread_info()->flags));
	} else {
		set_need_resched();
	}
}

/*
 * The idle thread. There's no useful work to be
 * done, so just try to conserve power and have a
 * low exit latency (ie sit in a loop waiting for
 * somebody to say that they'd like to reschedule)
 */
void cpu_idle (void)
{
	/* endless idle loop with no priority at all */
	while (1) {
		void (*idle)(void) = pm_idle;
		if (!idle)
			idle = default_idle;
		while (!need_resched())
			idle();
		schedule();
	}
}

static int __init idle_setup (char *str)
{
	if (!strncmp(str, "poll", 4)) {
		printk("using polling idle threads.\n");
		pm_idle = poll_idle;
	}

	return 1;
}

__setup("idle=", idle_setup);

static long no_idt[3];
static int reboot_mode;

#ifdef CONFIG_SMP
int reboot_smp = 0;
static int reboot_cpu = -1;
#endif
static int __init reboot_setup(char *str)
{
	while(1) {
		switch (*str) {
		case 'w': /* "warm" reboot (no memory testing etc) */
			reboot_mode = 0x1234;
			break;
		case 'c': /* "cold" reboot (with memory testing etc) */
			reboot_mode = 0x0;
			break;
#ifdef CONFIG_SMP
		case 's': /* "smp" reboot by executing reset on BSP or other CPU*/
			reboot_smp = 1;
			if (isdigit(str[1]))
				sscanf(str+1, "%d", &reboot_cpu);		
			else if (!strncmp(str,"smp",3))
				sscanf(str+3, "%d", &reboot_cpu); 
				/* we will leave sorting out the final value 
				when we are ready to reboot, since we might not
 				have set up boot_cpu_id or smp_num_cpu */
			break;
#endif
		}
		if((str = strchr(str,',')) != NULL)
			str++;
		else
			break;
	}
	return 1;
}

__setup("reboot=", reboot_setup);

static inline void kb_wait(void)
{
	int i;

	for (i=0; i<0x10000; i++)
		if ((inb_p(0x64) & 0x02) == 0)
			break;
}

void machine_restart(char * __unused)
{
#if CONFIG_SMP
	int cpuid;
	
	cpuid = GET_APIC_ID(apic_read(APIC_ID));

	if (reboot_smp) {

		/* check to see if reboot_cpu is valid 
		   if its not, default to the BSP */
		if ((reboot_cpu == -1) ||  
		      (reboot_cpu > (NR_CPUS -1))  || 
		      !(phys_cpu_present_map & (1<<cpuid))) 
			reboot_cpu = boot_cpu_id;

		reboot_smp = 0;  /* use this as a flag to only go through this once*/
		/* re-run this function on the other CPUs
		   it will fall though this section since we have 
		   cleared reboot_smp, and do the reboot if it is the
		   correct CPU, otherwise it halts. */
		if (reboot_cpu != cpuid)
			smp_call_function((void *)machine_restart , NULL, 1, 0);
	}

	/* if reboot_cpu is still -1, then we want a tradional reboot, 
	   and if we are not running on the reboot_cpu,, halt */
	if ((reboot_cpu != -1) && (cpuid != reboot_cpu)) {
		for (;;)
		__asm__ __volatile__ ("hlt");
	}
	/*
	 * Stop all CPUs and turn off local APICs and the IO-APIC, so
	 * other OSs see a clean IRQ state.
	 */
	if (notify_die(DIE_STOP,"cpustop",0,0) != NOTIFY_BAD)
	smp_send_stop();
	disable_IO_APIC();
#endif

		/* rebooting needs to touch the page at absolute addr 0 */
		*((unsigned short *)__va(0x472)) = reboot_mode;
		for (;;) {
			int i;
		/* First fondle with the keyboard controller. */ 
			for (i=0; i<100; i++) {
				kb_wait();
				udelay(50);
				outb(0xfe,0x64);         /* pulse reset low */
				udelay(50);
			}
			/* That didn't work - force a triple fault.. */
			__asm__ __volatile__("lidt %0": :"m" (no_idt));
			__asm__ __volatile__("int3");
		}

	/* Could do reset through the northbridge of Hammer here. */
}

void machine_halt(void)
{
}

void machine_power_off(void)
{
	if (pm_power_off)
		pm_power_off();
}

/* Prints also some state that isn't saved in the pt_regs */ 
void show_regs(struct pt_regs * regs)
{
	unsigned long cr0 = 0L, cr2 = 0L, cr3 = 0L, cr4 = 0L, fs, gs, shadowgs;
	unsigned int fsindex,gsindex;
	unsigned int ds,cs,es; 

	printk("\n");
	printk("Pid: %d, comm: %.20s %s\n", current->pid, current->comm, print_tainted());
	printk("RIP: %04lx:[<%016lx>]\n", regs->cs & 0xffff, regs->rip);
	printk("RSP: %016lx  EFLAGS: %08lx\n", regs->rsp, regs->eflags);
	printk("RAX: %016lx RBX: %016lx RCX: %016lx\n",
	       regs->rax, regs->rbx, regs->rcx);
	printk("RDX: %016lx RSI: %016lx RDI: %016lx\n",
	       regs->rdx, regs->rsi, regs->rdi); 
	printk("RBP: %016lx R08: %016lx R09: %016lx\n",
	       regs->rbp, regs->r8, regs->r9); 
	printk("R10: %016lx R11: %016lx R12: %016lx\n",
	       regs->r10, regs->r11, regs->r12); 
	printk("R13: %016lx R14: %016lx R15: %016lx\n",
	       regs->r13, regs->r14, regs->r15); 

	asm("movl %%ds,%0" : "=r" (ds)); 
	asm("movl %%cs,%0" : "=r" (cs)); 
	asm("movl %%es,%0" : "=r" (es)); 
	asm("movl %%fs,%0" : "=r" (fsindex));
	asm("movl %%gs,%0" : "=r" (gsindex));

	rdmsrl(MSR_FS_BASE, fs);
	rdmsrl(MSR_GS_BASE, gs); 
	rdmsrl(MSR_KERNEL_GS_BASE, shadowgs); 

	asm("movq %%cr0, %0": "=r" (cr0));
	asm("movq %%cr2, %0": "=r" (cr2));
	asm("movq %%cr3, %0": "=r" (cr3));
	asm("movq %%cr4, %0": "=r" (cr4));

	printk("FS:  %016lx(%04x) GS:%016lx(%04x) knlGS:%016lx\n", 
	       fs,fsindex,gs,gsindex,shadowgs); 
	printk("CS:  %04x DS: %04x ES: %04x CR0: %016lx\n", cs, ds, es, cr0); 
	printk("CR2: %016lx CR3: %016lx CR4: %016lx\n", cr2, cr3, cr4);
}

extern void load_gs_index(unsigned);

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
	struct task_struct *me = current;
	if (me->thread.io_bitmap_ptr) { 
		kfree(me->thread.io_bitmap_ptr); 
		me->thread.io_bitmap_ptr = NULL;
		(init_tss + smp_processor_id())->io_map_base = 
			INVALID_IO_BITMAP_OFFSET;
	}
}

void flush_thread(void)
{
	struct task_struct *tsk = current;

	memset(tsk->thread.debugreg, 0, sizeof(unsigned long)*8);
	/*
	 * Forget coprocessor state..
	 */
	clear_fpu(tsk);
	tsk->used_math = 0;
}

void release_thread(struct task_struct *dead_task)
{
	if (dead_task->mm) {
		if (dead_task->mm->context.size) {
			printk("WARNING: dead process %8s still has LDT? <%p/%d>\n",
					dead_task->comm,
					dead_task->mm->context.ldt,
					dead_task->mm->context.size);
			BUG();
		}
	}
}

int copy_thread(int nr, unsigned long clone_flags, unsigned long rsp, 
		unsigned long unused,
	struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs * childregs;
	struct task_struct *me = current;

	childregs = ((struct pt_regs *) (THREAD_SIZE + (unsigned long) p->thread_info)) - 1;

	*childregs = *regs;

	childregs->rax = 0;
	childregs->rsp = rsp;
	if (rsp == ~0) {
		childregs->rsp = (unsigned long)childregs;
	}

	p->thread.rsp = (unsigned long) childregs;
	p->thread.rsp0 = (unsigned long) (childregs+1);
	p->thread.userrsp = current->thread.userrsp; 

	p->thread.rip = (unsigned long) ret_from_fork;

	p->thread.fs = me->thread.fs;
	p->thread.gs = me->thread.gs;

	asm("movl %%gs,%0" : "=m" (p->thread.gsindex));
	asm("movl %%fs,%0" : "=m" (p->thread.fsindex));
	asm("movl %%es,%0" : "=m" (p->thread.es));
	asm("movl %%ds,%0" : "=m" (p->thread.ds));

	unlazy_fpu(current);	
	p->thread.i387 = current->thread.i387;

	if (unlikely(me->thread.io_bitmap_ptr != NULL)) { 
		p->thread.io_bitmap_ptr = kmalloc((IO_BITMAP_SIZE+1)*4, GFP_KERNEL);
		if (!p->thread.io_bitmap_ptr) 
			return -ENOMEM;
		memcpy(p->thread.io_bitmap_ptr, me->thread.io_bitmap_ptr, 
		       (IO_BITMAP_SIZE+1)*4);
	} 

	return 0;
}

/*
 * This special macro can be used to load a debugging register
 */
#define loaddebug(thread,register) \
		set_debug(thread->debugreg[register], register)

/*
 *	switch_to(x,y) should switch tasks from x to y.
 *
 * We fsave/fwait so that an exception goes off at the right time
 * (as a call from the fsave or fwait in effect) rather than to
 * the wrong process. 
 * 
 * This could still be optimized: 
 * - fold all the options into a flag word and test it with a single test.
 * - could test fs/gs bitsliced
 */
void __switch_to(struct task_struct *prev_p, struct task_struct *next_p)
{
	struct thread_struct *prev = &prev_p->thread,
				 *next = &next_p->thread;
	struct tss_struct *tss = init_tss + smp_processor_id();


	unlazy_fpu(prev_p);

	/*
	 * Reload esp0, LDT and the page table pointer:
	 */
	tss->rsp0 = next->rsp0;

	/* 
	 * Switch DS and ES.
	 */
	asm volatile("movl %%es,%0" : "=m" (prev->es)); 
	if (unlikely(next->es | prev->es))
		loadsegment(es, next->es); 
	
	asm volatile ("movl %%ds,%0" : "=m" (prev->ds)); 
	if (unlikely(next->ds | prev->ds))
		loadsegment(ds, next->ds);

	/* 
	 * Switch FS and GS.
	 * XXX Check if this is safe on SMP (!= -> |)
	 */
	{ 
		unsigned int fsindex;

		asm volatile("movl %%fs,%0" : "=g" (fsindex)); 
		if (unlikely(fsindex != next->fsindex)) /* or likely? */
			loadsegment(fs, next->fsindex);
		if (unlikely(fsindex != prev->fsindex))
			prev->fs = 0;				
		if ((fsindex != prev->fsindex) || (prev->fs != next->fs))
			wrmsrl(MSR_FS_BASE, next->fs); 
		prev->fsindex = fsindex;
	}
	{
		unsigned int gsindex;

		asm volatile("movl %%gs,%0" : "=g" (gsindex)); 
		if (unlikely(gsindex != next->gsindex))
			load_gs_index(next->gs); 
		if (unlikely(gsindex != prev->gsindex)) 
			prev->gs = 0;				
		if (gsindex != prev->gsindex || prev->gs != next->gs)
			wrmsrl(MSR_KERNEL_GS_BASE, next->gs); 
		prev->gsindex = gsindex;
	}

	/* 
	 * Switch the PDA context.
	 */
	prev->userrsp = read_pda(oldrsp); 
	write_pda(oldrsp, next->userrsp); 
	write_pda(pcurrent, next_p); 
	write_pda(kernelstack, (unsigned long)next_p->thread_info + THREAD_SIZE - PDA_STACKOFFSET);

	/*
	 * Now maybe reload the debug registers
	 */
	if (unlikely(next->debugreg[7])) {
		loaddebug(next, 0);
		loaddebug(next, 1);
		loaddebug(next, 2);
		loaddebug(next, 3);
		/* no 4 and 5 */
		loaddebug(next, 6);
		loaddebug(next, 7);
	}


	/* 
	 * Handle the IO bitmap 
	 */ 
	if (unlikely(prev->io_bitmap_ptr || next->io_bitmap_ptr)) {
		if (next->io_bitmap_ptr) {
			/*
			 * 4 cachelines copy ... not good, but not that
			 * bad either. Anyone got something better?
			 * This only affects processes which use ioperm().
			 */
			memcpy(tss->io_bitmap, next->io_bitmap_ptr,
				 IO_BITMAP_SIZE*sizeof(u32));
			tss->io_map_base = IO_BITMAP_OFFSET;
		} else {
			/*
			 * a bitmap offset pointing outside of the TSS limit
			 * causes a nicely controllable SIGSEGV if a process
			 * tries to use a port IO instruction. The first
			 * sys_ioperm() call sets up the bitmap properly.
			 */
			tss->io_map_base = INVALID_IO_BITMAP_OFFSET;
		}
	}
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage 
long sys_execve(char *name, char **argv,char **envp, struct pt_regs regs)
{
	long error;
	char * filename;

	filename = getname(name);
	error = PTR_ERR(filename);
	if (IS_ERR(filename)) 
		return error;
	error = do_execve(filename, argv, envp, &regs); 
	if (error == 0)
		current->ptrace &= ~PT_DTRACE;
	putname(filename);
	return error;
}

void set_personality_64bit(void)
{
	/* inherit personality from parent */

	/* Make sure to be in 64bit mode */
	clear_thread_flag(TIF_IA32); 
}

asmlinkage long sys_fork(struct pt_regs regs)
{
	struct task_struct *p;
	p = do_fork(SIGCHLD, regs.rsp, &regs, 0);
	return IS_ERR(p) ? PTR_ERR(p) : p->pid;
}

asmlinkage long sys_clone(unsigned long clone_flags, unsigned long newsp, struct pt_regs regs)
{
	struct task_struct *p;
	if (!newsp)
		newsp = regs.rsp;
	p = do_fork(clone_flags & ~CLONE_IDLETASK, newsp, &regs, 0);
	return IS_ERR(p) ? PTR_ERR(p) : p->pid;
}

/*
 * This is trivial, and on the face of it looks like it
 * could equally well be done in user mode.
 *
 * Not so, for quite unobvious reasons - register pressure.
 * In user mode vfork() cannot have a stack frame, and if
 * done by calling the "clone()" system call directly, you
 * do not have enough call-clobbered registers to hold all
 * the information you need.
 */
asmlinkage long sys_vfork(struct pt_regs regs)
{
	struct task_struct *p;
	p = do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs.rsp, &regs, 0);
	return IS_ERR(p) ? PTR_ERR(p) : p->pid;
}

/*
 * These bracket the sleeping functions..
 */
extern void scheduling_functions_start_here(void);
extern void scheduling_functions_end_here(void);
#define first_sched	((unsigned long) scheduling_functions_start_here)
#define last_sched	((unsigned long) scheduling_functions_end_here)

unsigned long get_wchan(struct task_struct *p)
{
	u64 fp,rip;
	int count = 0;

	if (!p || p == current || p->state==TASK_RUNNING)
		return 0; 
	if (p->thread.rsp < (u64)p || p->thread.rsp > (u64)p + THREAD_SIZE)
		return 0;
	fp = *(u64 *)(p->thread.rsp);
	do { 
		if (fp < (unsigned long)p || fp > (unsigned long)p+THREAD_SIZE)
			return 0; 
		rip = *(u64 *)(fp+8); 
		if (rip < first_sched || rip >= last_sched)
			return rip; 
		fp = *(u64 *)fp; 
	} while (count++ < 16); 
	return 0;
}
#undef last_sched
#undef first_sched

asmlinkage int sys_arch_prctl(int code, unsigned long addr)
{ 
	int ret = 0; 
	unsigned long tmp; 

	switch (code) { 
	case ARCH_SET_GS:
		if (addr >= TASK_SIZE) 
			return -EPERM; 
		asm volatile("movw %%gs,%0" : "=g" (current->thread.gsindex)); 
		current->thread.gs = addr;
		ret = checking_wrmsrl(MSR_KERNEL_GS_BASE, addr); 
		break;
	case ARCH_SET_FS:
		/* Not strictly needed for fs, but do it for symmetry
		   with gs */
		if (addr >= TASK_SIZE)
			return -EPERM; 
		asm volatile("movw %%fs,%0" : "=g" (current->thread.fsindex)); 
		current->thread.fs = addr;
		ret = checking_wrmsrl(MSR_FS_BASE, addr); 
		break;

		/* Returned value may not be correct when the user changed fs/gs */ 
	case ARCH_GET_FS:
		rdmsrl(MSR_FS_BASE, tmp);
		ret = put_user(tmp, (unsigned long *)addr); 
		break; 

	case ARCH_GET_GS: 
		rdmsrl(MSR_KERNEL_GS_BASE, tmp); 
		ret = put_user(tmp, (unsigned long *)addr); 
		break;

	default:
		ret = -EINVAL;
		break;
	} 
	return ret;	
} 

