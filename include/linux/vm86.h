#ifndef _LINUX_VM86_H
#define _LINUX_VM86_H

#define TF_MASK		0x00000100
#define IF_MASK		0x00000200
#define IOPL_MASK	0x00003000
#define NT_MASK		0x00004000
#define VM_MASK		0x00020000
#define AC_MASK		0x00040000

#define BIOSSEG		0x0f000

#define CPU_286		2
#define CPU_386		3
#define CPU_486		4

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
	unsigned short cs, __csh;
	long eflags;
	long esp;
	unsigned short ss, __ssh;
/*
 * these are specific to v86 mode:
 */
	unsigned short es, __esh;
	unsigned short ds, __dsh;
	unsigned short fs, __fsh;
	unsigned short gs, __gsh;
};

struct vm86_struct {
	struct vm86_regs regs;
	unsigned long flags;
	unsigned long screen_bitmap;
	unsigned long v_eflags;
	unsigned long cpu_type;
	unsigned long return_if_iflag;
	unsigned char int_revectored[0x100];
	unsigned char int21_revectored[0x100];
};

/*
 * flags masks
 */
#define VM86_SCREEN_BITMAP 1

#ifdef __KERNEL__

void handle_vm86_fault(struct vm86_regs *, long);

#endif

#endif
