#ifndef _ASM_IA64_SYSTEM_H
#define _ASM_IA64_SYSTEM_H

/*
 * System defines. Note that this is included both from .c and .S
 * files, so it does only defines, not any C code.  This is based
 * on information published in the Processor Abstraction Layer
 * and the System Abstraction Layer manual.
 *
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 Asit Mallick <asit.k.mallick@intel.com>
 * Copyright (C) 1999 Don Dugger <don.dugger@intel.com>
 */
#include <linux/config.h>

#include <asm/page.h>

#define KERNEL_START		(PAGE_OFFSET + 0x500000)

/*
 * The following #defines must match with vmlinux.lds.S:
 */
#define IVT_END_ADDR		(KERNEL_START + 0x8000)
#define ZERO_PAGE_ADDR		(IVT_END_ADDR + 0*PAGE_SIZE)
#define SWAPPER_PGD_ADDR	(IVT_END_ADDR + 1*PAGE_SIZE)

#define GATE_ADDR		(0xa000000000000000 + PAGE_SIZE)

#ifndef __ASSEMBLY__

#include <linux/types.h>

struct pci_vector_struct {
	__u16 bus;	/* PCI Bus number */
        __u32 pci_id;	/* ACPI split 16 bits device, 16 bits function (see section 6.1.1) */
        __u8 pin;	/* PCI PIN (0 = A, 1 = B, 2 = C, 3 = D) */
        __u8 irq;	/* IRQ assigned */
};

extern struct ia64_boot_param {
	__u64 command_line;		/* physical address of command line arguments */
	__u64 efi_systab;		/* physical address of EFI system table */
	__u64 efi_memmap;		/* physical address of EFI memory map */
	__u64 efi_memmap_size;		/* size of EFI memory map */
	__u64 efi_memdesc_size;		/* size of an EFI memory map descriptor */
	__u32 efi_memdesc_version;	/* memory descriptor version */
	struct {
		__u16 num_cols;	/* number of columns on console output device */
		__u16 num_rows;	/* number of rows on console output device */
		__u16 orig_x;	/* cursor's x position */
		__u16 orig_y;	/* cursor's y position */
	} console_info;
	__u16 num_pci_vectors;	/* number of ACPI derived PCI IRQ's*/
	__u64 pci_vectors;	/* physical address of PCI data (pci_vector_struct)*/
	__u64 fpswa;		/* physical address of the the fpswa interface */
} ia64_boot_param;

extern inline void
ia64_insn_group_barrier (void)
{
	__asm__ __volatile__ (";;" ::: "memory");
}

/*
 * Macros to force memory ordering.  In these descriptions, "previous"
 * and "subsequent" refer to program order; "visible" means that all
 * architecturally visible effects of a memory access have occurred
 * (at a minimum, this means the memory has been read or written).
 *
 *   wmb():	Guarantees that all preceding stores to memory-
 *		like regions are visible before any subsequent
 *		stores and that all following stores will be
 *		visible only after all previous stores.
 *   rmb():	Like wmb(), but for reads.
 *   mb():	wmb()/rmb() combo, i.e., all previous memory
 *		accesses are visible before all subsequent
 *		accesses and vice versa.  This is also known as
 *		a "fence."
 *
 * Note: "mb()" and its variants cannot be used as a fence to order
 * accesses to memory mapped I/O registers.  For that, mf.a needs to
 * be used.  However, we don't want to always use mf.a because (a)
 * it's (presumably) much slower than mf and (b) mf.a is supported for
 * sequential memory pages only.
 */
#define mb()	__asm__ __volatile__ ("mf" ::: "memory")
#define rmb()	mb()
#define wmb()	mb()

/*
 * XXX check on these---I suspect what Linus really wants here is
 * acquire vs release semantics but we can't discuss this stuff with
 * Linus just yet.  Grrr...
 */
#define set_mb(var, value)	do { (var) = (value); mb(); } while (0)
#define set_rmb(var, value)	do { (var) = (value); mb(); } while (0)
#define set_wmb(var, value)	do { (var) = (value); mb(); } while (0)

