/*
 *  linux/kernel/traps.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'asm.s'. Currently mostly a debugging-aid, will be extended
 * to mainly kill the offending process (probably by giving it a signal,
 * but possibly by killing it outright if necessary).
 */
#include <linux/head.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/segment.h>
#include <linux/ptrace.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/io.h>

#define get_seg_byte(seg,addr) ({ \
register char __res; \
__asm__("push %%fs;mov %%ax,%%fs;movb %%fs:%2,%%al;pop %%fs" \
	:"=a" (__res):"0" (seg),"m" (*(addr))); \
__res;})

#define get_seg_long(seg,addr) ({ \
register unsigned long __res; \
__asm__("push %%fs;mov %%ax,%%fs;movl %%fs:%2,%%eax;pop %%fs" \
	:"=a" (__res):"0" (seg),"m" (*(addr))); \
__res;})

#define _fs() ({ \
register unsigned short __res; \
__asm__("mov %%fs,%%ax":"=a" (__res):); \
__res;})

void page_exception(void);

void divide_error(void);
void debug(void);
void nmi(void);
void int3(void);
void overflow(void);
void bounds(void);
void invalid_op(void);
void device_not_available(void);
void double_fault(void);
void coprocessor_segment_overrun(void);
void invalid_TSS(void);
void segment_not_present(void);
void stack_segment(void);
void general_protection(void);
void page_fault(void);
void coprocessor_error(void);
void reserved(void);
void alignment_check(void);

/*static*/ void die_if_kernel(char * str, struct pt_regs * regs, long err)
{
	int i;

	if ((regs->eflags & VM_MASK) || ((0xffff & regs->cs) == USER_CS))
		return;
	printk("%s: %04x\n", str, err & 0xffff);
	printk("EIP:    %04x:%p\nEFLAGS: %p\n", 0xffff & regs->cs,regs->eip,regs->eflags);
	printk("eax: %08x   ebx: %08x   ecx: %08x   edx: %08x\n",
		regs->eax, regs->ebx, regs->ecx, regs->edx);
	printk("esi: %08x   edi: %08x   ebp: %08x\n",
		regs->esi, regs->edi, regs->ebp);
	printk("ds: %04x   es: %04x   fs: %04x   gs: %04x\n",
		regs->ds, regs->es, regs->fs, regs->gs);
	store_TR(i);
	printk("Pid: %d, process nr: %d\n", current->pid, 0xffff & i);
	for(i=0;i<10;i++)
		printk("%02x ",0xff & get_seg_byte(regs->cs,(i+(char *)regs->eip)));
	printk("\n");
	do_exit(SIGSEGV);
}

void do_double_fault(struct pt_regs * regs, long error_code)
{
	send_sig(SIGSEGV, current, 1);
	die_if_kernel("double fault",regs,error_code);
}

void do_general_protection(struct pt_regs * regs, long error_code)
{
	send_sig(SIGSEGV, current, 1);
	die_if_kernel("general protection",regs,error_code);
}

void do_alignment_check(struct pt_regs * regs, long error_code)
{
	send_sig(SIGSEGV, current, 1);
	die_if_kernel("alignment check",regs,error_code);
}

void do_divide_error(struct pt_regs * regs, long error_code)
{
	send_sig(SIGFPE, current, 1);
	die_if_kernel("divide error",regs,error_code);
}

void do_int3(struct pt_regs * regs, long error_code)
{
	if (current->flags & PF_PTRACED)
		current->blocked &= ~(1 << (SIGTRAP-1));
	send_sig(SIGTRAP, current, 1);
	die_if_kernel("int3",regs,error_code);
}

void do_nmi(struct pt_regs * regs, long error_code)
{
	printk("Uhhuh. NMI received. Dazed and confused, but trying to continue\n");
}

void do_debug(struct pt_regs * regs, long error_code)
{
	if (current->flags & PF_PTRACED)
		current->blocked &= ~(1 << (SIGTRAP-1));
	send_sig(SIGTRAP, current, 1);
	die_if_kernel("debug",regs,error_code);
}

