#ifndef _LINUX_VM86_H
#define _LINUX_VM86_H

#define VM_MASK 0x00020000

/*
 * This is the stack-layout when we have done a "SAVE_ALL" from vm86
 * mode - the main change is that the old segment descriptors aren't
 * useful any more and are forced to be zero by the kernel (and the
 * hardware when a trap occurs), and the real segment descriptors are
 * at the end of the structure. Look at ptrace.h to see the "normal"
 * setup.
 */

struct vm86_regs {
/*
 * normal regs, with special meaning for the segment descriptors..
 */
	long ebx;
	long ecx;
	long edx;
	long esi;
	long edi;
	long ebp;
	long eax;
	long __null_ds;
	long __null_es;
	long __null_fs;
	long __null_gs;
	long orig_eax;
	long eip;
	long cs;
	long eflags;
	long esp;
	long ss;
/*
 * these are specific to v86 mode:
 */
	long es;
	long ds;
	long fs;
	long gs;
};

/*
 * flags isn't even used yet: it's just there as an example of
 * what kind of information we might want to give sys_vm86() (or
 * want it to return to us).
 */
struct vm86_struct {
	struct vm86_regs regs;
	unsigned long flags;
};

#endif