/*
 * The group barrier in front of the rsm & ssm are necessary to ensure
 * that none of the previous instructions in the same group are
 * affected by the rsm/ssm.
 */
/* For spinlocks etc */

#ifdef CONFIG_IA64_DEBUG_IRQ

  extern unsigned long last_cli_ip;

# define local_irq_save(x)								\
do {											\
	unsigned long ip, psr;								\
											\
	__asm__ __volatile__ ("mov %0=psr;; rsm psr.i;;" : "=r" (psr) :: "memory");	\
	if (psr & (1UL << 14)) {							\
		__asm__ ("mov %0=ip" : "=r"(ip));					\
		last_cli_ip = ip;							\
	}										\
	(x) = psr;									\
} while (0)

# define local_irq_disable()								\
do {											\
	unsigned long ip, psr;								\
											\
	__asm__ __volatile__ ("mov %0=psr;; rsm psr.i;;" : "=r" (psr) :: "memory");	\
	if (psr & (1UL << 14)) {							\
		__asm__ ("mov %0=ip" : "=r"(ip));					\
		last_cli_ip = ip;							\
	}										\
} while (0)

# define local_irq_restore(x)						 \
do {									 \
	unsigned long ip, old_psr, psr = (x);				 \
									 \
	__asm__ __volatile__ ("mov %0=psr; mov psr.l=%1;; srlz.d"	 \
			      : "=&r" (old_psr) : "r" (psr) : "memory"); \
	if ((old_psr & (1UL << 14)) && !(psr & (1UL << 14))) {		 \
		__asm__ ("mov %0=ip" : "=r"(ip));			 \
		last_cli_ip = ip;					 \
	}								 \
} while (0)

#else /* !CONFIG_IA64_DEBUG_IRQ */
  /* clearing of psr.i is implicitly serialized (visible by next insn) */
# define local_irq_save(x)	__asm__ __volatile__ ("mov %0=psr;; rsm psr.i;;"	\
						      : "=r" (x) :: "memory")
# define local_irq_disable()	__asm__ __volatile__ (";; rsm psr.i;;" ::: "memory")
/* (potentially) setting psr.i requires data serialization: */
# define local_irq_restore(x)	__asm__ __volatile__ ("mov psr.l=%0;; srlz.d"		\
						      :: "r" (x) : "memory")
#endif /* !CONFIG_IA64_DEBUG_IRQ */

#define local_irq_enable()	__asm__ __volatile__ (";; ssm psr.i;; srlz.d" ::: "memory")

#define __cli()			local_irq_disable ()
#define __save_flags(flags)	__asm__ __volatile__ ("mov %0=psr" : "=r" (flags) :: "memory")
#define __save_and_cli(flags)	local_irq_save(flags)
#define save_and_cli(flags)	__save_and_cli(flags)


#ifdef CONFIG_IA64_SOFTSDV_HACKS
/*
 * Yech.  SoftSDV has a slight probem with psr.i and itc/itm.  If
 * PSR.i = 0 and ITC == ITM, you don't get the timer tick posted.  So,
 * I'll check if ITC is larger than ITM here and reset if neccessary.
 * I may miss a tick to two.
 * 
 * Don't include asm/delay.h; it causes include loops that are
 * mind-numbingly hard to follow.
 */

#define get_itc(x) __asm__ __volatile__("mov %0=ar.itc" : "=r"((x)) :: "memory")
#define get_itm(x) __asm__ __volatile__("mov %0=cr.itm" : "=r"((x)) :: "memory")
#define set_itm(x) __asm__ __volatile__("mov cr.itm=%0" :: "r"((x)) : "memory")

#define __restore_flags(x)			\
do {						\
        unsigned long itc, itm;			\
	local_irq_restore(x);			\
        get_itc(itc);				\
        get_itm(itm);				\
        if (itc > itm)				\
		set_itm(itc + 10);		\
} while (0)

#define __sti()					\
do {						\
	unsigned long itc, itm;			\
	local_irq_enable();			\
	get_itc(itc);				\
	get_itm(itm);				\
	if (itc > itm)				\
		set_itm(itc + 10);		\
} while (0)

#else /* !CONFIG_IA64_SOFTSDV_HACKS */

