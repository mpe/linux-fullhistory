/*
 *  linux/arch/i386/kernel/process.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#define __KERNEL_SYSCALLS__
#include <stdarg.h>

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
#include <linux/vmalloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/interrupt.h>
#include <linux/config.h>
#include <linux/unistd.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/reboot.h>
#include <linux/init.h>
#if defined(CONFIG_APM) && defined(CONFIG_APM_POWER_OFF)
#include <linux/apm_bios.h>
#endif

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/ldt.h>
#include <asm/processor.h>
#ifdef CONFIG_MATH_EMULATION
#include <asm/math_emu.h>
#endif
#include "irq.h"

spinlock_t semaphore_wake_lock = SPIN_LOCK_UNLOCKED;

struct task_struct *last_task_used_math = NULL;

#ifdef __SMP__
asmlinkage void ret_from_smpfork(void) __asm__("ret_from_smpfork");
#else
asmlinkage void ret_from_sys_call(void) __asm__("ret_from_sys_call");
#endif

#ifdef CONFIG_APM
extern int  apm_do_idle(void);
extern void apm_do_busy(void);
#endif

static int hlt_counter=0;

#define HARD_IDLE_TIMEOUT (HZ / 3)

void disable_hlt(void)
{
	hlt_counter++;
}

void enable_hlt(void)
{
	hlt_counter--;
}

#ifndef __SMP__

static void hard_idle(void)
{
	while (!need_resched) {
		if (boot_cpu_data.hlt_works_ok && !hlt_counter) {
#ifdef CONFIG_APM
				/* If the APM BIOS is not enabled, or there
				 is an error calling the idle routine, we
				 should hlt if possible.  We need to check
				 need_resched again because an interrupt
				 may have occurred in apm_do_idle(). */
			start_bh_atomic();
			if (!apm_do_idle() && !need_resched)
				__asm__("hlt");
			end_bh_atomic();
#else
			__asm__("hlt");
#endif
	        }
 		if (need_resched) 
 			break;
		schedule();
	}
#ifdef CONFIG_APM
	apm_do_busy();
#endif
}

/*
 * The idle loop on a uniprocessor i386..
 */
 
asmlinkage int sys_idle(void)
{
        unsigned long start_idle = 0;
	int ret = -EPERM;

	lock_kernel();
	if (current->pid != 0)
		goto out;
	/* endless idle loop with no priority at all */
	current->priority = -100;
	current->counter = -100;
	for (;;) {
		/*
		 *	We are locked at this point. So we can safely call
		 *	the APM bios knowing only one CPU at a time will do
		 *	so.
		 */
		if (!start_idle) 
			start_idle = jiffies;
		if (jiffies - start_idle > HARD_IDLE_TIMEOUT) 
			hard_idle();
		else  {
			if (boot_cpu_data.hlt_works_ok && !hlt_counter && !need_resched)
		        	__asm__("hlt");
		}
		run_task_queue(&tq_scheduler);
		if (need_resched) 
			start_idle = 0;
		schedule();
	}
	ret = 0;
out:
	unlock_kernel();
	return ret;
}

#else

/*
 *	This is being executed in task 0 'user space'.
 */

int cpu_idle(void *unused)
{
	current->priority = -100;
	while(1)
	{
		if(current_cpu_data.hlt_works_ok &&
		 		!hlt_counter && !need_resched)
			__asm("hlt");
		/*
		 * tq_scheduler currently assumes we're running in a process
		 * context (ie that we hold the kernel lock..)
		 */
		if (tq_scheduler) {
			lock_kernel();
			run_task_queue(&tq_scheduler);
			unlock_kernel();
		}
		/* endless idle loop with no priority at all */
		current->counter = -100;
		schedule();
	}
}

asmlinkage int sys_idle(void)
{
	if (current->pid != 0)
		return -EPERM;
	cpu_idle(NULL);
	return 0;
}

#endif

