/* $Id: head.h,v 1.27 1997/07/13 17:30:43 davem Exp $ */
#ifndef _SPARC64_HEAD_H
#define _SPARC64_HEAD_H

#include <asm/pstate.h>

#define KERNBASE    0x400000
#define BOOT_KERNEL b sparc64_boot; nop; nop; nop; nop; nop; nop; nop;

/* We need a "cleaned" instruction... */
#define CLEAN_WINDOW							\
	rdpr	%cleanwin, %l0;		add	%l0, 1, %l0;		\
	wrpr	%l0, 0x0, %cleanwin;					\
	clr	%o0;	clr	%o1;	clr	%o2;	clr	%o3;	\
	clr	%o4;	clr	%o5;	clr	%o6;	clr	%o7;	\
	clr	%l0;	clr	%l1;	clr	%l2;	clr	%l3;	\
	clr	%l4;	clr	%l5;	clr	%l6;	clr	%l7;	\
	retry;								\
	nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;

#define TRAP(routine)					\
	ba,pt	%xcc, etrap;				\
	 rd	%pc, %g7;				\
	call	routine;				\
	 add	%sp, STACK_BIAS + REGWIN_SZ, %o0;	\
	ba,pt	%xcc, rtrap;				\
	 clr	%l6;					\
	nop;						\
	nop;

#define TRAP_NOSAVE(routine)				\
	ba,pt	%xcc, routine;				\
	 nop;						\
	nop; nop; nop; nop; nop; nop;

#define TRAPTL1(routine)				\
	ba,pt	%xcc, etraptl1;				\
	 rd	%pc, %g7;				\
	call	routine;				\
	 add	%sp, STACK_BIAS + REGWIN_SZ, %o0;	\
	ba,pt	%xcc, rtrap;				\
	 clr	%l6;					\
	nop;						\
	nop;
	
#define TRAP_ARG(routine, arg)				\
	ba,pt	%xcc, etrap;				\
	 rd	%pc, %g7;				\
	add	%sp, STACK_BIAS + REGWIN_SZ, %o0;	\
	call	routine;				\
	 mov	arg, %o1;				\
	ba,pt	%xcc, rtrap;				\
	 clr	%l6;					\
	nop;
	
#define TRAPTL1_ARG(routine, arg)			\
	ba,pt	%xcc, etraptl1;				\
	 rd	%pc, %g7;				\
	add	%sp, STACK_BIAS + REGWIN_SZ, %o0;	\
	call	routine;				\
	 mov	arg, %o1;				\
	ba,pt	%xcc, rtrap;				\
	 clr	%l6;					\
	nop;
	
#define SYSCALL_TRAP(routine, systbl)			\
	ba,pt	%xcc, etrap;				\
	 rd	%pc, %g7;				\
	sethi	%hi(systbl), %l7;			\
	call	routine;				\
	 or	%l7, %lo(systbl), %l7;			\
	nop; nop; nop;
	
#define ACCESS_EXCEPTION_TRAP(routine)			\
	rdpr	%pstate, %g1;				\
	wrpr	%g1, PSTATE_MG|PSTATE_AG, %pstate;	\
	ba,pt	%xcc, etrap;				\
	 rd	%pc, %g7;				\
	call	routine;				\
	 add	%sp, STACK_BIAS + REGWIN_SZ, %o0;	\
	ba,pt	%xcc, rtrap;				\
	 clr	%l6;

#define ACCESS_EXCEPTION_TRAPTL1(routine)		\
	rdpr	%pstate, %g1;				\
	wrpr	%g1, PSTATE_MG|PSTATE_AG, %pstate;	\
	ba,pt	%xcc, etraptl1;				\
	 rd	%pc, %g7;				\
	call	routine;				\
	 add	%sp, STACK_BIAS + REGWIN_SZ, %o0;	\
	ba,pt	%xcc, rtrap;				\
	 clr	%l6;

#define SUNOS_SYSCALL_TRAP SYSCALL_TRAP(linux_sparc_syscall, sunos_sys_table)
#define	LINUX_32BIT_SYSCALL_TRAP SYSCALL_TRAP(linux_sparc_syscall, sys_call_table32)
#define LINUX_64BIT_SYSCALL_TRAP SYSCALL_TRAP(linux_sparc_syscall, sys_call_table64)
#define GETCC_TRAP TRAP(getcc)
#define SETCC_TRAP TRAP(setcc)
/* FIXME: Write these actually */	
#define NETBSD_SYSCALL_TRAP TRAP(netbsd_syscall)
#define SOLARIS_SYSCALL_TRAP TRAP(solaris_syscall)
#define BREAKPOINT_TRAP TRAP(breakpoint_trap)
#define INDIRECT_SOLARIS_SYSCALL(tlvl) TRAP_ARG(indirect_syscall, tlvl)

