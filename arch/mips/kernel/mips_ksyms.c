/* $Id: mips_ksyms.c,v 1.12 1998/09/16 22:50:41 ralf Exp $
 *
 * Export MIPS-specific functions needed for loadable modules.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 1998 by Ralf Baechle
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/in6.h>
#include <linux/pci.h>

#include <asm/checksum.h>
#include <asm/dma.h>
#include <asm/floppy.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/sgihpc.h>
#include <asm/softirq.h>
#include <asm/uaccess.h>

extern void *__bzero(void *__s, size_t __count);
extern long __strncpy_from_user_nocheck_asm(char *__to,
                                            const char *__from, long __len);
extern long __strncpy_from_user_asm(char *__to, const char *__from,
                                    long __len);
extern long __strlen_user_nocheck_asm(const char *s);
extern long __strlen_user_asm(const char *s);

EXPORT_SYMBOL(EISA_bus);

/*
 * String functions
 */
EXPORT_SYMBOL_NOVERS(memcmp);
EXPORT_SYMBOL_NOVERS(memset);
EXPORT_SYMBOL_NOVERS(memcpy);
EXPORT_SYMBOL_NOVERS(memmove);
EXPORT_SYMBOL_NOVERS(strcat);
EXPORT_SYMBOL_NOVERS(strchr);
EXPORT_SYMBOL_NOVERS(strlen);
EXPORT_SYMBOL_NOVERS(strncat);
EXPORT_SYMBOL_NOVERS(strnlen);
EXPORT_SYMBOL_NOVERS(strrchr);
EXPORT_SYMBOL_NOVERS(strtok);

EXPORT_SYMBOL(clear_page);
EXPORT_SYMBOL(__mips_bh_counter);
EXPORT_SYMBOL(local_bh_count);
EXPORT_SYMBOL(local_irq_count);
//EXPORT_SYMBOL(enable_irq);
//EXPORT_SYMBOL(disable_irq);

/*
 * Userspace access stuff.
 */
EXPORT_SYMBOL(__copy_user);
EXPORT_SYMBOL(__bzero);
EXPORT_SYMBOL(__strncpy_from_user_nocheck_asm);
EXPORT_SYMBOL(__strncpy_from_user_asm);
EXPORT_SYMBOL(__strlen_user_nocheck_asm);
EXPORT_SYMBOL(__strlen_user_asm);


/* Networking helper routines. */
EXPORT_SYMBOL(csum_partial_copy);

/*
 * Functions to control caches.
 */
EXPORT_SYMBOL(flush_page_to_ram);
EXPORT_SYMBOL(flush_cache_all);

/*
 * Base address of ports for Intel style I/O.
 */
EXPORT_SYMBOL(mips_io_port_base);

/*
 * Architecture specific stuff.
 */
#ifdef CONFIG_MIPS_JAZZ
EXPORT_SYMBOL(vdma_alloc);
EXPORT_SYMBOL(vdma_free);
EXPORT_SYMBOL(vdma_log2phys);
#endif

#ifdef CONFIG_SGI
EXPORT_SYMBOL(hpc3c0);
#endif

/*
 * Kernel hacking ...
 */
#include <asm/branch.h>
#include <linux/sched.h>

int register_fpe(void (*handler)(struct pt_regs *regs, unsigned int fcr31));
int unregister_fpe(void (*handler)(struct pt_regs *regs, unsigned int fcr31));

#ifdef CONFIG_MIPS_FPE_MODULE
EXPORT_SYMBOL(force_sig);
EXPORT_SYMBOL(__compute_return_epc);
EXPORT_SYMBOL(register_fpe);
EXPORT_SYMBOL(unregister_fpe);
#endif

#if CONFIG_PCI
EXPORT_SYMBOL(pci_devices);
#endif
