/*
 * linux/arch/alpha/kernel/ksyms.c
 *
 * Export the alpha-specific functions that are needed for loadable
 * modules.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/user.h>
#include <linux/elfcore.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/in6.h>

#include <asm/io.h>
#include <asm/hwrpb.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include <asm/checksum.h>
#include <linux/interrupt.h>
#include <asm/softirq.h>

#define __KERNEL_SYSCALLS__
#include <asm/unistd.h>

extern void bcopy (const char *src, char *dst, int len);
extern struct hwrpb_struct *hwrpb;
extern void dump_thread(struct pt_regs *, struct user *);
extern int dump_fpu(struct pt_regs *, elf_fpregset_t *);

/* these are C runtime functions with special calling conventions: */
extern void __divl (void);
extern void __reml (void);
extern void __divq (void);
extern void __remq (void);
extern void __divlu (void);
extern void __remlu (void);
extern void __divqu (void);
extern void __remqu (void);

EXPORT_SYMBOL(local_bh_count);
EXPORT_SYMBOL(local_irq_count);

/* platform dependent support */
EXPORT_SYMBOL(_inb);
EXPORT_SYMBOL(_inw);
EXPORT_SYMBOL(_inl);
EXPORT_SYMBOL(_outb);
EXPORT_SYMBOL(_outw);
EXPORT_SYMBOL(_outl);
EXPORT_SYMBOL(_readb);
EXPORT_SYMBOL(_readw);
EXPORT_SYMBOL(_readl);
EXPORT_SYMBOL(_writeb);
EXPORT_SYMBOL(_writew);
EXPORT_SYMBOL(_writel);
EXPORT_SYMBOL(insb);
EXPORT_SYMBOL(insw);
EXPORT_SYMBOL(insl);
EXPORT_SYMBOL(outsb);
EXPORT_SYMBOL(outsw);
EXPORT_SYMBOL(outsl);
EXPORT_SYMBOL(strcat);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(strcpy);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strncmp);
EXPORT_SYMBOL(strncpy);
EXPORT_SYMBOL(strnlen);
EXPORT_SYMBOL(strncat);
EXPORT_SYMBOL(strstr);
EXPORT_SYMBOL(strtok);
EXPORT_SYMBOL(strchr);
EXPORT_SYMBOL(strrchr);
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(__memcpy);
EXPORT_SYMBOL(__memset);
EXPORT_SYMBOL(__constant_c_memset);

EXPORT_SYMBOL(dump_thread);
EXPORT_SYMBOL(dump_fpu);
EXPORT_SYMBOL(hwrpb);
EXPORT_SYMBOL(wrusp);
EXPORT_SYMBOL(start_thread);

/* In-kernel system calls.  */
EXPORT_SYMBOL(__kernel_thread);
EXPORT_SYMBOL(sys_open);
EXPORT_SYMBOL(sys_dup);
EXPORT_SYMBOL(sys_exit);
EXPORT_SYMBOL(sys_write);
EXPORT_SYMBOL(sys_read);
EXPORT_SYMBOL(sys_lseek);
EXPORT_SYMBOL(__kernel_execve);
EXPORT_SYMBOL(sys_setsid);
EXPORT_SYMBOL(sys_sync);
EXPORT_SYMBOL(sys_wait4);

/* Networking helper routines. */
EXPORT_SYMBOL(csum_tcpudp_magic);
EXPORT_SYMBOL(ip_fast_csum);
EXPORT_SYMBOL(ip_compute_csum);
EXPORT_SYMBOL(csum_partial_copy);

/*
 * The following are specially called from the uaccess assembly stubs.
 */
EXPORT_SYMBOL_NOVERS(__copy_user);
EXPORT_SYMBOL_NOVERS(__do_clear_user);
EXPORT_SYMBOL(__strncpy_from_user);
EXPORT_SYMBOL(__strlen_user);

/*
 * The following are special because they're not called
 * explicitly (the C compiler or assembler generates them in
 * response to division operations).  Fortunately, their
 * interface isn't gonna change any time soon now, so it's OK
 * to leave it out of version control.
 */
# undef bcopy
# undef memcpy
# undef memset
EXPORT_SYMBOL_NOVERS(__divl);
EXPORT_SYMBOL_NOVERS(__divlu);
EXPORT_SYMBOL_NOVERS(__divq);
EXPORT_SYMBOL_NOVERS(__divqu);
EXPORT_SYMBOL_NOVERS(__reml);
EXPORT_SYMBOL_NOVERS(__remlu);
EXPORT_SYMBOL_NOVERS(__remq);
EXPORT_SYMBOL_NOVERS(__remqu);
EXPORT_SYMBOL_NOVERS(memcpy);
EXPORT_SYMBOL_NOVERS(memset);
