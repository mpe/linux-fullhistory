#ifndef __ASM_PPC_PROCESSOR_H
#define __ASM_PPC_PROCESSOR_H

/*
 * PowerPC machine specifics
 */

#define KERNEL_STACK_SIZE (4096) /* usable stack -- not buffers at either end */
#define KERNEL_STACK_MASK (~(KERNEL_STACK_SIZE-1))

/* Bit encodings for Machine State Register (MSR) */
#define MSR_POW		(1<<18)		/* Enable Power Management */
#define MSR_TGPR	(1<<17)		/* TLB Update registers in use */
#define MSR_ILE		(1<<16)		/* Interrupt Little-Endian enable */
#define MSR_EE		(1<<15)		/* External Interrupt enable */
#define MSR_PR		(1<<14)		/* Supervisor/User privilege */
#define MSR_FP		(1<<13)		/* Floating Point enable */
#define MSR_ME		(1<<12)		/* Machine Check enable */
#define MSR_FE0		(1<<11)		/* Floating Exception mode 0 */
#define MSR_SE		(1<<10)		/* Single Step */
#define MSR_BE		(1<<9)		/* Branch Trace */
#define MSR_FE1		(1<<8)		/* Floating Exception mode 1 */
#define MSR_IP		(1<<6)		/* Exception prefix 0x000/0xFFF */
#define MSR_IR		(1<<5)		/* Instruction MMU enable */
#define MSR_DR		(1<<4)		/* Data MMU enable */
#define MSR_RI		(1<<1)		/* Recoverable Exception */
#define MSR_LE		(1<<0)		/* Little-Endian enable */

#define MSR_		MSR_FE0|MSR_FE1|MSR_ME|MSR_FP
#define MSR_USER	MSR_FE0|MSR_FE1|MSR_ME|MSR_PR|MSR_EE|MSR_IR|MSR_DR

/* Bit encodings for Hardware Implementation Register (HID0) */
#define HID0_EMCP	(1<<31)		/* Enable Machine Check pin */
#define HID0_EBA	(1<<29)		/* Enable Bus Address Parity */
#define HID0_EBD	(1<<28)		/* Enable Bus Data Parity */
#define HID0_SBCLK	(1<<27)
#define HID0_EICE	(1<<26)
#define HID0_ECLK	(1<<25)
#define HID0_PAR	(1<<24)
#define HID0_DOZE	(1<<23)
#define HID0_NAP	(1<<22)
#define HID0_SLEEP	(1<<21)
#define HID0_DPM	(1<<20)
#define HID0_ICE	(1<<15)		/* Instruction Cache Enable */
#define HID0_DCE	(1<<14)		/* Data Cache Enable */
#define HID0_ILOCK	(1<<13)		/* Instruction Cache Lock */
#define HID0_DLOCK	(1<<12)		/* Data Cache Lock */
#define HID0_ICFI	(1<<11)		/* Instruction Cache Flash Invalidate */
#define HID0_DCI	(1<<10)		/* Data Cache Invalidate */
#define HID0_SIED	(1<<7)		/* Serial Instruction Execution [Disable] */
#define HID0_BHTE	(1<<2)		/* Branch History Table Enable */

/* fpscr settings */
#define FPSCR_FX        (1<<31)
#define FPSCR_FEX       (1<<30)



#ifndef __ASSEMBLY__
/*
 * PowerPC machine specifics
 */
extern inline void start_thread(struct pt_regs *, unsigned long, unsigned long );


/*
 * Bus types
 */
#define EISA_bus 0
#define EISA_bus__is_a_macro /* for versions in ksyms.c */
#define MCA_bus 0
#define MCA_bus__is_a_macro /* for versions in ksyms.c */

/*
 * Write Protection works right in supervisor mode on the PowerPC
 */
#define wp_works_ok 1
#define wp_works_ok__is_a_macro /* for versions in ksyms.c */

/*
 * User space process size: 2GB. This is hardcoded into a few places,
 * so don't change it unless you know what you are doing.
 *
 * "this is gonna have to change to 1gig for the sparc" - David S. Miller
 */
#define TASK_SIZE	(0x80000000UL)

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	(TASK_SIZE / 3)

struct thread_struct 
{
  unsigned long	ksp;		/* Kernel stack pointer */
  unsigned long	*pg_tables;	/* MMU information */
  unsigned long	segs[16];	/* MMU Segment registers */
  unsigned long	last_pc;	/* PC when last entered system */
  unsigned long	user_stack;	/* [User] Stack when entered kernel */
  double       	fpr[32];	/* Complete floating point set */
  unsigned long	wchan;		/* Event task is sleeping on */
  unsigned long	*regs;		/* Pointer to saved register state */
  unsigned long fp_used;	/* number of quantums fp was used */
  unsigned long fs;		/* for get_fs() validation */
  unsigned long expc;		/* exception handler addr (see fault.c) */
  unsigned long excount;	/* exception handler count */
};


#define INIT_TSS  { \
	sizeof(init_kernel_stack) + (long) &init_kernel_stack,\
	(long *)swapper_pg_dir, {0}, \
	0, 0, {0}, \
	0, 0, 0, \
	KERNEL_DS, 0, 0 \
}

#define INIT_MMAP { &init_mm, 0, 0x40000000, \
		      PAGE_SHARED, VM_READ | VM_WRITE | VM_EXEC, NULL, &init_mm.mmap }

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

/*
 * Return saved PC of a blocked thread. For now, this is the "user" PC
 */
static inline unsigned long thread_saved_pc(struct thread_struct *t)
{
	return (t->last_pc);
}

#define _PROC_Motorola 0
#define _PROC_IBM      1
#define _PROC_Be       2

int _Processor;

/* Allocation and freeing of basic task resources. */
#define alloc_task_struct()	kmalloc(sizeof(struct task_struct), GFP_KERNEL)
#define free_task_struct(p)	kfree(p)

#ifdef KERNEL_STACK_BUFFER
/* give a 1 page buffer below the stack - if change then change ppc_machine.h */
#define alloc_kernel_stack()  \
          (memset((void *)__get_free_pages(GFP_KERNEL,1,0),0,KERNEL_STACK_SIZE+PAGE_SIZE)+PAGE_SIZE)
#define free_kernel_stack(page) free_pages((page)-PAGE_SIZE,1)
#else
#define alloc_kernel_stack()    get_free_page(GFP_KERNEL)
#define free_kernel_stack(page) free_page((page))
#endif

#endif /* ASSEMBLY*/

#endif

