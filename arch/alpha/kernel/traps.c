/*
 * kernel/traps.c
 *
 * (C) Copyright 1994 Linus Torvalds
 */

/*
 * This file initializes the trap entry points
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/tty.h>

#include <asm/gentrap.h>
#include <asm/segment.h>
#include <asm/unaligned.h>

void die_if_kernel(char * str, struct pt_regs * regs, long err)
{
	long i;
	unsigned long sp;
	unsigned int * pc;

	if (regs->ps & 8)
		return;
	printk("%s(%d): %s %ld\n", current->comm, current->pid, str, err);
	sp = (unsigned long) (regs+1);
	printk("pc = [<%lx>] ps = %04lx\n", regs->pc, regs->ps);
	printk("rp = [<%lx>] sp = %lx\n", regs->r26, sp);
	printk("r0=%lx r1=%lx r2=%lx r3=%lx\n",
		regs->r0, regs->r1, regs->r2, regs->r3);
	printk("r8=%lx\n", regs->r8);
	printk("r16=%lx r17=%lx r18=%lx r19=%lx\n",
		regs->r16, regs->r17, regs->r18, regs->r19);
	printk("r20=%lx r21=%lx r22=%lx r23=%lx\n",
		regs->r20, regs->r21, regs->r22, regs->r23);
	printk("r24=%lx r25=%lx r26=%lx r27=%lx\n",
		regs->r24, regs->r25, regs->r26, regs->r27);
	printk("r28=%lx r29=%lx r30=%lx\n",
		regs->r28, regs->gp, sp);
	printk("Code:");
	pc = (unsigned int *) regs->pc;
	for (i = -3; i < 6; i++)
		printk("%c%08x%c",i?' ':'<',pc[i],i?' ':'>');
	printk("\n");
	do_exit(SIGSEGV);
}

asmlinkage void do_entArith(unsigned long summary, unsigned long write_mask,
			    unsigned long a2, unsigned long a3,
			    unsigned long a4, unsigned long a5,
			    struct pt_regs regs)
{
	if ((summary & 1)) {
		extern long alpha_fp_emul_imprecise (struct pt_regs * regs,
						     unsigned long write_mask);
		/*
		 * Software-completion summary bit is set, so try to
		 * emulate the instruction.
		 */
		if (alpha_fp_emul_imprecise(&regs, write_mask)) {
			return;		/* emulation was successful */
		}
	}
	printk("Arithmetic trap at %016lx: %02lx %016lx\n",
	       regs.pc, summary, write_mask);
	die_if_kernel("Arithmetic fault", &regs, 0);
	send_sig(SIGFPE, current, 1);
}

asmlinkage void do_entIF(unsigned long type, unsigned long a1, unsigned long a2,
			 unsigned long a3, unsigned long a4, unsigned long a5,
			 struct pt_regs regs)
{
	extern int ptrace_cancel_bpt (struct task_struct *who);

	die_if_kernel("Instruction fault", &regs, type);
	switch (type) {
	      case 0: /* breakpoint */
		if (ptrace_cancel_bpt(current)) {
			regs.pc -= 4;	/* make pc point to former bpt */
		}
		send_sig(SIGTRAP, current, 1);
		break;

	      case 2: /* gentrap */
		/*
		 * The translation from the gentrap error code into a
		 * siginfo structure (see /usr/include/sys/siginfo.h)
		 * is missing as Linux does not presently support the
		 * siginfo argument that is normally passed to a
		 * signal handler.
		 */
		switch ((long) regs.r16) {
		      case GEN_INTOVF: case GEN_INTDIV: case GEN_FLTOVF:
		      case GEN_FLTDIV: case GEN_FLTUND: case GEN_FLTINV:
		      case GEN_FLTINE:
			send_sig(SIGFPE, current, 1);
			break;

		      case GEN_DECOVF:
		      case GEN_DECDIV:
		      case GEN_DECINV:
		      case GEN_ROPRAND:
		      case GEN_ASSERTERR:
		      case GEN_NULPTRERR:
		      case GEN_STKOVF:
		      case GEN_STRLENERR:
		      case GEN_SUBSTRERR:
		      case GEN_RANGERR:
		      case GEN_SUBRNG:
		      case GEN_SUBRNG1:
		      case GEN_SUBRNG2:
		      case GEN_SUBRNG3:
		      case GEN_SUBRNG4:
		      case GEN_SUBRNG5:
		      case GEN_SUBRNG6:
		      case GEN_SUBRNG7:
			send_sig(SIGILL, current, 1);
			break;
		}
		break;

	      case 1: /* bugcheck */
	      case 3: /* FEN fault */
		send_sig(SIGILL, current, 1);
		break;

	      case 4: /* opDEC */
#ifdef CONFIG_ALPHA_NEED_ROUNDING_EMULATION
		{
			extern long alpha_fp_emul (unsigned long pc);
			unsigned int opcode;

			/* get opcode of faulting instruction: */
			opcode = get_user((__u32*)(regs.pc - 4)) >> 26;
			if (opcode == 0x16) {
				/*
				 * It's a FLTI instruction, emulate it
				 * (we don't do no stinkin' VAX fp...)
				 */
				if (!alpha_fp_emul(regs.pc - 4))
					send_sig(SIGFPE, current, 1);
				break;
			}
		}
#endif
		send_sig(SIGILL, current, 1);
		break;

	      default:
		panic("do_entIF: unexpected instruction-fault type");
	}
}

