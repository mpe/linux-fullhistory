/*
 * align.c - handle alignment exceptions for the Power PC.
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996 Paul Mackerras	(paulus@cs.anu.edu.au).
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/system.h>

struct aligninfo {
	unsigned char len;
	unsigned char flags;
};

#define INVALID	{ 0, 0 }

#define LD	1	/* load */
#define ST	2	/* store */
#define	SE	4	/* sign-extend value */
#define F	8	/* to/from fp regs */
#define U	0x10	/* update index register */
#define M	0x20	/* multiple load/store */
#define S	0x40	/* single-precision fp, or byte-swap value */
#define HARD	0x80	/* string, stwcx. */

/*
 * The PowerPC stores certain bits of the instruction that caused the
 * alignment exception in the DSISR register.  This array maps those
 * bits to information about the operand length and what the
 * instruction would do.
 */
static struct aligninfo aligninfo[128] = {
	{ 4, LD },		/* 00 0 0000: lwz / lwarx */
	INVALID,		/* 00 0 0001 */
	{ 4, ST },		/* 00 0 0010: stw */
	INVALID,		/* 00 0 0011 */
	{ 2, LD },		/* 00 0 0100: lhz */
	{ 2, LD+SE },		/* 00 0 0101: lha */
	{ 2, ST },		/* 00 0 0110: sth */
	{ 4, LD+M },		/* 00 0 0111: lmw */
	{ 4, LD+F+S },		/* 00 0 1000: lfs */
	{ 8, LD+F },		/* 00 0 1001: lfd */
	{ 4, ST+F+S },		/* 00 0 1010: stfs */
	{ 8, ST+F },		/* 00 0 1011: stfd */
	INVALID,		/* 00 0 1100 */
	INVALID,		/* 00 0 1101 */
	INVALID,		/* 00 0 1110 */
	INVALID,		/* 00 0 1111 */
	{ 4, LD+U },		/* 00 1 0000: lwzu */
	INVALID,		/* 00 1 0001 */
	{ 4, ST+U },		/* 00 1 0010: stwu */
	INVALID,		/* 00 1 0011 */
	{ 2, LD+U },		/* 00 1 0100: lhzu */
	{ 2, LD+SE+U },		/* 00 1 0101: lhau */
	{ 2, ST+U },		/* 00 1 0110: sthu */
	{ 4, ST+M },		/* 00 1 0111: stmw */
	{ 4, LD+F+S+U },	/* 00 1 1000: lfsu */
	{ 8, LD+F+U },		/* 00 1 1001: lfdu */
	{ 4, ST+F+S+U },	/* 00 1 1010: stfsu */
	{ 8, ST+F+U },		/* 00 1 1011: stfdu */
	INVALID,		/* 00 1 1100 */
	INVALID,		/* 00 1 1101 */
	INVALID,		/* 00 1 1110 */
	INVALID,		/* 00 1 1111 */
	INVALID,		/* 01 0 0000 */
	INVALID,		/* 01 0 0001 */
	INVALID,		/* 01 0 0010 */
	INVALID,		/* 01 0 0011 */
	INVALID,		/* 01 0 0100 */
	INVALID,		/* 01 0 0101: lwax?? */
	INVALID,		/* 01 0 0110 */
	INVALID,		/* 01 0 0111 */
	{ 0, LD+HARD },		/* 01 0 1000: lswx */
	{ 0, LD+HARD },		/* 01 0 1001: lswi */
	{ 0, ST+HARD },		/* 01 0 1010: stswx */
	{ 0, ST+HARD },		/* 01 0 1011: stswi */
	INVALID,		/* 01 0 1100 */
	INVALID,		/* 01 0 1101 */
	INVALID,		/* 01 0 1110 */
	INVALID,		/* 01 0 1111 */
	INVALID,		/* 01 1 0000 */
	INVALID,		/* 01 1 0001 */
	INVALID,		/* 01 1 0010 */
	INVALID,		/* 01 1 0011 */
	INVALID,		/* 01 1 0100 */
	INVALID,		/* 01 1 0101: lwaux?? */
	INVALID,		/* 01 1 0110 */
	INVALID,		/* 01 1 0111 */
	INVALID,		/* 01 1 1000 */
	INVALID,		/* 01 1 1001 */
	INVALID,		/* 01 1 1010 */
	INVALID,		/* 01 1 1011 */
	INVALID,		/* 01 1 1100 */
	INVALID,		/* 01 1 1101 */
	INVALID,		/* 01 1 1110 */
	INVALID,		/* 01 1 1111 */
	INVALID,		/* 10 0 0000 */
	INVALID,		/* 10 0 0001 */
	{ 0, ST+HARD },		/* 10 0 0010: stwcx. */
	INVALID,		/* 10 0 0011 */
	INVALID,		/* 10 0 0100 */
	INVALID,		/* 10 0 0101 */
	INVALID,		/* 10 0 0110 */
	INVALID,		/* 10 0 0111 */
	{ 4, LD+S },		/* 10 0 1000: lwbrx */
	INVALID,		/* 10 0 1001 */
	{ 4, ST+S },		/* 10 0 1010: stwbrx */
	INVALID,		/* 10 0 1011 */
	{ 2, LD+S },		/* 10 0 1100: lhbrx */
	INVALID,		/* 10 0 1101 */
	{ 2, ST+S },		/* 10 0 1110: sthbrx */
	INVALID,		/* 10 0 1111 */
	INVALID,		/* 10 1 0000 */
	INVALID,		/* 10 1 0001 */
	INVALID,		/* 10 1 0010 */
	INVALID,		/* 10 1 0011 */
	INVALID,		/* 10 1 0100 */
	INVALID,		/* 10 1 0101 */
	INVALID,		/* 10 1 0110 */
	INVALID,		/* 10 1 0111 */
	INVALID,		/* 10 1 1000 */
	INVALID,		/* 10 1 1001 */
	INVALID,		/* 10 1 1010 */
	INVALID,		/* 10 1 1011 */
	INVALID,		/* 10 1 1100 */
	INVALID,		/* 10 1 1101 */
	INVALID,		/* 10 1 1110 */
	{ 0, ST+HARD },		/* 10 1 1111: dcbz */
	{ 4, LD },		/* 11 0 0000: lwzx */
	INVALID,		/* 11 0 0001 */
	{ 4, ST },		/* 11 0 0010: stwx */
	INVALID,		/* 11 0 0011 */
	{ 2, LD },		/* 11 0 0100: lhzx */
	{ 2, LD+SE },		/* 11 0 0101: lhax */
	{ 2, ST },		/* 11 0 0110: sthx */
	INVALID,		/* 11 0 0111 */
	{ 4, LD+F+S },		/* 11 0 1000: lfsx */
	{ 8, LD+F },		/* 11 0 1001: lfdx */
	{ 4, ST+F+S },		/* 11 0 1010: stfsx */
	{ 8, ST+F },		/* 11 0 1011: stfdx */
	INVALID,		/* 11 0 1100 */
	INVALID,		/* 11 0 1101 */
	INVALID,		/* 11 0 1110 */
	INVALID,		/* 11 0 1111 */
	{ 4, LD+U },		/* 11 1 0000: lwzux */
	INVALID,		/* 11 1 0001 */
	{ 4, ST+U },		/* 11 1 0010: stwux */
	INVALID,		/* 11 1 0011 */
	{ 2, LD+U },		/* 11 1 0100: lhzux */
	{ 2, LD+SE+U },		/* 11 1 0101: lhaux */
	{ 2, ST+U },		/* 11 1 0110: sthux */
	INVALID,		/* 11 1 0111 */
	{ 4, LD+F+S+U },	/* 11 1 1000: lfsux */
	{ 8, LD+F+U },		/* 11 1 1001: lfdux */
	{ 4, ST+F+S+U },	/* 11 1 1010: stfsux */
	{ 8, ST+F+U },		/* 11 1 1011: stfdux */
	INVALID,		/* 11 1 1100 */
	INVALID,		/* 11 1 1101 */
	INVALID,		/* 11 1 1110 */
	INVALID,		/* 11 1 1111 */
};

