/*
 *  arch/mips/kernel/traps.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

/*
 * 'traps.c' handles hardware traps and faults after we have saved some
 * state in 'asm.s'. Currently mostly a debugging-aid, will be extended
 * to mainly kill the offending process (probably by giving it a signal,
 * but possibly by killing it outright if necessary).
 *
 * FIXME: This is the place for a fpu emulator.
 */
#include <linux/head.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/mm.h>

#include <asm/vector.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <asm/mipsregs.h>
#include <asm/bootinfo.h>

static inline void console_verbose(void)
{
	extern int console_loglevel;
	console_loglevel = 15;
}

/*
 * Machine specific interrupt handlers
 */
extern asmlinkage void acer_pica_61_handle_int(void);
extern asmlinkage void decstation_handle_int(void);
extern asmlinkage void deskstation_rpc44_handle_int(void);
extern asmlinkage void deskstation_tyne_handle_int(void);
extern asmlinkage void mips_magnum_4000_handle_int(void);

extern asmlinkage void handle_mod(void);
extern asmlinkage void handle_tlbl(void);
extern asmlinkage void handle_tlbs(void);
extern asmlinkage void handle_adel(void);
extern asmlinkage void handle_ades(void);
extern asmlinkage void handle_ibe(void);
extern asmlinkage void handle_dbe(void);
extern asmlinkage void handle_sys(void);
extern asmlinkage void handle_bp(void);
extern asmlinkage void handle_ri(void);
extern asmlinkage void handle_cpu(void);
extern asmlinkage void handle_ov(void);
extern asmlinkage void handle_tr(void);
extern asmlinkage void handle_vcei(void);
extern asmlinkage void handle_fpe(void);
extern asmlinkage void handle_vced(void);
extern asmlinkage void handle_watch(void);
extern asmlinkage void handle_reserved(void);

static char *cpu_names[] = CPU_NAMES;

unsigned long page_colour_mask;

int kstack_depth_to_print = 24;

/*
 * These constant is for searching for possible module text segments.
 * MODULE_RANGE is a guess of how much space is likely to be vmalloced.
 */
#define MODULE_RANGE (8*1024*1024)

void die_if_kernel(char * str, struct pt_regs * regs, long err)
{
	int	i;
	int	*stack;
	u32	*sp, *pc, addr, module_start, module_end;
	extern	char start_kernel, _etext;

	if ((regs->cp0_status & (ST0_ERL|ST0_EXL)) == 0)
		return;

	sp = (u32 *)regs->reg29;
	pc = (u32 *)regs->cp0_epc;

	console_verbose();
	printk("%s: %08lx\n", str, err );

	show_regs(regs);

	/*
	 * Dump the stack
	 */
	if (STACK_MAGIC != *(u32 *)current->kernel_stack_page)
		printk("Corrupted stack page\n");
	printk("Process %s (pid: %d, stackpage=%08lx)\nStack: ",
		current->comm, current->pid, current->kernel_stack_page);
	for(i=0;i<5;i++)
		printk("%08x ", *sp++);
	stack = (int *) sp;
	for(i=0; i < kstack_depth_to_print; i++) {
		if (((u32) stack & (PAGE_SIZE -1)) == 0)
			break;
		if (i && ((i % 8) == 0))
			printk("\n       ");
		printk("%08lx ", get_user(stack++));
	}
	printk("\nCall Trace: ");
	stack = (int *)sp;
	i = 1;
	module_start = VMALLOC_START;
	module_end = module_start + MODULE_RANGE;
	while (((u32)stack & (PAGE_SIZE -1)) != 0) {
		addr = get_user(stack++);
		/*
		 * If the address is either in the text segment of the
		 * kernel, or in the region which contains vmalloc'ed
		 * memory, it *may* be the address of a calling
		 * routine; if so, print it so that someone tracing
		 * down the cause of the crash will be able to figure
		 * out the call path that was taken.
		 */
		if (((addr >= (u32) &start_kernel) &&
		     (addr <= (u32) &_etext)) ||
		    ((addr >= module_start) && (addr <= module_end))) {
			if (i && ((i % 8) == 0))
				printk("\n       ");
			printk("%08x ", addr);
			i++;
		}
	}

	printk("\nCode : ");
	if ((!verify_area(VERIFY_READ, pc, 5 * sizeof(*pc)) ||
	     KSEGX(pc) == KSEG0 ||
	     KSEGX(pc) == KSEG1) &&
	    (((unsigned long) pc & 3) == 0))
	{
		for(i=0;i<5;i++)
			printk("%08x ", *pc++);
		printk("\n");
	}
	else
		printk("(Bad address in epc)\n");
while(1);
	do_exit(SIGSEGV);
}

