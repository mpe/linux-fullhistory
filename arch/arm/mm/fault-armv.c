/*
 *  linux/arch/arm/mm/fault-armv.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Modifications for ARM processor (c) 1995-1999 Russell King
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/unaligned.h>

#define FAULT_CODE_READ		0x02

#define DO_COW(m)		(!((m) & FAULT_CODE_READ))
#define READ_FAULT(m)		((m) & FAULT_CODE_READ)

#include "fault-common.c"

#ifdef DEBUG
static int sp_valid(unsigned long *sp)
{
	unsigned long addr = (unsigned long) sp;

	if (addr >= 0xb0000000 && addr < 0xd0000000)
		return 1;
	if (addr >= 0x03ff0000 && addr < 0x04000000)
		return 1;
	return 0;
}
#endif

#ifdef CONFIG_ALIGNMENT_TRAP
/*
 * 32-bit misaligned trap handler (c) 1998 San Mehat (CCC) -July 1998
 * /proc/sys/debug/alignment, modified and integrated into
 * Linux 2.1 by Russell King
 *
 * NOTE!!! This is not portable onto the ARM6/ARM7 processors yet.  Also,
 * it seems to give a severe performance impact (1 abort/ms - NW runs at
 * ARM6 speeds) with GCC 2.7.2.2 - needs checking with a later GCC/EGCS.
 *
 * IMHO, I don't think that the trap handler is advantageous on ARM6,7
 * processors (they'll run like an ARM3).  We'll see.
 */
#define CODING_BITS(i)	(i & 0x0e000000)

#define LDST_I_BIT(i)	(i & (1 << 26))		/* Immediate constant	*/
#define LDST_P_BIT(i)	(i & (1 << 24))		/* Preindex		*/
#define LDST_U_BIT(i)	(i & (1 << 23))		/* Add offset		*/
#define LDST_W_BIT(i)	(i & (1 << 21))		/* Writeback		*/
#define LDST_L_BIT(i)	(i & (1 << 20))		/* Load			*/

#define LDSTH_I_BIT(i)	(i & (1 << 22))		/* half-word immed	*/
#define LDM_S_BIT(i)	(i & (1 << 22))		/* write CPSR from SPSR	*/

#define RN_BITS(i)	((i >> 16) & 15)	/* Rn			*/
#define RD_BITS(i)	((i >> 12) & 15)	/* Rd			*/
#define RM_BITS(i)	(i & 15)		/* Rm			*/

#define REGMASK_BITS(i)	(i & 0xffff)
#define OFFSET_BITS(i)	(i & 0x0fff)

#define IS_SHIFT(i)	(i & 0x0ff0)
#define SHIFT_BITS(i)	((i >> 7) & 0x1f)
#define SHIFT_TYPE(i)	(i & 0x60)
#define SHIFT_LSL	0x00
#define SHIFT_LSR	0x20
#define SHIFT_ASR	0x40
#define SHIFT_RORRRX	0x60

static unsigned long ai_user;
static unsigned long ai_sys;
static unsigned long ai_skipped;
static unsigned long ai_half;
static unsigned long ai_word;
static unsigned long ai_multi;

static int proc_alignment_read(char *page, char **start, off_t off,
			       int count, int *eof, void *data)
{
	char *p = page;
	int len;

	p += sprintf(p, "User:\t\t%li\n", ai_user);
	p += sprintf(p, "System:\t\t%li\n", ai_sys);
	p += sprintf(p, "Skipped:\t%li\n", ai_skipped);
	p += sprintf(p, "Half:\t\t%li\n", ai_half);
	p += sprintf(p, "Word:\t\t%li\n", ai_word);
	p += sprintf(p, "Multi:\t\t%li\n", ai_multi);

	len = (p - page) - off;
	if (len < 0)
		len = 0;

	*eof = (len <= count) ? 1 : 0;
	*start = page + off;

	return len;
}

#ifdef CONFIG_SYSCTL
/*
 * This needs to be done after sysctl_init, otherwise sys/
 * will be overwritten.
 */
void __init alignment_init(void)
{
	struct proc_dir_entry *e;

	e = create_proc_entry("sys/debug/alignment", S_IFREG | S_IRUGO, NULL);

	if (e)
		e->read_proc = proc_alignment_read;
}

__initcall(alignment_init);
#endif

