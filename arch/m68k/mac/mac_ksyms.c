#include <linux/module.h>
#include <asm/ptrace.h>
#include <asm/traps.h>
/* Hook for mouse driver */
extern void (*adb_mouse_interrupt_hook) (char *);

EXPORT_SYMBOL(adb_mouse_interrupt_hook);