#define TRAP_IRQ(routine, level)			\
	rdpr	%pil, %g2;				\
	wrpr	%g0, 15, %pil;				\
	ba,pt	%xcc, etrap_irq;			\
	 rd	%pc, %g7;				\
	mov	level, %o0;				\
	call	routine;				\
	 add	%sp, STACK_BIAS + REGWIN_SZ, %o1;	\
	ba,a,pt	%xcc, rtrap_clr_l6;

/* On UP this is ok, and worth the effort, for SMP we need
 * a different mechanism and thus cannot do it all in trap table. -DaveM
 */
#ifndef __SMP__
#define TRAP_IVEC				\
	ldxa	[%g2] ASI_UDB_INTR_R, %g3;	\
	and	%g3, 0x7ff, %g3;		\
	sllx	%g3, 3, %g3;			\
	ldx	[%g1 + %g3], %g5;		\
	wr	%g5, 0x0, %set_softint;		\
	stxa	%g0, [%g0] ASI_INTR_RECEIVE;	\
	membar	#Sync;				\
	retry;
#else
#define TRAP_IVEC TRAP_NOSAVE(do_ivec)
#endif

#define BTRAP(lvl) TRAP_ARG(bad_trap, lvl)

#define BTRAPTL1(lvl) TRAPTL1_ARG(bad_trap_tl1, lvl)

#define FLUSH_WINDOW_TRAP						\
	ba,pt	%xcc, etrap;						\
	 rd	%pc, %g7;						\
	flushw;								\
	ldx	[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TNPC], %l1;	\
	add	%l1, 4, %l2;						\
	stx	%l1, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TPC];	\
	ba,pt	%xcc, rtrap_clr_l6;					\
	 stx	%l2, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TNPC];
	        
/* Before touching these macros, you owe it to yourself to go and
 * see how arch/sparc64/kernel/winfixup.S works... -DaveM
 */

/* Normal kernel spill */
#define SPILL_0_NORMAL					\
	stx	%l0, [%sp + STACK_BIAS + 0x00];		\
	stx	%l1, [%sp + STACK_BIAS + 0x08];		\
	stx	%l2, [%sp + STACK_BIAS + 0x10];		\
	stx	%l3, [%sp + STACK_BIAS + 0x18];		\
	stx	%l4, [%sp + STACK_BIAS + 0x20];		\
	stx	%l5, [%sp + STACK_BIAS + 0x28];		\
	stx	%l6, [%sp + STACK_BIAS + 0x30];		\
	stx	%l7, [%sp + STACK_BIAS + 0x38];		\
	stx	%i0, [%sp + STACK_BIAS + 0x40];		\
	stx	%i1, [%sp + STACK_BIAS + 0x48];		\
	stx	%i2, [%sp + STACK_BIAS + 0x50];		\
	stx	%i3, [%sp + STACK_BIAS + 0x58];		\
	stx	%i4, [%sp + STACK_BIAS + 0x60];		\
	stx	%i5, [%sp + STACK_BIAS + 0x68];		\
	stx	%i6, [%sp + STACK_BIAS + 0x70];		\
	stx	%i7, [%sp + STACK_BIAS + 0x78];		\
	saved; retry; nop; nop; nop; nop; nop; nop;	\
	nop; nop; nop; nop; nop; nop; nop; nop;

/* Normal 64bit spill */
#define SPILL_1_GENERIC(xxx)				\
	wr	%g0, xxx, %asi;				\
	stxa	%l0, [%sp + STACK_BIAS + 0x00] %asi;	\
	stxa	%l1, [%sp + STACK_BIAS + 0x08] %asi;	\
	stxa	%l2, [%sp + STACK_BIAS + 0x10] %asi;	\
	stxa	%l3, [%sp + STACK_BIAS + 0x18] %asi;	\
	stxa	%l4, [%sp + STACK_BIAS + 0x20] %asi;	\
	stxa	%l5, [%sp + STACK_BIAS + 0x28] %asi;	\
	stxa	%l6, [%sp + STACK_BIAS + 0x30] %asi;	\
	stxa	%l7, [%sp + STACK_BIAS + 0x38] %asi;	\
	stxa	%i0, [%sp + STACK_BIAS + 0x40] %asi;	\
	stxa	%i1, [%sp + STACK_BIAS + 0x48] %asi;	\
	stxa	%i2, [%sp + STACK_BIAS + 0x50] %asi;	\
	stxa	%i3, [%sp + STACK_BIAS + 0x58] %asi;	\
	stxa	%i4, [%sp + STACK_BIAS + 0x60] %asi;	\
	stxa	%i5, [%sp + STACK_BIAS + 0x68] %asi;	\
	stxa	%i6, [%sp + STACK_BIAS + 0x70] %asi;	\
	stxa	%i7, [%sp + STACK_BIAS + 0x78] %asi;	\
	saved; retry; nop; nop; nop; nop; nop; nop;	\
	nop; nop; nop; nop; nop;			\
	b,a,pt	%xcc, spill_fixup_mna;			\
	b,a,pt	%xcc, spill_fixup;

