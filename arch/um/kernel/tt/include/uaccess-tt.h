/*
 * Copyright (C) 2000 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#ifndef __TT_UACCESS_H
#define __TT_UACCESS_H

#include "linux/string.h"
#include "linux/sched.h"
#include "asm/processor.h"
#include "asm/errno.h"
#include "asm/current.h"
#include "asm/a.out.h"
#include "uml_uaccess.h"

#define ABOVE_KMEM (16 * 1024 * 1024)

extern unsigned long end_vm;
extern unsigned long uml_physmem;

#define under_task_size(addr, size) \
	(((unsigned long) (addr) < TASK_SIZE) && \
         (((unsigned long) (addr) + (size)) < TASK_SIZE))

#define is_stack(addr, size) \
	(((unsigned long) (addr) < STACK_TOP) && \
	 ((unsigned long) (addr) >= STACK_TOP - ABOVE_KMEM) && \
	 (((unsigned long) (addr) + (size)) <= STACK_TOP))

#define access_ok_tt(type, addr, size) \
	((type == VERIFY_READ) || (segment_eq(get_fs(), KERNEL_DS)) || \
         (((unsigned long) (addr) <= ((unsigned long) (addr) + (size))) && \
          (under_task_size(addr, size) || is_stack(addr, size))))

static inline int verify_area_tt(int type, const void * addr,
				 unsigned long size)
{
	return(access_ok_tt(type, addr, size) ? 0 : -EFAULT);
}

extern unsigned long get_fault_addr(void);

extern int __do_copy_from_user(void *to, const void *from, int n,
			       void **fault_addr, void **fault_catcher);
extern int __do_strncpy_from_user(char *dst, const char *src, size_t n,
				  void **fault_addr, void **fault_catcher);
extern int __do_clear_user(void *mem, size_t len, void **fault_addr,
			   void **fault_catcher);
extern int __do_strnlen_user(const char *str, unsigned long n,
			     void **fault_addr, void **fault_catcher);

extern int copy_from_user_tt(void *to, const void *from, int n);
extern int copy_to_user_tt(void *to, const void *from, int n);
extern int strncpy_from_user_tt(char *dst, const char *src, int count);
extern int __clear_user_tt(void *mem, int len);
extern int clear_user_tt(void *mem, int len);
extern int strnlen_user_tt(const void *str, int len);

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
