/*
 *  linux/arch/i386/traps.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'asm.s'. Currently mostly a debugging-aid, will be extended
 * to mainly kill the offending process (probably by giving it a signal,
 * but possibly by killing it outright if necessary).
 */
#include <linux/config.h>
#include <linux/head.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/config.h>
#include <linux/timer.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/io.h>

asmlinkage int system_call(void);
asmlinkage void lcall7(void);
struct desc_struct default_ldt = { 0, 0 };

static inline void console_verbose(void)
{
	extern int console_loglevel;
	console_loglevel = 15;
}

#define DO_ERROR(trapnr, signr, str, name, tsk) \
asmlinkage void do_##name(struct pt_regs * regs, long error_code) \
{ \
	tsk->tss.error_code = error_code; \
	tsk->tss.trap_no = trapnr; \
	force_sig(signr, tsk); \
	die_if_kernel(str,regs,error_code); \
}

#define get_seg_byte(seg,addr) ({ \
register unsigned char __res; \
__asm__("push %%fs;mov %%ax,%%fs;movb %%fs:%2,%%al;pop %%fs" \
	:"=a" (__res):"0" (seg),"m" (*(addr))); \
__res;})

#define get_seg_long(seg,addr) ({ \
register unsigned long __res; \
__asm__("push %%fs;mov %%ax,%%fs;movl %%fs:%2,%%eax;pop %%fs" \
	:"=a" (__res):"0" (seg),"m" (*(addr))); \
__res;})

#define _fs() ({ \
register unsigned short __res; \
__asm__("mov %%fs,%%ax":"=a" (__res):); \
__res;})

void page_exception(void);

asmlinkage void divide_error(void);
asmlinkage void debug(void);
asmlinkage void nmi(void);
asmlinkage void int3(void);
asmlinkage void overflow(void);
asmlinkage void bounds(void);
asmlinkage void invalid_op(void);
asmlinkage void device_not_available(void);
asmlinkage void double_fault(void);
asmlinkage void coprocessor_segment_overrun(void);
asmlinkage void invalid_TSS(void);
asmlinkage void segment_not_present(void);
asmlinkage void stack_segment(void);
asmlinkage void general_protection(void);
asmlinkage void page_fault(void);
asmlinkage void coprocessor_error(void);
asmlinkage void reserved(void);
asmlinkage void alignment_check(void);

int kstack_depth_to_print = 24;

/*
 * These constants are for searching for possible module text
 * segments.  VMALLOC_OFFSET comes from mm/vmalloc.c; MODULE_RANGE is
 * a guess of how much space is likely to be vmalloced.
 */
#define VMALLOC_OFFSET (8*1024*1024)
#define MODULE_RANGE (8*1024*1024)

/*static*/ void die_if_kernel(const char * str, struct pt_regs * regs, long err)
{
	int i;
	unsigned long esp;
	unsigned short ss;
	unsigned long *stack, addr, module_start, module_end;
	extern char start_kernel, _etext;

	esp = (unsigned long) &regs->esp;
	ss = KERNEL_DS;
	if ((regs->eflags & VM_MASK) || (3 & regs->cs) == 3)
		return;
	if (regs->cs & 3) {
		esp = regs->esp;
		ss = regs->ss;
	}
	console_verbose();
	printk("%s: %04lx\n", str, err & 0xffff);
	printk("CPU:    %d\n", smp_processor_id());
	printk("EIP:    %04x:[<%08lx>]\nEFLAGS: %08lx\n", 0xffff & regs->cs,regs->eip,regs->eflags);
	printk("eax: %08lx   ebx: %08lx   ecx: %08lx   edx: %08lx\n",
		regs->eax, regs->ebx, regs->ecx, regs->edx);
	printk("esi: %08lx   edi: %08lx   ebp: %08lx   esp: %08lx\n",
		regs->esi, regs->edi, regs->ebp, esp);
	printk("ds: %04x   es: %04x   fs: %04x   gs: %04x   ss: %04x\n",
		regs->ds, regs->es, regs->fs, regs->gs, ss);
	store_TR(i);
	if (STACK_MAGIC != *(unsigned long *)current->kernel_stack_page)
		printk("Corrupted stack page\n");
	printk("Process %s (pid: %d, process nr: %d, stackpage=%08lx)\nStack: ",
		current->comm, current->pid, 0xffff & i, current->kernel_stack_page);
	stack = (unsigned long *) esp;
	for(i=0; i < kstack_depth_to_print; i++) {
		if (((long) stack & 4095) == 0)
			break;
		if (i && ((i % 8) == 0))
			printk("\n       ");
		printk("%08lx ", get_seg_long(ss,stack++));
	}
	printk("\nCall Trace: ");
	stack = (unsigned long *) esp;
	i = 1;
	module_start = ((high_memory + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1));
	module_end = module_start + MODULE_RANGE;
	while (((long) stack & 4095) != 0) {
		addr = get_seg_long(ss, stack++);
		/*
		 * If the address is either in the text segment of the
		 * kernel, or in the region which contains vmalloc'ed
		 * memory, it *may* be the address of a calling
		 * routine; if so, print it so that someone tracing
		 * down the cause of the crash will be able to figure
		 * out the call path that was taken.
		 */
		if (((addr >= (unsigned long) &start_kernel) &&
		     (addr <= (unsigned long) &_etext)) ||
		    ((addr >= module_start) && (addr <= module_end))) {
			if (i && ((i % 8) == 0))
				printk("\n       ");
			printk("[<%08lx>] ", addr);
			i++;
		}
	}
	printk("\nCode: ");
	for(i=0;i<20;i++)
		printk("%02x ",0xff & get_seg_byte(regs->cs,(i+(char *)regs->eip)));
	printk("\n");
	do_exit(SIGSEGV);
}

