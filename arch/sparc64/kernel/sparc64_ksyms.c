/* $Id: sparc64_ksyms.c,v 1.49 1998/10/28 08:11:28 jj Exp $
 * arch/sparc64/kernel/sparc64_ksyms.c: Sparc64 specific ksyms support.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Eddie C. Dost (ecd@skynet.be)
 */

/* Tell string.h we don't want memcpy etc. as cpp defines */
#define EXPORT_SYMTAB_STROPS
#define PROMLIB_INTERNAL

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/in6.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#include <asm/oplib.h>
#include <asm/delay.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/softirq.h>
#include <asm/hardirq.h>
#include <asm/idprom.h>
#include <asm/svr4.h>
#include <asm/head.h>
#include <asm/smp.h>
#include <asm/mostek.h>
#include <asm/ptrace.h>
#include <asm/user.h>
#include <asm/uaccess.h>
#include <asm/checksum.h>
#include <asm/fpumacro.h>
#ifdef CONFIG_SBUS
#include <asm/sbus.h>
#include <asm/dma.h>
#endif
#ifdef CONFIG_PCI
#include <asm/ebus.h>
#endif
#include <asm/a.out.h>
#include <asm/svr4.h>

struct poll {
	int fd;
	short events;
	short revents;
};

extern unsigned prom_cpu_nodes[NR_CPUS];
extern void die_if_kernel(char *str, struct pt_regs *regs);
extern unsigned long sunos_mmap(unsigned long, unsigned long, unsigned long,
				unsigned long, unsigned long, unsigned long);
void _sigpause_common (unsigned int set, struct pt_regs *);
extern void *__bzero(void *, size_t);
extern void *__bzero_noasi(void *, size_t);
extern void *__memscan_zero(void *, size_t);
extern void *__memscan_generic(void *, int, size_t);
extern int __memcmp(const void *, const void *, __kernel_size_t);
extern int __strncmp(const char *, const char *, __kernel_size_t);
extern __kernel_size_t __strlen(const char *);
extern __kernel_size_t strlen(const char *);
extern char saved_command_line[];
extern char *getname32(u32 name);
extern void linux_sparc_syscall(void);
extern void rtrap(void);
extern void show_regs(struct pt_regs *);
extern void solaris_syscall(void);
extern void syscall_trace(void);
extern u32 sunos_sys_table[], sys_call_table32[];
extern void tl0_solaris(void);
extern void sys_sigsuspend(void);
extern int sys_getppid(void);
extern int svr4_getcontext(svr4_ucontext_t *uc, struct pt_regs *regs);
extern int svr4_setcontext(svr4_ucontext_t *uc, struct pt_regs *regs);
extern int sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg);
extern int sys32_ioctl(unsigned int fd, unsigned int cmd, u32 arg);
extern int (*handle_mathemu)(struct pt_regs *, struct fpustate *);
                
extern void bcopy (const char *, char *, int);
extern int __ashrdi3(int, int);

extern void dump_thread(struct pt_regs *, struct user *);

#ifdef __SMP__
extern spinlock_t scheduler_lock;
extern spinlock_t kernel_flag;
extern int smp_num_cpus;
#ifdef SPIN_LOCK_DEBUG
extern void _do_spin_lock (spinlock_t *lock, char *str);
extern void _do_spin_unlock (spinlock_t *lock);
extern int _spin_trylock (spinlock_t *lock);
extern void _do_read_lock(rwlock_t *rw, char *str);
extern void _do_read_unlock(rwlock_t *rw, char *str);
extern void _do_write_lock(rwlock_t *rw, char *str);
extern void _do_write_unlock(rwlock_t *rw);
#endif
#endif

/* One thing to note is that the way the symbols of the mul/div
 * support routines are named is a mess, they all start with
 * a '.' which makes it a bitch to export, here is the trick:
 */