/* Normal 32bit spill */
#define SPILL_2_GENERIC(xxx)				\
	wr	%g0, xxx, %asi;				\
	srl	%sp, 0, %sp;				\
	stwa	%l0, [%sp + 0x00] %asi;			\
	stwa	%l1, [%sp + 0x04] %asi;			\
	stwa	%l2, [%sp + 0x08] %asi;			\
	stwa	%l3, [%sp + 0x0c] %asi;			\
	stwa	%l4, [%sp + 0x10] %asi;			\
	stwa	%l5, [%sp + 0x14] %asi;			\
	stwa	%l6, [%sp + 0x18] %asi;			\
	stwa	%l7, [%sp + 0x1c] %asi;			\
	stwa	%i0, [%sp + 0x20] %asi;			\
	stwa	%i1, [%sp + 0x24] %asi;			\
	stwa	%i2, [%sp + 0x28] %asi;			\
	stwa	%i3, [%sp + 0x2c] %asi;			\
	stwa	%i4, [%sp + 0x30] %asi;			\
	stwa	%i5, [%sp + 0x34] %asi;			\
	stwa	%i6, [%sp + 0x38] %asi;			\
	stwa	%i7, [%sp + 0x3c] %asi;			\
	saved; retry; nop; nop; nop; nop;		\
	nop; nop; nop; nop; nop; nop;			\
	b,a,pt	%xcc, spill_fixup_mna;			\
	b,a,pt	%xcc, spill_fixup;

#define SPILL_1_NORMAL SPILL_1_GENERIC(ASI_AIUP)
#define SPILL_2_NORMAL SPILL_2_GENERIC(ASI_AIUP)
#define SPILL_3_NORMAL SPILL_0_NORMAL
#define SPILL_4_NORMAL SPILL_0_NORMAL
#define SPILL_5_NORMAL SPILL_0_NORMAL
#define SPILL_6_NORMAL SPILL_0_NORMAL
#define SPILL_7_NORMAL SPILL_0_NORMAL

#define SPILL_0_OTHER SPILL_0_NORMAL
#define SPILL_1_OTHER SPILL_1_GENERIC(ASI_AIUS)
#define SPILL_2_OTHER SPILL_2_GENERIC(ASI_AIUS)
#define SPILL_3_OTHER SPILL_3_NORMAL
#define SPILL_4_OTHER SPILL_4_NORMAL
#define SPILL_5_OTHER SPILL_5_NORMAL
#define SPILL_6_OTHER SPILL_6_NORMAL
#define SPILL_7_OTHER SPILL_7_NORMAL

/* Normal kernel fill */
#define FILL_0_NORMAL					\
	ldx	[%sp + STACK_BIAS + 0x00], %l0;		\
	ldx	[%sp + STACK_BIAS + 0x08], %l1;		\
	ldx	[%sp + STACK_BIAS + 0x10], %l2;		\
	ldx	[%sp + STACK_BIAS + 0x18], %l3;		\
	ldx	[%sp + STACK_BIAS + 0x20], %l4;		\
	ldx	[%sp + STACK_BIAS + 0x28], %l5;		\
	ldx	[%sp + STACK_BIAS + 0x30], %l6;		\
	ldx	[%sp + STACK_BIAS + 0x38], %l7;		\
	ldx	[%sp + STACK_BIAS + 0x40], %i0;		\
	ldx	[%sp + STACK_BIAS + 0x48], %i1;		\
	ldx	[%sp + STACK_BIAS + 0x50], %i2;		\
	ldx	[%sp + STACK_BIAS + 0x58], %i3;		\
	ldx	[%sp + STACK_BIAS + 0x60], %i4;		\
	ldx	[%sp + STACK_BIAS + 0x68], %i5;		\
	ldx	[%sp + STACK_BIAS + 0x70], %i6;		\
	ldx	[%sp + STACK_BIAS + 0x78], %i7;		\
	restored; retry; nop; nop; nop; nop; nop; nop;	\
	nop; nop; nop; nop; nop; nop; nop; nop;

