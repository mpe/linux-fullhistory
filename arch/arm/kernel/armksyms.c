#include <linux/config.h>
#include <linux/module.h>
#include <linux/user.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/pci.h>

#include <asm/ecard.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/dma.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>

extern void dump_thread(struct pt_regs *, struct user *);
extern int dump_fpu(struct pt_regs *, struct user_fp_struct *);

/*
 * libgcc functions - functions that are used internally by the
 * compiler...  (prototypes are not correct though, but that
 * doesn't really matter since they're not versioned).
 */
extern void __gcc_bcmp(void);
extern void __ashldi3(void);
extern void __ashrdi3(void);
extern void __cmpdi2(void);
extern void __divdi3(void);
extern void __divsi3(void);
extern void __lshrdi3(void);
extern void __moddi3(void);
extern void __modsi3(void);
extern void __muldi3(void);
extern void __negdi2(void);
extern void __ucmpdi2(void);
extern void __udivdi3(void);
extern void __udivmoddi4(void);
extern void __udivsi3(void);
extern void __umoddi3(void);
extern void __umodsi3(void);

extern void inswb(unsigned int port, void *to, int len);
extern void outswb(unsigned int port, const void *to, int len);

/*
 * floating point math emulator support.
 * These will not change.  If they do, then a new version
 * of the emulator will have to be compiled...
 * fp_current is never actually dereferenced - it is just
 * used as a pointer to pass back for send_sig().
 */
extern void (*fp_save)(unsigned char *);
extern void (*fp_restore)(unsigned char *);
extern void fp_setup(void);
extern void fpreturn(void);
extern void fpundefinstr(void);
extern void fp_enter(void);
extern void fp_printk(void);
extern struct task_struct *fp_current;
extern void fp_send_sig(int);

/* platform dependent support */
EXPORT_SYMBOL(dump_thread);
EXPORT_SYMBOL(dump_fpu);
EXPORT_SYMBOL(udelay);
EXPORT_SYMBOL(xchg_str);

/* expansion card support */
#ifdef CONFIG_ARCH_ACORN
EXPORT_SYMBOL(ecard_startfind);
EXPORT_SYMBOL(ecard_find);
EXPORT_SYMBOL(ecard_readchunk);
EXPORT_SYMBOL(ecard_address);
#endif

/* processor dependencies */
EXPORT_SYMBOL(processor);

/* io */
EXPORT_SYMBOL(outswb);
EXPORT_SYMBOL(outsw);
EXPORT_SYMBOL(inswb);
EXPORT_SYMBOL(insw);

/* address translation */
#ifndef __virt_to_phys__is_a_macro
EXPORT_SYMBOL(__virt_to_phys);
#endif
#ifndef __phys_to_virt__is_a_macro
EXPORT_SYMBOL(__phys_to_virt);
#endif
#ifndef __virt_to_bus__is_a_macro
EXPORT_SYMBOL(__virt_to_bus);
#endif
#ifndef __bus_to_virt__is_a_macro
EXPORT_SYMBOL(__bus_to_virt);
#endif

EXPORT_SYMBOL(quicklists);
EXPORT_SYMBOL(__bad_pmd);
EXPORT_SYMBOL(__bad_pmd_kernel);

/*
 * floating point math emulator support.
 * These symbols will never change their calling convention...
 */
EXPORT_SYMBOL_NOVERS(fpreturn);
EXPORT_SYMBOL_NOVERS(fpundefinstr);
EXPORT_SYMBOL_NOVERS(fp_enter);
EXPORT_SYMBOL_NOVERS(fp_save);
EXPORT_SYMBOL_NOVERS(fp_restore);
EXPORT_SYMBOL_NOVERS(fp_setup);

const char __kstrtab_fp_printk[] __attribute__((section(".kstrtab"))) = __MODULE_STRING(fp_printk);
const struct module_symbol __ksymtab_fp_printk __attribute__((section("__ksymtab"))) =
{ (unsigned long)&printk, __kstrtab_fp_printk };