DO_ERROR( 0, SIGFPE,  "divide error", divide_error, current)
DO_ERROR( 3, SIGTRAP, "int3", int3, current)
DO_ERROR( 4, SIGSEGV, "overflow", overflow, current)
DO_ERROR( 5, SIGSEGV, "bounds", bounds, current)
DO_ERROR( 6, SIGILL,  "invalid operand", invalid_op, current)
DO_ERROR( 7, SIGSEGV, "device not available", device_not_available, current)
DO_ERROR( 8, SIGSEGV, "double fault", double_fault, current)
DO_ERROR( 9, SIGFPE,  "coprocessor segment overrun", coprocessor_segment_overrun, last_task_used_math)
DO_ERROR(10, SIGSEGV, "invalid TSS", invalid_TSS, current)
DO_ERROR(11, SIGBUS,  "segment not present", segment_not_present, current)
DO_ERROR(12, SIGBUS,  "stack segment", stack_segment, current)
DO_ERROR(15, SIGSEGV, "reserved", reserved, current)
DO_ERROR(17, SIGSEGV, "alignment check", alignment_check, current)

asmlinkage void do_general_protection(struct pt_regs * regs, long error_code)
{
	if (regs->eflags & VM_MASK) {
		handle_vm86_fault((struct vm86_regs *) regs, error_code);
		return;
	}
	die_if_kernel("general protection",regs,error_code);
	current->tss.error_code = error_code;
	current->tss.trap_no = 13;
	force_sig(SIGSEGV, current);	
}

asmlinkage void do_nmi(struct pt_regs * regs, long error_code)
{
#ifdef CONFIG_SMP_NMI_INVAL
	smp_flush_tlb_rcv();
#else
#ifndef CONFIG_IGNORE_NMI
	printk("Uhhuh. NMI received. Dazed and confused, but trying to continue\n");
	printk("You probably have a hardware problem with your RAM chips or a\n");
	printk("power saving mode enabled.\n");
#endif	
#endif
}

asmlinkage void do_debug(struct pt_regs * regs, long error_code)
{
	if (regs->eflags & VM_MASK) {
		handle_vm86_debug((struct vm86_regs *) regs, error_code);
		return;
	}
	force_sig(SIGTRAP, current);
	current->tss.trap_no = 1;
	current->tss.error_code = error_code;
	if ((regs->cs & 3) == 0) {
		/* If this is a kernel mode trap, then reset db7 and allow us to continue */
		__asm__("movl %0,%%db7"
			: /* no output */
			: "r" (0));
		return;
	}
	die_if_kernel("debug",regs,error_code);
}

/*
 * Allow the process which triggered the interrupt to recover the error
 * condition.
 *  - the status word is saved in the cs selector.
 *  - the tag word is saved in the operand selector.
 *  - the status word is then cleared and the tags all set to Empty.
 *
 * This will give sufficient information for complete recovery provided that
 * the affected process knows or can deduce the code and data segments
 * which were in force when the exception condition arose.
 *
 * Note that we play around with the 'TS' bit to hopefully get
 * the correct behaviour even in the presence of the asynchronous
 * IRQ13 behaviour
 */
