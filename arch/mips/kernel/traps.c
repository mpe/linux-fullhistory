/*
 *  arch/mips/kernel/traps.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * 'traps.c' handles hardware traps and faults after we have saved some
 * state in 'asm.s'. Currently mostly a debugging-aid, will be extended
 * to mainly kill the offending process (probably by giving it a signal,
 * but possibly by killing it outright if necessary).
 */
#include <linux/head.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/config.h>
#include <linux/timer.h>

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

#define get_seg_byte(seg,addr) ({ \
register unsigned char __res; \
__res = get_user_byte(addr); \
__res;})

#define get_seg_long(seg,addr) ({ \
register unsigned long __res; \
__res = get_user_word(addr); \
__res;})

extern asmlinkage void deskstation_tyne_handle_int(void);
extern asmlinkage void acer_pica_61_handle_int(void);
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

char *cpu_names[] = CPU_NAMES;

int kstack_depth_to_print = 24;

/*
 * These constants are for searching for possible module text
 * segments.  VMALLOC_OFFSET comes from mm/vmalloc.c; MODULE_RANGE is
 * a guess of how much space is likely to be vmalloced.
 */
#define VMALLOC_OFFSET (8*1024*1024)
#define MODULE_RANGE (8*1024*1024)

void die_if_kernel(char * str, struct pt_regs * regs, long err)
{
	int i;
	unsigned long *sp, *pc;
	unsigned long *stack, addr, module_start, module_end;
	extern char start_kernel, etext;

	if (regs->cp0_status & (ST0_ERL|ST0_EXL) == 0)
		return;

	sp = (unsigned long *)regs->reg29;
	pc = (unsigned long *)regs->cp0_epc;

	console_verbose();
	printk("%s: %08lx\n", str, err );

	/*
	 * Saved main processor registers
	 */
	printk("at   : %08lx\n", regs->reg1);
	printk("v0   : %08lx %08lx\n", regs->reg2, regs->reg3);
	printk("a0   : %08lx %08lx %08lx %08lx\n",
	       regs->reg4, regs->reg5, regs->reg6, regs->reg7);
	printk("t0   : %08lx %08lx %08lx %08lx %08lx\n",
	       regs->reg8, regs->reg9, regs->reg10, regs->reg11, regs->reg12);
	printk("t5   : %08lx %08lx %08lx %08lx %08lx\n",
	       regs->reg13, regs->reg14, regs->reg15, regs->reg24, regs->reg25);
	printk("s0   : %08lx %08lx %08lx %08lx\n",
	       regs->reg16, regs->reg17, regs->reg18, regs->reg19);
	printk("s4   : %08lx %08lx %08lx %08lx\n",
	       regs->reg20, regs->reg21, regs->reg22, regs->reg23);
	printk("gp   : %08lx\n", regs->reg28);
	printk("sp   : %08lx\n", regs->reg29);
	printk("fp/s8: %08lx\n", regs->reg30);
	printk("ra   : %08lx\n", regs->reg31);

	/*
	 * Saved cp0 registers
	 */
	printk("epc  : %08lx\nStatus: %08lx\nCause : %08lx\n",
	       regs->cp0_epc, regs->cp0_status, regs->cp0_cause);

	/*
	 * Some goodies...
	 */
	printk("Int  : %ld\n", regs->interrupt);

	/*
	 * Dump the stack
	 */
	if (STACK_MAGIC != *(unsigned long *)current->kernel_stack_page)
		printk("Corrupted stack page\n");
	printk("Process %s (pid: %d, process nr: %d, stackpage=%08lx)\nStack: ",
		current->comm, current->pid, 0xffff & i,
	        current->kernel_stack_page);
	for(i=0;i<5;i++)
		printk("%08lx ", *sp++);
	stack = (unsigned long *) sp;
	for(i=0; i < kstack_depth_to_print; i++) {
		if (((long) stack & 4095) == 0)
			break;
		if (i && ((i % 8) == 0))
			printk("\n       ");
		printk("%08lx ", get_seg_long(ss,stack++));
	}
	printk("\nCall Trace: ");
	stack = (unsigned long *) sp;
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
		     (addr <= (unsigned long) &etext)) ||
		    ((addr >= module_start) && (addr <= module_end))) {
			if (i && ((i % 8) == 0))
				printk("\n       ");
			printk("%08lx ", addr);
			i++;
		}
	}

	printk("\nCode : ");
	for(i=0;i<5;i++)
		printk("%08lx ", *pc++);
	printk("\n");
	do_exit(SIGSEGV);
}

