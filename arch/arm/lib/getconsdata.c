/*
 * linux/arch/arm/lib/getconsdata.c
 *
 * Copyright (C) 1995, 1996 Russell King
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/unistd.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>

#define OFF_TSK(n) (unsigned long)&(((struct task_struct *)0)->n)
#define OFF_MM(n) (unsigned long)&(((struct mm_struct *)0)->n)

#ifdef KERNEL_DOMAIN
unsigned long kernel_domain = KERNEL_DOMAIN;
#endif
#ifdef USER_DOMAIN
unsigned long user_domain = USER_DOMAIN;
#endif
unsigned long addr_limit = OFF_TSK(addr_limit);
unsigned long tss_memmap = OFF_TSK(tss.memmap);
unsigned long mm = OFF_TSK(mm);
unsigned long pgd = OFF_MM(pgd);
unsigned long tss_save = OFF_TSK(tss.save);
unsigned long tss_fpesave = OFF_TSK(tss.fpstate.soft.save);
#if defined(CONFIG_CPU_ARM2) || defined(CONFIG_CPU_ARM3)
unsigned long tss_memcmap = OFF_TSK(tss.memcmap);
#endif