/*
 * entUna has a different register layout to be reasonably simple. It
 * needs access to all the integer registers (the kernel doesn't use
 * fp-regs), and it needs to have them in order for simpler access.
 *
 * Due to the non-standard register layout (and because we don't want
 * to handle floating-point regs), user-mode unaligned accesses are
 * handled separately by do_entUnaUser below.
 *
 * Oh, btw, we don't handle the "gp" register correctly, but if we fault
 * on a gp-register unaligned load/store, something is _very_ wrong
 * in the kernel anyway..
 */
struct allregs {
	unsigned long regs[32];
	unsigned long ps, pc, gp, a0, a1, a2;
};

struct unaligned_stat {
	unsigned long count, va, pc;
} unaligned[2];

asmlinkage void do_entUna(void * va, unsigned long opcode, unsigned long reg,
	unsigned long a3, unsigned long a4, unsigned long a5,
	struct allregs regs)
{
	static int cnt = 0;
	static long last_time = 0;

	if (cnt >= 5 && jiffies - last_time > 5*HZ) {
		cnt = 0;
	}
	if (++cnt < 5) {
		printk("kernel: unaligned trap at %016lx: %p %lx %ld\n",
		       regs.pc - 4, va, opcode, reg);
	}
	last_time = jiffies;

	++unaligned[0].count;
	unaligned[0].va = (unsigned long) va - 4;
	unaligned[0].pc = regs.pc;

	/* $16-$18 are PAL-saved, and are offset by 19 entries */
	if (reg >= 16 && reg <= 18)
		reg += 19;
	switch (opcode) {
		case 0x28: /* ldl */
			*(reg+regs.regs) = (int) ldl_u(va);
			return;
		case 0x29: /* ldq */
			*(reg+regs.regs) = ldq_u(va);
			return;
		case 0x2c: /* stl */
			stl_u(*(reg+regs.regs), va);
			return;
		case 0x2d: /* stq */
			stq_u(*(reg+regs.regs), va);
			return;
	}
	printk("Bad unaligned kernel access at %016lx: %p %lx %ld\n",
		regs.pc, va, opcode, reg);
	do_exit(SIGSEGV);
}

/*
 * Convert an s-floating point value in memory format to the
 * corresponding value in register format.  The exponent
 * needs to be remapped to preserve non-finite values
 * (infinities, not-a-numbers, denormals).
 */
static inline unsigned long s_mem_to_reg (unsigned long s_mem)
{
	unsigned long frac    = (s_mem >>  0) & 0x7fffff;
	unsigned long sign    = (s_mem >> 31) & 0x1;
	unsigned long exp_msb = (s_mem >> 30) & 0x1;
	unsigned long exp_low = (s_mem >> 23) & 0x7f;
	unsigned long exp;

	exp = (exp_msb << 10) | exp_low;	/* common case */
	if (exp_msb) {
		if (exp_low == 0x7f) {
			exp = 0x3ff;
		}
	} else {
		if (exp_low == 0x00) {
			exp = 0x000;
		} else {
			exp |= (0x7 << 8);
		}
	}
	return (sign << 63) | (exp << 52) | (frac << 29);
}

/*
 * Convert an s-floating point value in register format to the
 * corresponding value in memory format.
 */
static inline unsigned long s_reg_to_mem (unsigned long s_reg)
{
	return ((s_reg >> 62) << 30) | ((s_reg << 5) >> 34);
}

/*
 * Handle user-level unaligned fault.  Handling user-level unaligned
 * faults is *extremely* slow and produces nasty messages.  A user
 * program *should* fix unaligned faults ASAP.
 *
 * Notice that we have (almost) the regular kernel stack layout here,
 * so finding the appropriate registers is a little more difficult
 * than in the kernel case.
 *
 * Finally, we handle regular integer load/stores only.  In
 * particular, load-linked/store-conditionally and floating point
 * load/stores are not supported.  The former make no sense with
 * unaligned faults (they are guaranteed to fail) and I don't think
 * the latter will occur in any decent program.
 *
 * Sigh. We *do* have to handle some FP operations, because GCC will
 * uses them as temporary storage for integer memory to memory copies.
 * However, we need to deal with stt/ldt and sts/lds only.
 */