void math_error(void)
{
	struct task_struct * task;

	clts();
#ifdef __SMP__
	task = current;
#else
	task = last_task_used_math;
	last_task_used_math = NULL;
	if (!task) {
		__asm__("fnclex");
		return;
	}
#endif
	/*
	 *	Save the info for the exception handler
	 */
	__asm__ __volatile__("fnsave %0":"=m" (task->tss.i387.hard));
	task->flags&=~PF_USEDFPU;
	stts();

	force_sig(SIGFPE, task);
	task->tss.trap_no = 16;
	task->tss.error_code = 0;
}

asmlinkage void do_coprocessor_error(struct pt_regs * regs, long error_code)
{
	ignore_irq13 = 1;
	math_error();
}

/*
 *  'math_state_restore()' saves the current math information in the
 * old math state array, and gets the new ones from the current task
 *
 * Careful.. There are problems with IBM-designed IRQ13 behaviour.
 * Don't touch unless you *really* know how it works.
 */
asmlinkage void math_state_restore(void)
{
	__asm__ __volatile__("clts");		/* Allow maths ops (or we recurse) */

/*
 *	SMP is actually simpler than uniprocessor for once. Because
 *	we can't pull the delayed FPU switching trick Linus does
 *	we simply have to do the restore each context switch and
 *	set the flag. switch_to() will always save the state in
 *	case we swap processors. We also don't use the coprocessor
 *	timer - IRQ 13 mode isn't used with SMP machines (thank god).
 */
#ifndef __SMP__
	if (last_task_used_math == current)
		return;
	if (last_task_used_math)
		__asm__("fnsave %0":"=m" (last_task_used_math->tss.i387));
	else
		__asm__("fnclex");
	last_task_used_math = current;
#endif

	if(current->used_math)
		__asm__("frstor %0": :"m" (current->tss.i387));
	else
	{
		/*
		 *	Our first FPU usage, clean the chip.
		 */
		__asm__("fninit");
		current->used_math = 1;
	}
	current->flags|=PF_USEDFPU;		/* So we fnsave on switch_to() */
}

#ifndef CONFIG_MATH_EMULATION

asmlinkage void math_emulate(long arg)
{
  printk("math-emulation not enabled and no coprocessor found.\n");
  printk("killing %s.\n",current->comm);
  force_sig(SIGFPE,current);
  schedule();
}

#endif /* CONFIG_MATH_EMULATION */

void trap_init(void)
{
	int i;
	struct desc_struct * p;
	static int smptrap=0;
	
	if(smptrap)
	{
		__asm__("pushfl ; andl $0xffffbfff,(%esp) ; popfl");
		load_ldt(0);
		return;
	}
	smptrap++;
	if (strncmp((char*)0x0FFFD9, "EISA", 4) == 0)
		EISA_bus = 1;
	set_call_gate(&default_ldt,lcall7);
	set_trap_gate(0,&divide_error);
	set_trap_gate(1,&debug);
	set_trap_gate(2,&nmi);
	set_system_gate(3,&int3);	/* int3-5 can be called from all */
	set_system_gate(4,&overflow);
	set_system_gate(5,&bounds);
	set_trap_gate(6,&invalid_op);
	set_trap_gate(7,&device_not_available);
	set_trap_gate(8,&double_fault);
	set_trap_gate(9,&coprocessor_segment_overrun);
	set_trap_gate(10,&invalid_TSS);
	set_trap_gate(11,&segment_not_present);
	set_trap_gate(12,&stack_segment);
	set_trap_gate(13,&general_protection);
	set_trap_gate(14,&page_fault);
	set_trap_gate(15,&reserved);
	set_trap_gate(16,&coprocessor_error);
	set_trap_gate(17,&alignment_check);
	for (i=18;i<48;i++)
		set_trap_gate(i,&reserved);
	set_system_gate(0x80,&system_call);
/* set up GDT task & ldt entries */
	p = gdt+FIRST_TSS_ENTRY;
	set_tss_desc(p, &init_task.tss);
	p++;
	set_ldt_desc(p, &default_ldt, 1);
	p++;
	for(i=1 ; i<NR_TASKS ; i++) {
		p->a=p->b=0;
		p++;
		p->a=p->b=0;
		p++;
	}
/* Clear NT, so that we won't have troubles with that later on */
	__asm__("pushfl ; andl $0xffffbfff,(%esp) ; popfl");
	load_TR(0);
	load_ldt(0);
}
