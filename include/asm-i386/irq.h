#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

/*
 *	linux/include/asm/irq.h
 *
 *	(C) 1992, 1993 Linus Torvalds
 *
 *	IRQ/IPI changes taken from work by Thomas Radke <tomsoft@informatik.tu-chemnitz.de>
 */

#include <linux/linkage.h>
#include <asm/segment.h>

#define NR_IRQS 16

#define TIMER_IRQ 0

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

#define __STR(x) #x
#define STR(x) __STR(x)


#define SAVE_ALL \
	"cld\n\t" \
	"push %es\n\t" \
	"push %ds\n\t" \
	"pushl %eax\n\t" \
	"pushl %ebp\n\t" \
	"pushl %edi\n\t" \
	"pushl %esi\n\t" \
	"pushl %edx\n\t" \
	"pushl %ecx\n\t" \
	"pushl %ebx\n\t" \
	"movl $" STR(KERNEL_DS) ",%edx\n\t" \
	"mov %dx,%ds\n\t" \
	"mov %dx,%es\n\t"

/*
 * SAVE_MOST/RESTORE_MOST is used for the faster version of IRQ handlers,
 * installed by using the SA_INTERRUPT flag. These kinds of IRQ's don't
 * call the routines that do signal handling etc on return, and can have
 * more relaxed register-saving etc. They are also atomic, and are thus
 * suited for small, fast interrupts like the serial lines or the harddisk
 * drivers, which don't actually need signal handling etc.
 *
 * Also note that we actually save only those registers that are used in
 * C subroutines (%eax, %edx and %ecx), so if you do something weird,
 * you're on your own. The only segments that are saved (not counting the
 * automatic stack and code segment handling) are %ds and %es, and they
 * point to kernel space. No messing around with %fs here.
 */
#define SAVE_MOST \
	"cld\n\t" \
	"push %es\n\t" \
	"push %ds\n\t" \
	"pushl %eax\n\t" \
	"pushl %edx\n\t" \
	"pushl %ecx\n\t" \
	"movl $" STR(KERNEL_DS) ",%edx\n\t" \
	"mov %dx,%ds\n\t" \
	"mov %dx,%es\n\t"

#define RESTORE_MOST \
	"popl %ecx\n\t" \
	"popl %edx\n\t" \
	"popl %eax\n\t" \
	"pop %ds\n\t" \
	"pop %es\n\t" \
	"iret"

/*
 * The "inb" instructions are not needed, but seem to change the timings
 * a bit - without them it seems that the harddisk driver won't work on
 * all hardware. Arghh.
 */
#define ACK_FIRST(mask,nr) \
	"inb $0x21,%al\n\t" \
	"jmp 1f\n" \
	"1:\tjmp 1f\n" \
	"1:\torb $" #mask ","SYMBOL_NAME_STR(cache_21)"\n\t" \
	"movb "SYMBOL_NAME_STR(cache_21)",%al\n\t" \
	"outb %al,$0x21\n\t" \
	"jmp 1f\n" \
	"1:\tjmp 1f\n" \
	"1:\tmovb $0x20,%al\n\t" \
	"outb %al,$0x20\n\t"

#define ACK_SECOND(mask,nr) \
	"inb $0xA1,%al\n\t" \
	"jmp 1f\n" \
	"1:\tjmp 1f\n" \
	"1:\torb $" #mask ","SYMBOL_NAME_STR(cache_A1)"\n\t" \
	"movb "SYMBOL_NAME_STR(cache_A1)",%al\n\t" \
	"outb %al,$0xA1\n\t" \
	"jmp 1f\n" \
	"1:\tjmp 1f\n" \
	"1:\tmovb $0x20,%al\n\t" \
	"outb %al,$0xA0\n\t" \
	"jmp 1f\n" \
	"1:\tjmp 1f\n" \
	"1:\toutb %al,$0x20\n\t"

#define UNBLK_FIRST(mask) \
	"inb $0x21,%al\n\t" \
	"jmp 1f\n" \
	"1:\tjmp 1f\n" \
	"1:\tandb $~(" #mask "),"SYMBOL_NAME_STR(cache_21)"\n\t" \
	"movb "SYMBOL_NAME_STR(cache_21)",%al\n\t" \
	"outb %al,$0x21\n\t"

#define UNBLK_SECOND(mask) \
	"inb $0xA1,%al\n\t" \
	"jmp 1f\n" \
	"1:\tjmp 1f\n" \
	"1:\tandb $~(" #mask "),"SYMBOL_NAME_STR(cache_A1)"\n\t" \
	"movb "SYMBOL_NAME_STR(cache_A1)",%al\n\t" \
	"outb %al,$0xA1\n\t"

#define IRQ_NAME2(nr) nr##_interrupt(void)
#define IRQ_NAME(nr) IRQ_NAME2(IRQ##nr)
#define FAST_IRQ_NAME(nr) IRQ_NAME2(fast_IRQ##nr)
#define BAD_IRQ_NAME(nr) IRQ_NAME2(bad_IRQ##nr)

#ifdef	__SMP__

#define GET_CURRENT \
	"movl "SYMBOL_NAME_STR(apic_reg)", %ebx\n\t" \
	"movl 32(%ebx), %ebx\n\t" \
	"shrl $22,%ebx\n\t" \
	"andl $0x3C,%ebx\n\t" \
	"movl " SYMBOL_NAME_STR(current_set) "(,%ebx),%ebx\n\t"
	
#else

