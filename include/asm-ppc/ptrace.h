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
#ifdef KERNEL
extern void show_regs(struct pt_regs *);
#endif

/* Offsets used by 'ptrace' system call interface */
/* Note: these should correspond to gpr[x]        */
#define PT_R0	0
#define PT_R1	1
#define PT_R2	2
#define PT_R3	3
#define PT_R4	4
#define PT_R5	5
#define PT_R6	6
#define PT_R7	7
#define PT_R8	8
#define PT_R9	9
#define PT_R10	10
#define PT_R11	11
#define PT_R12	12
#define PT_R13	13
#define PT_R14	14
#define PT_R15	15
#define PT_R16	16
#define PT_R17	17
#define PT_R18	18
#define PT_R19	19
#define PT_R20	20
#define PT_R21	21
#define PT_R22	22
#define PT_R23	23
#define PT_R24	24
#define PT_R25	25
#define PT_R26	26
#define PT_R27	27
#define PT_R28	28
#define PT_R29	29
#define PT_R30	30
#define PT_R31	31

#define PT_NIP	32
#define PT_MSR	33
#define PT_ORIG_R3 34
#define PT_CTR	35
#define PT_LNK	36
#define PT_XER	37
#define PT_CCR	38

#define PT_FPR0	48

#endif

