/*
 * Architecture-specific kernel symbols
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/string.h>
EXPORT_SYMBOL_NOVERS(memset);
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL_NOVERS(memcpy);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(strcat);
EXPORT_SYMBOL(strchr);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(strcpy);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strncat);
EXPORT_SYMBOL(strncmp);
EXPORT_SYMBOL(strncpy);
EXPORT_SYMBOL(strstr);
EXPORT_SYMBOL(strtok);

#include <linux/pci.h>
EXPORT_SYMBOL(pci_alloc_consistent);
EXPORT_SYMBOL(pci_free_consistent);

#include <linux/in6.h>
#include <asm/checksum.h>
EXPORT_SYMBOL(csum_partial_copy_nocheck);

#include <asm/irq.h>
EXPORT_SYMBOL(enable_irq);
EXPORT_SYMBOL(disable_irq);

#include <asm/processor.h>
EXPORT_SYMBOL(cpu_data);
EXPORT_SYMBOL(kernel_thread);

#ifdef CONFIG_SMP
#include <asm/hardirq.h>
EXPORT_SYMBOL(synchronize_irq);

#include <asm/smplock.h>
EXPORT_SYMBOL(kernel_flag);

#include <asm/system.h>
EXPORT_SYMBOL(__global_sti);
EXPORT_SYMBOL(__global_cli);
EXPORT_SYMBOL(__global_save_flags);
EXPORT_SYMBOL(__global_restore_flags);

#endif

#include <asm/uaccess.h>
EXPORT_SYMBOL(__copy_user);

#include <asm/unistd.h>
EXPORT_SYMBOL(__ia64_syscall);

/* from arch/ia64/lib */
extern void __divdi3(void);
extern void __udivdi3(void);
extern void __moddi3(void);
extern void __umoddi3(void);

EXPORT_SYMBOL_NOVERS(__divdi3);
EXPORT_SYMBOL_NOVERS(__udivdi3);
EXPORT_SYMBOL_NOVERS(__moddi3);
EXPORT_SYMBOL_NOVERS(__umoddi3);
