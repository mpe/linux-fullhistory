/*
 * include/asm-i386/processor.h
 *
 * Copyright (C) 1994 Linus Torvalds
 */

#ifndef __ASM_I386_PROCESSOR_H
#define __ASM_I386_PROCESSOR_H

#include <asm/vm86.h>
#include <asm/math_emu.h>
#include <asm/segment.h>
#include <asm/page.h>

/*
 *  CPU type and hardware bug flags. Kept separately for each CPU.
 *  Members of this structure are referenced in head.S, so think twice
 *  before touching them. [mj]
 */

struct cpuinfo_x86 {
	__u8	x86;		/* CPU family */
	__u8	x86_vendor;	/* CPU vendor */
	__u8	x86_model;
	__u8	x86_mask;
	char	wp_works_ok;	/* It doesn't on 386's */
	char	hlt_works_ok;	/* Problems on some 486Dx4's and old 386's */
	char	hard_math;
	char	rfu;
	int	cpuid_level;	/* Maximum supported CPUID level, -1=no CPUID */
	__u32	x86_capability;
	char	x86_vendor_id[16];
	char	x86_model_id[64];
	int 	x86_cache_size;  /* in KB - valid for CPUS which support this
				    call  */
	int	fdiv_bug;
	int	f00f_bug;
	unsigned long loops_per_sec;
	unsigned long *pgd_quick;
	unsigned long *pte_quick;
	unsigned long pgtable_cache_sz;
};

#define X86_VENDOR_INTEL 0
#define X86_VENDOR_CYRIX 1
#define X86_VENDOR_AMD 2
#define X86_VENDOR_UMC 3
#define X86_VENDOR_NEXGEN 4
#define X86_VENDOR_CENTAUR 5
#define X86_VENDOR_UNKNOWN 0xff

/*
 * capabilities of CPUs
 */

#define X86_FEATURE_FPU		0x00000001	/* onboard FPU */
#define X86_FEATURE_VME		0x00000002	/* Virtual Mode Extensions */
#define X86_FEATURE_DE		0x00000004	/* Debugging Extensions */
#define X86_FEATURE_PSE		0x00000008	/* Page Size Extensions */
#define X86_FEATURE_TSC		0x00000010	/* Time Stamp Counter */
#define X86_FEATURE_MSR		0x00000020	/* Model-Specific Registers, RDMSR, WRMSR */
#define X86_FEATURE_PAE		0x00000040	/* Physical Address Extensions */
#define X86_FEATURE_MCE		0x00000080	/* Machine Check Exceptions */
#define X86_FEATURE_CX8		0x00000100	/* CMPXCHG8 instruction */
#define X86_FEATURE_APIC	0x00000200	/* onboard APIC */
#define X86_FEATURE_10		0x00000400
#define X86_FEATURE_SEP		0x00000800	/* Fast System Call */ 
#define X86_FEATURE_MTRR	0x00001000	/* Memory Type Range Registers */
#define X86_FEATURE_PGE		0x00002000	/* Page Global Enable */
#define X86_FEATURE_MCA		0x00004000	/* Machine Check Architecture */
#define X86_FEATURE_CMOV	0x00008000	/* CMOV instruction (FCMOVCC and FCOMI too if FPU present) */
#define X86_FEATURE_PAT	0x00010000	/* Page Attribute Table */
#define X86_FEATURE_PSE36	0x00020000	/* 36-bit PSEs */
#define X86_FEATURE_18		0x00040000
#define X86_FEATURE_19		0x00080000
#define X86_FEATURE_20		0x00100000
#define X86_FEATURE_21		0x00200000
#define X86_FEATURE_22		0x00400000
#define X86_FEATURE_MMX		0x00800000	/* multimedia extensions */
#define X86_FEATURE_FXSR	0x01000000	/* FXSAVE and FXRSTOR instructions (fast save and restore of FPU context), and CR4.OSFXSR (OS uses these instructions) available */
#define X86_FEATURE_25		0x02000000
#define X86_FEATURE_26		0x04000000
#define X86_FEATURE_27		0x08000000
#define X86_FEATURE_28		0x10000000
#define X86_FEATURE_29		0x20000000
#define X86_FEATURE_30		0x40000000
#define X86_FEATURE_AMD3D	0x80000000

