#ifndef __PPC_SYSTEM_H
#define __PPC_SYSTEM_H

#include <linux/delay.h>

#define mb()  __asm__ __volatile__ ("sync" : : : "memory")

#define __save_flags(flags)	({\
	__asm__ __volatile__ ("mfmsr %0" : "=r" ((flags)) : : "memory"); })
/* using Paul's in misc.S now -- Cort */
extern void __restore_flags(unsigned long flags);

/*
  #define __sti() _soft_sti(void)
  #define __cli() _soft_cli(void)
 */
extern void __sti(void);
extern void __cli(void);

extern void _hard_sti(void);
extern void _hard_cli(void);
extern void _soft_sti(void);
extern void _soft_cli(void);
extern int _disable_interrupts(void);
extern void _enable_interrupts(int);

extern void flush_instruction_cache(void);
extern void hard_reset_now(void);
extern void poweroff_now(void);
extern void find_scsi_boot(void);
extern int sd_find_target(void *, int);
extern int _get_PVR(void);
extern void via_cuda_init(void);
extern void read_rtc_time(void);
extern void pmac_find_display(void);
extern void giveup_fpu(void);
extern void store_cache_range(unsigned long, unsigned long);
extern void cvt_fd(float *from, double *to);
extern void cvt_df(double *from, float *to);

struct device_node;
extern void note_scsi_host(struct device_node *, void *);

struct task_struct;
extern void switch_to(struct task_struct *prev, struct task_struct *next);

struct thread_struct;
extern void _switch(struct thread_struct *prev, struct thread_struct *next,
		    unsigned long context);

struct pt_regs;
extern int do_signal(unsigned long oldmask, struct pt_regs *regs);
extern void dump_regs(struct pt_regs *);

#ifndef __SMP__
#define cli()	__cli()
#define sti()	__sti()
#define save_flags(flags)	__save_flags(flags)
#define restore_flags(flags)	__restore_flags(flags)

#else
#error need global cli/sti etc. defined for SMP
#endif

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

/* this guy lives in arch/ppc/kernel */
extern inline unsigned long *xchg_u32(void *m, unsigned long val);

/*
 *  these guys don't exist.
 *  someone should create them.
 *              -- Cort
 */
extern void *xchg_u64(void *ptr, unsigned long val);
extern int xchg_u8(char *m, char val);

/*
 * This function doesn't exist, so you'll get a linker error
 * if something tries to do an invalid xchg().
 *
 * This only works if the compiler isn't horribly bad at optimizing.
 * gcc-2.5.8 reportedly can't handle this, but as that doesn't work
 * too well on the alpha anyway..
 */
extern void __xchg_called_with_bad_pointer(void);

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



extern inline int tas(char * m)
{
	return xchg_u8(m,1);
}

extern inline void * xchg_ptr(void * m, void * val)
{
	return (void *) xchg_u32(m, (unsigned long) val);
}

#endif
