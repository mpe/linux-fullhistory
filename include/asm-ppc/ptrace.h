#ifndef _PPC_PTRACE_H
#define _PPC_PTRACE_H

/*
 * this should only contain volatile regs
 * since we can keep non-volatile in the tss
 * should set this up when only volatiles are saved
 * by intr code.
 *
 * I can't find any reference to the above comment (from Gary Thomas)
 * about _underhead/_overhead in the sys V abi for the ppc
 * dated july 25, 1994.
 *
 * the stack must be kept to a size that is a multiple of 16
 * so this includes the stack frame overhead 
 * -- Cort.
 */

/*
 * GCC sometimes accesses words at negative offsets from the stack
 * pointer, although the SysV ABI says it shouldn't.  To cope with
 * this, we leave this much untouched space on the stack on exception
 * entry.
 */
#define STACK_FRAME_OVERHEAD 16
#define STACK_UNDERHEAD	64

#ifndef __ASSEMBLY__
struct pt_regs {
	unsigned long gpr[32];
	unsigned long nip;	
	unsigned long msr;	
	unsigned long ctr;	
	unsigned long link;	
	unsigned long ccr;	
	unsigned long xer;	
	unsigned long dar;	/* Fault registers */
	unsigned long dsisr;
#if 0  
	unsigned long srr1;
	unsigned long srr0;
	unsigned long hash1, hash2;
	unsigned long imiss, dmiss;
	unsigned long icmp, dcmp;
#endif  
	unsigned long orig_gpr3; /* Used for restarting system calls */
	unsigned long result;	/* Result of a system call */
	unsigned long trap;	/* Reason for being here */
	unsigned long marker;	/* Should have DEADDEAD */
};


#define instruction_pointer(regs) ((regs)->nip)
#define user_mode(regs) ((regs)->msr & 0x4000)
#ifdef KERNEL
extern void show_regs(struct pt_regs *);
#endif

/* should include and generate these in ppc_defs.h -- Cort */
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
#endif /* __ASSEMBLY__ */

#endif /* _PPC_PTRACE_H */