/* Normal 64bit fill */
#define FILL_1_GENERIC(xxx)				\
	wr	%g0, xxx, %asi;				\
	ldxa	[%sp + STACK_BIAS + 0x00] %asi, %l0;	\
	ldxa	[%sp + STACK_BIAS + 0x08] %asi, %l1;	\
	ldxa	[%sp + STACK_BIAS + 0x10] %asi, %l2;	\
	ldxa	[%sp + STACK_BIAS + 0x18] %asi, %l3;	\
	ldxa	[%sp + STACK_BIAS + 0x20] %asi, %l4;	\
	ldxa	[%sp + STACK_BIAS + 0x28] %asi, %l5;	\
	ldxa	[%sp + STACK_BIAS + 0x30] %asi, %l6;	\
	ldxa	[%sp + STACK_BIAS + 0x38] %asi, %l7;	\
	ldxa	[%sp + STACK_BIAS + 0x40] %asi, %i0;	\
	ldxa	[%sp + STACK_BIAS + 0x48] %asi, %i1;	\
	ldxa	[%sp + STACK_BIAS + 0x50] %asi, %i2;	\
	ldxa	[%sp + STACK_BIAS + 0x58] %asi, %i3;	\
	ldxa	[%sp + STACK_BIAS + 0x60] %asi, %i4;	\
	ldxa	[%sp + STACK_BIAS + 0x68] %asi, %i5;	\
	ldxa	[%sp + STACK_BIAS + 0x70] %asi, %i6;	\
	ldxa	[%sp + STACK_BIAS + 0x78] %asi, %i7;	\
	restored; retry; nop; nop; nop; nop; nop; nop;	\
	nop; nop; nop; nop; nop;			\
	b,a,pt	%xcc, fill_fixup_mna;			\
	b,a,pt	%xcc, fill_fixup;

/* Normal 32bit fill */
#define FILL_2_GENERIC(xxx)				\
	wr	%g0, xxx, %asi;				\
	srl	%sp, 0, %sp;				\
	lduwa	[%sp + 0x00] %asi, %l0;			\
	lduwa	[%sp + 0x04] %asi, %l1;			\
	lduwa	[%sp + 0x08] %asi, %l2;			\
	lduwa	[%sp + 0x0c] %asi, %l3;			\
	lduwa	[%sp + 0x10] %asi, %l4;			\
	lduwa	[%sp + 0x14] %asi, %l5;			\
	lduwa	[%sp + 0x18] %asi, %l6;			\
	lduwa	[%sp + 0x1c] %asi, %l7;			\
	lduwa	[%sp + 0x20] %asi, %i0;			\
	lduwa	[%sp + 0x24] %asi, %i1;			\
	lduwa	[%sp + 0x28] %asi, %i2;			\
	lduwa	[%sp + 0x2c] %asi, %i3;			\
	lduwa	[%sp + 0x30] %asi, %i4;			\
	lduwa	[%sp + 0x34] %asi, %i5;			\
	lduwa	[%sp + 0x38] %asi, %i6;			\
	lduwa	[%sp + 0x3c] %asi, %i7;			\
	restored; retry; nop; nop; nop; nop;		\
	nop; nop; nop; nop; nop; nop;			\
	b,a,pt	%xcc, fill_fixup_mna;			\
	b,a,pt	%xcc, fill_fixup;

#define FILL_1_NORMAL FILL_1_GENERIC(ASI_AIUP)
#define FILL_2_NORMAL FILL_2_GENERIC(ASI_AIUP)
#define FILL_3_NORMAL FILL_0_NORMAL
#define FILL_4_NORMAL FILL_0_NORMAL
#define FILL_5_NORMAL FILL_0_NORMAL
#define FILL_6_NORMAL FILL_0_NORMAL
#define FILL_7_NORMAL FILL_0_NORMAL

#define FILL_0_OTHER FILL_0_NORMAL
#define FILL_1_OTHER FILL_1_GENERIC(ASI_AIUS)
#define FILL_2_OTHER FILL_2_GENERIC(ASI_AIUS)
#define FILL_3_OTHER FILL_3_NORMAL
#define FILL_4_OTHER FILL_4_NORMAL
#define FILL_5_OTHER FILL_5_NORMAL
#define FILL_6_OTHER FILL_6_NORMAL
#define FILL_7_OTHER FILL_7_NORMAL

#endif /* !(_SPARC64_HEAD_H) */
