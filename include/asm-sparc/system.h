/* $Id: system.h,v 1.29 1996/04/03 02:17:52 davem Exp $ */
#ifndef __SPARC_SYSTEM_H
#define __SPARC_SYSTEM_H

#include <linux/kernel.h>

#include <asm/segment.h>

#ifdef __KERNEL__
#include <asm/page.h>
#include <asm/oplib.h>
#include <asm/psr.h>
#endif

#define EMPTY_PGT       (&empty_bad_page)
#define EMPTY_PGE	(&empty_bad_page_table)

#ifndef __ASSEMBLY__

/*
 * Sparc (general) CPU types
 */
enum sparc_cpu {
  sun4        = 0x00,
  sun4c       = 0x01,
  sun4m       = 0x02,
  sun4d       = 0x03,
  sun4e       = 0x04,
  sun4u       = 0x05, /* V8 ploos ploos */
  sun_unknown = 0x06,
};

extern enum sparc_cpu sparc_cpu_model;

extern unsigned long empty_bad_page;
extern unsigned long empty_bad_page_table;
extern unsigned long empty_zero_page;

extern struct linux_romvec *romvec;
#define halt() romvec->pv_halt()

/* When a context switch happens we must flush all user windows so that
 * the windows of the current process are flushed onto its stack. This
 * way the windows are all clean for the next process and the stack
 * frames are up to date.
 */
extern void flush_user_windows(void);
extern void synchronize_user_stack(void);
extern void sparc_switch_to(void *new_task);
#ifndef __SMP__
#define switch_to(prev, next) do { \
			  flush_user_windows(); \
		          switch_to_context(next); \
			  prev->tss.current_ds = active_ds; \
                          active_ds = next->tss.current_ds; \
			  if(last_task_used_math != next) \
				  next->tss.kregs->psr &= ~PSR_EF; \
                          sparc_switch_to(next); \
                     } while(0)
#else

extern void fpsave(unsigned long *fpregs, unsigned long *fsr,
		   void *fpqueue, unsigned long *fpqdepth);

#define switch_to(prev, next) do { \
			  cli(); \
			  if(prev->flags & PF_USEDFPU) { \
                          	fpsave(&prev->tss.float_regs[0], &prev->tss.fsr, \
                          	       &prev->tss.fpqueue[0], &prev->tss.fpqdepth); \
                          	prev->flags &= ~PF_USEDFPU; \
                          	prev->tss.kregs->psr &= ~PSR_EF; \
                          } \
			  prev->lock_depth = syscall_count; \
			  kernel_counter += (next->lock_depth - prev->lock_depth); \
			  syscall_count = next->lock_depth; \
			  flush_user_windows(); \
		          switch_to_context(next); \
			  prev->tss.current_ds = active_ds; \
                          active_ds = next->tss.current_ds; \
                          sparc_switch_to(next); \
			  sti(); \
                     } while(0)
#endif

/* Changing the IRQ level on the Sparc. */
extern inline void setipl(int __new_ipl)
{
	__asm__ __volatile__("rd %%psr, %%g1\n\t"
			     "andn %%g1, %1, %%g1\n\t"
			     "sll %0, 8, %%g2\n\t"
			     "and %%g2, %1, %%g2\n\t"
			     "or %%g1, %%g2, %%g1\n\t"
			     "wr %%g1, 0x0, %%psr\n\t"
			     "nop; nop; nop\n\t" : :
			     "r" (__new_ipl), "i" (PSR_PIL) :
			     "g1", "g2");
}

extern inline int getipl(void)
{
	int retval;

	__asm__ __volatile__("rd %%psr, %0\n\t"
			     "and %0, %1, %0\n\t"
			     "srl %0, 8, %0\n\t" :
			     "=r" (retval) :
			     "i" (PSR_PIL));
	return retval;
}

extern inline int swpipl(int __new_ipl)
{
	int retval;

	__asm__ __volatile__("rd %%psr, %%g1\n\t"
			     "srl %%g1, 8, %0\n\t"
			     "and %0, 15, %0\n\t"
			     "andn %%g1, %2, %%g1\n\t"
			     "and %1, 15, %%g2\n\t"
			     "sll %%g2, 8, %%g2\n\t"
			     "or %%g1, %%g2, %%g1\n\t"
			     "wr %%g1, 0x0, %%psr\n\t"
			     "nop; nop; nop\n\t" :
			     "=r" (retval) :
			     "r" (__new_ipl), "i" (PSR_PIL) :
			     "g1", "g2");
	return retval;
}

extern char spdeb_buf[256];

#define cli()			setipl(15)  /* 15 = no int's except nmi's */
#define sti()			setipl(0)   /* I'm scared */
#define save_flags(flags)	do { flags = getipl(); } while (0)
#define restore_flags(flags)	setipl(flags)

#define nop() __asm__ __volatile__ ("nop");

extern inline unsigned long xchg_u32(volatile unsigned long *m, unsigned long val)
{
	unsigned long flags, retval;

	save_flags(flags); cli();
	retval = *m;
	*m = val;
	restore_flags(flags);
	return retval;
}

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))
#define tas(ptr) (xchg((ptr),1))

extern void __xchg_called_with_bad_pointer(void);

static inline unsigned long __xchg(unsigned long x, volatile void * ptr, int size)
{
	switch (size) {
	case 4:
		return xchg_u32(ptr, x);
	};
	__xchg_called_with_bad_pointer();
	return x;
}

#endif /* __ASSEMBLY__ */

#endif /* !(__SPARC_SYSTEM_H) */
