#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/uaccess.h>

#include "ieee-math.h"

#define	OPC_PAL		0x00

#define OPC_INTA	0x10
#define OPC_INTL	0x11
#define OPC_INTS	0x12
#define OPC_INTM	0x13
#define OPC_FLTV	0x15
#define OPC_FLTI	0x16
#define OPC_FLTL	0x17

#define OPC_MISC	0x18

#define	OPC_JSR		0x1a

/*
 * "Base" function codes for the FLTI-class instructions.  These
 * instructions all have opcode 0x16.  Note that in most cases these
 * actually correspond to the "chopped" form of the instruction.  Not
 * to worry---we extract the qualifier bits separately and deal with
 * them separately.  Notice that base function code 0x2c is used for
 * both CVTTS and CVTST.  The other bits in the function code are used
 * to distinguish the two.
 */
#define FLTI_FUNC_ADDS			0x000
#define FLTI_FUNC_ADDT			0x020
#define FLTI_FUNC_CMPTEQ		0x025
#define FLTI_FUNC_CMPTLT		0x026
#define FLTI_FUNC_CMPTLE		0x027
#define FLTI_FUNC_CMPTUN		0x024
#define FLTI_FUNC_CVTTS_or_CVTST	0x02c
#define FLTI_FUNC_CVTTQ			0x02f
#define FLTI_FUNC_CVTQS			0x03c
#define FLTI_FUNC_CVTQT			0x03e
#define FLTI_FUNC_DIVS			0x003
#define FLTI_FUNC_DIVT			0x023
#define FLTI_FUNC_MULS			0x002
#define FLTI_FUNC_MULT			0x022
#define FLTI_FUNC_SUBS			0x001
#define FLTI_FUNC_SUBT			0x021

#define FLTI_FUNC_CVTQL			0x030	/* opcode 0x17 */

#define MISC_TRAPB	0x0000
#define MISC_EXCB	0x0400

extern unsigned long alpha_read_fp_reg (unsigned long reg);
extern void alpha_write_fp_reg (unsigned long reg, unsigned long val);


#ifdef MODULE

MODULE_DESCRIPTION("FP Software completion module");

extern long (*alpha_fp_emul_imprecise)(struct pt_regs *, unsigned long);
extern long (*alpha_fp_emul) (unsigned long pc);

static long (*save_emul_imprecise)(struct pt_regs *, unsigned long);
static long (*save_emul) (unsigned long pc);

long do_alpha_fp_emul_imprecise(struct pt_regs *, unsigned long);
long do_alpha_fp_emul(unsigned long);

int init_module(void)
{
	save_emul_imprecise = alpha_fp_emul_imprecise;
	save_emul = alpha_fp_emul;
	alpha_fp_emul_imprecise = do_alpha_fp_emul_imprecise;
	alpha_fp_emul = do_alpha_fp_emul;
	return 0;
}

void cleanup_module(void)
{
	alpha_fp_emul_imprecise = save_emul_imprecise;
	alpha_fp_emul = save_emul;
}

#undef  alpha_fp_emul_imprecise
#define alpha_fp_emul_imprecise		do_alpha_fp_emul_imprecise
#undef  alpha_fp_emul
#define alpha_fp_emul			do_alpha_fp_emul

#endif /* MODULE */

/*
 * Emulate the floating point instruction at address PC.  Returns 0 if
 * emulation fails.  Notice that the kernel does not and cannot use FP
 * regs.  This is good because it means that instead of
 * saving/restoring all fp regs, we simply stick the result of the
 * operation into the appropriate register.
 */
