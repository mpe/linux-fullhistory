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
#include <linux/mm.h>

#include <asm/segment.h>
#include <asm/pgtable.h>
#include <asm/io.h>

/*
 * Known problems:
 *
 * Interrupt handling is not guaranteed:
 * - a real x86 will disable all interrupts for one instruction
 *   after a "mov ss,xx" to make stack handling atomic even without
 *   the 'lss' instruction. We can't guarantee this in v86 mode,
 *   as the next instruction might result in a page fault or similar.
 * - a real x86 will have interrupts disabled for one instruction
 *   past the 'sti' that enables them. We don't bother with all the
 *   details yet..
 *
 * Hopefully these problems do not actually matter for anything.
 */

/*
 * 8- and 16-bit register defines..
 */
#define AL(regs)	(((unsigned char *)&((regs)->eax))[0])
#define AH(regs)	(((unsigned char *)&((regs)->eax))[1])
#define IP(regs)	(*(unsigned short *)&((regs)->eip))
#define SP(regs)	(*(unsigned short *)&((regs)->esp))

/*
 * virtual flags (16 and 32-bit versions)
 */
#define VFLAGS	(*(unsigned short *)&(current->tss.v86flags))
#define VEFLAGS	(current->tss.v86flags)

#define set_flags(X,new,mask) \
((X) = ((X) & ~(mask)) | ((new) & (mask)))

#define SAFE_MASK	(0xDD5)
#define RETURN_MASK	(0xDFF)

asmlinkage struct pt_regs * save_v86_state(struct vm86_regs * regs)
{
	unsigned long tmp;

	if (!current->tss.vm86_info) {
		printk("no vm86_info: BAD\n");
		do_exit(SIGSEGV);
	}
	set_flags(regs->eflags, VEFLAGS, VIF_MASK | current->tss.v86mask);
	memcpy_tofs(&current->tss.vm86_info->regs,regs,sizeof(*regs));
	put_fs_long(current->tss.screen_bitmap,&current->tss.vm86_info->screen_bitmap);
	tmp = current->tss.esp0;
	current->tss.esp0 = current->saved_kernel_stack;
	current->saved_kernel_stack = 0;
	return (struct pt_regs *) tmp;
}

static void mark_screen_rdonly(struct task_struct * tsk)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	int i;

	pgd = pgd_offset(tsk, 0xA0000);
	if (pgd_none(*pgd))
		return;
	if (pgd_bad(*pgd)) {
		printk("vm86: bad pgd entry [%p]:%08lx\n", pgd, pgd_val(*pgd));
		pgd_clear(pgd);
		return;
	}
	pmd = pmd_offset(pgd, 0xA0000);
	if (pmd_none(*pmd))
		return;
	if (pmd_bad(*pmd)) {
		printk("vm86: bad pmd entry [%p]:%08lx\n", pmd, pmd_val(*pmd));
		pmd_clear(pmd);
		return;
	}
	pte = pte_offset(pmd, 0xA0000);
	for (i = 0; i < 32; i++) {
		if (pte_present(*pte))
			*pte = pte_wrprotect(*pte);
		pte++;
	}
	invalidate();
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
 	VEFLAGS = info.regs.eflags;
	info.regs.eflags &= SAFE_MASK;
	info.regs.eflags |= pt_regs->eflags & ~SAFE_MASK;
	info.regs.eflags |= VM_MASK;

	switch (info.cpu_type) {
		case CPU_286:
			current->tss.v86mask = 0;
			break;
		case CPU_386:
			current->tss.v86mask = NT_MASK | IOPL_MASK;
			break;
		case CPU_486:
			current->tss.v86mask = AC_MASK | NT_MASK | IOPL_MASK;
			break;
		default:
			current->tss.v86mask = ID_MASK | AC_MASK | NT_MASK | IOPL_MASK;
			break;
	}

/*
 * Save old state, set default return value (%eax) to 0
 */
	pt_regs->eax = 0;
	current->saved_kernel_stack = current->tss.esp0;
	current->tss.esp0 = (unsigned long) pt_regs;
	current->tss.vm86_info = v86;

	current->tss.screen_bitmap = info.screen_bitmap;
	if (info.flags & VM86_SCREEN_BITMAP)
		mark_screen_rdonly(current);
	__asm__ __volatile__("movl %0,%%esp\n\t"
		"jmp ret_from_sys_call"
		: /* no outputs */
		:"r" (&info.regs));
	return 0;
}