extern struct cpuinfo_x86 boot_cpu_data;

#ifdef __SMP__
extern struct cpuinfo_x86 cpu_data[];
#define current_cpu_data cpu_data[smp_processor_id()]
#else
#define cpu_data &boot_cpu_data
#define current_cpu_data boot_cpu_data
#endif

extern char ignore_irq13;

extern void identify_cpu(struct cpuinfo_x86 *);
extern void print_cpu_info(struct cpuinfo_x86 *);
extern void dodgy_tsc(void);

/*
 *	Generic CPUID function
 */
extern inline void cpuid(int op, int *eax, int *ebx, int *ecx, int *edx)
{
	__asm__("cpuid"
		: "=a" (*eax),
		  "=b" (*ebx),
		  "=c" (*ecx),
		  "=d" (*edx)
		: "a" (op)
		: "cc");
}

/*
 *      Cyrix CPU configuration register indexes
 */
#define CX86_CCR2 0xc2
#define CX86_CCR3 0xc3
#define CX86_CCR4 0xe8
#define CX86_CCR5 0xe9
#define CX86_DIR0 0xfe
#define CX86_DIR1 0xff

/*
 *      Cyrix CPU indexed register access macros
 */

#define getCx86(reg) ({ outb((reg), 0x22); inb(0x23); })

#define setCx86(reg, data) do { \
	outb((reg), 0x22); \
	outb((data), 0x23); \
} while (0)

/*
 * Bus types (default is ISA, but people can check others with these..)
 */
extern int EISA_bus;
extern int MCA_bus;

/* from system description table in BIOS.  Mostly for MCA use, but
others may find it useful. */
extern unsigned int machine_id;
extern unsigned int machine_submodel_id;
extern unsigned int BIOS_revision;

/*
 * User space process size: 3GB (default).
 */
#define TASK_SIZE	(PAGE_OFFSET)

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	(TASK_SIZE / 3)

/*
 * Size of io_bitmap in longwords: 32 is ports 0-0x3ff.
 */
#define IO_BITMAP_SIZE	32

struct i387_hard_struct {
	long	cwd;
	long	swd;
	long	twd;
	long	fip;
	long	fcs;
	long	foo;
	long	fos;
	long	st_space[20];	/* 8*10 bytes for each FP-reg = 80 bytes */
	long	status;		/* software status information */
};

struct i387_soft_struct {
	long	cwd;
	long	swd;
	long	twd;
	long	fip;
	long	fcs;
	long	foo;
	long	fos;
	long	st_space[20];	/* 8*10 bytes for each FP-reg = 80 bytes */
	unsigned char	ftop, changed, lookahead, no_update, rm, alimit;
	struct info	*info;
	unsigned long	entry_eip;
};

union i387_union {
	struct i387_hard_struct hard;
	struct i387_soft_struct soft;
};

typedef struct {
	unsigned long seg;
} mm_segment_t;

