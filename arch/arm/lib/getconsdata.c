/*
 * linux/arch/arm/lib/getconsdata.c
 *
 * Copyright (C) 1995-1998 Russell King
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/unistd.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>

#undef PAGE_READONLY

#define OFF_TSK(n) (unsigned long)&(((struct task_struct *)0)->n)
#define OFF_MM(n) (unsigned long)&(((struct mm_struct *)0)->n)

#ifdef KERNEL_DOMAIN
unsigned long DOM_KERNELDOMAIN = KERNEL_DOMAIN;
#endif
#ifdef USER_DOMAIN
unsigned long DOM_USERDOMAIN = USER_DOMAIN;
#endif

unsigned long TSK_STATE = OFF_TSK(state);
unsigned long TSK_FLAGS = OFF_TSK(flags);
unsigned long TSK_NEED_RESCHED = OFF_TSK(need_resched);
unsigned long TSK_SIGPENDING = OFF_TSK(sigpending);
unsigned long TSK_ADDR_LIMIT = OFF_TSK(addr_limit);
unsigned long TSK_USED_MATH = OFF_TSK(used_math);

unsigned long MM = OFF_TSK(mm);
unsigned long PGD = OFF_MM(pgd);

unsigned long TSS_MEMMAP = OFF_TSK(tss.memmap);
unsigned long TSS_SAVE = OFF_TSK(tss.save);
unsigned long TSS_FPESAVE = OFF_TSK(tss.fpstate.soft.save);
#ifdef CONFIG_CPU_26
unsigned long TSS_MEMCMAP = OFF_TSK(tss.memcmap);
#endif

#ifdef _PAGE_PRESENT
unsigned long PAGE_PRESENT = _PAGE_PRESENT;
#endif
#ifdef _PAGE_RW
unsigned long PAGE_RW = _PAGE_RW;
#endif
#ifdef _PAGE_USER
unsigned long PAGE_USER = _PAGE_USER;
#endif
#ifdef _PAGE_ACCESSED
unsigned long PAGE_ACCESSED = _PAGE_ACCESSED;
#endif
#ifdef _PAGE_DIRTY
unsigned long PAGE_DIRTY = _PAGE_DIRTY;
#endif
#ifdef _PAGE_READONLY
unsigned long PAGE_READONLY = _PAGE_READONLY;
#endif
#ifdef _PAGE_NOT_USER
unsigned long PAGE_NOT_USER = _PAGE_NOT_USER;
#endif
#ifdef _PAGE_OLD
unsigned long PAGE_OLD = _PAGE_OLD;
#endif
#ifdef _PAGE_CLEAN
unsigned long PAGE_CLEAN = _PAGE_CLEAN;
#endif

unsigned long KSWI_BASE = 0x900000;
unsigned long KSWI_SYS_BASE = 0x9f0000;
unsigned long SYS_ERROR0 = 0x9f0000;