const char __kstrtab_fp_send_sig[] __attribute__((section(".kstrtab"))) = __MODULE_STRING(fp_send_sig);
const struct module_symbol __ksymtab_fp_send_sig __attribute__((section("__ksymtab"))) =
{ (unsigned long)&send_sig, __kstrtab_fp_send_sig };

//EXPORT_SYMBOL_NOVERS(fp_current);

	/*
	 * string / mem functions
	 */
EXPORT_SYMBOL_NOVERS(strcpy);
EXPORT_SYMBOL_NOVERS(strncpy);
EXPORT_SYMBOL_NOVERS(strcat);
EXPORT_SYMBOL_NOVERS(strncat);
EXPORT_SYMBOL_NOVERS(strcmp);
EXPORT_SYMBOL_NOVERS(strncmp);
EXPORT_SYMBOL_NOVERS(strchr);
EXPORT_SYMBOL_NOVERS(strlen);
EXPORT_SYMBOL_NOVERS(strnlen);
EXPORT_SYMBOL_NOVERS(strspn);
EXPORT_SYMBOL_NOVERS(strpbrk);
EXPORT_SYMBOL_NOVERS(strtok);
EXPORT_SYMBOL_NOVERS(strrchr);
EXPORT_SYMBOL_NOVERS(memset);
EXPORT_SYMBOL_NOVERS(memcpy);
EXPORT_SYMBOL_NOVERS(memmove);
EXPORT_SYMBOL_NOVERS(memcmp);
EXPORT_SYMBOL_NOVERS(memscan);
EXPORT_SYMBOL_NOVERS(memzero);

	/* user mem (segment) */
#if defined(CONFIG_CPU_32)
EXPORT_SYMBOL(__arch_copy_from_user);
EXPORT_SYMBOL(__arch_copy_to_user);
EXPORT_SYMBOL(__arch_clear_user);
EXPORT_SYMBOL(__arch_strlen_user);
#elif defined(CONFIG_CPU_26)
EXPORT_SYMBOL(uaccess_kernel);
EXPORT_SYMBOL(uaccess_user);
#endif

	/* gcc lib functions */
EXPORT_SYMBOL_NOVERS(__gcc_bcmp);
EXPORT_SYMBOL_NOVERS(__ashldi3);
EXPORT_SYMBOL_NOVERS(__ashrdi3);
EXPORT_SYMBOL_NOVERS(__cmpdi2);
EXPORT_SYMBOL_NOVERS(__divdi3);
EXPORT_SYMBOL_NOVERS(__divsi3);
EXPORT_SYMBOL_NOVERS(__lshrdi3);
EXPORT_SYMBOL_NOVERS(__moddi3);
EXPORT_SYMBOL_NOVERS(__modsi3);
EXPORT_SYMBOL_NOVERS(__muldi3);
EXPORT_SYMBOL_NOVERS(__negdi2);
EXPORT_SYMBOL_NOVERS(__ucmpdi2);
EXPORT_SYMBOL_NOVERS(__udivdi3);
EXPORT_SYMBOL_NOVERS(__udivmoddi4);
EXPORT_SYMBOL_NOVERS(__udivsi3);
EXPORT_SYMBOL_NOVERS(__umoddi3);
EXPORT_SYMBOL_NOVERS(__umodsi3);

	/* bitops */
EXPORT_SYMBOL(set_bit);
EXPORT_SYMBOL(test_and_set_bit);
EXPORT_SYMBOL(clear_bit);
EXPORT_SYMBOL(test_and_clear_bit);
EXPORT_SYMBOL(change_bit);
EXPORT_SYMBOL(test_and_change_bit);
EXPORT_SYMBOL(find_first_zero_bit);
EXPORT_SYMBOL(find_next_zero_bit);
