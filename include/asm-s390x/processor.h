/*
 *  include/asm-s390/processor.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/processor.h"
 *    Copyright (C) 1994, Linus Torvalds
 */

#ifndef __ASM_S390_PROCESSOR_H
#define __ASM_S390_PROCESSOR_H

#include <asm/page.h>
#include <asm/ptrace.h>

#ifdef __KERNEL__
/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ void *pc; __asm__("basr %0,0":"=a"(pc)); pc; })

/*
 *  CPU type and hardware bug flags. Kept separately for each CPU.
 *  Members of this structure are referenced in head.S, so think twice
 *  before touching them. [mj]
 */

typedef struct
{
        unsigned int version :  8;
        unsigned int ident   : 24;
        unsigned int machine : 16;
        unsigned int unused  : 16;
} __attribute__ ((packed)) cpuid_t;

struct cpuinfo_S390
{
        cpuid_t  cpu_id;
        unsigned long loops_per_jiffy;
        unsigned long *pgd_quick;
        unsigned long *pmd_quick;
        unsigned long *pte_quick;
        unsigned long pgtable_cache_sz;
        __u16    cpu_addr;
        __u16    cpu_nr;
        __u16    pad[2];
};

extern void print_cpu_info(struct cpuinfo_S390 *);

/* Lazy FPU handling on uni-processor */
extern struct task_struct *last_task_used_math;

/*
 * User space process size: 4TB (default).
 */
#define TASK_SIZE       (0x20000000000UL)
#define TASK31_SIZE     (0x80000000UL)

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE \
	(test_thread_flag(TIF_31BIT) ? (TASK31_SIZE / 2) : (TASK_SIZE / 2))

#define THREAD_SIZE (4*PAGE_SIZE)

typedef struct {
       __u32 ar4;
} mm_segment_t;

/* if you change the thread_struct structure, you must
 * update the _TSS_* defines in entry.S
 */

struct thread_struct
 {
	s390_fp_regs fp_regs;
        __u32   ar2;                   /* kernel access register 2         */
        __u32   ar4;                   /* kernel access register 4         */
        addr_t  ksp;                   /* kernel stack pointer             */
        addr_t  user_seg;              /* HSTD                             */
        addr_t  prot_addr;             /* address of protection-excep.     */
        __u32   error_code;            /* error-code of last prog-excep.   */
        __u32   trap_no;
        per_struct per_info;/* Must be aligned on an 4 byte boundary*/
	/* Used to give failing instruction back to user for ieee exceptions */
	addr_t  ieee_instruction_pointer; 
	unsigned long flags;            /* various flags */
        /* pfault_wait is used to block the process on a pfault event */
	addr_t  pfault_wait;
};

typedef struct thread_struct thread_struct;

#define INIT_THREAD {{0,{{0},{0},{0},{0},{0},{0},{0},{0},{0},{0}, \
			    {0},{0},{0},{0},{0},{0}}},            \
                     0, 0,                                        \
                    sizeof(init_stack) + (addr_t) &init_stack,    \
              (__pa((addr_t) &swapper_pg_dir[0]) + _REGION_TABLE),\
                     0,0,0,                                       \
                     (per_struct) {{{{0,}}},0,0,0,0,{{0,}}},      \
		     0, 0, 0				          \
} 

/* need to define ... */
#define start_thread(regs, new_psw, new_stackp) do {            \
        regs->psw.mask  = PSW_USER_BITS;                        \
        regs->psw.addr  = new_psw;                              \
        regs->gprs[15]  = new_stackp;                           \
} while (0)

#define start_thread31(regs, new_psw, new_stackp) do {          \
	regs->psw.mask  = PSW_USER32_BITS;			\
        regs->psw.addr  = new_psw;                              \
        regs->gprs[15]  = new_stackp;                           \
} while (0)


/* Forward declaration, a strange C thing */
struct task_struct;
struct mm_struct;

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);
extern int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags);

/*
 * Return saved PC of a blocked thread.
 */
extern inline unsigned long thread_saved_pc(struct task_struct *t);

/*
 * Print register of task into buffer. Used in fs/proc/array.c.
 */
extern char *task_show_regs(struct task_struct *task, char *buffer);