#define EXPORT_SYMBOL_PRIVATE(sym)				\
extern int __sparc_priv_ ## sym (int) __asm__("__" ## #sym);	\
const struct module_symbol __export_priv_##sym			\
__attribute__((section("__ksymtab"))) =				\
{ (unsigned long) &__sparc_priv_ ## sym, "__" ## #sym }

/* used by various drivers */
#ifdef __SMP__
/* Kernel wide locking */
EXPORT_SYMBOL(scheduler_lock);
EXPORT_SYMBOL(kernel_flag);

/* Software-IRQ BH locking */
EXPORT_SYMBOL(global_bh_lock);
EXPORT_SYMBOL(global_bh_count);
EXPORT_SYMBOL(synchronize_bh);

/* Hard IRQ locking */
EXPORT_SYMBOL(global_irq_holder);
EXPORT_SYMBOL(global_irq_lock);
EXPORT_SYMBOL(global_irq_count);
EXPORT_SYMBOL(synchronize_irq);
EXPORT_SYMBOL_PRIVATE(global_cli);
EXPORT_SYMBOL_PRIVATE(global_sti);
EXPORT_SYMBOL_PRIVATE(global_save_flags);
EXPORT_SYMBOL_PRIVATE(global_restore_flags);

/* Per-CPU information table */
EXPORT_SYMBOL(cpu_data);

/* Misc SMP information */
EXPORT_SYMBOL(smp_num_cpus);

/* Spinlock debugging library, optional. */
#ifdef SPIN_LOCK_DEBUG
EXPORT_SYMBOL(_do_spin_lock);
EXPORT_SYMBOL(_do_spin_unlock);
EXPORT_SYMBOL(_spin_trylock);
EXPORT_SYMBOL(_do_read_lock);
EXPORT_SYMBOL(_do_read_unlock);
EXPORT_SYMBOL(_do_write_lock);
EXPORT_SYMBOL(_do_write_unlock);
#endif

#else
EXPORT_SYMBOL(local_irq_count);
EXPORT_SYMBOL(local_bh_count);
#endif
EXPORT_SYMBOL(enable_irq);
EXPORT_SYMBOL(disable_irq);

EXPORT_SYMBOL_PRIVATE(flushw_user);

EXPORT_SYMBOL(mstk48t02_regs);
EXPORT_SYMBOL(request_fast_irq);
EXPORT_SYMBOL(sparc_alloc_io);
EXPORT_SYMBOL(sparc_free_io);
EXPORT_SYMBOL(sparc_ultra_unmapioaddr);
EXPORT_SYMBOL(mmu_get_scsi_sgl);
EXPORT_SYMBOL(mmu_get_scsi_one);
EXPORT_SYMBOL(sparc_dvma_malloc);
EXPORT_SYMBOL(mmu_release_scsi_one);
EXPORT_SYMBOL(mmu_release_scsi_sgl);
#if CONFIG_SBUS
EXPORT_SYMBOL(SBus_chain);
EXPORT_SYMBOL(dma_chain);
#endif
#if CONFIG_PCI
EXPORT_SYMBOL(ebus_chain);
EXPORT_SYMBOL(pci_dvma_offset);
EXPORT_SYMBOL(pci_dvma_mask);
EXPORT_SYMBOL(pci_dvma_v2p_hash);
EXPORT_SYMBOL(pci_dvma_p2v_hash);
EXPORT_SYMBOL(empty_zero_page);
EXPORT_SYMBOL(outsb);
EXPORT_SYMBOL(outsw);
EXPORT_SYMBOL(outsl);
EXPORT_SYMBOL(insb);
EXPORT_SYMBOL(insw);
EXPORT_SYMBOL(insl);
#endif

/* Solaris/SunOS binary compatibility */
EXPORT_SYMBOL(_sigpause_common);
EXPORT_SYMBOL(sunos_mmap);

/* Should really be in linux/kernel/ksyms.c */
EXPORT_SYMBOL(dump_thread);

/* math-emu wants this */
EXPORT_SYMBOL(die_if_kernel);

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
EXPORT_SYMBOL(saved_command_line);
EXPORT_SYMBOL(prom_getname);
EXPORT_SYMBOL(prom_feval);
EXPORT_SYMBOL(prom_getstring);
EXPORT_SYMBOL(prom_apply_sbus_ranges);
EXPORT_SYMBOL(prom_getint);
EXPORT_SYMBOL(prom_getintdefault);
EXPORT_SYMBOL(__prom_getchild);
EXPORT_SYMBOL(__prom_getsibling);

/* sparc library symbols */
EXPORT_SYMBOL(bcopy);
EXPORT_SYMBOL(__strlen);
#if __GNUC__ > 2 || __GNUC_MINOR__ >= 91
EXPORT_SYMBOL(strlen);
#endif
EXPORT_SYMBOL(strnlen);
EXPORT_SYMBOL(strcpy);
EXPORT_SYMBOL(strncpy);
EXPORT_SYMBOL(strcat);
EXPORT_SYMBOL(strncat);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(strchr);
EXPORT_SYMBOL(strrchr);
EXPORT_SYMBOL(strpbrk);
EXPORT_SYMBOL(strtok);
EXPORT_SYMBOL(strstr);
EXPORT_SYMBOL(strspn);

#ifdef CONFIG_SOLARIS_EMUL_MODULE
EXPORT_SYMBOL(getname32);
EXPORT_SYMBOL(linux_sparc_syscall);
EXPORT_SYMBOL(rtrap);
EXPORT_SYMBOL(show_regs);
EXPORT_SYMBOL(solaris_syscall);
EXPORT_SYMBOL(syscall_trace);
EXPORT_SYMBOL(sunos_sys_table);
EXPORT_SYMBOL(sys_call_table32);
EXPORT_SYMBOL(tl0_solaris);
EXPORT_SYMBOL(sys_sigsuspend);
EXPORT_SYMBOL(sys_getppid);
EXPORT_SYMBOL(svr4_getcontext);
EXPORT_SYMBOL(svr4_setcontext);
EXPORT_SYMBOL(prom_cpu_nodes);
EXPORT_SYMBOL(sys_ioctl);
EXPORT_SYMBOL(sys32_ioctl);
EXPORT_SYMBOL(get_unmapped_area);
EXPORT_SYMBOL(move_addr_to_kernel);
EXPORT_SYMBOL(move_addr_to_user);
#endif

/* Special internal versions of library functions. */
EXPORT_SYMBOL(__memcpy);
EXPORT_SYMBOL(__memset);
EXPORT_SYMBOL(clear_page);
EXPORT_SYMBOL(__bzero);
EXPORT_SYMBOL(__memscan_zero);
EXPORT_SYMBOL(__memscan_generic);
EXPORT_SYMBOL(__memcmp);
EXPORT_SYMBOL(__strncmp);
EXPORT_SYMBOL(__memmove);

EXPORT_SYMBOL(csum_partial_copy_sparc64);

/* Moving data to/from userspace. */
EXPORT_SYMBOL(__copy_to_user);
EXPORT_SYMBOL(__copy_from_user);
EXPORT_SYMBOL(__strncpy_from_user);
EXPORT_SYMBOL(__bzero_noasi);

/* No version information on this, heavily used in inline asm,
 * and will always be 'void __ret_efault(void)'.
 */
EXPORT_SYMBOL_NOVERS(__ret_efault);

/* No version information on these, as gcc produces such symbols. */
EXPORT_SYMBOL_NOVERS(memcmp);
EXPORT_SYMBOL_NOVERS(memcpy);
EXPORT_SYMBOL_NOVERS(memset);
EXPORT_SYMBOL_NOVERS(memmove);