static inline void return_to_32bit(struct vm86_regs * regs16, int retval)
{
	struct pt_regs * regs32;

	regs32 = save_v86_state(regs16);
	regs32->eax = retval;
	__asm__ __volatile__("movl %0,%%esp\n\t"
		"jmp ret_from_sys_call"
		: : "r" (regs32));
}

static inline void set_IF(struct vm86_regs * regs)
{
	VEFLAGS |= VIF_MASK;
	if (VEFLAGS & VIP_MASK)
		return_to_32bit(regs, VM86_STI);
}

static inline void clear_IF(struct vm86_regs * regs)
{
	VEFLAGS &= ~VIF_MASK;
}

static inline void clear_TF(struct vm86_regs * regs)
{
	regs->eflags &= ~TF_MASK;
}

static inline void set_vflags_long(unsigned long eflags, struct vm86_regs * regs)
{
	set_flags(VEFLAGS, eflags, current->tss.v86mask);
	set_flags(regs->eflags, eflags, SAFE_MASK);
	if (eflags & IF_MASK)
		set_IF(regs);
}

static inline void set_vflags_short(unsigned short flags, struct vm86_regs * regs)
{
	set_flags(VFLAGS, flags, current->tss.v86mask);
	set_flags(regs->eflags, flags, SAFE_MASK);
	if (flags & IF_MASK)
		set_IF(regs);
}

static inline unsigned long get_vflags(struct vm86_regs * regs)
{
	unsigned long flags = regs->eflags & RETURN_MASK;

	if (VEFLAGS & VIF_MASK)
		flags |= IF_MASK;
	return flags | (VEFLAGS & current->tss.v86mask);
}

static inline int is_revectored(int nr, struct revectored_struct * bitmap)
{
	if (verify_area(VERIFY_READ, bitmap, 256/8) < 0)
		return 1;
	__asm__ __volatile__("btl %2,%%fs:%1\n\tsbbl %0,%0"
		:"=r" (nr)
		:"m" (*bitmap),"r" (nr));
	return nr;
}

/*
 * Boy are these ugly, but we need to do the correct 16-bit arithmetic.
 * Gcc makes a mess of it, so we do it inline and use non-obvious calling
 * conventions..
 */
#define pushb(base, ptr, val) \
__asm__ __volatile__( \
	"decw %w0\n\t" \
	"movb %2,%%fs:0(%1,%0)" \
	: "=r" (ptr) \
	: "r" (base), "q" (val), "0" (ptr))

#define pushw(base, ptr, val) \
__asm__ __volatile__( \
	"decw %w0\n\t" \
	"movb %h2,%%fs:0(%1,%0)\n\t" \
	"decw %w0\n\t" \
	"movb %b2,%%fs:0(%1,%0)" \
	: "=r" (ptr) \
	: "r" (base), "q" (val), "0" (ptr))

#define pushl(base, ptr, val) \
__asm__ __volatile__( \
	"decw %w0\n\t" \
	"rorl $16,%2\n\t" \
	"movb %h2,%%fs:0(%1,%0)\n\t" \
	"decw %w0\n\t" \
	"movb %b2,%%fs:0(%1,%0)\n\t" \
	"decw %w0\n\t" \
	"rorl $16,%2\n\t" \
	"movb %h2,%%fs:0(%1,%0)\n\t" \
	"decw %w0\n\t" \
	"movb %b2,%%fs:0(%1,%0)" \
	: "=r" (ptr) \
	: "r" (base), "q" (val), "0" (ptr))

#define popb(base, ptr) \
({ unsigned long __res; \
__asm__ __volatile__( \
	"movb %%fs:0(%1,%0),%b2\n\t" \
	"incw %w0" \
	: "=r" (ptr), "=r" (base), "=q" (__res) \
	: "0" (ptr), "1" (base), "2" (0)); \
__res; })

#define popw(base, ptr) \
({ unsigned long __res; \
__asm__ __volatile__( \
	"movb %%fs:0(%1,%0),%b2\n\t" \
	"incw %w0\n\t" \
	"movb %%fs:0(%1,%0),%h2\n\t" \
	"incw %w0" \
	: "=r" (ptr), "=r" (base), "=q" (__res) \
	: "0" (ptr), "1" (base), "2" (0)); \
__res; })