void do_adel(struct pt_regs *regs)
{
	send_sig(SIGSEGV, current, 1);
}

void do_ades(struct pt_regs *regs)
{
	send_sig(SIGSEGV, current, 1);
}

void do_ibe(struct pt_regs *regs)
{
	send_sig(SIGSEGV, current, 1);
}

void do_dbe(struct pt_regs *regs)
{
	send_sig(SIGSEGV, current, 1);
}

void do_ov(struct pt_regs *regs)
{
	send_sig(SIGFPE, current, 1);
}

void do_fpe(struct pt_regs *regs)
{
	/*
	 * FIXME: This is the place for a fpu emulator. Not written
	 * yet and the demand seems to be quite low.
	 */
	printk("Caught FPE exception at %lx.\n", regs->cp0_epc);
	send_sig(SIGFPE, current, 1);
}

void do_bp(struct pt_regs *regs)
{
	send_sig(SIGILL, current, 1);
}

void do_tr(struct pt_regs *regs)
{
	send_sig(SIGILL, current, 1);
}

void do_ri(struct pt_regs *regs)
{
	send_sig(SIGILL, current, 1);
}

void do_cpu(struct pt_regs *regs)
{
	unsigned long pc;
	unsigned int insn;

	/*
	 * Check whether this was a cp1 instruction
	 */
	pc = regs->cp0_epc;
	if (regs->cp0_cause & (1<<31))
		pc += 4;
	insn = *(unsigned int *)pc;
	insn &= 0xfc000000;
	switch(insn) {
		case 0x44000000:
		case 0xc4000000:
		case 0xe4000000:
			printk("CP1 instruction - enabling cp1.\n");
			regs->cp0_status |= ST0_CU1;
			/*
			 * No need to handle branch delay slots
			 */
			break;
		default:
			/*
			 * This wasn't a cp1 instruction and therefore illegal.
			 * Default is to kill the process.
			 */
			send_sig(SIGILL, current, 1);
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
	/*
	 * Only possible on R4[04]00. No way to handle this because
	 * I don't have such a cpu.
	 */
	panic("Caught WATCH exception - can't handle yet\n");
}

void do_reserved(struct pt_regs *regs)
{
	/*
	 * Game over - no way to handle this if it ever occurs.
	 * Most probably caused by a new unknown cpu type or a
	 * after another deadly hard/software error.
	 */
	panic("Caught reserved exception - can't handle.\n");
}

void trap_init(void)
{
	int	i;

	/*
	 * FIXME: Mips Magnum R4000 has an EISA bus!
	 */
	EISA_bus = 0;

	/*
	 * Setup default vectors
	 */
	for (i=0;i<=31;i++)
		set_except_vector(i, handle_reserved);

	/*
	 * Handling the following exceptions depends mostly of the cpu type
	 */
	switch(boot_info.cputype) {
	case CPU_R4000MC:
	case CPU_R4400MC:
	case CPU_R4000SC:
	case CPU_R4400SC:
		/*
		 * Handlers not implemented yet
		 */
		set_except_vector(14, handle_vcei);
		set_except_vector(31, handle_vced);
	case CPU_R4000PC:
	case CPU_R4400PC:
		/*
		 * Handler not implemented yet
		 */
		set_except_vector(23, handle_watch);
	case CPU_R4200:
	case CPU_R4600:
		set_except_vector(1, handle_mod);
		set_except_vector(2, handle_tlbl);
		set_except_vector(3, handle_tlbs);
		set_except_vector(4, handle_adel);
		set_except_vector(5, handle_ades);
		set_except_vector(6, handle_ibe);
		set_except_vector(7, handle_dbe);
		set_except_vector(8, handle_sys);
		set_except_vector(9, handle_bp);
		set_except_vector(10, handle_ri);
		set_except_vector(11, handle_cpu);
		set_except_vector(12, handle_ov);
		set_except_vector(13, handle_tr);
		set_except_vector(15, handle_fpe);
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
	case CPU_R10000:
		printk("Detected unsupported CPU type %s.\n",
		       cpu_names[boot_info.cputype]);
		panic("Can't handle CPU\n");
		break;
	case CPU_UNKNOWN:
	default:
		panic("Unknown type of CPU");
		}

	/*
	 * The interrupt handler depends of both type of the board and cpu
	 */
	switch(boot_info.machtype) {
	case MACH_DESKSTATION_TYNE:
		set_except_vector(0, deskstation_tyne_handle_int);
		break;
	case MACH_ACER_PICA_61:
		set_except_vector(0, acer_pica_61_handle_int);
		break;
	default:
		panic("Unknown machine type");
		}
}