#define SWAP(a, b)	(t = (a), (a) = (b), (b) = t)

int
fix_alignment(struct pt_regs *regs)
{
	int instr, nb, flags;
	int i, t;
	int reg, areg;
	unsigned char *addr;
	union {
		long l;
		float f;
		double d;
		unsigned char v[8];
	} data;

	instr = (regs->dsisr >> 10) & 0x7f;
	nb = aligninfo[instr].len;
	if (nb == 0)
		return 0;	/* too hard or invalid instruction bits */
	flags = aligninfo[instr].flags;
	addr = (unsigned char *) regs->dar;
	reg = (regs->dsisr >> 5) & 0x1f;	/* source/dest register */

	/* Verify the address of the operand */
	if (user_mode(regs)) {
		if (verify_area((flags & ST? VERIFY_WRITE: VERIFY_READ), addr, nb))
			return -EFAULT;	/* bad address */
	}

#ifdef __SMP__
	if ((flags & F) && (regs->msr & MSR_FP) )
		smp_giveup_fpu(current);
#else	
	if ((flags & F) && last_task_used_math == current)
		giveup_fpu();
#endif
	if (flags & M)
		return 0;		/* too hard for now */

	/* If we read the operand, copy it in */
	if (flags & LD) {
		if (nb == 2) {
			data.v[0] = data.v[1] = 0;
			if (__get_user(data.v[2], addr)
			    || __get_user(data.v[3], addr+1))
				return -EFAULT;
		} else {
			for (i = 0; i < nb; ++i)
				if (__get_user(data.v[i], addr+i))
					return -EFAULT;
		}
	}

	switch (flags & ~U) {
	case LD+SE:
		if (data.v[2] >= 0x80)
			data.v[0] = data.v[1] = -1;
		/* fall through */
	case LD:
		regs->gpr[reg] = data.l;
		break;
	case LD+S:
		if (nb == 2) {
			SWAP(data.v[2], data.v[3]);
		} else {
			SWAP(data.v[0], data.v[3]);
			SWAP(data.v[1], data.v[2]);
		}
		regs->gpr[reg] = data.l;
		break;
	case ST:
		data.l = regs->gpr[reg];
		break;
	case ST+S:
		data.l = regs->gpr[reg];
		if (nb == 2) {
			SWAP(data.v[2], data.v[3]);
		} else {
			SWAP(data.v[0], data.v[3]);
			SWAP(data.v[1], data.v[2]);
		}
		break;
	case LD+F:
		current->tss.fpr[reg] = data.d;
		break;
	case ST+F:
		data.d = current->tss.fpr[reg];
		break;
	/* these require some floating point conversions... */
	/* note that giveup_fpu enables the FPU for the kernel */
	/* we'd like to use the assignment, but we have to compile
	 * the kernel with -msoft-float so it doesn't use the
	 * fp regs for copying 8-byte objects. */
	case LD+F+S:
#ifdef __SMP__
		if (regs->msr & MSR_FP )
			smp_giveup_fpu(current);
#else	
		giveup_fpu();
#endif		
		cvt_fd(&data.f, &current->tss.fpr[reg], &current->tss.fpscr);
		/* current->tss.fpr[reg] = data.f; */
		break;
	case ST+F+S:
#ifdef __SMP__
		if (regs->msr & MSR_FP )
			smp_giveup_fpu(current);
#else	
		giveup_fpu();
#endif		
		cvt_df(&current->tss.fpr[reg], &data.f, &current->tss.fpscr);
		/* data.f = current->tss.fpr[reg]; */
		break;
	default:
		printk("align: can't handle flags=%x\n", flags);
		return 0;
	}

	if (flags & ST) {
		if (nb == 2) {
			if (__put_user(data.v[2], addr)
			    || __put_user(data.v[3], addr+1))
				return -EFAULT;
		} else {
			for (i = 0; i < nb; ++i)
				if (__put_user(data.v[i], addr+i))
					return -EFAULT;
		}
	}

	if (flags & U) {
		areg = regs->dsisr & 0x1f;	/* register to update */
		regs->gpr[areg] = regs->dar;
	}

	return 1;
}
