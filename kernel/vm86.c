/*
 *  linux/kernel/vm86.c
 *
 *  Copyright (C) 1994  Linus Torvalds
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/string.h>
#include <linux/ptrace.h>

#include <asm/segment.h>
#include <asm/io.h>

asmlinkage struct pt_regs * save_v86_state(struct vm86_regs * regs)
{
	unsigned long stack;

	if (!current->vm86_info) {
		printk("no vm86_info: BAD\n");
		do_exit(SIGSEGV);
	}
	memcpy_tofs(&(current->vm86_info->regs),regs,sizeof(*regs));
	put_fs_long(current->screen_bitmap,&(current->vm86_info->screen_bitmap));
	stack = current->tss.esp0;
	current->tss.esp0 = current->saved_kernel_stack;
	current->saved_kernel_stack = 0;
	return (struct pt_regs *) stack;
}

static void mark_screen_rdonly(struct task_struct * tsk)
{
	unsigned long tmp;
	unsigned long *pg_table;

	if ((tmp = tsk->tss.cr3) != 0) {
		tmp = *(unsigned long *) tmp;
		if (tmp & PAGE_PRESENT) {
			tmp &= PAGE_MASK;
			pg_table = (0xA0000 >> PAGE_SHIFT) + (unsigned long *) tmp;
			tmp = 32;
			while (tmp--) {
				if (PAGE_PRESENT & *pg_table)
					*pg_table &= ~PAGE_RW;
				pg_table++;
			}
		}
	}
}

asmlinkage int sys_vm86(struct vm86_struct * v86)
{
	struct vm86_struct info;
	struct pt_regs * pt_regs = (struct pt_regs *) &v86;
	int error;

	if (current->saved_kernel_stack)
		return -EPERM;
	/* v86 must be readable (now) and writable (for save_v86_state) */
	error = verify_area(VERIFY_WRITE,v86,sizeof(*v86));
	if (error)
		return error;
	memcpy_fromfs(&info,v86,sizeof(info));
/*
 * make sure the vm86() system call doesn't try to do anything silly
 */
	info.regs.__null_ds = 0;
	info.regs.__null_es = 0;
	info.regs.__null_fs = 0;
	info.regs.__null_gs = 0;
/*
 * The eflags register is also special: we cannot trust that the user
 * has set it up safely, so this makes sure interrupt etc flags are
 * inherited from protected mode.
 */
	info.regs.eflags &= 0x00000dd5;
	info.regs.eflags |= ~0x00000dd5 & pt_regs->eflags;
	info.regs.eflags |= VM_MASK;
	current->saved_kernel_stack = current->tss.esp0;
	current->tss.esp0 = (unsigned long) pt_regs;
	current->vm86_info = v86;
	current->screen_bitmap = info.screen_bitmap;
	if (info.flags & VM86_SCREEN_BITMAP)
		mark_screen_rdonly(current);
	__asm__ __volatile__("movl %0,%%esp\n\t"
		"pushl $ret_from_sys_call\n\t"
		"ret"
		: /* no outputs */
		:"g" ((long) &(info.regs)),"a" (info.regs.eax));
	return 0;
}

static inline void return_to_32bit(struct vm86_regs * regs16, int retval)
{
	struct pt_regs * regs32;

	regs32 = save_v86_state(regs16);
	regs32->eax = retval;
	__asm__("movl %0,%%esp\n\t"
		"jmp ret_from_sys_call"
		: : "r" (regs32));
}

