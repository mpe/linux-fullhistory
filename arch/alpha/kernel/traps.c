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
#include <asm/uaccess.h>
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
	printk("%s: arithmetic trap at %016lx: %02lx %016lx\n",
		current->comm, regs.pc, summary, write_mask);
	die_if_kernel("Arithmetic fault", &regs, 0);
	force_sig(SIGFPE, current);
}

asmlinkage void do_entIF(unsigned long type, unsigned long a1,
			 unsigned long a2, unsigned long a3, unsigned long a4,
			 unsigned long a5, struct pt_regs regs)
{
	extern int ptrace_cancel_bpt (struct task_struct *who);

	die_if_kernel("Instruction fault", &regs, type);
	switch (type) {
	      case 0: /* breakpoint */
		if (ptrace_cancel_bpt(current)) {
			regs.pc -= 4;	/* make pc point to former bpt */
		}
		force_sig(SIGTRAP, current);
		break;

	      case 2: /* gentrap */
		/*
		 * The exception code should be passed on to the signal
		 * handler as the second argument.  Linux doesn't do that
		 * yet (also notice that Linux *always* behaves like
		 * DEC Unix with SA_SIGINFO off; see DEC Unix man page
		 * for sigaction(2)).
		 */
		switch ((long) regs.r16) {
		      case GEN_INTOVF: case GEN_INTDIV: case GEN_FLTOVF:
		      case GEN_FLTDIV: case GEN_FLTUND: case GEN_FLTINV:
		      case GEN_FLTINE:
			force_sig(SIGFPE, current);
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
			force_sig(SIGILL, current);
			break;
		}
		break;

	      case 1: /* bugcheck */
	      case 3: /* FEN fault */
		force_sig(SIGILL, current);
		break;

	      case 4: /* opDEC */
#ifdef CONFIG_ALPHA_NEED_ROUNDING_EMULATION
		{
			extern long alpha_fp_emul (unsigned long pc);
			unsigned int opcode;

			/* get opcode of faulting instruction: */
			get_user(opcode, (__u32*)(regs.pc - 4));
			opcode >>= 26;
			if (opcode == 0x16) {
				/*
				 * It's a FLTI instruction, emulate it
				 * (we don't do no stinkin' VAX fp...)
				 */
				if (!alpha_fp_emul(regs.pc - 4))
					force_sig(SIGFPE, current);
				break;
			}
		}
#endif
		force_sig(SIGILL, current);
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


/* Macro for exception fixup code to access integer registers.  */
#define una_reg(r)  (regs.regs[(r) >= 16 && (r) <= 18 ? (r)+19 : (r)])


asmlinkage void do_entUna(void * va, unsigned long opcode, unsigned long reg,
	unsigned long a3, unsigned long a4, unsigned long a5,
	struct allregs regs)
{
	static int cnt = 0;
	static long last_time = 0;
	long error, tmp1, tmp2, tmp3, tmp4;
	unsigned long pc = regs.pc - 4;
	unsigned fixup;

	if (cnt >= 5 && jiffies - last_time > 5*HZ) {
		cnt = 0;
	}
	if (++cnt < 5) {
		printk("kernel: unaligned trap at %016lx: %p %lx %ld\n",
		       pc, va, opcode, reg);
	}
	last_time = jiffies;

	unaligned[0].count++;
	unaligned[0].va = (unsigned long) va;
	unaligned[0].pc = pc;

	/* We don't want to use the generic get/put unaligned macros as
	   we want to trap exceptions.  Only if we actually get an
	   exception will we decide whether we should have caught it.  */

	switch (opcode) {
#ifdef __HAVE_CPU_BWX
	case 0x0c: /* ldwu */
		__asm__ __volatile__(
		"1:	ldq_u %1,0(%3)\n"
		"2:	ldq_u %2,1(%3)\n"
		"	extwl %1,%3,%1\n"
		"	extwh %2,%3,%2\n"
		"3:\n"
		".section __ex_table,\"a\"\n"
		"	.gprel32 1b\n"
		"	lda %1,3b-1b(%0)\n"
		"	.gprel32 2b\n"
		"	lda %2,3b-2b(%0)\n"
		".text"
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2)
			: "r"(va), "0"(0));
		if (error)
			goto got_exception;
		una_reg(reg) = tmp1|tmp2;
		return;
#endif

	case 0x28: /* ldl */
		__asm__ __volatile__(
		"1:	ldq_u %1,0(%3)\n"
		"2:	ldq_u %2,3(%3)\n"
		"	extll %1,%3,%1\n"
		"	extlh %2,%3,%2\n"
		"3:\n"
		".section __ex_table,\"a\"\n"
		"	.gprel32 1b\n"
		"	lda %1,3b-1b(%0)\n"
		"	.gprel32 2b\n"
		"	lda %2,3b-2b(%0)\n"
		".text"
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2)
			: "r"(va), "0"(0));
		if (error)
			goto got_exception;
		una_reg(reg) = (int)(tmp1|tmp2);
		return;

	case 0x29: /* ldq */
		__asm__ __volatile__(
		"1:	ldq_u %1,0(%3)\n"
		"2:	ldq_u %2,7(%3)\n"
		"	extql %1,%3,%1\n"
		"	extqh %2,%3,%2\n"
		"3:\n"
		".section __ex_table,\"a\"\n"
		"	.gprel32 1b\n"
		"	lda %1,3b-1b(%0)\n"
		"	.gprel32 2b\n"
		"	lda %2,3b-2b(%0)\n"
		".text"
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2)
			: "r"(va), "0"(0));
		if (error)
			goto got_exception;
		una_reg(reg) = tmp1|tmp2;
		return;

	/* Note that the store sequences do not indicate that they change
	   memory because it _should_ be affecting nothing in this context.
	   (Otherwise we have other, much larger, problems.)  */
#ifdef __HAVE_CPU_BWX
	case 0x0d: /* stw */
		__asm__ __volatile__(
		"1:	ldq_u %2,1(%5)\n"
		"2:	ldq_u %1,0(%5)\n"
		"	inswh %6,%5,%4\n"
		"	inswl %6,%5,%3\n"
		"	mskwh %2,%5,%2\n"
		"	mskwl %1,%5,%1\n"
		"	or %2,%4,%2\n"
		"	or %1,%3,%1\n"
		"3:	stq_u %2,1(%5)\n"
		"4:	stq_u %1,0(%5)\n"
		"5:\n"
		".section __ex_table,\"a\"\n"
		"	.gprel32 1b\n"
		"	lda %2,5b-1b(%0)\n"
		"	.gprel32 2b\n"
		"	lda %1,5b-2b(%0)\n"
		"	.gprel32 3b\n"
		"	lda $31,5b-3b(%0)\n"
		"	.gprel32 4b\n"
		"	lda $31,5b-4b(%0)\n"
		".text"
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2),
			  "=&r"(tmp3), "=&r"(tmp4)
			: "r"(va), "r"(una_reg(reg)), "0"(0));
		if (error)
			goto got_exception;
		return;
