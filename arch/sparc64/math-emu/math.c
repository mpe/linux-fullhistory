/* $Id: math.c,v 1.5 1998/06/12 14:54:27 jj Exp $
 * arch/sparc64/math-emu/math.c
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *
 * Emulation routines originate from soft-fp package, which is part
 * of glibc and has appropriate copyrights in it.
 */

#include <linux/types.h>
#include <linux/sched.h>

#include <asm/fpumacro.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>

#define FLOATFUNC(x) extern int x(void *,void *,void *);

FLOATFUNC(FMOVQ)
FLOATFUNC(FNEGQ)
FLOATFUNC(FABSQ)
FLOATFUNC(FSQRTQ)
FLOATFUNC(FADDQ)
FLOATFUNC(FSUBQ)
FLOATFUNC(FMULQ)
FLOATFUNC(FDIVQ)
FLOATFUNC(FDMULQ)
FLOATFUNC(FQTOX)
FLOATFUNC(FXTOQ)
FLOATFUNC(FQTOS)
FLOATFUNC(FQTOD)
FLOATFUNC(FITOQ)
FLOATFUNC(FSTOQ)
FLOATFUNC(FDTOQ)
FLOATFUNC(FQTOI)
FLOATFUNC(FCMPQ)
FLOATFUNC(FCMPEQ)

FLOATFUNC(FSQRTS)
FLOATFUNC(FSQRTD)
FLOATFUNC(FADDS)
FLOATFUNC(FADDD)
FLOATFUNC(FSUBS)
FLOATFUNC(FSUBD)
FLOATFUNC(FMULS)
FLOATFUNC(FMULD)
FLOATFUNC(FDIVS)
FLOATFUNC(FDIVD)
FLOATFUNC(FSMULD)
FLOATFUNC(FSTOX)
FLOATFUNC(FDTOX)
FLOATFUNC(FDTOS)
FLOATFUNC(FSTOD)
FLOATFUNC(FSTOI)
FLOATFUNC(FDTOI)