long
alpha_fp_emul (unsigned long pc)
{
	unsigned long opcode, fa, fb, fc, func, mode;
	unsigned long fpcw = current->tss.flags;
	unsigned long va, vb, vc, res, fpcr;
	__u32 insn;

	MOD_INC_USE_COUNT;

	get_user(insn, (__u32*)pc);
	fc     = (insn >>  0) &  0x1f;	/* destination register */
	func   = (insn >>  5) & 0x7ff;
	fb     = (insn >> 16) &  0x1f;
	fa     = (insn >> 21) &  0x1f;
	opcode = insn >> 26;
	
	va = alpha_read_fp_reg(fa);
	vb = alpha_read_fp_reg(fb);
	fpcr = rdfpcr();

	/*
	 * Try the operation in software.  First, obtain the rounding
	 * mode...
	 */
	mode = func & 0xc0;
	if (mode == 0xc0) {
	    /* dynamic---get rounding mode from fpcr: */
	    mode = ((fpcr & FPCR_DYN_MASK) >> FPCR_DYN_SHIFT) << ROUND_SHIFT;
	}
	mode |= (fpcw & IEEE_TRAP_ENABLE_MASK);

	if ((IEEE_TRAP_ENABLE_MASK & 0xc0)) {
		extern int something_is_wrong (void);
		something_is_wrong();
	}

	/* least 6 bits contain operation code: */
	switch (func & 0x3f) {
	      case FLTI_FUNC_CMPTEQ:
		res = ieee_CMPTEQ(va, vb, &vc);
		break;

	      case FLTI_FUNC_CMPTLT:
		res = ieee_CMPTLT(va, vb, &vc);
		break;

	      case FLTI_FUNC_CMPTLE:
		res = ieee_CMPTLE(va, vb, &vc);
		break;

	      case FLTI_FUNC_CMPTUN:
		res = ieee_CMPTUN(va, vb, &vc);
		break;

	      case FLTI_FUNC_CVTQL:
		/*
		 * Notice: We can get here only due to an integer
		 * overflow.  Such overflows are reported as invalid
		 * ops.  We return the result the hw would have
		 * computed.
		 */
		vc = ((vb & 0xc0000000) << 32 |	/* sign and msb */
		      (vb & 0x3fffffff) << 29);	/* rest of the integer */
		res = FPCR_INV;
		break;

	      case FLTI_FUNC_CVTQS:
		res = ieee_CVTQS(mode, vb, &vc);
		break;

	      case FLTI_FUNC_CVTQT:
		res = ieee_CVTQT(mode, vb, &vc);
		break;

	      case FLTI_FUNC_CVTTS_or_CVTST:
		if (func == 0x6ac) {
			/*
			 * 0x2ac is also CVTST, but if the /S
			 * qualifier isn't set, we wouldn't be here in
			 * the first place...
			 */
			res = ieee_CVTST(mode, vb, &vc);
		} else {
			res = ieee_CVTTS(mode, vb, &vc);
		}
		break; 

	      case FLTI_FUNC_DIVS:
		res = ieee_DIVS(mode, va, vb, &vc);
		break;

	      case FLTI_FUNC_DIVT:
		res = ieee_DIVT(mode, va, vb, &vc);
		break;

	      case FLTI_FUNC_MULS:
		res = ieee_MULS(mode, va, vb, &vc);
		break;

	      case FLTI_FUNC_MULT:
		res = ieee_MULT(mode, va, vb, &vc);
		break;

	      case FLTI_FUNC_SUBS:
		res = ieee_SUBS(mode, va, vb, &vc);
		break;

	      case FLTI_FUNC_SUBT:
		res = ieee_SUBT(mode, va, vb, &vc);
		break;

	      case FLTI_FUNC_ADDS:
		res = ieee_ADDS(mode, va, vb, &vc);
		break;

	      case FLTI_FUNC_ADDT:
		res = ieee_ADDT(mode, va, vb, &vc);
		break;

	      case FLTI_FUNC_CVTTQ:
		res = ieee_CVTTQ(mode, vb, &vc);
		break;

	      default:
		printk("alpha_fp_emul: unexpected function code %#lx at %#lx\n",
		       func & 0x3f, pc);
		MOD_DEC_USE_COUNT;
		return 0;
	}

	/*
	 * Take the appropriate action for each possible
	 * floating-point result:
	 *
	 *	- Set the appropriate bits in the FPCR
	 *	- If the specified exception is enabled in the FPCR,
	 *	  return.  The caller (entArith) will dispatch
	 *	  the appropriate signal to the translated program.
	 *
	 * In addition, properly track the exception state in software
	 * as described in the Alpha Architectre Handbook section 4.7.7.3.
	 */
	if (res) {
		/* Record exceptions in software control word.  */
		current->tss.flags = fpcw |= res >> 35;

		/* Update hardware control register */
		fpcr &= (~FPCR_MASK | FPCR_DYN_MASK);
		fpcr |= ieee_swcr_to_fpcr(fpcw | (~fpcw&IEEE_STATUS_MASK)>>16);
		wrfpcr(fpcr);

		/* Do we generate a signal?  */
		if (res >> 51 & fpcw & IEEE_TRAP_ENABLE_MASK) {
			MOD_DEC_USE_COUNT;
			return 0;
		}
	}

	/*
	 * Whoo-kay... we got this far, and we're not generating a signal
	 * to the translated program.  All that remains is to write the
	 * result:
	 */
	alpha_write_fp_reg(fc, vc);

	MOD_DEC_USE_COUNT;
	return 1;
}


long
alpha_fp_emul_imprecise (struct pt_regs *regs, unsigned long write_mask)
{
	unsigned long trigger_pc = regs->pc - 4;
	unsigned long insn, opcode, rc;

	MOD_INC_USE_COUNT;

	/*
	 * Turn off the bits corresponding to registers that are the
	 * target of instructions that set bits in the exception
	 * summary register.  We have some slack doing this because a
	 * register that is the target of a trapping instruction can
	 * be written at most once in the trap shadow.
	 *
	 * Branches, jumps, TRAPBs, EXCBs and calls to PALcode all
	 * bound the trap shadow, so we need not look any further than
	 * up to the first occurrence of such an instruction.
	 */
	while (write_mask) {
		get_user(insn, (__u32*)(trigger_pc));
		opcode = insn >> 26;
		rc = insn & 0x1f;

		switch (opcode) {
		      case OPC_PAL:
		      case OPC_JSR:
		      case 0x30 ... 0x3f:	/* branches */
			MOD_DEC_USE_COUNT;
			return 0;

		      case OPC_MISC:
			switch (insn & 0xffff) {
			      case MISC_TRAPB:
			      case MISC_EXCB:
				MOD_DEC_USE_COUNT;
				return 0;

			      default:
				break;
			}
			break;

		      case OPC_INTA:
		      case OPC_INTL:
		      case OPC_INTS:
		      case OPC_INTM:
			write_mask &= ~(1UL << rc);
			break;

		      case OPC_FLTV:
		      case OPC_FLTI:
		      case OPC_FLTL:
			write_mask &= ~(1UL << (rc + 32));
			break;
		}
		if (!write_mask) {
			if ((opcode == OPC_FLTI || opcode == OPC_FLTL)
			    && alpha_fp_emul(trigger_pc))
			{
			    /* re-execute insns in trap-shadow: */
			    regs->pc = trigger_pc + 4;
			    MOD_DEC_USE_COUNT;
			    return 1;
			}
			break;
		}
		trigger_pc -= 4;
	}
	MOD_DEC_USE_COUNT;
	return 0;
}
