/*
 *  linux/arch/i386/traps.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'asm.s'.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/delay.h>

#ifdef CONFIG_MCA
#include <linux/mca.h>
#include <asm/processor.h>
#endif

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/spinlock.h>
#include <asm/atomic.h>
#include <asm/debugreg.h>
#include <asm/desc.h>

#include <asm/smp.h>

#ifdef CONFIG_X86_VISWS_APIC
#include <asm/fixmap.h>
#include <asm/cobalt.h>
#include <asm/lithium.h>
#endif

asmlinkage int system_call(void);
asmlinkage void lcall7(void);

struct desc_struct default_ldt = { 0, 0 };

/*
 * The IDT has to be page-aligned to simplify the Pentium
 * F0 0F bug workaround.. We have a special link segment
 * for this.
 */
struct desc_struct idt_table[256] __attribute__((__section__(".data.idt"))) = { {0, 0}, };

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
	die_if_no_fixup(str,regs,error_code); \
}

#define DO_VM86_ERROR(trapnr, signr, str, name, tsk) \
asmlinkage void do_##name(struct pt_regs * regs, long error_code) \
{ \
	lock_kernel(); \
	if (regs->eflags & VM_MASK) { \
		if (!handle_vm86_trap((struct kernel_vm86_regs *) regs, error_code, trapnr)) \
			goto out; \
		/* else fall through */ \
	} \
	tsk->tss.error_code = error_code; \
	tsk->tss.trap_no = trapnr; \
	force_sig(signr, tsk); \
	die_if_kernel(str,regs,error_code); \
out: \
	unlock_kernel(); \
}

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
asmlinkage void spurious_interrupt_bug(void);

int kstack_depth_to_print = 24;

/*
 * These constants are for searching for possible module text
 * segments.  VMALLOC_OFFSET comes from mm/vmalloc.c; MODULE_RANGE is
 * a guess of how much space is likely to be vmalloced.
 */
#define VMALLOC_OFFSET (8*1024*1024)
#define MODULE_RANGE (8*1024*1024)

