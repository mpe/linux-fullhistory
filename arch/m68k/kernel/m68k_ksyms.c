#include <linux/module.h>
#include <linux/linkage.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/user.h>
#include <linux/elfcore.h>

#include <asm/setup.h>
#include <asm/machdep.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/semaphore.h>

asmlinkage long long __ashrdi3 (long long, int);
extern char m68k_debug_device[];

extern void dump_thread(struct pt_regs *, struct user *);
extern int dump_fpu(elf_fpregset_t *);

/* platform dependent support */

EXPORT_SYMBOLS(memcmp);
EXPORT_SYMBOLS(m68k_machtype);
EXPORT_SYMBOLS(m68k_cputype);
EXPORT_SYMBOLS(m68k_is040or060);
EXPORT_SYMBOLS(cache_push);
EXPORT_SYMBOLS(cache_push_v);
EXPORT_SYMBOLS(cache_clear);
EXPORT_SYMBOLS(mm_vtop);
EXPORT_SYMBOLS(mm_ptov);
EXPORT_SYMBOLS(mm_end_of_chunk);
EXPORT_SYMBOLS(m68k_debug_device);
EXPORT_SYMBOLS(request_irq);
EXPORT_SYMBOLS(free_irq);
EXPORT_SYMBOLS(dump_fpu);
EXPORT_SYMBOLS(dump_thread);
EXPORT_SYMBOLS(strnlen);
EXPORT_SYMBOLS(strrchr);
EXPORT_SYMBOLS(strstr);

/* The following are special because they're not called
   explicitly (the C compiler generates them).  Fortunately,
   their interface isn't gonna change any time soon now, so
   it's OK to leave it out of version control.  */
EXPORT_SYMBOLS_NOVERS(__ashrdi3);
EXPORT_SYMBOLS_NOVERS(memcpy);
EXPORT_SYMBOLS_NOVERS(memset);

EXPORT_SYMBOLS_NOVERS(__down_failed);
EXPORT_SYMBOLS_NOVERS(__up_wakeup);
