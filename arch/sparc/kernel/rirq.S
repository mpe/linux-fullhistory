/* rirq.S: Needed to return from an interrupt on SMP with no
 *         locks held or released.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/cprefix.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/psr.h>
#include <asm/asi.h>
#include <asm/smp.h>
#include <asm/contregs.h>
#include <asm/winmacro.h>
#include <asm/asmmacro.h>

#define t_psr     l0
#define t_pc      l1
#define t_npc     l2
#define t_wim     l3
#define twin_tmp1 l4
#define twin_tmp2 l5
#define twin_tmp3 l6
#define curptr    g6

	/* 7 WINDOW SPARC PATCH INSTRUCTIONS */
	.globl	rirq_7win_patch1, rirq_7win_patch2, rirq_7win_patch3
	.globl	rirq_7win_patch4, rirq_7win_patch5
rirq_7win_patch1:	srl	%t_wim, 0x6, %twin_tmp2
rirq_7win_patch2:	and	%twin_tmp2, 0x7f, %twin_tmp2
rirq_7win_patch3:	srl	%g1, 7, %g2
rirq_7win_patch4:	srl	%g2, 6, %g2
rirq_7win_patch5:	and	%g1, 0x7f, %g1
	/* END OF PATCH INSTRUCTIONS */

	.globl	ret_irq_entry, rirq_patch1, rirq_patch2
	.globl	rirq_patch3, rirq_patch4, rirq_patch5
ret_irq_entry:
	ld	[%sp + REGWIN_SZ + PT_PSR], %t_psr
	andcc	%t_psr, PSR_PS, %g0
	bne	ret_irq_kernel
	 nop

ret_irq_user:
	wr	%t_psr, 0x0, %psr
	WRITE_PAUSE

	ld	[%curptr + THREAD_W_SAVED], %twin_tmp1
	orcc	%g0, %twin_tmp1, %g0
	be	ret_irq_nobufwins
	 nop

	/* User has toasty windows, must grab klock. */
	ENTER_SYSCALL

	wr	%t_psr, PSR_ET, %psr
	WRITE_PAUSE

	mov	1, %o1
	call	C_LABEL(try_to_clear_window_buffer)
	 add	%sp, REGWIN_SZ, %o0

	/* We have klock, so we must return just like a normal trap. */
	b	ret_trap_entry
	 clr	%l5

ret_irq_nobufwins:
	/* Load up the user's out registers so we can pull
	 * a window from the stack, if necessary.
	 */
	LOAD_PT_INS(sp)

	/* If there are already live user windows in the
	 * set we can return from trap safely.
	 */
	ld	[%curptr + THREAD_UMASK], %twin_tmp1
	orcc	%g0, %twin_tmp1, %g0
	bne	ret_irq_userwins_ok
	 nop
	
		/* Calculate new %wim, we have to pull a register
		 * window from the users stack.
		 */
ret_irq_pull_one_window:
		rd	%wim, %t_wim
		sll	%t_wim, 0x1, %twin_tmp1
rirq_patch1:	srl	%t_wim, 0x7, %twin_tmp2
		or	%twin_tmp2, %twin_tmp1, %twin_tmp2
rirq_patch2:	and	%twin_tmp2, 0xff, %twin_tmp2

		wr	%twin_tmp2, 0x0, %wim
		WRITE_PAUSE

				/* Here comes the architecture specific 
				 * branch to the user stack checking routine
				 * for return from traps.
				 */
				.globl	C_LABEL(rirq_mmu_patchme)
C_LABEL(rirq_mmu_patchme):	b	C_LABEL(sun4c_reti_stackchk)
				 andcc	%fp, 0x7, %g0	

ret_irq_userwins_ok:
	LOAD_PT_PRIV(sp, t_psr, t_pc, t_npc)
	or	%t_pc, %t_npc, %g2
	andcc	%g2, 0x3, %g0
	bne	ret_irq_unaligned_pc
	 nop

	LOAD_PT_YREG(sp, g1)
	LOAD_PT_GLOBALS(sp)

	wr	%t_psr, 0x0, %psr
	WRITE_PAUSE

	jmp	%t_pc
	rett	%t_npc
	
ret_irq_unaligned_pc:
	add	%sp, REGWIN_SZ, %o0
	ld	[%sp + REGWIN_SZ + PT_PC], %o1
	ld	[%sp + REGWIN_SZ + PT_NPC], %o2
	ld	[%sp + REGWIN_SZ + PT_PSR], %o3

	wr	%t_wim, 0x0, %wim		! or else...
	WRITE_PAUSE

	/* User has unaligned crap, must grab klock. */
	ENTER_SYSCALL

	wr	%t_psr, PSR_ET, %psr
	WRITE_PAUSE

	call	C_LABEL(do_memaccess_unaligned)
	 nop

	/* We have klock, so we must return just like a normal trap. */
	b	ret_trap_entry
	 clr	%l5

