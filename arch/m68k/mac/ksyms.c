#include <linux/module.h>
#include <linux/pci.h>
#include <asm/ptrace.h>
#include <asm/traps.h>
/* Hook for mouse driver */
extern void (*mac_mouse_interrupt_hook) (char *);

EXPORT_SYMBOL(mac_mouse_interrupt_hook);

#if CONFIG_PCI
EXPORT_SYMBOL(pci_devices);
#endif