/*
 * This routine reboots the machine by asking the keyboard
 * controller to pulse the reset-line low. We try that for a while,
 * and if it doesn't work, we do some other stupid things.
 */

static long no_idt[2] = {0, 0};
static int reboot_mode = 0;
static int reboot_thru_bios = 0;

__initfunc(void reboot_setup(char *str, int *ints))
{
	while(1) {
		switch (*str) {
		case 'w': /* "warm" reboot (no memory testing etc) */
			reboot_mode = 0x1234;
			break;
		case 'c': /* "cold" reboot (with memory testing etc) */
			reboot_mode = 0x0;
			break;
		case 'b': /* "bios" reboot by jumping through the BIOS */
			reboot_thru_bios = 1;
			break;
		case 'h': /* "hard" reboot by toggling RESET and/or crashing the CPU */
			reboot_thru_bios = 0;
			break;
		}
		if((str = strchr(str,',')) != NULL)
			str++;
		else
			break;
	}
}


/* The following code and data reboots the machine by switching to real
   mode and jumping to the BIOS reset entry point, as if the CPU has
   really been reset.  The previous version asked the keyboard
   controller to pulse the CPU reset line, which is more thorough, but
   doesn't work with at least one type of 486 motherboard.  It is easy
   to stop this code working; hence the copious comments. */

static unsigned long long
real_mode_gdt_entries [3] =
{
	0x0000000000000000ULL,	/* Null descriptor */
	0x00009a000000ffffULL,	/* 16-bit real-mode 64k code at 0x00000000 */
	0x000092000100ffffULL	/* 16-bit real-mode 64k data at 0x00000100 */
};

static struct
{
	unsigned short       size __attribute__ ((packed));
	unsigned long long * base __attribute__ ((packed));
}
real_mode_gdt = { sizeof (real_mode_gdt_entries) - 1, real_mode_gdt_entries },
real_mode_idt = { 0x3ff, 0 };

/* This is 16-bit protected mode code to disable paging and the cache,
   switch to real mode and jump to the BIOS reset code.

   The instruction that switches to real mode by writing to CR0 must be
   followed immediately by a far jump instruction, which set CS to a
   valid value for real mode, and flushes the prefetch queue to avoid
   running instructions that have already been decoded in protected
   mode.

   Clears all the flags except ET, especially PG (paging), PE
   (protected-mode enable) and TS (task switch for coprocessor state
   save).  Flushes the TLB after paging has been disabled.  Sets CD and
   NW, to disable the cache on a 486, and invalidates the cache.  This
   is more like the state of a 486 after reset.  I don't know if
   something else should be done for other chips.

   More could be done here to set up the registers as if a CPU reset had
   occurred; hopefully real BIOSes don't assume much. */

static unsigned char real_mode_switch [] =
{
	0x66, 0x0f, 0x20, 0xc0,			/*    movl  %cr0,%eax        */
	0x66, 0x83, 0xe0, 0x11,			/*    andl  $0x00000011,%eax */
	0x66, 0x0d, 0x00, 0x00, 0x00, 0x60,	/*    orl   $0x60000000,%eax */
	0x66, 0x0f, 0x22, 0xc0,			/*    movl  %eax,%cr0        */
	0x66, 0x0f, 0x22, 0xd8,			/*    movl  %eax,%cr3        */
	0x66, 0x0f, 0x20, 0xc3,			/*    movl  %cr0,%ebx        */
	0x66, 0x81, 0xe3, 0x00, 0x00, 0x00, 0x60,	/*    andl  $0x60000000,%ebx */
	0x74, 0x02,				/*    jz    f                */
	0x0f, 0x08,				/*    invd                   */
	0x24, 0x10,				/* f: andb  $0x10,al         */
	0x66, 0x0f, 0x22, 0xc0,			/*    movl  %eax,%cr0        */
	0xea, 0x00, 0x00, 0xff, 0xff		/*    ljmp  $0xffff,$0x0000  */
};

