#ifndef __ASM_PPC_PROCESSOR_H
#define __ASM_PPC_PROCESSOR_H

#include <asm/ptrace.h>
#include <asm/residual.h>

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

#define MSR_		MSR_ME|MSR_FE0|MSR_FE1|MSR_RI
#define MSR_KERNEL      MSR_|MSR_IR|MSR_DR
#define MSR_USER	MSR_KERNEL|MSR_PR|MSR_EE

/* Bit encodings for Hardware Implementation Register (HID0)
   on PowerPC 603, 604, etc. processors (not 601). */
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

#define _MACH_Motorola 1 /* motorola prep */
#define _MACH_IBM      2 /* ibm prep */
#define _MACH_Pmac     4 /* pmac or pmac clone (non-chrp) */
#define _MACH_chrp     8 /* chrp machine */

#ifndef __ASSEMBLY__
extern int _machine;

/* if we're a prep machine */
#define is_prep (_machine & (_MACH_Motorola|_MACH_IBM))
/*
 * if we have openfirmware - pmac/chrp have it implicitly
 * but we have to check residual data to know on prep
 */
extern __inline__ int have_of(void)
{
	if ( (_machine & (_MACH_Pmac|_MACH_chrp)) /*||
	     ( is_prep && (res.VitalProductData.FirmwareSupplier & OpenFirmware))*/)
		return 1; 
	else 
		return 0;
}

struct task_struct;
void start_thread(struct pt_regs *regs, unsigned long nip, unsigned long sp);
void release_thread(struct task_struct *);

/*
 * Bus types
 */
#define EISA_bus 0
#define EISA_bus__is_a_macro /* for versions in ksyms.c */
#define MCA_bus 0
#define MCA_bus__is_a_macro /* for versions in ksyms.c */

/*
 * this is the minimum allowable io space due to the location
 * of the io areas on prep (first one at 0x80000000) but
 * as soon as I get around to remapping the io areas with the BATs
 * to match the mac we can raise this. -- Cort
 */
#define TASK_SIZE	(0x80000000UL)

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE     (TASK_SIZE / 8 * 3)

struct thread_struct {
	unsigned long	ksp;		/* Kernel stack pointer */
	unsigned long	wchan;		/* Event task is sleeping on */
	struct pt_regs	*regs;		/* Pointer to saved register state */
	unsigned long   fs;		/* for get_fs() validation */
	signed long     last_syscall;
	double		fpr[32];	/* Complete floating point set */
	unsigned long	fpscr_pad;	/* fpr ... fpscr must be contiguous */
	unsigned long	fpscr;		/* Floating point status */
};

#define INIT_SP		(sizeof(init_stack) + (unsigned long) &init_stack)

#define INIT_TSS  { \
	INIT_SP, /* ksp */ \
	0, /* wchan */ \
	(struct pt_regs *)INIT_SP - 1, /* regs */ \
	KERNEL_DS, /*fs*/ \
	0, /* last_syscall */ \
	{0}, 0, 0 \
}

#define INIT_MMAP { &init_mm, KERNELBASE/*0*/, 0xffffffff/*0x40000000*/, \
		      PAGE_SHARED, VM_READ | VM_WRITE | VM_EXEC }

/*
 * Return saved PC of a blocked thread. For now, this is the "user" PC
 */
static inline unsigned long thread_saved_pc(struct thread_struct *t)
{
	return (t->regs) ? t->regs->nip : 0;
}

/*
 * NOTE! The task struct and the stack go together
 */
#define alloc_task_struct() \
	((struct task_struct *) __get_free_pages(GFP_KERNEL,1,0))
#define free_task_struct(p)	free_pages((unsigned long)(p),1)

/* in process.c - for early bootup debug -- Cort */
int ll_printk(const char *, ...);
void ll_puts(const char *);

#define init_task	(init_task_union.task)
#define init_stack	(init_task_union.stack)

#endif /* ndef ASSEMBLY*/

  
#endif /* __ASM_PPC_PROCESSOR_H */