#endif

	case 0x2c: /* stl */
		__asm__ __volatile__(
		"1:	ldq_u %2,3(%5)\n"
		"2:	ldq_u %1,0(%5)\n"
		"	inslh %6,%5,%4\n"
		"	insll %6,%5,%3\n"
		"	msklh %2,%5,%2\n"
		"	mskll %1,%5,%1\n"
		"	or %2,%4,%2\n"
		"	or %1,%3,%1\n"
		"3:	stq_u %2,3(%5)\n"
		"4:	stq_u %1,0(%5)\n"
		"5:\n"
		".section __ex_table,\"a\"\n"
		"	.gprel32 1b\n"
		"	lda %2,5b-1b(%0)\n"
		"	.gprel32 2b\n"
		"	lda %1,5b-2b(%0)\n"
		"	.gprel32 3b\n"
		"	lda $31,5b-3b(%0)\n"
		"	.gprel32 4b\n"
		"	lda $31,5b-4b(%0)\n"
		".text"
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2),
			  "=&r"(tmp3), "=&r"(tmp4)
			: "r"(va), "r"(una_reg(reg)), "0"(0));
		if (error)
			goto got_exception;
		return;

	case 0x2d: /* stq */
		__asm__ __volatile__(
		"1:	ldq_u %2,7(%5)\n"
		"2:	ldq_u %1,0(%5)\n"
		"	insqh %6,%5,%4\n"
		"	insql %6,%5,%3\n"
		"	mskqh %2,%5,%2\n"
		"	mskql %1,%5,%1\n"
		"	or %2,%4,%2\n"
		"	or %1,%3,%1\n"
		"3:	stq_u %2,7(%5)\n"
		"4:	stq_u %1,0(%5)\n"
		"5:\n"
		".section __ex_table,\"a\"\n\t"
		"	.gprel32 1b\n"
		"	lda %2,5b-1b(%0)\n"
		"	.gprel32 2b\n"
		"	lda %1,5b-2b(%0)\n"
		"	.gprel32 3b\n"
		"	lda $31,5b-3b(%0)\n"
		"	.gprel32 4b\n"
		"	lda $31,5b-4b(%0)\n"
		".text"
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2),
			  "=&r"(tmp3), "=&r"(tmp4)
			: "r"(va), "r"(una_reg(reg)), "0"(0));
		if (error)
			goto got_exception;
		return;
	}
	printk("Bad unaligned kernel access at %016lx: %p %lx %ld\n",
		pc, va, opcode, reg);
	do_exit(SIGSEGV);
	return;