int do_mathemu(struct pt_regs *regs, struct fpustate *f)
{
	unsigned long pc = regs->tpc;
	unsigned long tstate = regs->tstate;
	u32 insn = 0;
	int type = 0; /* 01 is single, 10 is double, 11 is quad, 
			 000011 is rs1, 001100 is rs2, 110000 is rd (00 in rd is fcc)
			 111100000000 tells which ftt may that happen in */
	int freg;
	static u64 zero[2] = { 0L, 0L };
	int flags;
	int (*func)(void *,void *,void *) = NULL;

	if(tstate & TSTATE_PRIV)
		die_if_kernel("FPQuad from kernel", regs);
	if(current->tss.flags & SPARC_FLAG_32BIT)
		pc = (u32)pc;
	if (get_user(insn, (u32 *)pc) != -EFAULT) {
		if ((insn & 0xc1f80000) == 0x81a00000) /* FPOP1 */ {
			switch ((insn >> 5) & 0x1ff) {
			/* QUAD - ftt == 3 */
			case 0x003: type = 0x33c; func = FMOVQ; break;
			case 0x007: type = 0x33c; func = FNEGQ; break;
			case 0x00b: type = 0x33c; func = FABSQ; break;
			case 0x02b: type = 0x33c; func = FSQRTQ; break;
			case 0x043: type = 0x33f; func = FADDQ; break;
			case 0x047: type = 0x33f; func = FSUBQ; break;
			case 0x04b: type = 0x33f; func = FMULQ; break;
			case 0x04f: type = 0x33f; func = FDIVQ; break;
			case 0x06e: type = 0x33a; func = FDMULQ; break;
			case 0x083: type = 0x32c; func = FQTOX; break;
			case 0x08c: type = 0x338; func = FXTOQ; break;
			case 0x0c7: type = 0x31c; func = FQTOS; break;
			case 0x0cb: type = 0x32c; func = FQTOD; break;
			case 0x0cc: type = 0x334; func = FITOQ; break;
			case 0x0cd: type = 0x334; func = FSTOQ; break;
			case 0x0ce: type = 0x338; func = FDTOQ; break;
			case 0x0d3: type = 0x31c; func = FQTOI; break;
			/* SUBNORMAL - ftt == 2 */
			case 0x029: type = 0x214; func = FSQRTS; break;
			case 0x02a: type = 0x228; func = FSQRTD; break;
			case 0x041: type = 0x215; func = FADDS; break;
			case 0x042: type = 0x22a; func = FADDD; break;
			case 0x045: type = 0x215; func = FSUBS; break;
			case 0x046: type = 0x22a; func = FSUBD; break;
			case 0x049: type = 0x215; func = FMULS; break;
			case 0x04a: type = 0x22a; func = FMULD; break;
			case 0x04d: type = 0x215; func = FDIVS; break;
			case 0x04e: type = 0x22a; func = FDIVD; break;
			case 0x069: type = 0x225; func = FSMULD; break;
			case 0x081: type = 0x224; func = FSTOX; break;
			case 0x082: type = 0x228; func = FDTOX; break;
			case 0x0c6: type = 0x218; func = FDTOS; break;
			case 0x0c9: type = 0x224; func = FSTOD; break;
			case 0x0d1: type = 0x214; func = FSTOI; break;
			case 0x0d2: type = 0x218; func = FDTOI; break;
			}
		}
		else if ((insn & 0xc1f80000) == 0x81a80000) /* FPOP2 */ {
			switch ((insn >> 5) & 0x1ff) {
			case 0x053: type = 0x30f; func = FCMPQ; break;
			case 0x057: type = 0x30f; func = FCMPEQ; break;
			}
		}
	}
	if (type) {
		void *rs1 = NULL, *rs2 = NULL, *rd = NULL;
		
		freg = (current->tss.xfsr[0] >> 14) & 0xf;
		if (freg != (type >> 8))
			goto err;
		current->tss.xfsr[0] &= ~0x1c000;
		freg = ((insn >> 14) & 0x1f);
		switch (type & 0x3) {
		case 3: if (freg & 2) {
				current->tss.xfsr[0] |= (6 << 14) /* invalid_fp_register */;
				goto err;
			}
		case 2: freg = ((freg & 1) << 5) | (freg & 0x1e);
		case 1: rs1 = (void *)&f->regs[freg]; 
			flags = (freg < 32) ? FPRS_DL : FPRS_DU; 
			if (!(current->tss.fpsaved[0] & flags))
				rs1 = (void *)&zero;
			break;
		}
		freg = (insn & 0x1f);
		switch ((type >> 2) & 0x3) {
		case 3: if (freg & 2) {
				current->tss.xfsr[0] |= (6 << 14) /* invalid_fp_register */;
				goto err;
			}
		case 2: freg = ((freg & 1) << 5) | (freg & 0x1e);
		case 1: rs2 = (void *)&f->regs[freg];
			flags = (freg < 32) ? FPRS_DL : FPRS_DU; 
			if (!(current->tss.fpsaved[0] & flags))
				rs2 = (void *)&zero;
			break;
		}
		freg = ((insn >> 25) & 0x1f);
		switch ((type >> 4) & 0x3) {
		case 0: rd = (void *)(((long)&current->tss.xfsr[0]) | (freg & 3)); break;
		case 3: if (freg & 2) {
				current->tss.xfsr[0] |= (6 << 14) /* invalid_fp_register */;
				goto err;
			}
		case 2: freg = ((freg & 1) << 5) | (freg & 0x1e);
		case 1: rd = (void *)&f->regs[freg];
			flags = (freg < 32) ? FPRS_DL : FPRS_DU; 
			if (!(current->tss.fpsaved[0] & FPRS_FEF)) {
				current->tss.fpsaved[0] = FPRS_FEF;
				current->tss.gsr[0] = 0;
			}
			if (!(current->tss.fpsaved[0] & flags)) {
				if (freg < 32)
					memset(f->regs, 0, 32*sizeof(u32));
				else
					memset(f->regs+32, 0, 32*sizeof(u32));
			}
			current->tss.fpsaved[0] |= flags;
			break;
		}
		func(rd, rs2, rs1);
		regs->tpc = regs->tnpc;
		regs->tnpc += 4;
		return 1;
	}
err:	return 0;
}
