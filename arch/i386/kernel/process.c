/*
 *  linux/arch/i386/kernel/process.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/ldt.h>
#include <linux/user.h>
#include <linux/a.out.h>

#include <asm/segment.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/io.h>

asmlinkage void ret_from_sys_call(void) __asm__("ret_from_sys_call");

static int hlt_counter=0;

void disable_hlt(void)
{
	hlt_counter++;
}

void enable_hlt(void)
{
	hlt_counter--;
}

/*
 * The idle loop on a i386..
 */
asmlinkage int sys_idle(void)
{
	int i;
	pmd_t * pmd;

	if (current->pid != 0)
		return -EPERM;

	/* Map out the low memory: it's no longer needed */
	pmd = pmd_offset(swapper_pg_dir, 0);
	for (i = 0 ; i < 768 ; i++)
		pmd_clear(pmd++);

	/* endless idle loop with no priority at all */
	current->counter = -100;
	for (;;) {
		if (hlt_works_ok && !hlt_counter && !need_resched)
			__asm__("hlt");
		schedule();
	}
}

/*
 * This routine reboots the machine by asking the keyboard
 * controller to pulse the reset-line low. We try that for a while,
 * and if it doesn't work, we do some other stupid things.
 */
static long no_idt[2] = {0, 0};

static inline void kb_wait(void)
{
	int i;

	for (i=0; i<0x10000; i++)
		if ((inb_p(0x64) & 0x02) == 0)
			break;
}

void hard_reset_now(void)
{
	int i, j;

	sti();
/* rebooting needs to touch the page at absolute addr 0 */
	pg0[0] = 7;
	*((unsigned short *)0x472) = 0x1234;
	for (;;) {
		for (i=0; i<100; i++) {
			kb_wait();
			for(j = 0; j < 100000 ; j++)
				/* nothing */;
			outb(0xfe,0x64);	 /* pulse reset low */
		}
		__asm__ __volatile__("\tlidt %0": "=m" (no_idt));
	}
}

void show_regs(struct pt_regs * regs)
{
	printk("\n");
	printk("EIP: %04x:%08lx",0xffff & regs->cs,regs->eip);
	if (regs->cs & 3)
		printk(" ESP: %04x:%08lx",0xffff & regs->ss,regs->esp);
	printk(" EFLAGS: %08lx\n",regs->eflags);
	printk("EAX: %08lx EBX: %08lx ECX: %08lx EDX: %08lx\n",
		regs->eax,regs->ebx,regs->ecx,regs->edx);
	printk("ESI: %08lx EDI: %08lx EBP: %08lx",
		regs->esi, regs->edi, regs->ebp);
	printk(" DS: %04x ES: %04x FS: %04x GS: %04x\n",
		0xffff & regs->ds,0xffff & regs->es,
		0xffff & regs->fs,0xffff & regs->gs);
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
	/* forget local segments */
	__asm__ __volatile__("mov %w0,%%fs ; mov %w0,%%gs ; lldt %w0"
		: /* no outputs */
		: "r" (0));
	current->tss.ldt = 0;
	if (current->ldt) {
		void * ldt = current->ldt;
		current->ldt = NULL;
		vfree(ldt);
	}
}

void flush_thread(void)
{
	int i;

	if (current->ldt) {
		free_page((unsigned long) current->ldt);
		current->ldt = NULL;
		for (i=1 ; i<NR_TASKS ; i++) {
			if (task[i] == current)  {
				set_ldt_desc(gdt+(i<<1)+
					     FIRST_LDT_ENTRY,&default_ldt, 1);
				load_ldt(i);
			}
		}	
	}

	for (i=0 ; i<8 ; i++)
		current->debugreg[i] = 0;
}

void copy_thread(int nr, unsigned long clone_flags, unsigned long esp,
	struct task_struct * p, struct pt_regs * regs)
{
	int i;
	struct pt_regs * childregs;