asmlinkage void do_entUnaUser(void * va, unsigned long opcode, unsigned long reg,
			      unsigned long * frame)
{
	long dir, size;
	unsigned long *reg_addr, *pc_addr, usp, zero = 0;
	static int cnt = 0;
	static long last_time = 0;
	extern void alpha_write_fp_reg (unsigned long reg, unsigned long val);
	extern unsigned long alpha_read_fp_reg (unsigned long reg);

	pc_addr = frame + 7 + 20 + 1;			/* pc in PAL frame */

	if (cnt >= 5 && jiffies - last_time > 5*HZ) {
		cnt = 0;
	}
	if (++cnt < 5) {
		printk("%s(%d): unaligned trap at %016lx: %p %lx %ld\n",
		       current->comm, current->pid,
		       *pc_addr - 4, va, opcode, reg);
	}
	last_time = jiffies;

	++unaligned[1].count;
	unaligned[1].va = (unsigned long) va - 4;
	unaligned[1].pc = *pc_addr;

	dir = VERIFY_READ;
	if (opcode & 0x4) {
		/* it's a stl, stq, stt, or sts */
		dir = VERIFY_WRITE;
	}
	size = 4;
	if (opcode & 0x1) {
		/* it's a quadword op */
		size = 8;
	}
	if (verify_area(dir, va, size)) {
		*pc_addr -= 4;	/* make pc point to faulting insn */
		send_sig(SIGSEGV, current, 1);
		return;
	}

	reg_addr = frame;
	if (opcode >= 0x28) {
		/* it's an integer load/store */
		if (reg < 9) {
			reg_addr += 7 + reg;			/* v0-t7 in SAVE_ALL frame */
		} else if (reg < 16) {
			reg_addr += (reg - 9);			/* s0-s6 in entUna frame */
		} else if (reg < 19) {
			reg_addr += 7 + 20 + 3 + (reg - 16);	/* a0-a2 in PAL frame */
		} else if (reg < 29) {
			reg_addr += 7 + 9 + (reg - 19);		/* a3-at in SAVE_ALL frame */
		} else {
			switch (reg) {
			      case 29:				/* gp in PAL frame */
				reg_addr += 7 + 20 + 2;
				break;
			      case 30:				/* usp in PAL regs */
				usp = rdusp();
				reg_addr = &usp;
				break;
			      case 31:				/* zero "register" */
				reg_addr = &zero;
				break;
			}
		}
	}

	switch (opcode) {
	      case 0x22:						/* lds */
		alpha_write_fp_reg(reg, s_mem_to_reg(ldl_u(va)));
		break;
	      case 0x26:						/* lds */
		alpha_write_fp_reg(reg, s_reg_to_mem(ldl_u(va)));
		break;

	      case 0x23: alpha_write_fp_reg(reg, ldq_u(va)); break;	/* ldt */
	      case 0x27: stq_u(alpha_read_fp_reg(reg), va);  break;	/* stt */

	      case 0x28: *reg_addr = (int) ldl_u(va);	     break;	/* ldl */
	      case 0x29: *reg_addr = ldq_u(va);		     break;	/* ldq */
	      case 0x2c: stl_u(*reg_addr, va);		     break;	/* stl */
	      case 0x2d: stq_u(*reg_addr, va);		     break;	/* stq */
	      default:
		*pc_addr -= 4;	/* make pc point to faulting insn */
		send_sig(SIGBUS, current, 1);
		return;
	}

	if (opcode >= 0x28 && reg == 30 && dir == VERIFY_WRITE) {
		wrusp(usp);
	} 
}

/*
 * DEC means people to use the "retsys" instruction for return from
 * a system call, but they are clearly misguided about this. We use
 * "rti" in all cases, and fill in the stack with the return values.
 * That should make signal handling etc much cleaner.
 *
 * Even more horribly, DEC doesn't allow system calls from kernel mode.
 * "Security" features letting the user do something the kernel can't
 * are a thinko. DEC palcode is strange. The PAL-code designers probably
 * got terminally tainted by VMS at some point.
 */
asmlinkage long do_entSys(unsigned long a0, unsigned long a1, unsigned long a2,
	unsigned long a3, unsigned long a4, unsigned long a5, struct pt_regs regs)
{
	if (regs.r0 != 112)
	  printk("<sc %ld(%lx,%lx,%lx)>", regs.r0, a0, a1, a2);
	return -1;
}

extern asmlinkage void entMM(void);
extern asmlinkage void entIF(void);
extern asmlinkage void entArith(void);
extern asmlinkage void entUna(void);
extern asmlinkage void entSys(void);

void trap_init(void)
{
	unsigned long gptr;

	/*
	 * Tell PAL-code what global pointer we want in the kernel..
	 */
	__asm__("br %0,___tmp\n"
		"___tmp:\tldgp %0,0(%0)"
		: "=r" (gptr));
	wrkgp(gptr);

	wrent(entArith, 1);
	wrent(entMM, 2);
	wrent(entIF, 3);
	wrent(entUna, 4);
	wrent(entSys, 5);
}
