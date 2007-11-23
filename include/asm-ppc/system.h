#ifndef __PPC_SYSTEM_H
#define __PPC_SYSTEM_H

#include <linux/kdev_t.h>
#include <asm/processor.h>
#include <asm/atomic.h>

/*
 * Memory barrier.
 * The sync instruction guarantees that all memory accesses initiated
 * by this processor have been performed (with respect to all other
 * mechanisms that access memory).  The eieio instruction is a barrier
 * providing an ordering (separately) for (a) cacheable stores and (b)
 * loads and stores to non-cacheable memory (e.g. I/O devices).
 *
 * mb() prevents loads and stores being reordered across this point.
 * rmb() prevents loads being reordered across this point.
 * wmb() prevents stores being reordered across this point.
 *
 * We can use the eieio instruction for wmb, but since it doesn't
 * give any ordering guarantees about loads, we have to use the
 * stronger but slower sync instruction for mb and rmb.
 */
#define mb()  __asm__ __volatile__ ("sync" : : : "memory")
#define rmb()  __asm__ __volatile__ ("sync" : : : "memory")
#define wmb()  __asm__ __volatile__ ("eieio" : : : "memory")

extern void xmon_irq(int, void *, struct pt_regs *);
extern void xmon(struct pt_regs *excp);

#define __save_flags(flags)	({\
	__asm__ __volatile__ ("mfmsr %0" : "=r" ((flags)) : : "memory"); })
#define __save_and_cli(flags)	({__save_flags(flags);__cli();})

/* Data cache block flush - write out the cache line containing the
   specified address and then invalidate it in the cache. */
extern __inline__ void dcbf(void *line)
{
	asm("dcbf %0,%1; sync" : : "r" (line), "r" (0));
}

extern __inline__ void __restore_flags(unsigned long flags)
{
        extern atomic_t ppc_n_lost_interrupts;
	extern void do_lost_interrupts(unsigned long);

        if ((flags & MSR_EE) && atomic_read(&ppc_n_lost_interrupts) != 0) {
                do_lost_interrupts(flags);
        } else {
                __asm__ __volatile__ ("sync; mtmsr %0; isync"
                              : : "r" (flags) : "memory");
        }
}


extern void __sti(void);
extern void __cli(void);
extern int _disable_interrupts(void);
extern void _enable_interrupts(int);

extern void print_backtrace(unsigned long *);
extern void show_regs(struct pt_regs * regs);
extern void flush_instruction_cache(void);
extern void hard_reset_now(void);
extern void poweroff_now(void);
extern int _get_PVR(void);
extern long _get_L2CR(void);
extern void _set_L2CR(unsigned long);
extern void via_cuda_init(void);
extern void pmac_nvram_init(void);
extern void read_rtc_time(void);
extern void pmac_find_display(void);
extern void giveup_fpu(struct task_struct *);
extern void enable_kernel_fp(void);
extern void cvt_fd(float *from, double *to, unsigned long *fpscr);
extern void cvt_df(double *from, float *to, unsigned long *fpscr);

struct device_node;
extern void note_scsi_host(struct device_node *, void *);

struct task_struct;
#define switch_to(prev,next,last) _switch_to((prev),(next),&(last))
extern void _switch_to(struct task_struct *, struct task_struct *,
		       struct task_struct **);

struct thread_struct;
extern struct task_struct *_switch(struct thread_struct *prev,
				     struct thread_struct *next,
				     unsigned long context);

struct pt_regs;
extern void dump_regs(struct pt_regs *);

#ifndef __SMP__

#define cli()	__cli()
#define sti()	__sti()
#define save_flags(flags)	__save_flags(flags)
#define restore_flags(flags)	__restore_flags(flags)
#define save_and_cli(flags)	__save_and_cli(flags)

#else /* __SMP__ */

extern void __global_cli(void);
extern void __global_sti(void);
extern unsigned long __global_save_flags(void);
extern void __global_restore_flags(unsigned long);
#define cli() __global_cli()
#define sti() __global_sti()
#define save_flags(x) ((x)=__global_save_flags())
#define restore_flags(x) __global_restore_flags(x)

#endif /* !__SMP__ */

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

extern unsigned long xchg_u64(void *ptr, unsigned long val);
extern unsigned long xchg_u32(void *ptr, unsigned long val);

/*
 * This function doesn't exist, so you'll get a linker error
 * if something tries to do an invalid xchg().
 *
 * This only works if the compiler isn't horribly bad at optimizing.
 * gcc-2.5.8 reportedly can't handle this, but as that doesn't work
 * too well on the alpha anyway..
 */
extern void __xchg_called_with_bad_pointer(void);

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))
#define tas(ptr) (xchg((ptr),1))

static inline unsigned long __xchg(unsigned long x, void * ptr, int size)
{
	switch (size) {
	case 4:
		return (unsigned long )xchg_u32(ptr, x);
	case 8:
		return (unsigned long )xchg_u64(ptr, x);
	}
	__xchg_called_with_bad_pointer();
	return x;


}

extern inline void * xchg_ptr(void * m, void * val)
{
	return (void *) xchg_u32(m, (unsigned long) val);
}

#endif