static inline void kb_wait(void)
{
	int i;

	for (i=0; i<0x10000; i++)
		if ((inb_p(0x64) & 0x02) == 0)
			break;
}

void machine_restart(char * __unused)
{
#if __SMP__
	/*
	 * turn off the IO-APIC, so we can do a clean reboot
	 */
	init_pic_mode();
#endif

	if(!reboot_thru_bios) {
		/* rebooting needs to touch the page at absolute addr 0 */
		*((unsigned short *)__va(0x472)) = reboot_mode;
		for (;;) {
			int i;
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
	}

	cli();

	/* Write zero to CMOS register number 0x0f, which the BIOS POST
	   routine will recognize as telling it to do a proper reboot.  (Well
	   that's what this book in front of me says -- it may only apply to
	   the Phoenix BIOS though, it's not clear).  At the same time,
	   disable NMIs by setting the top bit in the CMOS address register,
	   as we're about to do peculiar things to the CPU.  I'm not sure if
	   `outb_p' is needed instead of just `outb'.  Use it to be on the
	   safe side. */

	outb_p (0x8f, 0x70);
	outb_p (0x00, 0x71);

	/* Remap the kernel at virtual address zero, as well as offset zero
	   from the kernel segment.  This assumes the kernel segment starts at
	   virtual address PAGE_OFFSET. */

	memcpy (swapper_pg_dir, swapper_pg_dir + USER_PGD_PTRS,
		sizeof (swapper_pg_dir [0]) * KERNEL_PGD_PTRS);

	/* Make sure the first page is mapped to the start of physical memory.
	   It is normally not mapped, to trap kernel NULL pointer dereferences. */

	pg0[0] = 7;

	/*
	 * Use `swapper_pg_dir' as our page directory.  We bother with
	 * `SET_PAGE_DIR' because although might be rebooting, but if we change
	 * the way we set root page dir in the future, then we wont break a
	 * seldom used feature ;)
	 */

	SET_PAGE_DIR(current,swapper_pg_dir);

	/* Write 0x1234 to absolute memory location 0x472.  The BIOS reads
	   this on booting to tell it to "Bypass memory test (also warm
	   boot)".  This seems like a fairly standard thing that gets set by
	   REBOOT.COM programs, and the previous reset routine did this
	   too. */

	*((unsigned short *)0x472) = reboot_mode;

	/* For the switch to real mode, copy some code to low memory.  It has
	   to be in the first 64k because it is running in 16-bit mode, and it
	   has to have the same physical and virtual address, because it turns
	   off paging.  Copy it near the end of the first page, out of the way
	   of BIOS variables. */

	memcpy ((void *) (0x1000 - sizeof (real_mode_switch)),
		real_mode_switch, sizeof (real_mode_switch));

	/* Set up the IDT for real mode. */

	__asm__ __volatile__ ("lidt %0" : : "m" (real_mode_idt));

	/* Set up a GDT from which we can load segment descriptors for real
	   mode.  The GDT is not used in real mode; it is just needed here to
	   prepare the descriptors. */

	__asm__ __volatile__ ("lgdt %0" : : "m" (real_mode_gdt));

	/* Load the data segment registers, and thus the descriptors ready for
	   real mode.  The base address of each segment is 0x100, 16 times the
	   selector value being loaded here.  This is so that the segment
	   registers don't have to be reloaded after switching to real mode:
	   the values are consistent for real mode operation already. */

	__asm__ __volatile__ ("movl $0x0010,%%eax\n"
				"\tmovl %%ax,%%ds\n"
				"\tmovl %%ax,%%es\n"
				"\tmovl %%ax,%%fs\n"
				"\tmovl %%ax,%%gs\n"
				"\tmovl %%ax,%%ss" : : : "eax");

	/* Jump to the 16-bit code that we copied earlier.  It disables paging
	   and the cache, switches to real mode, and jumps to the BIOS reset
	   entry point. */

	__asm__ __volatile__ ("ljmp $0x0008,%0"
				:
				: "i" ((void *) (0x1000 - sizeof (real_mode_switch))));
}

void machine_halt(void)
{
}

void machine_power_off(void)
{
#if defined(CONFIG_APM) && defined(CONFIG_APM_POWER_OFF)
	apm_set_power_state(APM_STATE_OFF);
#endif
}


void show_regs(struct pt_regs * regs)
{
	printk("\n");
	printk("EIP: %04x:[<%08lx>]",0xffff & regs->xcs,regs->eip);
	if (regs->xcs & 3)
		printk(" ESP: %04x:%08lx",0xffff & regs->xss,regs->esp);
	printk(" EFLAGS: %08lx\n",regs->eflags);
	printk("EAX: %08lx EBX: %08lx ECX: %08lx EDX: %08lx\n",
		regs->eax,regs->ebx,regs->ecx,regs->edx);
	printk("ESI: %08lx EDI: %08lx EBP: %08lx",
		regs->esi, regs->edi, regs->ebp);
	printk(" DS: %04x ES: %04x\n",
		0xffff & regs->xds,0xffff & regs->xes);
}

void release_segments(struct mm_struct *mm)
{
	void * ldt;

	/* forget local segments */
	__asm__ __volatile__("movl %w0,%%fs ; movl %w0,%%gs ; lldt %w0"
		: /* no outputs */
		: "r" (0));
	current->tss.ldt = 0;

	ldt = mm->segments;
	if (ldt) {
		mm->segments = NULL;
		vfree(ldt);
	}
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
	/* forget lazy i387 state */
	if (last_task_used_math == current)
		last_task_used_math = NULL;
}

void flush_thread(void)
{
	int i;

	for (i=0 ; i<8 ; i++)
		current->debugreg[i] = 0;

	/*
	 * Forget coprocessor state..
	 */
#ifdef __SMP__
	if (current->flags & PF_USEDFPU) {
		stts();
	}
#else
	if (last_task_used_math == current) {
		last_task_used_math = NULL;
		stts();
	}
#endif
	current->used_math = 0;
	current->flags &= ~PF_USEDFPU;
}

void release_thread(struct task_struct *dead_task)
{
}

void copy_segments(int nr, struct task_struct *p, struct mm_struct *new_mm)
{
	int ldt_size = 1;
	void * ldt = &default_ldt;
	struct mm_struct * old_mm = current->mm;

	p->tss.ldt = _LDT(nr);
	if (old_mm->segments) {
		new_mm->segments = vmalloc(LDT_ENTRIES*LDT_ENTRY_SIZE);
		if (new_mm->segments) {
			ldt = new_mm->segments;
			ldt_size = LDT_ENTRIES;
			memcpy(ldt, old_mm->segments, LDT_ENTRIES*LDT_ENTRY_SIZE);
		}
	}
	set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY, ldt, ldt_size);
}

