/* $Id: sparc_ksyms.c,v 1.43 1997/01/26 07:12:30 davem Exp $
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
#include <asm/uaccess.h>
#ifdef CONFIG_SBUS
#include <asm/sbus.h>
#include <asm/dma.h>
#endif
#include <asm/a.out.h>

struct poll {
	int fd;
	short events;
	short revents;
};

extern int svr4_getcontext (svr4_ucontext_t *, struct pt_regs *);
extern int svr4_setcontext (svr4_ucontext_t *, struct pt_regs *);
extern unsigned long sunos_mmap(unsigned long, unsigned long, unsigned long,
				unsigned long, unsigned long, unsigned long);
void _sigpause_common (unsigned int set, struct pt_regs *);
extern void __copy_1page(void *, const void *);
extern void __memcpy(void *, const void *, __kernel_size_t);
extern void __memmove(void *, const void *, __kernel_size_t);
extern void *__memset(void *, int, __kernel_size_t);
extern void *bzero_1page(void *);
extern void *__bzero(void *, size_t);
extern void *__memscan_zero(void *, size_t);
extern void *__memscan_generic(void *, int, size_t);
extern int __memcmp(const void *, const void *, __kernel_size_t);
extern int __strncmp(const char *, const char *, __kernel_size_t);

extern int __copy_user(unsigned long to, unsigned long from, int size);
extern int __clear_user(unsigned long addr, int size);
extern int __strncpy_from_user(unsigned long dest, unsigned long src, int count);

extern void bcopy (const char *, char *, int);
extern int __ashrdi3(int, int);

extern void dump_thread(struct pt_regs *, struct user *);

/* One thing to note is that the way the symbols of the mul/div
 * support routines are named is a mess, they all start with
 * a '.' which makes it a bitch to export, here is the trick:
 */