void do_overflow(struct pt_regs * regs, long error_code)
{
	send_sig(SIGSEGV, current, 1);
	die_if_kernel("overflow",regs,error_code);
}

void do_bounds(struct pt_regs * regs, long error_code)
{
	send_sig(SIGSEGV, current, 1);
	die_if_kernel("bounds",regs,error_code);
}

void do_invalid_op(struct pt_regs * regs, long error_code)
{
	send_sig(SIGILL, current, 1);
	die_if_kernel("invalid operand",regs,error_code);
}

void do_device_not_available(struct pt_regs * regs, long error_code)
{
	send_sig(SIGSEGV, current, 1);
	die_if_kernel("device not available",regs,error_code);
}

void do_coprocessor_segment_overrun(struct pt_regs * regs, long error_code)
{
	send_sig(SIGFPE, last_task_used_math, 1);
	die_if_kernel("coprocessor segment overrun",regs,error_code);
}

void do_invalid_TSS(struct pt_regs * regs,long error_code)
{
	send_sig(SIGSEGV, current, 1);
	die_if_kernel("invalid TSS",regs,error_code);
}

void do_segment_not_present(struct pt_regs * regs,long error_code)
{
	send_sig(SIGSEGV, current, 1);
	die_if_kernel("segment not present",regs,error_code);
}

void do_stack_segment(struct pt_regs * regs,long error_code)
{
	send_sig(SIGSEGV, current, 1);
	die_if_kernel("stack segment",regs,error_code);
}

void do_coprocessor_error(struct pt_regs * regs, long error_code)
{
  /*
    Allow the process which triggered the interrupt to recover the error
    condition.
    The status word is saved in the cs selector.
    The tag word is saved in the operand selector.
    The status word is then cleared and the tags all set to Empty.
    This will give sufficient information for complete recovery provided that
    the affected process knows or can deduce the code and data segments
    which were in force when the exception condition arose.
    */
	#define FPU_ENV (*(struct i387_hard_struct *)env)
	char env[28];

	ignore_irq13 = 1;
	send_sig(SIGFPE, last_task_used_math, 1);

	__asm__ __volatile__("fnstenv %0; fnclex": "=m" (FPU_ENV));
	FPU_ENV.fcs = (FPU_ENV.swd & 0x0000ffff) | (FPU_ENV.fcs & 0xffff0000);
	FPU_ENV.fos = FPU_ENV.twd;
	FPU_ENV.swd &= 0xffff0000;
	FPU_ENV.twd = 0xffffffff;
	__asm__ __volatile__("fldenv %0"::"m" (FPU_ENV));
}

void do_reserved(struct pt_regs * regs, long error_code)
{
	send_sig(SIGSEGV, current, 1);
	die_if_kernel("reserved (15,17-47) error",regs,error_code);
}

void trap_init(void)
{
	int i;

	set_trap_gate(0,&divide_error);
	set_trap_gate(1,&debug);
	set_trap_gate(2,&nmi);
	set_system_gate(3,&int3);	/* int3-5 can be called from all */
	set_system_gate(4,&overflow);
	set_system_gate(5,&bounds);
	set_trap_gate(6,&invalid_op);
	set_trap_gate(7,&device_not_available);
	set_trap_gate(8,&double_fault);
	set_trap_gate(9,&coprocessor_segment_overrun);
	set_trap_gate(10,&invalid_TSS);
	set_trap_gate(11,&segment_not_present);
	set_trap_gate(12,&stack_segment);
	set_trap_gate(13,&general_protection);
	set_trap_gate(14,&page_fault);
	set_trap_gate(15,&reserved);
	set_trap_gate(16,&coprocessor_error);
	set_trap_gate(17,&alignment_check);
	for (i=18;i<48;i++)
		set_trap_gate(i,&reserved);
}