#define GET_CURRENT \
	"movl " SYMBOL_NAME_STR(current_set) ",%ebx\n\t"
	
#endif

#ifdef __SMP__

/*
 *	Message pass must be a fast IRQ..
 */

#define BUILD_MSGIRQ(chip,nr,mask) \
asmlinkage void IRQ_NAME(nr); \
asmlinkage void FAST_IRQ_NAME(nr); \
asmlinkage void BAD_IRQ_NAME(nr); \
__asm__( \
"\n"__ALIGN_STR"\n" \
SYMBOL_NAME_STR(IRQ) #nr "_interrupt:\n\t" \
	"pushl $-"#nr"-2\n\t" \
	SAVE_ALL \
	ACK_##chip(mask,(nr&7)) \
	"sti\n\t" \
	"movl %esp,%eax\n\t" \
	"pushl %eax\n\t" \
	"pushl $" #nr "\n\t" \
	"call "SYMBOL_NAME_STR(do_IRQ)"\n\t" \
	"addl $8,%esp\n\t" \
	"cli\n\t" \
	UNBLK_##chip(mask) \
	"jmp ret_from_intr\n" \
"\n"__ALIGN_STR"\n" \
SYMBOL_NAME_STR(fast_IRQ) #nr "_interrupt:\n\t" \
	SAVE_MOST \
	ACK_##chip(mask,(nr&7)) \
	"pushl $" #nr "\n\t" \
	"call "SYMBOL_NAME_STR(do_fast_IRQ)"\n\t" \
	"addl $4,%esp\n\t" \
	"cli\n\t" \
	UNBLK_##chip(mask) \
	RESTORE_MOST \
"\n"__ALIGN_STR"\n" \
SYMBOL_NAME_STR(bad_IRQ) #nr "_interrupt:\n\t" \
	SAVE_MOST \
	ACK_##chip(mask,(nr&7)) \
	RESTORE_MOST);

#define BUILD_RESCHEDIRQ(nr) \
asmlinkage void IRQ_NAME(nr); \
__asm__( \
"\n"__ALIGN_STR"\n" \
SYMBOL_NAME_STR(IRQ) #nr "_interrupt:\n\t" \
	"pushl $-"#nr"-2\n\t" \
	SAVE_ALL \
	"sti\n\t" \
	"movl %esp,%eax\n\t" \
	"pushl %eax\n\t" \
	"pushl $" #nr "\n\t" \
	"call "SYMBOL_NAME_STR(smp_reschedule_irq)"\n\t" \
	"addl $8,%esp\n\t" \
	"cli\n\t" \
	"jmp ret_from_intr\n");

#endif /* __SMP__ */

#define BUILD_IRQ(chip,nr,mask) \
asmlinkage void IRQ_NAME(nr); \
asmlinkage void FAST_IRQ_NAME(nr); \
asmlinkage void BAD_IRQ_NAME(nr); \
__asm__( \
"\n"__ALIGN_STR"\n" \
SYMBOL_NAME_STR(IRQ) #nr "_interrupt:\n\t" \
	"pushl $-"#nr"-2\n\t" \
	SAVE_ALL \
	ACK_##chip(mask,(nr&7)) \
	"sti\n\t" \
	"movl %esp,%eax\n\t" \
	"pushl %eax\n\t" \
	"pushl $" #nr "\n\t" \
	"call "SYMBOL_NAME_STR(do_IRQ)"\n\t" \
	"addl $8,%esp\n\t" \
	"cli\n\t" \
	UNBLK_##chip(mask) \
	"jmp ret_from_intr\n" \
"\n"__ALIGN_STR"\n" \
SYMBOL_NAME_STR(fast_IRQ) #nr "_interrupt:\n\t" \
	SAVE_MOST \
	ACK_##chip(mask,(nr&7)) \
	"pushl $" #nr "\n\t" \
	"call "SYMBOL_NAME_STR(do_fast_IRQ)"\n\t" \
	"addl $4,%esp\n\t" \
	"cli\n\t" \
	UNBLK_##chip(mask) \
	RESTORE_MOST \
"\n"__ALIGN_STR"\n" \
SYMBOL_NAME_STR(bad_IRQ) #nr "_interrupt:\n\t" \
	SAVE_MOST \
	ACK_##chip(mask,(nr&7)) \
	RESTORE_MOST);
	
#define BUILD_TIMER_IRQ(chip,nr,mask) \
asmlinkage void IRQ_NAME(nr); \
asmlinkage void FAST_IRQ_NAME(nr); \
asmlinkage void BAD_IRQ_NAME(nr); \
__asm__( \
"\n"__ALIGN_STR"\n" \
SYMBOL_NAME_STR(fast_IRQ) #nr "_interrupt:\n\t" \
SYMBOL_NAME_STR(bad_IRQ) #nr "_interrupt:\n\t" \
SYMBOL_NAME_STR(IRQ) #nr "_interrupt:\n\t" \
	"pushl $-"#nr"-2\n\t" \
	SAVE_ALL \
	ACK_##chip(mask,(nr&7)) \
	"movl %esp,%eax\n\t" \
	"pushl %eax\n\t" \
	"pushl $" #nr "\n\t" \
	"call "SYMBOL_NAME_STR(do_IRQ)"\n\t" \
	"addl $8,%esp\n\t" \
	"cli\n\t" \
	UNBLK_##chip(mask) \
	"jmp ret_from_intr\n");

#endif /* _ASM_IRQ_H */