static int
do_alignment_exception(struct pt_regs *regs)
{
	unsigned int instr, rd, rn, correction, nr_regs, regbits;
	unsigned long eaddr;
	union { unsigned long un; signed long sn; } offset;

	if (user_mode(regs)) {
		set_cr(cr_no_alignment);
		ai_user += 1;
		return 0;
	}

	ai_sys += 1;

	instr = *(unsigned long *)instruction_pointer(regs);
	correction = 4; /* sometimes 8 on ARMv3 */
	regs->ARM_pc += correction + 4;

	rd = RD_BITS(instr);
	rn = RN_BITS(instr);
	eaddr = regs->uregs[rn];

	switch(CODING_BITS(instr)) {
	case 0x00000000:
		if ((instr & 0x0ff00ff0) == 0x01000090) {
			ai_skipped += 1;
			printk(KERN_ERR "Unaligned trap: not handling swp instruction\n");
			return 1;
		}

		if (((instr & 0x0e000090) == 0x00000090) && (instr & 0x60) != 0) {
			ai_half += 1;
			if (LDSTH_I_BIT(instr))
				offset.un = (instr & 0xf00) >> 4 | (instr & 15);
			else
				offset.un = regs->uregs[RM_BITS(instr)];

			if (LDST_P_BIT(instr)) {
				if (LDST_U_BIT(instr))
					eaddr += offset.un;
				else
					eaddr -= offset.un;
			}

			if (LDST_L_BIT(instr))
				regs->uregs[rd] = get_unaligned((unsigned short *)eaddr);
			else
				put_unaligned(regs->uregs[rd], (unsigned short *)eaddr);

			/* signed half-word? */
			if (instr & 0x40)
				regs->uregs[rd] = (long)((short) regs->uregs[rd]);

			if (!LDST_P_BIT(instr)) {
				if (LDST_U_BIT(instr))
					eaddr += offset.un;
				else
					eaddr -= offset.un;
				regs->uregs[rn] = eaddr;
			} else if (LDST_W_BIT(instr))
				regs->uregs[rn] = eaddr;
			break;
		}

	default:
		ai_skipped += 1;
		panic("Alignment trap: not handling instruction %08X at %08lX",
				instr, regs->ARM_pc - correction - 4);
		break;

	case 0x04000000:
		offset.un = OFFSET_BITS(instr);
		goto ldr_str;

	case 0x06000000:
		offset.un = regs->uregs[RM_BITS(instr)];

		if (IS_SHIFT(instr)) {
			unsigned int shiftval = SHIFT_BITS(instr);

			switch(SHIFT_TYPE(instr)) {
			case SHIFT_LSL:
				offset.un <<= shiftval;
				break;

			case SHIFT_LSR:
				offset.un >>= shiftval;
				break;

			case SHIFT_ASR:
				offset.sn >>= shiftval;
				break;

			case SHIFT_RORRRX:
				if (shiftval == 0) {
					offset.un >>= 1;
					if (regs->ARM_cpsr & CC_C_BIT)
						offset.un |= 1 << 31;
				} else
					offset.un = offset.un >> shiftval |
							  offset.un << (32 - shiftval);
				break;
			}
		}

	ldr_str:
		ai_word += 1;
		if (LDST_P_BIT(instr)) {
			if (LDST_U_BIT(instr))
				eaddr += offset.un;
			else
				eaddr -= offset.un;
		} else {
			if (LDST_W_BIT(instr)) {
				printk(KERN_ERR "Not handling ldrt/strt correctly\n");
				return 1;
			}
		}

		if (LDST_L_BIT(instr)) {
			regs->uregs[rd] = get_unaligned((unsigned long *)eaddr);
			if (rd == 15)
				correction = 0;
		} else
			put_unaligned(regs->uregs[rd], (unsigned long *)eaddr);

		if (!LDST_P_BIT(instr)) {
			if (LDST_U_BIT(instr))
				eaddr += offset.un;
			else
				eaddr -= offset.un;

			regs->uregs[rn] = eaddr;
		} else if (LDST_W_BIT(instr))
			regs->uregs[rn] = eaddr;
		break;

	case 0x08000000:
		if (LDM_S_BIT(instr))
			panic("Alignment trap: not handling LDM with s-bit\n");
		ai_multi += 1;

		for (regbits = REGMASK_BITS(instr), nr_regs = 0; regbits; regbits >>= 1)
			nr_regs += 4;

		if  (!LDST_U_BIT(instr))
			eaddr -= nr_regs;

		if ((LDST_U_BIT(instr) == 0 && LDST_P_BIT(instr) == 0) ||
		    (LDST_U_BIT(instr)      && LDST_P_BIT(instr)))
			eaddr += 4;

		for (regbits = REGMASK_BITS(instr), rd = 0; regbits; regbits >>= 1, rd += 1)
			if (regbits & 1) {
				if (LDST_L_BIT(instr)) {
					regs->uregs[rd] = get_unaligned((unsigned long *)eaddr);
					if (rd == 15)
						correction = 0;
				} else
					put_unaligned(regs->uregs[rd], (unsigned long *)eaddr);
				eaddr += 4;
			}

		if (LDST_W_BIT(instr)) {
			if (LDST_P_BIT(instr) && !LDST_U_BIT(instr))
				eaddr -= nr_regs;
			else if (LDST_P_BIT(instr))
				eaddr -= 4;
			else if (!LDST_U_BIT(instr))
				eaddr -= 4 + nr_regs;
			regs->uregs[rn] = eaddr;
		}
		break;
	}

	regs->ARM_pc -= correction;

	return 0;
}