int copy_thread(int nr, unsigned long clone_flags, unsigned long esp,
	struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs * childregs;

	p->tss.tr = _TSS(nr);
	p->tss.es = __KERNEL_DS;
	p->tss.cs = __KERNEL_CS;
	p->tss.ss = __KERNEL_DS;
	p->tss.ds = __KERNEL_DS;
	p->tss.fs = __USER_DS;
	p->tss.gs = __USER_DS;
	p->tss.ss0 = __KERNEL_DS;
	p->tss.esp0 = 2*PAGE_SIZE + (unsigned long) p;
	childregs = ((struct pt_regs *) (p->tss.esp0)) - 1;
	p->tss.esp = (unsigned long) childregs;
#ifdef __SMP__
	p->tss.eip = (unsigned long) ret_from_smpfork;
	p->tss.eflags = regs->eflags & 0xffffcdff;  /* iopl always 0 for a new process */
#else
	p->tss.eip = (unsigned long) ret_from_sys_call;
	p->tss.eflags = regs->eflags & 0xffffcfff;  /* iopl always 0 for a new process */
#endif
	p->tss.ebx = (unsigned long) p;
	*childregs = *regs;
	childregs->eax = 0;
	childregs->esp = esp;
	p->tss.back_link = 0;
	set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));

	/*
	 * a bitmap offset pointing outside of the TSS limit causes a nicely
	 * controllable SIGSEGV. The first sys_ioperm() call sets up the
	 * bitmap properly.
	 */
	p->tss.bitmap = sizeof(struct thread_struct);

