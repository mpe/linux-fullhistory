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
	int	fdiv_bug;
	int	f00f_bug;
	unsigned long loops_per_sec;
};

#define X86_VENDOR_INTEL 0
#define X86_VENDOR_CYRIX 1
#define X86_VENDOR_AMD 2
#define X86_VENDOR_UMC 3
#define X86_VENDOR_NEXGEN 4
#define X86_VENDOR_CENTAUR 5
#define X86_VENDOR_UNKNOWN 0xff

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
 * User space process size: 3GB. This is hardcoded into a few places,
 * so don't change it unless you know what you are doing.
 */
#define TASK_SIZE	(0xC0000000UL)

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
/* floating point info */
	union i387_union i387;
/* virtual 86 mode info */
	struct vm86_struct * vm86_info;
	unsigned long screen_bitmap;
	unsigned long v86flags, v86mask, v86mode, saved_esp0;
};

#define INIT_MMAP \
{ &init_mm, 0, 0, PAGE_SHARED, VM_READ | VM_WRITE | VM_EXEC, NULL, &init_mm.mmap }

#define INIT_TSS  { \
	0,0, \
	sizeof(init_stack) + (long) &init_stack, \
	__KERNEL_DS, 0, \
	0,0,0,0,0,0, \
	(long) &swapper_pg_dir - PAGE_OFFSET, \
	0,0,0,0,0,0,0,0,0,0, \
	__USER_DS,0,__USER_DS,0,__USER_DS,0, \
	__USER_DS,0,__USER_DS,0,__USER_DS,0, \
	_LDT(0),0, \
	0, 0x8000, \
	{~0, }, /* ioperm */ \
	_TSS(0), 0, 0, 0, (mm_segment_t) { 0 } /* obsolete */ , \
	{ { 0, }, },  /* 387 state */ \
	NULL, 0, 0, 0, 0, 0 /* vm86_info */, \
}

#define start_thread(regs, new_eip, new_esp) do {\
	unsigned long seg = __USER_DS; \
	__asm__("mov %w0,%%fs ; mov %w0,%%gs":"=r" (seg) :"0" (seg)); \
	set_fs(USER_DS); \
	regs->xds = seg; \
	regs->xes = seg; \
	regs->xss = seg; \
	regs->xcs = __USER_CS; \
	regs->eip = new_eip; \
	regs->esp = new_esp; \
} while (0)

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

/*
 * Return saved PC of a blocked thread.
 */
extern inline unsigned long thread_saved_pc(struct thread_struct *t)
{
	return ((unsigned long *)t->esp)[3];
}

/* Allocation and freeing of basic task resources. */
/*
 * NOTE! The task struct and the stack go together
 */
#define alloc_task_struct() \
	((struct task_struct *) __get_free_pages(GFP_KERNEL,1,0))
#define free_task_struct(p)	free_pages((unsigned long)(p),1)

#define init_task	(init_task_union.task)
#define init_stack	(init_task_union.stack)

#endif /* __ASM_I386_PROCESSOR_H */
