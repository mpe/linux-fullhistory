#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

/*
 *	linux/include/asm/irq.h
 *
 *	(C) 1992 Linus Torvalds
 */
 
#define SAVE_ALL \
	"cld\n\t" \
	"push %gs\n\t" \
	"push %fs\n\t" \
	"push %es\n\t" \
	"push %ds\n\t" \
	"pushl %eax\n\t" \
	"pushl %ebp\n\t" \
	"pushl %edi\n\t" \
	"pushl %esi\n\t" \
	"pushl %edx\n\t" \
	"pushl %ecx\n\t" \
	"pushl %ebx\n\t" \
	"movl $0x10,%edx\n\t" \
	"mov %dx,%ds\n\t" \
	"mov %dx,%es\n\t" \
	"movl $0x17,%edx\n\t" \
	"mov %dx,%fs\n\t"

#define ACK_FIRST(mask) \
	"inb $0x21,%al\n\t" \
	"jmp 1f\n" \
	"1:\tjmp 1f\n" \
	"1:\torb $" #mask ",%al\n\t" \
	"outb %al,$0x21\n\t" \
	"jmp 1f\n" \
	"1:\tjmp 1f\n" \
	"1:\tmovb $0x20,%al\n\t" \
	"outb %al,$0x20\n\t"

#define ACK_SECOND(mask) \
	"inb $0xA1,%al\n\t" \
	"jmp 1f\n" \
	"1:\tjmp 1f\n" \
	"1:\torb $" #mask ",%al\n\t" \
	"outb %al,$0xA1\n\t" \
	"jmp 1f\n" \
	"1:\tjmp 1f\n" \
	"1:\tmovb $0x20,%al\n\t" \
	"outb %al,$0xA0" \
	"jmp 1f\n" \
	"1:\tjmp 1f\n" \
	"1:\toutb %al,$0x20\n\t"

#define IRQ_NAME2(nr) nr##_interrupt()
#define IRQ_NAME(nr) IRQ_NAME2(IRQ##nr)
	
#define BUILD_IRQ(chip,nr,mask) \
extern void IRQ_NAME(nr); \
__asm__( \
"\n.align 2\n" \
".globl _IRQ" #nr "_interrupt\n" \
"_IRQ" #nr "_interrupt:\n\t" \
	"pushl $-1\n\t" \
	SAVE_ALL \
	"cli\n\t" \
	ACK_##chip(mask) \
	"sti\n\t" \
	"movl %esp,%ebx\n\t" \
	"pushl %ebx\n\t" \
	"pushl $" #nr "\n\t" \
	"call _do_IRQ\n\t" \
	"addl $8,%esp\n\t" \
	"jmp ret_from_sys_call");

#endif