#define __sti()			local_irq_enable ()
#define __restore_flags(flags)	local_irq_restore(flags)

#endif /* !CONFIG_IA64_SOFTSDV_HACKS */

#ifdef CONFIG_SMP
  extern void __global_cli (void);
  extern void __global_sti (void);
  extern unsigned long __global_save_flags (void);
  extern void __global_restore_flags (unsigned long);
# define cli()			__global_cli()
# define sti()			__global_sti()
# define save_flags(flags)	((flags) = __global_save_flags())
# define restore_flags(flags)	__global_restore_flags(flags)
#else /* !CONFIG_SMP */
# define cli()			__cli()
# define sti()			__sti()
# define save_flags(flags)	__save_flags(flags)
# define restore_flags(flags)	__restore_flags(flags)
#endif /* !CONFIG_SMP */

/*
 * Force an unresolved reference if someone tries to use
 * ia64_fetch_and_add() with a bad value.
 */
extern unsigned long __bad_size_for_ia64_fetch_and_add (void);
extern unsigned long __bad_increment_for_ia64_fetch_and_add (void);

#define IA64_FETCHADD(tmp,v,n,sz)						\
({										\
	switch (sz) {								\
	      case 4:								\
		__asm__ __volatile__ ("fetchadd4.rel %0=%1,%3"			\
				      : "=r"(tmp), "=m"(__atomic_fool_gcc(v))	\
				      : "m" (__atomic_fool_gcc(v)), "i"(n));	\
		break;								\
										\
	      case 8:								\
		__asm__ __volatile__ ("fetchadd8.rel %0=%1,%3"			\
				      : "=r"(tmp), "=m"(__atomic_fool_gcc(v))	\
				      : "m" (__atomic_fool_gcc(v)), "i"(n));	\
		break;								\
										\
	      default:								\
		__bad_size_for_ia64_fetch_and_add();				\
	}									\
})

#define ia64_fetch_and_add(i,v)							\
({										\
	__u64 _tmp;								\
	volatile __typeof__(*(v)) *_v = (v);					\
	switch (i) {								\
	      case -16:	IA64_FETCHADD(_tmp, _v, -16, sizeof(*(v))); break;	\
	      case  -8:	IA64_FETCHADD(_tmp, _v,  -8, sizeof(*(v))); break;	\
	      case  -4:	IA64_FETCHADD(_tmp, _v,  -4, sizeof(*(v))); break;	\
	      case  -1:	IA64_FETCHADD(_tmp, _v,  -1, sizeof(*(v))); break;	\
	      case   1:	IA64_FETCHADD(_tmp, _v,   1, sizeof(*(v))); break;	\
	      case   4:	IA64_FETCHADD(_tmp, _v,   4, sizeof(*(v))); break;	\
	      case   8:	IA64_FETCHADD(_tmp, _v,   8, sizeof(*(v))); break;	\
	      case  16:	IA64_FETCHADD(_tmp, _v,  16, sizeof(*(v))); break;	\
	      default:								\
		_tmp = __bad_increment_for_ia64_fetch_and_add();		\
		break;								\
	}									\
	(__typeof__(*v)) (_tmp + (i));	/* return new value */			\
})

/*
 * This function doesn't exist, so you'll get a linker error if
 * something tries to do an invalid xchg().
 */
extern void __xchg_called_with_bad_pointer (void);

static __inline__ unsigned long
__xchg (unsigned long x, volatile void *ptr, int size)
{
	unsigned long result;

	switch (size) {
	      case 1:
		__asm__ __volatile ("xchg1 %0=%1,%2" : "=r" (result)
				    : "m" (*(char *) ptr), "r" (x) : "memory");
		return result;

	      case 2:
		__asm__ __volatile ("xchg2 %0=%1,%2" : "=r" (result)
				    : "m" (*(short *) ptr), "r" (x) : "memory");
		return result;

	      case 4:
		__asm__ __volatile ("xchg4 %0=%1,%2" : "=r" (result)
				    : "m" (*(int *) ptr), "r" (x) : "memory");
		return result;

	      case 8:
		__asm__ __volatile ("xchg8 %0=%1,%2" : "=r" (result)
				    : "m" (*(long *) ptr), "r" (x) : "memory");
		return result;
	}
	__xchg_called_with_bad_pointer();
	return x;
}