void handle_vm86_fault(struct vm86_regs * regs, long error_code)
{
	unsigned char *csp;
	unsigned short *ssp;
	unsigned short flags;
	unsigned char i;

	csp = (unsigned char *) ((regs->cs << 4) + (regs->eip & 0xffff));
	ssp = (unsigned short *) ((regs->ss << 4) + (regs->esp & 0xffff));

	switch (get_fs_byte(csp)) {

	/* operand size override */
	case 0x66:
		switch (get_fs_byte(++csp)) {

		/* pushfd */
		case 0x9c:
			regs->esp -= 4;
			regs->eip += 2;
			if (get_fs_long(&(current->vm86_info->cpu_type)) == CPU_386)
			  put_fs_long(((regs->eflags) & ~(AC_MASK|NT_MASK|IOPL_MASK|IF_MASK)) |
				(get_fs_long(&(current->vm86_info->v_eflags)) & (NT_MASK|IOPL_MASK|IF_MASK)), ssp-2);
			else
			  put_fs_long(((regs->eflags) & ~(AC_MASK|NT_MASK|IOPL_MASK|IF_MASK)) |
				(get_fs_long(&(current->vm86_info->v_eflags)) & (AC_MASK|NT_MASK|IOPL_MASK|IF_MASK)), ssp-2);
			return;

		/* popfd */
		case 0x9d:
			regs->esp += 4;
			regs->eip += 2;
			flags = get_fs_word(ssp+1);
			put_fs_word(flags, (unsigned short *) &(current->vm86_info->v_eflags) +1);
			goto return_from_popf;
		}

	/* pushf */
	case 0x9c:
		regs->esp -= 2;
		regs->eip++;
		if (get_fs_long(&(current->vm86_info->cpu_type)) == CPU_286)
		  put_fs_word(((regs->eflags) & 0x0dd5) |
			(get_fs_word(&(current->vm86_info->v_eflags)) & ~0xfdd5), --ssp);
		else
		  put_fs_word(((regs->eflags) & ~(NT_MASK|IOPL_MASK|IF_MASK)) |
			(get_fs_word(&(current->vm86_info->v_eflags)) & (NT_MASK|IOPL_MASK|IF_MASK)), --ssp);
		return;

	/* popf */
	case 0x9d:
		regs->esp += 2;
		regs->eip++;
	return_from_popf:
		flags = get_fs_word(ssp);
		regs->eflags &= ~0x00000dd5;
		regs->eflags |= flags & 0x00000dd5;
		put_fs_word(flags, &(current->vm86_info->v_eflags));
		goto do_dosemu_timer;

	/* int 3 */
	case 0xcc:
		if (get_fs_word((void *)14) == BIOSSEG || regs->cs == BIOSSEG
			|| get_fs_byte(&(current->vm86_info->int_revectored[3])))
			return_to_32bit(regs, SIGSEGV);
		i = 3;
		regs->eip++;
		goto return_from_int_xx;

	/* int xx */
	case 0xcd:
		i = get_fs_byte(++csp);
		if (get_fs_word((void *)((i<<2)+2)) == BIOSSEG
			|| regs->cs == BIOSSEG
			|| get_fs_byte(&(current->vm86_info->int_revectored[i])))
			return_to_32bit(regs, SIGSEGV);
		if ((i==0x21) && get_fs_byte(&(current->vm86_info->int21_revectored[((regs->eax >> 4) & 0xff)])))
			return_to_32bit(regs, SIGSEGV);
		regs->eip+=2;
		return_from_int_xx:
		regs->esp -= 6;
		if (get_fs_long(&(current->vm86_info->cpu_type)) == CPU_286)
		  put_fs_word(((regs->eflags) & 0x0dd5) |
			(get_fs_word(&(current->vm86_info->v_eflags)) & ~0xfdd5), --ssp);
		else
		  put_fs_word(((regs->eflags) & ~IF_MASK) |
			(get_fs_word(&(current->vm86_info->v_eflags)) & IF_MASK), --ssp);
		put_fs_word(regs->cs, --ssp);
		put_fs_word((unsigned short)(regs->eip), --ssp);
		regs->cs = get_fs_word((void *)((i<<2)+2));
		regs->eip = (unsigned long) get_fs_word((void *)(i<<2));
		regs->eflags &= ~TF_MASK;
		and_fs_long(~IF_MASK, &(current->vm86_info->v_eflags));
		return;

	/* iret */
	case 0xcf:
		regs->esp += 6;
		regs->eip = get_fs_word(ssp++);
		regs->cs = get_fs_word(ssp++);
		goto return_from_popf;

	/* cli */
	case 0xfa:
		regs->eip++;
		and_fs_long(~IF_MASK, &(current->vm86_info->v_eflags));
		return;

	/* sti */
	case 0xfb:
		regs->eip++;
		or_fs_long(IF_MASK, &(current->vm86_info->v_eflags));
	do_dosemu_timer:
		if ((get_fs_long(&(current->vm86_info->v_eflags)) & IF_MASK) &&
		    get_fs_long(&(current->vm86_info->return_if_iflag)))
			break;
		return;

	default:
		return_to_32bit(regs, SIGSEGV);
	}
	return_to_32bit(regs, SIGALRM);
}