got_exception:
	/* Ok, we caught the exception, but we don't want it.  Is there
	   someone to pass it along to?  */
	if ((fixup = search_exception_table(pc)) != 0) {
		unsigned long newpc;
		newpc = fixup_exception(una_reg, fixup, pc);
		printk("Forwarding unaligned exception at %lx (%lx)\n",
		       pc, newpc);
		(&regs)->pc = newpc;
		return;
	}

	/* Yikes!  No one to forward the exception to.  */
	printk("%s: unhandled unaligned exception at pc=%lx ra=%lx"
	       " (bad address = %p)\n", current->comm,
	       pc, una_reg(26), va);
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
		force_sig(SIGSEGV, current);
		return;
	}

	reg_addr = frame;
	if (opcode >= 0x28) {
		/* it's an integer load/store */
		switch (reg) {
		      case 0: case 1: case 2: case 3: case 4:
		      case 5: case 6: case 7: case 8:
			/* v0-t7 in SAVE_ALL frame */
			reg_addr += 7 + reg;
			break;

		      case 9: case 10: case 11: case 12:
		      case 13: case 14: case 15:
			/* s0-s6 in entUna frame */
			reg_addr += (reg - 9);
			break;

		      case 16: case 17: case 18:
			/* a0-a2 in PAL frame */
			reg_addr += 7 + 20 + 3 + (reg - 16);
			break;

		      case 19: case 20: case 21: case 22: case 23:
		      case 24: case 25: case 26: case 27: case 28:
			/* a3-at in SAVE_ALL frame */
			reg_addr += 7 + 9 + (reg - 19);
			break;

		      case 29:
			/* gp in PAL frame */
			reg_addr += 7 + 20 + 2;
			break;

		      case 30:
			/* usp in PAL regs */
			usp = rdusp();
			reg_addr = &usp;
			break;

		      case 31:
			/* zero "register" */
			reg_addr = &zero;
			break;
		}
	}

	switch (opcode) {
	      case 0x22: /* lds */
		alpha_write_fp_reg(reg, s_mem_to_reg(
			get_unaligned((unsigned int *)va)));
		break;
	      case 0x26: /* sts */
		put_unaligned(s_reg_to_mem(alpha_read_fp_reg(reg)),
			      (unsigned int *)va);
		break;

	      case 0x23: /* ldt */
		alpha_write_fp_reg(reg, get_unaligned((unsigned long *)va));
		break;
	      case 0x27: /* stt */
		put_unaligned(alpha_read_fp_reg(reg), (unsigned long *)va);
		break;

	      case 0x28: /* ldl */
		*reg_addr = get_unaligned((int *)va);
		break;
	      case 0x2c: /* stl */
		put_unaligned(*reg_addr, (int *)va);
		break;

	      case 0x29: /* ldq */
		*reg_addr = get_unaligned((long *)va);
		break;
	      case 0x2d: /* stq */
		put_unaligned(*reg_addr, (long *)va);
		break;

	      default:
		*pc_addr -= 4;	/* make pc point to faulting insn */
		force_sig(SIGBUS, current);
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
			  unsigned long a3, unsigned long a4, unsigned long a5,
			  struct pt_regs regs)
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