#define xchg(ptr,x)							     \
  ((__typeof__(*(ptr))) __xchg ((unsigned long) (x), (ptr), sizeof(*(ptr))))
#define tas(ptr)	(xchg ((ptr), 1))

/* 
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 */

#define __HAVE_ARCH_CMPXCHG 1

/*
 * This function doesn't exist, so you'll get a linker error
 * if something tries to do an invalid cmpxchg().
 */
extern long __cmpxchg_called_with_bad_pointer(void);

struct __xchg_dummy { unsigned long a[100]; };
#define __xg(x) (*(struct __xchg_dummy *)(x))

#define ia64_cmpxchg(sem,ptr,old,new,size)				\
({									\
	__typeof__(ptr) _p_ = (ptr);					\
	__typeof__(new) _n_ = (new);					\
	__u64 _o_, _r_;							\
									\
	switch (size) {							\
	      case 1: _o_ = (__u8 ) (old); break;			\
	      case 2: _o_ = (__u16) (old); break;			\
	      case 4: _o_ = (__u32) (old); break;			\
	      case 8: _o_ = (__u64) (old); break;			\
	      default:							\
	}								\
	 __asm__ __volatile__ ("mov ar.ccv=%0;;" :: "rO"(_o_));		\
	switch (size) {							\
	      case 1:							\
		__asm__ __volatile__ ("cmpxchg1."sem" %0=%2,%3,ar.ccv"	\
				      : "=r"(_r_), "=m"(__xg(_p_))	\
				      : "m"(__xg(_p_)), "r"(_n_));	\
		break;							\
									\
	      case 2:							\
		__asm__ __volatile__ ("cmpxchg2."sem" %0=%2,%3,ar.ccv"	\
				      : "=r"(_r_), "=m"(__xg(_p_))	\
				      : "m"(__xg(_p_)), "r"(_n_));	\
		break;							\
									\
	      case 4:							\
		__asm__ __volatile__ ("cmpxchg4."sem" %0=%2,%3,ar.ccv"	\
				      : "=r"(_r_), "=m"(__xg(_p_))	\
				      : "m"(__xg(_p_)), "r"(_n_));	\
		break;							\
									\
	      case 8:							\
		__asm__ __volatile__ ("cmpxchg8."sem" %0=%2,%3,ar.ccv"	\
				      : "=r"(_r_), "=m"(__xg(_p_))	\
				      : "m"(__xg(_p_)), "r"(_n_));	\
		break;							\
									\
	      default:							\
		_r_ = __cmpxchg_called_with_bad_pointer();		\
		break;							\
	}								\
	(__typeof__(old)) _r_;						\
})

#define cmpxchg_acq(ptr,o,n)	ia64_cmpxchg("acq", (ptr), (o), (n), sizeof(*(ptr)))
#define cmpxchg_rel(ptr,o,n)	ia64_cmpxchg("rel", (ptr), (o), (n), sizeof(*(ptr)))

/* for compatibility with other platforms: */
#define cmpxchg(ptr,o,n)	cmpxchg_acq(ptr,o,n)

#ifdef CONFIG_IA64_DEBUG_CMPXCHG
# define CMPXCHG_BUGCHECK_DECL	int _cmpxchg_bugcheck_count = 128;
# define CMPXCHG_BUGCHECK(v)							\
  do {										\
	if (_cmpxchg_bugcheck_count-- <= 0) {					\
		void *ip;							\
		extern int printk(const char *fmt, ...);			\
		asm ("mov %0=ip" : "=r"(ip));					\
		printk("CMPXCHG_BUGCHECK: stuck at %p on word %p\n", ip, (v));	\
		break;								\
	}									\
  } while (0)
#else /* !CONFIG_IA64_DEBUG_CMPXCHG */
# define CMPXCHG_BUGCHECK_DECL
# define CMPXCHG_BUGCHECK(v)
#endif /* !CONFIG_IA64_DEBUG_CMPXCHG */

#ifdef __KERNEL__