#endif

#define BUG_PROC_MSG \
  "Buggy processor (%08X), trying to continue.\n" \
  "Please read http://www.arm.linux.org.uk/state.html for more information"

asmlinkage void
do_DataAbort(unsigned long addr, int fsr, int error_code, struct pt_regs *regs)
{
	if (user_mode(regs)) {
		if (addr == regs->ARM_pc) {
			static int first = 1;
			if (first) {
				/*
				 * I want statistical information on this problem!
				 */
				printk(KERN_ERR BUG_PROC_MSG, fsr);
				first = 0;
			}
			return;
		}
	}

#define DIE(signr,nam)\
		force_sig(signr, current);\
		die(nam, regs, fsr);\
		do_exit(signr);\
		break

	switch (fsr & 15) {
	/*
	 *  0 - vector exception
	 */
	case 0:
		force_sig(SIGSEGV, current);
		if (!user_mode(regs)) {
			die("vector exception", regs, fsr);
			do_exit(SIGSEGV);
		}
		break;

	/*
	 * 15 - permission fault on page
	 *  5 - page-table entry descriptor fault
	 *  7 - first-level descriptor fault
	 */
	case 15: case 5: case 7:
		do_page_fault(addr, error_code, regs);
		break;

	/*
	 * 13 - permission fault on section
	 */
	case 13:
		force_sig(SIGSEGV, current);
		if (!user_mode(regs)) {
			die("section permission fault", regs, fsr);
			do_exit(SIGSEGV);
		} else {
#ifdef CONFIG_DEBUG_USER
			printk("%s: permission fault on section, "
			       "address=0x%08lx, code %d\n",
			       current->comm, addr, error_code);
#ifdef DEBUG
			{
				unsigned int i, j;
				unsigned long *sp;

				sp = (unsigned long *) (regs->ARM_sp - 128);
				for (j = 0; j < 20 && sp_valid(sp); j++) {
					printk("%p: ", sp);
					for (i = 0; i < 8 && sp_valid(sp); i += 1, sp++)
						printk("%08lx ", *sp);
					printk("\n");
				}
				show_regs(regs);
				c_backtrace(regs->ARM_fp, regs->ARM_cpsr);
			}
#endif
#endif
		}
		break;

	case 1:
	case 3:
#ifdef CONFIG_ALIGNMENT_TRAP
		if (!do_alignment_exception(regs))
			break;
#endif
		/*
		 * this should never happen
		 */
		DIE(SIGBUS, "Alignment exception");
		break;

	case 2:
		DIE(SIGKILL, "Terminal exception");
	case 12:
	case 14:
		DIE(SIGBUS, "External abort on translation");
	case 9:
	case 11:
		DIE(SIGSEGV, "Domain fault");

	case 4:
	case 6:
		DIE(SIGBUS, "External abort on linefetch");
	case 8:
	case 10:
		DIE(SIGBUS, "External abort on non-linefetch");
	}
}

asmlinkage int
do_PrefetchAbort(unsigned long addr, struct pt_regs *regs)
{
	do_page_fault(addr, FAULT_CODE_READ, regs);
	return 1;
}