static void
fix_ade(struct pt_regs *regs, int write)
{
	printk("Received address error (ade%c)\n", write ? 's' : 'l');
	panic("Fixing address errors not implemented yet");
}

void do_adel(struct pt_regs *regs)
{
	if(current->tss.mflags & MF_FIXADE)
	{
		fix_ade(regs, 0);
		return;
	}
	show_regs(regs);
while(1);
	dump_tlb_nonwired();
	send_sig(SIGSEGV, current, 1);
}

void do_ades(struct pt_regs *regs)
{
	struct task_struct *p;
	unsigned long	pc = regs->cp0_epc;
	int	i;

	if(current->tss.mflags & MF_FIXADE)
	{
		fix_ade(regs, 1);
		return;
	}
while(1);
	read_lock(&tasklist_lock);
	for_each_task(p) {
		if(p->pid >= 2) {
			printk("Process %d\n", p->pid);
			dump_list_process(p, pc);
		}
	}
	read_unlock(&tasklist_lock);
	show_regs(regs);
	dump_tlb_nonwired();
	send_sig(SIGSEGV, current, 1);
}

/*
 * The ibe/dbe exceptions are signaled by onboard hardware and should get
 * a board specific handlers to get maximum available information. Bus
 * errors are always symptom of hardware malfunction or a kernel error.
 *
 * FIXME: Linux/68k sends a SIGSEGV for a buserror which seems to be wrong.
 * This is certainly wrong. Actually, all hardware errors (ades,adel,ibe,dbe)
 * are bus errors and therefor should send a SIGBUS! (Andy)
 */
void do_ibe(struct pt_regs *regs)
{
show_regs(regs);
while(1);
	send_sig(SIGBUS, current, 1);
}

void do_dbe(struct pt_regs *regs)
{
show_regs(regs);
while(1);
	send_sig(SIGBUS, current, 1);
}

void do_ov(struct pt_regs *regs)
{
show_regs(regs);
while(1);
	send_sig(SIGFPE, current, 1);
}

void do_fpe(struct pt_regs *regs)
{
show_regs(regs);
while(1);
	send_sig(SIGFPE, current, 1);
}

void do_bp(struct pt_regs *regs)
{
show_regs(regs);
while(1);
	send_sig(SIGILL, current, 1);
}

void do_tr(struct pt_regs *regs)
{
show_regs(regs);
while(1);
	send_sig(SIGILL, current, 1);
}

void do_ri(struct pt_regs *regs)
{
	struct task_struct *p;
	int	i;

	read_lock(&tasklist_lock);
	for_each_task(p) {
		if(p->pid >= 2) {
			printk("Process %d\n", p->pid);
			dump_list_process(p, 0x7ffff000);
		}
	}
	show_regs(regs);
while(1);
	send_sig(SIGILL, current, 1);
}

void do_cpu(struct pt_regs *regs)
{
	unsigned int cpid;

	cpid = (regs->cp0_cause >> CAUSEB_CE) & 3;
	switch(cpid)
	{
	case 1:
		regs->cp0_status |= ST0_CU1;
		break;
	case 0:
		/*
		 * CPU for cp0 can only happen in user mode
		 */
	case 2:
	case 3:
		send_sig(SIGILL, current, 1);
		break;
	}
}