extern void ia64_save_debug_regs (unsigned long *save_area);
extern void ia64_load_debug_regs (unsigned long *save_area);

#define prepare_to_switch()    do { } while(0)

#ifdef CONFIG_IA32_SUPPORT
# define IS_IA32_PROCESS(regs)	(ia64_psr(regs)->is != 0)
# define IA32_STATE(prev,next)							\
	if (IS_IA32_PROCESS(ia64_task_regs(prev))) {				\
	    __asm__ __volatile__("mov %0=ar.eflag":"=r"((prev)->thread.eflag));	\
	    __asm__ __volatile__("mov %0=ar.fsr":"=r"((prev)->thread.fsr));	\
	    __asm__ __volatile__("mov %0=ar.fcr":"=r"((prev)->thread.fcr));	\
	    __asm__ __volatile__("mov %0=ar.fir":"=r"((prev)->thread.fir));	\
	    __asm__ __volatile__("mov %0=ar.fdr":"=r"((prev)->thread.fdr));	\
	}									\
	if (IS_IA32_PROCESS(ia64_task_regs(next))) {				\
	    __asm__ __volatile__("mov ar.eflag=%0"::"r"((next)->thread.eflag));	\
	    __asm__ __volatile__("mov ar.fsr=%0"::"r"((next)->thread.fsr));	\
	    __asm__ __volatile__("mov ar.fcr=%0"::"r"((next)->thread.fcr));	\
	    __asm__ __volatile__("mov ar.fir=%0"::"r"((next)->thread.fir));	\
	    __asm__ __volatile__("mov ar.fdr=%0"::"r"((next)->thread.fdr));	\
	}
#else /* !CONFIG_IA32_SUPPORT */
# define IA32_STATE(prev,next)
# define IS_IA32_PROCESS(regs)		0
#endif /* CONFIG_IA32_SUPPORT */

/*
 * Context switch from one thread to another.  If the two threads have
 * different address spaces, schedule() has already taken care of
 * switching to the new address space by calling switch_mm().
 *
 * Disabling access to the fph partition and the debug-register
 * context switch MUST be done before calling ia64_switch_to() since a
 * newly created thread returns directly to
 * ia64_ret_from_syscall_clear_r8.
 */
extern struct task_struct *ia64_switch_to (void *next_task);
#define __switch_to(prev,next,last) do {					\
	ia64_psr(ia64_task_regs(next))->dfh = (ia64_get_fpu_owner() != (next));	\
	if ((prev)->thread.flags & IA64_THREAD_DBG_VALID) {			\
		ia64_save_debug_regs(&(prev)->thread.dbr[0]);			\
	}									\
	if ((next)->thread.flags & IA64_THREAD_DBG_VALID) {			\
		ia64_load_debug_regs(&(next)->thread.dbr[0]);			\
	}									\
	IA32_STATE(prev,next);							\
	(last) = ia64_switch_to((next));					\
} while (0)

#ifdef CONFIG_SMP 
  /*
   * In the SMP case, we save the fph state when context-switching
   * away from a thread that owned and modified fph.  This way, when
   * the thread gets scheduled on another CPU, the CPU can pick up the
   * state frm task->thread.fph, avoiding the complication of having
   * to fetch the latest fph state from another CPU.  If the thread
   * happens to be rescheduled on the same CPU later on and nobody
   * else has touched the FPU in the meantime, the thread will fault
   * upon the first access to fph but since the state in fph is still
   * valid, no other overheads are incurred.  In other words, CPU
   * affinity is a Good Thing.
   */
# define switch_to(prev,next,last) do {							\
	if (ia64_get_fpu_owner() == (prev) && ia64_psr(ia64_task_regs(prev))->mfh) {	\
		(prev)->thread.flags |= IA64_THREAD_FPH_VALID;				\
		__ia64_save_fpu((prev)->thread.fph);					\
	}										\
	__switch_to(prev,next,last);							\
  } while (0)
#else
# define switch_to(prev,next,last) 	__switch_to(prev,next,last)
#endif

#endif /* __KERNEL__ */

#endif /* __ASSEMBLY__ */

#endif /* _ASM_IA64_SYSTEM_H */