struct thread_struct {
	unsigned short	back_link,__blh;
	unsigned long	esp0;
	unsigned short	ss0,__ss0h;
	unsigned long	esp1;
	unsigned short	ss1,__ss1h;
	unsigned long	esp2;
	unsigned short	ss2,__ss2h;
	unsigned long	cr3;
	unsigned long	eip;
	unsigned long	eflags;
	unsigned long	eax,ecx,edx,ebx;
	unsigned long	esp;
	unsigned long	ebp;
	unsigned long	esi;
	unsigned long	edi;
	unsigned short	es, __esh;
	unsigned short	cs, __csh;
	unsigned short	ss, __ssh;
	unsigned short	ds, __dsh;
	unsigned short	fs, __fsh;
	unsigned short	gs, __gsh;
	unsigned short	ldt, __ldth;
	unsigned short	trace, bitmap;
	unsigned long	io_bitmap[IO_BITMAP_SIZE+1];
	unsigned long	tr;
	unsigned long	cr2, trap_no, error_code;
	mm_segment_t	segment;
/* debug registers */
	long debugreg[8];  /* Hardware debugging registers */
/* floating point info */
	union i387_union i387;
/* virtual 86 mode info */
	struct vm86_struct * vm86_info;
	unsigned long screen_bitmap;
	unsigned long v86flags, v86mask, v86mode, saved_esp0;
};

#define INIT_MMAP \
{ &init_mm, 0, 0, NULL, PAGE_SHARED, VM_READ | VM_WRITE | VM_EXEC, 1, NULL, NULL }

#define INIT_TSS  {						\
	0,0, /* back_link, __blh */				\
	sizeof(init_stack) + (long) &init_stack, /* esp0 */	\
	__KERNEL_DS, 0, /* ss0 */				\
	0,0,0,0,0,0, /* stack1, stack2 */			\
	(long) &swapper_pg_dir - PAGE_OFFSET, /* cr3 */		\
	0,0, /* eip,eflags */					\
	0,0,0,0, /* eax,ecx,edx,ebx */				\
	0,0,0,0, /* esp,ebp,esi,edi */				\
	0,0,0,0,0,0, /* es,cs,ss */				\
	0,0,0,0,0,0, /* ds,fs,gs */				\
	_LDT(0),0, /* ldt */					\
	0, 0x8000, /* tace, bitmap */				\
	{~0, }, /* ioperm */					\
	_TSS(0), 0, 0, 0, (mm_segment_t) { 0 }, /* obsolete */	\
	{ 0, },							\
	{ { 0, }, },  /* 387 state */				\
	NULL, 0, 0, 0, 0, 0, /* vm86_info */			\
}

#define start_thread(regs, new_eip, new_esp) do {		\
	__asm__("movl %w0,%%fs ; movl %w0,%%gs": :"r" (0));	\
	set_fs(USER_DS);					\
	regs->xds = __USER_DS;					\
	regs->xes = __USER_DS;					\
	regs->xss = __USER_DS;					\
	regs->xcs = __USER_CS;					\
	regs->eip = new_eip;					\
	regs->esp = new_esp;					\
} while (0)

/* Forward declaration, a strange C thing */
struct mm_struct;

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);
extern int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags);

/* Copy and release all segment info associated with a VM */
extern void copy_segments(int nr, struct task_struct *p, struct mm_struct * mm);
extern void release_segments(struct mm_struct * mm);
extern void forget_segments(void);

/*
 * FPU lazy state save handling..
 */
#define save_fpu(tsk) do { \
	asm volatile("fnsave %0\n\tfwait":"=m" (tsk->tss.i387)); \
	tsk->flags &= ~PF_USEDFPU; \
	stts(); \
} while (0)

#define unlazy_fpu(tsk) do { \
	if (tsk->flags & PF_USEDFPU) \
		save_fpu(tsk); \
} while (0)

#define clear_fpu(tsk) do { \
	if (tsk->flags & PF_USEDFPU) { \
		tsk->flags &= ~PF_USEDFPU; \
		stts(); \
	} \
} while (0)

/*
 * Return saved PC of a blocked thread.
 */
extern inline unsigned long thread_saved_pc(struct thread_struct *t)
{
	return ((unsigned long *)t->esp)[3];
}

extern struct task_struct * alloc_task_struct(void);
extern void free_task_struct(struct task_struct *);

#define init_task	(init_task_union.task)
#define init_stack	(init_task_union.stack)

#endif /* __ASM_I386_PROCESSOR_H */