#define popl(base, ptr) \
({ unsigned long __res; \
__asm__ __volatile__( \
	"movb %%fs:0(%1,%0),%b2\n\t" \
	"incw %w0\n\t" \
	"movb %%fs:0(%1,%0),%h2\n\t" \
	"incw %w0\n\t" \
	"rorl $16,%2\n\t" \
	"movb %%fs:0(%1,%0),%b2\n\t" \
	"incw %w0\n\t" \
	"movb %%fs:0(%1,%0),%h2\n\t" \
	"incw %w0\n\t" \
	"rorl $16,%2" \
	: "=r" (ptr), "=r" (base), "=q" (__res) \
	: "0" (ptr), "1" (base)); \
__res; })

static void do_int(struct vm86_regs *regs, int i, unsigned char * ssp, unsigned long sp)
{
	unsigned short *intr_ptr, seg;

	if (regs->cs == BIOSSEG)
		goto cannot_handle;
	if (is_revectored(i, &current->tss.vm86_info->int_revectored))
		goto cannot_handle;
	if (i==0x21 && is_revectored(AH(regs),&current->tss.vm86_info->int21_revectored))
		goto cannot_handle;
	intr_ptr = (unsigned short *) (i << 2);
	if (verify_area(VERIFY_READ, intr_ptr, 4) < 0)
		goto cannot_handle;
	seg = get_fs_word(intr_ptr+1);
	if (seg == BIOSSEG)
		goto cannot_handle;
	pushw(ssp, sp, get_vflags(regs));
	pushw(ssp, sp, regs->cs);
	pushw(ssp, sp, IP(regs));
	regs->cs = seg;
	SP(regs) -= 6;
	IP(regs) = get_fs_word(intr_ptr+0);
	clear_TF(regs);
	clear_IF(regs);
	return;

cannot_handle:
	return_to_32bit(regs, VM86_INTx + (i << 8));
}

void handle_vm86_debug(struct vm86_regs * regs, long error_code)
{
#if 0
	do_int(regs, 1, (unsigned char *) (regs->ss << 4), SP(regs));
#else
	if (current->flags & PF_PTRACED)
		current->blocked &= ~(1 << (SIGTRAP-1));
	send_sig(SIGTRAP, current, 1);
	current->tss.trap_no = 1;
	current->tss.error_code = error_code;
#endif
}

void handle_vm86_fault(struct vm86_regs * regs, long error_code)
{
	unsigned char *csp, *ssp;
	unsigned long ip, sp;

	csp = (unsigned char *) (regs->cs << 4);
	ssp = (unsigned char *) (regs->ss << 4);
	sp = SP(regs);
	ip = IP(regs);

	switch (popb(csp, ip)) {

	/* operand size override */
	case 0x66:
		switch (popb(csp, ip)) {

		/* pushfd */
		case 0x9c:
			SP(regs) -= 4;
			IP(regs) += 2;
			pushl(ssp, sp, get_vflags(regs));
			return;

		/* popfd */
		case 0x9d:
			SP(regs) += 4;
			IP(regs) += 2;
			set_vflags_long(popl(ssp, sp), regs);
			return;

		/* iretd */
		case 0xcf:
			SP(regs) += 12;
			IP(regs) = (unsigned short)popl(ssp, sp);
			regs->cs = (unsigned short)popl(ssp, sp);
			set_vflags_long(popl(ssp, sp), regs);
			return;
		}

	/* pushf */
	case 0x9c:
		SP(regs) -= 2;
		IP(regs)++;
		pushw(ssp, sp, get_vflags(regs));
		return;

	/* popf */
	case 0x9d:
		SP(regs) += 2;
		IP(regs)++;
		set_vflags_short(popw(ssp, sp), regs);
		return;

	/* int xx */
	case 0xcd:
		IP(regs) += 2;
		do_int(regs, popb(csp, ip), ssp, sp);
		return;

	/* iret */
	case 0xcf:
		SP(regs) += 6;
		IP(regs) = popw(ssp, sp);
		regs->cs = popw(ssp, sp);
		set_vflags_short(popw(ssp, sp), regs);
		return;

	/* cli */
	case 0xfa:
		IP(regs)++;
		clear_IF(regs);
		return;

	/* sti */
	/*
	 * Damn. This is incorrect: the 'sti' instruction should actually
	 * enable interrupts after the /next/ instruction. Not good.
	 *
	 * Probably needs some horsing around with the TF flag. Aiee..
	 */
	case 0xfb:
		IP(regs)++;
		set_IF(regs);
		return;

	default:
		return_to_32bit(regs, VM86_UNKNOWN);
	}
}