ret_irq_kernel:
	wr	%t_psr, 0x0, %psr
	WRITE_PAUSE

		/* Will the rett land us in the invalid window? */
		mov	2, %g1
		sll	%g1, %t_psr, %g1
rirq_patch3:	srl	%g1, 8, %g2
		or	%g1, %g2, %g1
		rd	%wim, %g2
		andcc	%g2, %g1, %g0
		be	1f		! Nope, just return from the trap
		 nop

		/* We have to grab a window before returning. */
		sll	%g2, 0x1, %g1
rirq_patch4:	srl	%g2, 7,  %g2
		or	%g1, %g2, %g1
rirq_patch5:	and	%g1, 0xff, %g1

	wr	%g1, 0x0, %wim
	WRITE_PAUSE

	restore	%g0, %g0, %g0
	LOAD_WINDOW(sp)
	save	%g0, %g0, %g0

	/* Reload the entire frame in case this is from a
	 * kernel system call or whatever...
	 */
1:
	LOAD_PT_ALL(sp, t_psr, t_pc, t_npc, g1)

	wr	%t_psr, 0x0, %psr
	WRITE_PAUSE

	jmp	%t_pc
	rett	%t_npc

ret_irq_user_stack_is_bolixed:
	wr	%t_wim, 0x0, %wim
	WRITE_PAUSE

	/* User has a toasty window, must grab klock. */
	ENTER_SYSCALL

	wr	%t_psr, PSR_ET, %psr
	WRITE_PAUSE

	call	C_LABEL(window_ret_fault)
	 add	%sp, REGWIN_SZ, %o0

	/* We have klock, so we must return just like a normal trap. */
	b	ret_trap_entry
	 clr	%l5

	.globl	C_LABEL(sun4c_reti_stackchk)
C_LABEL(sun4c_reti_stackchk):
	be	1f
	 and	%fp, 0xfff, %g1		! delay slot

	b,a	ret_irq_user_stack_is_bolixed

	/* See if we have to check the sanity of one page or two */
1:
	add	%g1, 0x38, %g1
	sra	%fp, 29, %g2
	add	%g2, 0x1, %g2
	andncc	%g2, 0x1, %g0
	be	1f
	 andncc	%g1, 0xff8, %g0

	/* %sp is in vma hole, yuck */
	b,a	ret_irq_user_stack_is_bolixed

1:
	be	sun4c_reti_onepage	/* Only one page to check */
	 lda	[%fp] ASI_PTE, %g2

sun4c_reti_twopages:
	add	%fp, 0x38, %g1
	sra	%g1, 29, %g2
	add	%g2, 0x1, %g2
	andncc	%g2, 0x1, %g0
	be	1f
	 lda	[%g1] ASI_PTE, %g2

	/* Second page is in vma hole */
	b,a	ret_irq_user_stack_is_bolixed

1:
	srl	%g2, 29, %g2
	andcc	%g2, 0x4, %g0
	bne	sun4c_reti_onepage
	 lda	[%fp] ASI_PTE, %g2

	/* Second page has bad perms */
	b,a	ret_irq_user_stack_is_bolixed

sun4c_reti_onepage:
	srl	%g2, 29, %g2
	andcc	%g2, 0x4, %g0
	bne	1f
	 nop

	/* A page had bad page permissions, losing... */
	b,a	ret_irq_user_stack_is_bolixed

	/* Whee, things are ok, load the window and continue. */
1:
	restore	%g0, %g0, %g0

	LOAD_WINDOW(sp)

	save	%g0, %g0, %g0
	b,a	ret_irq_userwins_ok

	.globl	C_LABEL(srmmu_reti_stackchk)
C_LABEL(srmmu_reti_stackchk):
	sethi	%hi(C_LABEL(page_offset)), %g1
	bne	ret_irq_user_stack_is_bolixed
	 ld	[%g1 + %lo(C_LABEL(page_offset))], %g1
	cmp	%g1, %fp
	bleu	ret_irq_user_stack_is_bolixed
	 mov	AC_M_SFSR, %g1
	lda	[%g1] ASI_M_MMUREGS, %g0

	lda	[%g0] ASI_M_MMUREGS, %g1
	or	%g1, 0x2, %g1
	sta	%g1, [%g0] ASI_M_MMUREGS

	restore	%g0, %g0, %g0

	LOAD_WINDOW(sp)

	save	%g0, %g0, %g0

	andn	%g1, 0x2, %g1
	sta	%g1, [%g0] ASI_M_MMUREGS

	mov	AC_M_SFAR, %g2
	lda	[%g2] ASI_M_MMUREGS, %g2

	mov	AC_M_SFSR, %g1
	lda	[%g1] ASI_M_MMUREGS, %g1
	andcc	%g1, 0x2, %g0
	bne	ret_irq_user_stack_is_bolixed
	 nop

	b,a	ret_irq_userwins_ok