void do_vcei(struct pt_regs *regs)
{
	/*
	 * Only possible on R4[04]00[SM]C. No handler because
	 * I don't have such a cpu.
	 */
	panic("Caught VCEI exception - can't handle yet\n");
}

void do_vced(struct pt_regs *regs)
{
	/*
	 * Only possible on R4[04]00[SM]C. No handler because
	 * I don't have such a cpu.
	 */
	panic("Caught VCED exception - can't handle yet\n");
}

void do_watch(struct pt_regs *regs)
{
	panic("Caught WATCH exception - can't handle yet\n");
}

void do_reserved(struct pt_regs *regs)
{
	/*
	 * Game over - no way to handle this if it ever occurs.
	 * Most probably caused by a new unknown cpu type or
	 * after another deadly hard/software error.
	 */
	panic("Caught reserved exception - can't handle.\n");
}

void trap_init(void)
{
	unsigned long	i;
	void watch_set(unsigned long, unsigned long);

	if(boot_info.machtype == MACH_MIPS_MAGNUM_4000)
		EISA_bus = 1;

	/*
	 * Setup default vectors
	 */
	for (i=0;i<=31;i++)
		set_except_vector(i, handle_reserved);

	/*
	 * Handling the following exceptions depends mostly of the cpu type
	 */
	switch(boot_info.cputype) {
	/*
	 * The R10000 is in most aspects similar to the R4400.  It however
	 * should get some special optimizations.
	 */
	case CPU_R10000:
		write_32bit_cp0_register(CP0_FRAMEMASK, 0);
		set_cp0_status(ST0_XX, ST0_XX);
		page_colour_mask = 0x3000;
		panic("CPU too expensive - making holiday in the ANDES!");
		break;
	case CPU_R4000MC:
	case CPU_R4400MC:
	case CPU_R4000SC:
	case CPU_R4400SC:
		/*
		 * Handlers not implemented yet.  If should ever be used -
		 * otherwise it's a bug in the Linux/MIPS kernel, anyway.
		 */
		set_except_vector(14, handle_vcei);
		set_except_vector(31, handle_vced);
	case CPU_R4000PC:
	case CPU_R4400PC:
	case CPU_R4200:
     /* case CPU_R4300: */
		/*
		 * Use watch exception to trap on access to address zero
		 */
		set_except_vector(23, handle_watch);
		watch_set(KSEG0, 3);
	case CPU_R4600:
		set_except_vector(1, handle_mod);
		set_except_vector(2, handle_tlbl);
		set_except_vector(3, handle_tlbs);
		set_except_vector(4, handle_adel);
		set_except_vector(5, handle_ades);
		/*
		 * The following two are signaled by onboard hardware and
		 * should get board specific handlers to get maximum
		 * available information.
		 */
		set_except_vector(6, handle_ibe);
		set_except_vector(7, handle_dbe);

		set_except_vector(8, handle_sys);
		set_except_vector(9, handle_bp);
		set_except_vector(10, handle_ri);
		set_except_vector(11, handle_cpu);
		set_except_vector(12, handle_ov);
		set_except_vector(13, handle_tr);
		set_except_vector(15, handle_fpe);

		/*
		 * Compute mask for page_colour().  This is based on the
		 * size of the data cache.  Does the size of the icache
		 * need to be accounted for?
		 */
		i = read_32bit_cp0_register(CP0_CONFIG);
		i = (i >> 26) & 7;
		page_colour_mask = 1 << (12 + i);
		break;
	case CPU_R2000:
	case CPU_R3000:
	case CPU_R3000A:
	case CPU_R3041:
	case CPU_R3051:
	case CPU_R3052:
	case CPU_R3081:
	case CPU_R3081E:
	case CPU_R6000:
	case CPU_R6000A:
	case CPU_R8000:
		printk("Detected unsupported CPU type %s.\n",
			cpu_names[boot_info.cputype]);
		panic("Can't handle CPU\n");
		break;

	case CPU_UNKNOWN:
	default:
		panic("Unknown CPU type");
	}

	/*
	 * The interrupt handler mostly depends of the board type.
	 */
	set_except_vector(0, feature->handle_int);
}