#ifdef __SMP__
	if (current->flags & PF_USEDFPU)
#else
	if (last_task_used_math == current)
#endif
		__asm__("clts ; fnsave %0 ; frstor %0":"=m" (p->tss.i387));

	return 0;
}

/*
 * fill in the fpu structure for a core dump..
 */
int dump_fpu (struct pt_regs * regs, struct user_i387_struct* fpu)
{
	int fpvalid;

	if ((fpvalid = current->used_math) != 0) {
		if (boot_cpu_data.hard_math) {
		  if (last_task_used_math == current) {
			  __asm__("clts ; fsave %0; fwait": :"m" (*fpu));
		  }
			else
				memcpy(fpu,&current->tss.i387.hard,sizeof(*fpu));
		} else {
			memcpy(fpu,&current->tss.i387.hard,sizeof(*fpu));
		}
	}

	return fpvalid;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
	int i;

/* changed the size calculations - should hopefully work better. lbt */
	dump->magic = CMAGIC;
	dump->start_code = 0;
	dump->start_stack = regs->esp & ~(PAGE_SIZE - 1);
	dump->u_tsize = ((unsigned long) current->mm->end_code) >> PAGE_SHIFT;
	dump->u_dsize = ((unsigned long) (current->mm->brk + (PAGE_SIZE-1))) >> PAGE_SHIFT;
	dump->u_dsize -= dump->u_tsize;
	dump->u_ssize = 0;
	for (i = 0; i < 8; i++)
		dump->u_debugreg[i] = current->debugreg[i];  

	if (dump->start_stack < TASK_SIZE)
		dump->u_ssize = ((unsigned long) (TASK_SIZE - dump->start_stack)) >> PAGE_SHIFT;

	dump->regs.ebx = regs->ebx;
	dump->regs.ecx = regs->ecx;
	dump->regs.edx = regs->edx;
	dump->regs.esi = regs->esi;
	dump->regs.edi = regs->edi;
	dump->regs.ebp = regs->ebp;
	dump->regs.eax = regs->eax;
	dump->regs.ds = regs->xds;
	dump->regs.es = regs->xes;
	__asm__("movl %%fs,%0":"=r" (dump->regs.fs));
	__asm__("movl %%gs,%0":"=r" (dump->regs.gs));
	dump->regs.orig_eax = regs->orig_eax;
	dump->regs.eip = regs->eip;
	dump->regs.cs = regs->xcs;
	dump->regs.eflags = regs->eflags;
	dump->regs.esp = regs->esp;
	dump->regs.ss = regs->xss;

	dump->u_fpvalid = dump_fpu (regs, &dump->i387);
}

asmlinkage int sys_fork(struct pt_regs regs)
{
	int ret;

	lock_kernel();
	ret = do_fork(SIGCHLD, regs.esp, &regs);
	unlock_kernel();
	return ret;
}

asmlinkage int sys_clone(struct pt_regs regs)
{
	unsigned long clone_flags;
	unsigned long newsp;
	int ret;

	lock_kernel();
	clone_flags = regs.ebx;
	newsp = regs.ecx;
	if (!newsp)
		newsp = regs.esp;
	ret = do_fork(clone_flags, newsp, &regs);
	unlock_kernel();
	return ret;
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(struct pt_regs regs)
{
	int error;
	char * filename;

	lock_kernel();
	filename = getname((char *) regs.ebx);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, (char **) regs.ecx, (char **) regs.edx, &regs);
	putname(filename);
out:
	unlock_kernel();
	return error;
}
