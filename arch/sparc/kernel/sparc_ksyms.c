/* $Id: sparc_ksyms.c,v 1.24 1996/10/27 08:36:08 davem Exp $
 * arch/sparc/kernel/ksyms.c: Sparc specific ksyms support.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Eddie C. Dost (ecd@skynet.be)
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>

#include <asm/oplib.h>
#include <asm/delay.h>
#include <asm/system.h>
#include <asm/auxio.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/idprom.h>
#include <asm/svr4.h>
#include <asm/head.h>
#include <asm/smp.h>
#include <asm/mostek.h>
#include <asm/ptrace.h>
#include <asm/user.h>
#ifdef CONFIG_SBUS
#include <asm/sbus.h>
#endif

struct poll {
	int fd;
	short events;
	short revents;
};

extern int svr4_getcontext (svr4_ucontext_t *, struct pt_regs *);
extern int svr4_setcontext (svr4_ucontext_t *, struct pt_regs *);
extern int sunos_poll(struct poll * ufds, size_t nfds, int timeout);
extern unsigned long sunos_mmap(unsigned long, unsigned long, unsigned long,
				unsigned long, unsigned long, unsigned long);
void _sigpause_common (unsigned int set, struct pt_regs *);
extern void __copy_1page(void *, const void *);
extern void *__memcpy(void *, const void *, __kernel_size_t);
extern void *__memset(void *, int, __kernel_size_t);
extern void *bzero_1page(void *);
extern void *__bzero(void *, size_t);
extern void *__memscan_zero(void *, size_t);
extern void *__memscan_generic(void *, int, size_t);
extern int __memcmp(const void *, const void *, __kernel_size_t);
extern int __strncmp(const char *, const char *, __kernel_size_t);

extern int __copy_to_user(unsigned long to, unsigned long from, int size);
extern int __copy_from_user(unsigned long to, unsigned long from, int size);
extern int __clear_user(unsigned long addr, int size);
extern int __strncpy_from_user(unsigned long dest, unsigned long src, int count);

extern void bcopy (const char *, char *, int);
extern int __ashrdi3(int, int);

extern void dump_thread(struct pt_regs *, struct user *);

/* One thing to note is that the way the symbols of the mul/div
 * support routines are named is a mess, they all start with
 * a '.' which makes it a bitch to export, here is the trick:
 */
#define DD(sym) extern int __sparc_dot_ ## sym (int) __asm__("." ## #sym)
#define XD(sym) { (void *) & __sparc_dot_ ## sym, "." ## #sym }

DD(rem);
DD(urem);
DD(div);
DD(udiv);
DD(mul);
DD(umul);

static struct symbol_table arch_symbol_table = {
#include <linux/symtab_begin.h>

	/* used by various drivers */
	X(sparc_cpu_model),
#ifdef __SMP__
	X(kernel_flag),
	X(kernel_counter),
	X(active_kernel_processor),
	X(syscall_count),
#endif
	X(page_offset),

	X(udelay),
	X(mstk48t02_regs),
#if CONFIG_SUN_AUXIO
	X(auxio_register),
#endif
	X(request_fast_irq),
	X(sparc_alloc_io),
	X(sparc_free_io),
	X(mmu_unlockarea),
	X(mmu_lockarea),
	X(SBus_chain),

	/* Solaris/SunOS binary compatibility */
	X(svr4_setcontext),
	X(svr4_getcontext),
	X(_sigpause_common),
	X(sunos_mmap),
	X(sunos_poll),

	/* Should really be in linux/kernel/ksyms.c */
	X(dump_thread),

	/* prom symbols */
	X(idprom),
	X(prom_root_node),
	X(prom_getchild),
        X(prom_getsibling),
	X(prom_searchsiblings),
	X(prom_firstprop),
        X(prom_nextprop),
	X(prom_getproplen),
	X(prom_getproperty),
        X(prom_setprop),
        X(prom_nodeops),
	X(prom_getbootargs),
	X(prom_apply_obio_ranges),
	X(prom_getname),
	X(prom_feval),
	X(romvec),

	/* sparc library symbols */
	X(bcopy),
	X(memmove),
	X(memscan),
	X(strlen),
	X(strnlen),
	X(strcpy),
	X(strncpy),
	X(strcat),
	X(strncat),
	X(strcmp),
	X(strncmp),
	X(strchr),
	X(strrchr),
	X(strpbrk),
	X(strtok),
	X(strstr),
	X(strspn),

	/* Special internal versions of library functions. */
	X(__copy_1page),
	X(__memcpy),
	X(__memset),
	X(bzero_1page),
	X(__bzero),
	X(__memscan_zero),
	X(__memscan_generic),
	X(__memcmp),
	X(__strncmp),

	/* Moving data to/from userspace. */
	X(__copy_to_user),
	X(__copy_from_user),
	X(__clear_user),
	X(__strncpy_from_user),

	/* No version information on these, as gcc produces such symbols. */
	XNOVERS(memcmp),
	XNOVERS(memcpy),
	XNOVERS(memset),
	XNOVERS(__ashrdi3),

	XD(rem),
	XD(urem),
	XD(mul),
	XD(umul),
	XD(div),
	XD(udiv),
#include <linux/symtab_end.h>
};

void arch_syms_export(void)
{
	register_symtab(&arch_symbol_table);
#if CONFIG_AP1000
	ap_register_ksyms();
#endif
}