unsigned long get_wchan(struct task_struct *p);
#define __KSTK_PTREGS(tsk) ((struct pt_regs *) \
        (((addr_t) tsk->thread_info + THREAD_SIZE - sizeof(struct pt_regs)) & -8L))
#define KSTK_EIP(tsk)	(__KSTK_PTREGS(tsk)->psw.addr)
#define KSTK_ESP(tsk)	(__KSTK_PTREGS(tsk)->gprs[15])

#define cpu_relax()	barrier()

/*
 * Set PSW mask to specified value, while leaving the
 * PSW addr pointing to the next instruction.
 */

static inline void __load_psw_mask (unsigned long mask)
{
	unsigned long addr;

	psw_t psw;
	psw.mask = mask;

	asm volatile (
		"    larl  %0,1f\n"
		"    stg   %0,8(%1)\n"
		"    lpswe 0(%1)\n"
		"1:"
		: "=&d" (addr) : "a" (&psw) : "memory", "cc" );
}

/*
 * Function to stop a processor until an interruption occured
 */
static inline void enabled_wait(void)
{
	unsigned long reg;
	psw_t wait_psw;

	wait_psw.mask = PSW_BASE_BITS | PSW_MASK_IO | PSW_MASK_EXT |
		PSW_MASK_MCHECK | PSW_MASK_WAIT;
	asm volatile (
		"    larl  %0,0f\n"
		"    stg   %0,8(%1)\n"
		"    lpswe 0(%1)\n"
		"0:"
		: "=&a" (reg) : "a" (&wait_psw) : "memory", "cc" );
}

/*
 * Function to drop a processor into disabled wait state
 */

static inline void disabled_wait(addr_t code)
{
        char psw_buffer[2*sizeof(psw_t)];
        char ctl_buf[8];
        psw_t *dw_psw = (psw_t *)(((unsigned long) &psw_buffer+sizeof(psw_t)-1)
                                  & -sizeof(psw_t));

        dw_psw->mask = PSW_BASE_BITS | PSW_MASK_WAIT;
        dw_psw->addr = code;
        /* 
         * Store status and then load disabled wait psw,
         * the processor is dead afterwards
         */
        asm volatile ("    stctg 0,0,0(%1)\n"
                      "    ni    4(%1),0xef\n" /* switch off protection */
                      "    lctlg 0,0,0(%1)\n"
                      "    lghi  1,0x1000\n"
                      "    stpt  0x328(1)\n"      /* store timer */
                      "    stckc 0x330(1)\n"      /* store clock comparator */
                      "    stpx  0x318(1)\n"      /* store prefix register */
                      "    stam  0,15,0x340(1)\n" /* store access registers */
                      "    stfpc 0x31c(1)\n"      /* store fpu control */
                      "    std   0,0x200(1)\n"    /* store f0 */
                      "    std   1,0x208(1)\n"    /* store f1 */
                      "    std   2,0x210(1)\n"    /* store f2 */
                      "    std   3,0x218(1)\n"    /* store f3 */
                      "    std   4,0x220(1)\n"    /* store f4 */
                      "    std   5,0x228(1)\n"    /* store f5 */
                      "    std   6,0x230(1)\n"    /* store f6 */
                      "    std   7,0x238(1)\n"    /* store f7 */
                      "    std   8,0x240(1)\n"    /* store f8 */
                      "    std   9,0x248(1)\n"    /* store f9 */
                      "    std   10,0x250(1)\n"   /* store f10 */
                      "    std   11,0x258(1)\n"   /* store f11 */
                      "    std   12,0x260(1)\n"   /* store f12 */
                      "    std   13,0x268(1)\n"   /* store f13 */
                      "    std   14,0x270(1)\n"   /* store f14 */
                      "    std   15,0x278(1)\n"   /* store f15 */
                      "    stmg  0,15,0x280(1)\n" /* store general registers */
                      "    stctg 0,15,0x380(1)\n" /* store control registers */
                      "    oi    0x384(1),0x10\n" /* fake protection bit */
                      "    lpswe 0(%0)"
                      : : "a" (dw_psw), "a" (&ctl_buf) : "cc", "0", "1");
}

#endif

#endif                                 /* __ASM_S390_PROCESSOR_H           */

