#ifndef _PPC_PTRACE_H
#define _PPC_PTRACE_H


/*
 * This struct defines the way the registers are stored on the
 * kernel stack during a system call or other kernel entry.
 * Note: the "_overhead" and "_underhead" spaces are stack locations
 * used by called routines.  Because of the way the PowerPC ABI
 * specifies the function prologue/epilogue, registers can be
 * saved in stack locations which are below the current stack
 * pointer (_underhead).  If an interrupt occurs during this
 * [albeit] small time interval, registers which were saved on
 * the stack could be trashed by the interrupt save code.  The
 * "_underhead" leaves a hole just in case this happens.  It also
 * wastes 80 bytes of stack if it doesn't!  Similarly, the called
 * routine stores some information "above" the stack pointer before
 * if gets adjusted.  This is covered by the "_overhead" field
 * and [thankfully] is not totally wasted.
 *
 */

struct pt_regs {
	unsigned long _overhead[14]; /* Callee's SP,LR,params */
	unsigned long gpr[32];
	unsigned long nip;
	unsigned long msr;
	unsigned long ctr;
	unsigned long link;
	unsigned long ccr;
	unsigned long xer;
	unsigned long dar;	/* Fault registers */
	unsigned long dsisr;
	unsigned long hash1, hash2;
	unsigned long imiss, dmiss;
	unsigned long icmp, dcmp;
	unsigned long orig_gpr3; /* Used for restarting system calls */
	unsigned long result;    /* Result of a system call */
	double        fpr[4];    /* Caution! Only FP0-FP3 save on interrupts */
	double        fpcsr;
	unsigned long trap;	/* Reason for being here */
	unsigned long marker;	/* Should have DEADDEAD */
	unsigned long _underhead[20]; /* Callee's register save area */
	unsigned long edx;	/* for binfmt_elf.c which wants edx */
};

#define instruction_pointer(regs) ((regs)->nip)
#define user_mode(regs) ((regs)->msr & 0x4000)
extern void show_regs(struct pt_regs *);

#endif