static void show_registers(struct pt_regs *regs)
{
	int i;
	int in_kernel = 1;
	unsigned long esp;
	unsigned short ss;
	unsigned long *stack, addr, module_start, module_end;
	extern char _stext, _etext;

	esp = (unsigned long) (1+regs);
	ss = __KERNEL_DS;
	if (regs->xcs & 3) {
		in_kernel = 0;
		esp = regs->esp;
		ss = regs->xss & 0xffff;
	}
	printk("CPU:    %d\nEIP:    %04x:[<%08lx>]\nEFLAGS: %08lx\n",
		smp_processor_id(), 0xffff & regs->xcs, regs->eip, regs->eflags);
	printk("eax: %08lx   ebx: %08lx   ecx: %08lx   edx: %08lx\n",
		regs->eax, regs->ebx, regs->ecx, regs->edx);
	printk("esi: %08lx   edi: %08lx   ebp: %08lx   esp: %08lx\n",
		regs->esi, regs->edi, regs->ebp, esp);
	printk("ds: %04x   es: %04x   ss: %04x\n",
		regs->xds & 0xffff, regs->xes & 0xffff, ss);
	store_TR(i);
	printk("Process %s (pid: %d, process nr: %d, stackpage=%08lx)",
		current->comm, current->pid, 0xffff & i, 4096+(unsigned long)current);

	/*
	 * When in-kernel, we also print out the stack and code at the
	 * time of the fault..
	 */
	if (in_kernel) {
		printk("\nStack: ");
		stack = (unsigned long *) esp;
		for(i=0; i < kstack_depth_to_print; i++) {
			if (((long) stack & 4095) == 0)
				break;
			if (i && ((i % 8) == 0))
				printk("\n       ");
			printk("%08lx ", *stack++);
		}
		printk("\nCall Trace: ");
		stack = (unsigned long *) esp;
		i = 1;
		module_start = PAGE_OFFSET + (max_mapnr << PAGE_SHIFT);
		module_start = ((module_start + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1));
		module_end = module_start + MODULE_RANGE;
		while (((long) stack & 4095) != 0) {
			addr = *stack++;
			/*
			 * If the address is either in the text segment of the
			 * kernel, or in the region which contains vmalloc'ed
			 * memory, it *may* be the address of a calling
			 * routine; if so, print it so that someone tracing
			 * down the cause of the crash will be able to figure
			 * out the call path that was taken.
			 */
			if (((addr >= (unsigned long) &_stext) &&
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
			printk("%02x ", ((unsigned char *)regs->eip)[i]);
	}
	printk("\n");
}	

spinlock_t die_lock;

void die(const char * str, struct pt_regs * regs, long err)
{
	console_verbose();
	spin_lock_irq(&die_lock);
	printk("%s: %04lx\n", str, err & 0xffff);
	show_registers(regs);
	spin_unlock_irq(&die_lock);
	do_exit(SIGSEGV);
}

static inline void die_if_kernel(const char * str, struct pt_regs * regs, long err)
{
	if (!(regs->eflags & VM_MASK) && !(3 & regs->xcs))
		die(str, regs, err);
}

static void die_if_no_fixup(const char * str, struct pt_regs * regs, long err)
{
	if (!(regs->eflags & VM_MASK) && !(3 & regs->xcs))
	{
		unsigned long fixup;
		fixup = search_exception_table(regs->eip);
		if (fixup) {
			regs->eip = fixup;
			return;
		}
		die(str, regs, err);
	}
}

DO_VM86_ERROR( 0, SIGFPE,  "divide error", divide_error, current)
DO_VM86_ERROR( 3, SIGTRAP, "int3", int3, current)
DO_VM86_ERROR( 4, SIGSEGV, "overflow", overflow, current)
DO_VM86_ERROR( 5, SIGSEGV, "bounds", bounds, current)
DO_ERROR( 6, SIGILL,  "invalid operand", invalid_op, current)
DO_VM86_ERROR( 7, SIGSEGV, "device not available", device_not_available, current)
DO_ERROR( 8, SIGSEGV, "double fault", double_fault, current)
DO_ERROR( 9, SIGFPE,  "coprocessor segment overrun", coprocessor_segment_overrun, current)
DO_ERROR(10, SIGSEGV, "invalid TSS", invalid_TSS, current)
DO_ERROR(11, SIGBUS,  "segment not present", segment_not_present, current)
DO_ERROR(12, SIGBUS,  "stack segment", stack_segment, current)
DO_ERROR(17, SIGSEGV, "alignment check", alignment_check, current)
DO_ERROR(18, SIGSEGV, "reserved", reserved, current)
/* I don't have documents for this but it does seem to cover the cache
   flush from user space exception some people get. */
DO_ERROR(19, SIGSEGV, "cache flush denied", cache_flush_denied, current)

asmlinkage void cache_flush_denied(struct pt_regs * regs, long error_code)
{
	if (regs->eflags & VM_MASK) {
		handle_vm86_fault((struct kernel_vm86_regs *) regs, error_code);
		return;
	}
	die_if_kernel("cache flush denied",regs,error_code);
	current->tss.error_code = error_code;
	current->tss.trap_no = 19;
	force_sig(SIGSEGV, current);
}

asmlinkage void do_general_protection(struct pt_regs * regs, long error_code)
{
	if (regs->eflags & VM_MASK)
		goto gp_in_vm86;

	if (!(regs->xcs & 3))
		goto gp_in_kernel;

	current->tss.error_code = error_code;
	current->tss.trap_no = 13;
	force_sig(SIGSEGV, current);
	return;

gp_in_vm86:
	lock_kernel();
	handle_vm86_fault((struct kernel_vm86_regs *) regs, error_code);
	unlock_kernel();
	return;

gp_in_kernel:
	{
		unsigned long fixup;
		fixup = search_exception_table(regs->eip);
		if (fixup) {
			regs->eip = fixup;
			return;
		}
		die("general protection fault", regs, error_code);
	}
}

static void mem_parity_error(unsigned char reason, struct pt_regs * regs)
{
	printk("Uhhuh. NMI received. Dazed and confused, but trying to continue\n");
	printk("You probably have a hardware problem with your RAM chips\n");
}	

static void io_check_error(unsigned char reason, struct pt_regs * regs)
{
	unsigned long i;

	printk("NMI: IOCK error (debug interrupt?)\n");
	show_registers(regs);

	/* Re-enable the IOCK line, wait for a few seconds */
	reason |= 8;
	outb(reason, 0x61);
	i = 2000;
	while (--i) udelay(1000);
	reason &= ~8;
	outb(reason, 0x61);
}

static void unknown_nmi_error(unsigned char reason, struct pt_regs * regs)
{
#ifdef CONFIG_MCA
	/* Might actually be able to figure out what the guilty party
	* is. */
	if( MCA_bus ) {
		mca_handle_nmi();
		return;
	}
#endif
	printk("Uhhuh. NMI received for unknown reason %02x.\n", reason);
	printk("Dazed and confused, but trying to continue\n");
	printk("Do you have a strange power saving mode enabled?\n");
}

asmlinkage void do_nmi(struct pt_regs * regs, long error_code)
{
	unsigned char reason = inb(0x61);
	extern atomic_t nmi_counter;

	atomic_inc(&nmi_counter);
	if (reason & 0x80)
		mem_parity_error(reason, regs);
	if (reason & 0x40)
		io_check_error(reason, regs);
	if (!(reason & 0xc0))
		unknown_nmi_error(reason, regs);
}

/*
 * Careful - we must not do a lock-kernel until we have checked that the
 * debug fault happened in user mode. Getting debug exceptions while
 * in the kernel has to be handled without locking, to avoid deadlocks..
 *
 * Being careful here means that we don't have to be as careful in a
 * lot of more complicated places (task switching can be a bit lazy
 * about restoring all the debug state, and ptrace doesn't have to
 * find every occurrence of the TF bit that could be saved away even
 * by user code - and we don't have to be careful about what values
 * can be written to the debug registers because there are no really
 * bad cases).
 */
asmlinkage void do_debug(struct pt_regs * regs, long error_code)
{
	unsigned int condition;
	struct task_struct *tsk = current;

	if (regs->eflags & VM_MASK)
		goto debug_vm86;

	__asm__ __volatile__("movl %%db6,%0" : "=r" (condition));

	/* Mask out spurious TF errors due to lazy TF clearing */
	if (condition & DR_STEP) {
		/*
		 * The TF error should be masked out only if the current
		 * process is not traced and if the TRAP flag has been set
		 * previously by a tracing process (condition detected by
		 * the PF_DTRACE flag); remember that the i386 TRAP flag
		 * can be modified by the process itself in user mode,
		 * allowing programs to debug themselves without the ptrace()
		 * interface.
		 */
		if ((tsk->flags & (PF_DTRACE|PF_PTRACED)) == PF_DTRACE)
			goto clear_TF;
	}

	/* Mast out spurious debug traps due to lazy DR7 setting */
	if (condition & (DR_TRAP0|DR_TRAP1|DR_TRAP2|DR_TRAP3)) {
		if (!tsk->tss.debugreg[7])
			goto clear_dr7;
	}

	/* If this is a kernel mode trap, we need to reset db7 to allow us to continue sanely */
	if ((regs->xcs & 3) == 0)
		goto clear_dr7;

	/* Ok, finally something we can handle */
	tsk->tss.trap_no = 1;
	tsk->tss.error_code = error_code;
	force_sig(SIGTRAP, tsk);
	return;

debug_vm86:
	lock_kernel();
	handle_vm86_trap((struct kernel_vm86_regs *) regs, error_code, 1);
	unlock_kernel();
	return;

clear_dr7:
	__asm__("movl %0,%%db7"
		: /* no output */
		: "r" (0));
	return;

clear_TF:
	regs->eflags &= ~TF_MASK;
	return;
}

/*
 * Note that we play around with the 'TS' bit in an attempt to get
 * the correct behaviour even in the presence of the asynchronous
 * IRQ13 behaviour
 */
void math_error(void)
{
	struct task_struct * task;

	/*
	 * Save the info for the exception handler
	 * (this will also clear the error)
	 */
	task = current;
	save_fpu(task);
	task->tss.trap_no = 16;
	task->tss.error_code = 0;
	force_sig(SIGFPE, task);
}

asmlinkage void do_coprocessor_error(struct pt_regs * regs, long error_code)
{
	ignore_irq13 = 1;
	math_error();
}

asmlinkage void do_spurious_interrupt_bug(struct pt_regs * regs,
					  long error_code)
{
#if 0
	/* No need to warn about this any longer. */
	printk("Ignoring P6 Local APIC Spurious Interrupt Bug...\n");
#endif
}

/*
 *  'math_state_restore()' saves the current math information in the
 * old math state array, and gets the new ones from the current task
 *
 * Careful.. There are problems with IBM-designed IRQ13 behaviour.
 * Don't touch unless you *really* know how it works.
 */
asmlinkage void math_state_restore(struct pt_regs regs)
{
	__asm__ __volatile__("clts");		/* Allow maths ops (or we recurse) */
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
	lock_kernel();
	printk("math-emulation not enabled and no coprocessor found.\n");
	printk("killing %s.\n",current->comm);
	force_sig(SIGFPE,current);
	schedule();
	unlock_kernel();
}

#endif /* CONFIG_MATH_EMULATION */

__initfunc(void trap_init_f00f_bug(void))
{
	unsigned long page;
	pgd_t * pgd;
	pmd_t * pmd;
	pte_t * pte;

	/*
	 * Allocate a new page in virtual address space, 
	 * move the IDT into it and write protect this page.
	 */
	page = (unsigned long) vmalloc(PAGE_SIZE);
	pgd = pgd_offset(&init_mm, page);
	pmd = pmd_offset(pgd, page);
	pte = pte_offset(pmd, page);
	free_page(pte_page(*pte));
	*pte = mk_pte(&idt_table, PAGE_KERNEL_RO);
	local_flush_tlb();

	/*
	 * "idt" is magic - it overlaps the idt_descr
	 * variable so that updating idt will automatically
	 * update the idt descriptor..
	 */
	idt = (struct desc_struct *)page;
	__asm__ __volatile__("lidt %0": "=m" (idt_descr));
}

#define _set_gate(gate_addr,type,dpl,addr) \
do { \
  int __d0, __d1; \
  __asm__ __volatile__ ("movw %%dx,%%ax\n\t" \
	"movw %4,%%dx\n\t" \
	"movl %%eax,%0\n\t" \
	"movl %%edx,%1" \
	:"=m" (*((long *) (gate_addr))), \
	 "=m" (*(1+(long *) (gate_addr))), "=&a" (__d0), "=&d" (__d1) \
	:"i" ((short) (0x8000+(dpl<<13)+(type<<8))), \
	 "3" ((char *) (addr)),"2" (__KERNEL_CS << 16)); \
} while (0)


/*
 * This needs to use 'idt_table' rather than 'idt', and
 * thus use the _nonmapped_ version of the IDT, as the
 * Pentium F0 0F bugfix can have resulted in the mapped
 * IDT being write-protected.
 */
void set_intr_gate(unsigned int n, void *addr)
{
	_set_gate(idt_table+n,14,0,addr);
}

static void __init set_trap_gate(unsigned int n, void *addr)
{
	_set_gate(idt_table+n,15,0,addr);
}

static void __init set_system_gate(unsigned int n, void *addr)
{
	_set_gate(idt_table+n,15,3,addr);
}

static void __init set_call_gate(void *a, void *addr)
{
	_set_gate(a,12,3,addr);
}

#define _set_seg_desc(gate_addr,type,dpl,base,limit) {\
	*((gate_addr)+1) = ((base) & 0xff000000) | \
		(((base) & 0x00ff0000)>>16) | \
		((limit) & 0xf0000) | \
		((dpl)<<13) | \
		(0x00408000) | \
		((type)<<8); \
	*(gate_addr) = (((base) & 0x0000ffff)<<16) | \
		((limit) & 0x0ffff); }

#define _set_tssldt_desc(n,addr,limit,type) \
__asm__ __volatile__ ("movw %3,0(%2)\n\t" \
	"movw %%ax,2(%2)\n\t" \
	"rorl $16,%%eax\n\t" \
	"movb %%al,4(%2)\n\t" \
	"movb %4,5(%2)\n\t" \
	"movb $0,6(%2)\n\t" \
	"movb %%ah,7(%2)\n\t" \
	"rorl $16,%%eax" \
	: "=m"(*(n)) : "a" (addr), "r"(n), "ir"(limit), "i"(type))

void set_tss_desc(unsigned int n, void *addr)
{
	_set_tssldt_desc(gdt_table+FIRST_TSS_ENTRY+(n<<1), (int)addr, 235, 0x89);
}

void set_ldt_desc(unsigned int n, void *addr, unsigned int size)
{
	_set_tssldt_desc(gdt_table+FIRST_LDT_ENTRY+(n<<1), (int)addr, ((size << 3) - 1), 0x82);
}

#ifdef CONFIG_X86_VISWS_APIC

/*
 * On Rev 005 motherboards legacy device interrupt lines are wired directly
 * to Lithium from the 307.  But the PROM leaves the interrupt type of each
 * 307 logical device set appropriate for the 8259.  Later we'll actually use
 * the 8259, but for now we have to flip the interrupt types to
 * level triggered, active lo as required by Lithium.
 */

#define	REG	0x2e	/* The register to read/write */
#define	DEV	0x07	/* Register: Logical device select */
#define	VAL	0x2f	/* The value to read/write */

static void
superio_outb(int dev, int reg, int val)
{
	outb(DEV, REG);
	outb(dev, VAL);
	outb(reg, REG);
	outb(val, VAL);
}

static int __attribute__ ((unused))
superio_inb(int dev, int reg)
{
	outb(DEV, REG);
	outb(dev, VAL);
	outb(reg, REG);
	return inb(VAL);
}

#define	FLOP	3	/* floppy logical device */
#define	PPORT	4	/* parallel logical device */
#define	UART5	5	/* uart2 logical device (not wired up) */
#define	UART6	6	/* uart1 logical device (THIS is the serial port!) */
#define	IDEST	0x70	/* int. destination (which 307 IRQ line) reg. */
#define	ITYPE	0x71	/* interrupt type register */

/* interrupt type bits */
#define	LEVEL	0x01	/* bit 0, 0 == edge triggered */
#define	ACTHI	0x02	/* bit 1, 0 == active lo */

static void
superio_init(void)
{
	if (visws_board_type == VISWS_320 && visws_board_rev == 5) {
		superio_outb(UART6, IDEST, 0);	/* 0 means no intr propagated */
		printk("SGI 320 rev 5: disabling 307 uart1 interrupt\n");
	}
}

static void
lithium_init(void)
{
	set_fixmap(FIX_LI_PCIA, LI_PCI_A_PHYS);
	printk("Lithium PCI Bridge A, Bus Number: %d\n",
				li_pcia_read16(LI_PCI_BUSNUM) & 0xff);
	set_fixmap(FIX_LI_PCIB, LI_PCI_B_PHYS);
	printk("Lithium PCI Bridge B (PIIX4), Bus Number: %d\n",
				li_pcib_read16(LI_PCI_BUSNUM) & 0xff);

	/* XXX blindly enables all interrupts */
	li_pcia_write16(LI_PCI_INTEN, 0xffff);
	li_pcib_write16(LI_PCI_INTEN, 0xffff);
}

static void
cobalt_init(void)
{
	/*
	 * On normal SMP PC this is used only with SMP, but we have to
	 * use it and set it up here to start the Cobalt clock
	 */
	set_fixmap(FIX_APIC_BASE, APIC_PHYS_BASE);
	printk("Local APIC ID %lx\n", apic_read(APIC_ID));
	printk("Local APIC Version %lx\n", apic_read(APIC_VERSION));

	set_fixmap(FIX_CO_CPU, CO_CPU_PHYS);
	printk("Cobalt Revision %lx\n", co_cpu_read(CO_CPU_REV));

	set_fixmap(FIX_CO_APIC, CO_APIC_PHYS);
	printk("Cobalt APIC ID %lx\n", co_apic_read(CO_APIC_ID));

	/* Enable Cobalt APIC being careful to NOT change the ID! */
	co_apic_write(CO_APIC_ID, co_apic_read(CO_APIC_ID)|CO_APIC_ENABLE);

	printk("Cobalt APIC enabled: ID reg %lx\n", co_apic_read(CO_APIC_ID));
}
#endif
void __init trap_init(void)
{
	/* Initially up all of the IDT to jump to unexpected */
	init_unexpected_irq();

	if (readl(0x0FFFD9) == 'E' + ('I'<<8) + ('S'<<16) + ('A'<<24))
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
	set_trap_gate(15,&spurious_interrupt_bug);
	set_trap_gate(16,&coprocessor_error);
	set_trap_gate(17,&alignment_check);
	set_system_gate(0x80,&system_call);

	/* set up GDT task & ldt entries */
	set_tss_desc(0, &init_task.tss);
	set_ldt_desc(0, &default_ldt, 1);

	/* Clear NT, so that we won't have troubles with that later on */
	__asm__("pushfl ; andl $0xffffbfff,(%esp) ; popfl");
	load_TR(0);
	load_ldt(0);
#ifdef CONFIG_X86_VISWS_APIC
	superio_init();
	lithium_init();
	cobalt_init();
#endif
}