	p->tss.es = KERNEL_DS;
	p->tss.cs = KERNEL_CS;
	p->tss.ss = KERNEL_DS;
	p->tss.ds = KERNEL_DS;
	p->tss.fs = USER_DS;
	p->tss.gs = KERNEL_DS;
	p->tss.ss0 = KERNEL_DS;
	p->tss.esp0 = p->kernel_stack_page + PAGE_SIZE;
	p->tss.tr = _TSS(nr);
	childregs = ((struct pt_regs *) (p->kernel_stack_page + PAGE_SIZE)) - 1;
	p->tss.esp = (unsigned long) childregs;
	p->tss.eip = (unsigned long) ret_from_sys_call;
	*childregs = *regs;
	childregs->eax = 0;
	childregs->esp = esp;
	p->tss.back_link = 0;
	p->tss.eflags = regs->eflags & 0xffffcfff;	/* iopl is always 0 for a new process */
	p->tss.ldt = _LDT(nr);
	if (p->ldt) {
		p->ldt = (struct desc_struct*) vmalloc(LDT_ENTRIES*LDT_ENTRY_SIZE);
		if (p->ldt != NULL)
			memcpy(p->ldt, current->ldt, LDT_ENTRIES*LDT_ENTRY_SIZE);
	}
	set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
	if (p->ldt)
		set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,p->ldt, 512);
	else
		set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&default_ldt, 1);
	p->tss.bitmap = offsetof(struct thread_struct,io_bitmap);
	for (i = 0; i < IO_BITMAP_SIZE+1 ; i++) /* IO bitmap is actually SIZE+1 */
		p->tss.io_bitmap[i] = ~0;
	if (last_task_used_math == current)
		__asm__("clts ; fnsave %0 ; frstor %0":"=m" (p->tss.i387));
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
	dump->u_tsize = ((unsigned long) current->mm->end_code) >> 12;
	dump->u_dsize = ((unsigned long) (current->mm->brk + (PAGE_SIZE-1))) >> 12;
	dump->u_dsize -= dump->u_tsize;
	dump->u_ssize = 0;
	for (i = 0; i < 8; i++)
		dump->u_debugreg[i] = current->debugreg[i];  

	if (dump->start_stack < TASK_SIZE)
		dump->u_ssize = ((unsigned long) (TASK_SIZE - dump->start_stack)) >> 12;

	dump->regs = *regs;

/* Flag indicating the math stuff is valid. We don't support this for the
   soft-float routines yet */
	if (hard_math) {
		if ((dump->u_fpvalid = current->used_math) != 0) {
			if (last_task_used_math == current)
				__asm__("clts ; fnsave %0": :"m" (dump->i387));
			else
				memcpy(&dump->i387,&current->tss.i387.hard,sizeof(dump->i387));
		}
	} else {
		/* we should dump the emulator state here, but we need to
		   convert it into standard 387 format first.. */
		dump->u_fpvalid = 0;
	}
}

asmlinkage int sys_fork(struct pt_regs regs)
{
	return do_fork(COPYVM | SIGCHLD, regs.esp, &regs);
}

asmlinkage int sys_clone(struct pt_regs regs)
{
#ifdef CLONE_ACTUALLY_WORKS_OK
	unsigned long clone_flags;
	unsigned long newsp;

	newsp = regs.ebx;
	clone_flags = regs.ecx;
	if (!newsp)
		newsp = regs.esp;
	if (newsp == regs.esp)
		clone_flags |= COPYVM;
	return do_fork(clone_flags, newsp, &regs);
#else
	return -ENOSYS;
#endif
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(struct pt_regs regs)
{
	int error;
	char * filename;

	error = getname((char *) regs.ebx, &filename);
	if (error)
		return error;
	error = do_execve(filename, (char **) regs.ecx, (char **) regs.edx, &regs);
	putname(filename);
	return error;
}