#define EXPORT_SYMBOL_DOT(sym)					\
extern int __sparc_dot_ ## sym (int) __asm__("." #sym);		\
__EXPORT_SYMBOL(__sparc_dot_ ## sym, "." #sym)

#define EXPORT_SYMBOL_PRIVATE(sym)				\
extern int __sparc_priv_ ## sym (int) __asm__("__" ## #sym);	\
const struct module_symbol __export_priv_##sym			\
__attribute__((section("__ksymtab"))) =				\
{ (unsigned long) &__sparc_priv_ ## sym, "__" ## #sym }

/* used by various drivers */
EXPORT_SYMBOL(sparc_cpu_model);
#ifdef __SMP__
EXPORT_SYMBOL(klock_info);
#endif
EXPORT_SYMBOL_PRIVATE(_lock_kernel);
EXPORT_SYMBOL_PRIVATE(_unlock_kernel);
EXPORT_SYMBOL(page_offset);
EXPORT_SYMBOL(stack_top);

/* Atomic operations. */
EXPORT_SYMBOL_PRIVATE(_xchg32);
EXPORT_SYMBOL_PRIVATE(_atomic_add);
EXPORT_SYMBOL_PRIVATE(_atomic_sub);

/* Bit operations. */
EXPORT_SYMBOL_PRIVATE(_set_bit);
EXPORT_SYMBOL_PRIVATE(_clear_bit);
EXPORT_SYMBOL_PRIVATE(_change_bit);
EXPORT_SYMBOL_PRIVATE(_set_le_bit);
EXPORT_SYMBOL_PRIVATE(_clear_le_bit);

EXPORT_SYMBOL(udelay);
EXPORT_SYMBOL(mstk48t02_regs);
#if CONFIG_SUN_AUXIO
EXPORT_SYMBOL(auxio_register);
#endif
EXPORT_SYMBOL(request_fast_irq);
EXPORT_SYMBOL(sparc_alloc_io);
EXPORT_SYMBOL(sparc_free_io);
EXPORT_SYMBOL(io_remap_page_range);
EXPORT_SYMBOL(mmu_v2p);
EXPORT_SYMBOL(mmu_unlockarea);
EXPORT_SYMBOL(mmu_lockarea);
EXPORT_SYMBOL(mmu_get_scsi_sgl);
EXPORT_SYMBOL(mmu_get_scsi_one);
EXPORT_SYMBOL(mmu_release_scsi_sgl);
EXPORT_SYMBOL(mmu_release_scsi_one);
EXPORT_SYMBOL(sparc_dvma_malloc);
EXPORT_SYMBOL(sun4c_unmapioaddr);
EXPORT_SYMBOL(srmmu_unmapioaddr);
#if CONFIG_SBUS
EXPORT_SYMBOL(SBus_chain);
EXPORT_SYMBOL(dma_chain);
#endif

/* Solaris/SunOS binary compatibility */
EXPORT_SYMBOL(svr4_setcontext);
EXPORT_SYMBOL(svr4_getcontext);
EXPORT_SYMBOL(_sigpause_common);
EXPORT_SYMBOL(sunos_mmap);

/* Should really be in linux/kernel/ksyms.c */
EXPORT_SYMBOL(dump_thread);

/* prom symbols */
EXPORT_SYMBOL(idprom);
EXPORT_SYMBOL(prom_root_node);
EXPORT_SYMBOL(prom_getchild);
EXPORT_SYMBOL(prom_getsibling);
EXPORT_SYMBOL(prom_searchsiblings);
EXPORT_SYMBOL(prom_firstprop);
EXPORT_SYMBOL(prom_nextprop);
EXPORT_SYMBOL(prom_getproplen);
EXPORT_SYMBOL(prom_getproperty);
EXPORT_SYMBOL(prom_node_has_property);
EXPORT_SYMBOL(prom_setprop);
EXPORT_SYMBOL(prom_nodeops);
EXPORT_SYMBOL(prom_getbootargs);
EXPORT_SYMBOL(prom_apply_obio_ranges);
EXPORT_SYMBOL(prom_getname);
EXPORT_SYMBOL(prom_feval);
EXPORT_SYMBOL(prom_getstring);
EXPORT_SYMBOL(prom_apply_sbus_ranges);
EXPORT_SYMBOL(prom_getint);
EXPORT_SYMBOL(prom_getintdefault);
EXPORT_SYMBOL(romvec);

/* sparc library symbols */
EXPORT_SYMBOL(bcopy);
EXPORT_SYMBOL(memscan);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strnlen);
EXPORT_SYMBOL(strcpy);
EXPORT_SYMBOL(strncpy);
EXPORT_SYMBOL(strcat);
EXPORT_SYMBOL(strncat);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(strncmp);
EXPORT_SYMBOL(strchr);
EXPORT_SYMBOL(strrchr);
EXPORT_SYMBOL(strpbrk);
EXPORT_SYMBOL(strtok);
EXPORT_SYMBOL(strstr);
EXPORT_SYMBOL(strspn);

/* Special internal versions of library functions. */
EXPORT_SYMBOL(__copy_1page);
EXPORT_SYMBOL(__memcpy);
EXPORT_SYMBOL(__memset);
EXPORT_SYMBOL(bzero_1page);
EXPORT_SYMBOL(__bzero);
EXPORT_SYMBOL(__memscan_zero);
EXPORT_SYMBOL(__memscan_generic);
EXPORT_SYMBOL(__memcmp);
EXPORT_SYMBOL(__strncmp);
EXPORT_SYMBOL(__memmove);

/* Moving data to/from userspace. */
EXPORT_SYMBOL(__copy_user);
EXPORT_SYMBOL(__clear_user);
EXPORT_SYMBOL(__strncpy_from_user);

/* No version information on this, heavily used in inline asm,
 * and will always be 'void __ret_efault(void)'.
 */
EXPORT_SYMBOL_NOVERS(__ret_efault);

/* No version information on these, as gcc produces such symbols. */
EXPORT_SYMBOL_NOVERS(memcmp);
EXPORT_SYMBOL_NOVERS(memcpy);
EXPORT_SYMBOL_NOVERS(memset);
EXPORT_SYMBOL_NOVERS(memmove);
EXPORT_SYMBOL_NOVERS(__ashrdi3);

EXPORT_SYMBOL_DOT(rem);
EXPORT_SYMBOL_DOT(urem);
EXPORT_SYMBOL_DOT(mul);
EXPORT_SYMBOL_DOT(umul);
EXPORT_SYMBOL_DOT(div);
EXPORT_SYMBOL_DOT(udiv);
