/* $Id: head.h,v 1.7 1997/03/18 18:00:36 jj Exp $ */
#ifndef _SPARC64_HEAD_H
#define _SPARC64_HEAD_H

#define KERNBASE    0xfffff80000000000
#define BOOT_KERNEL b sparc64_boot; nop; nop; nop; nop; nop; nop; nop;

#define CLEAN_WINDOW							\
	clr	%o0;	clr	%o1;	clr	%o2;	clr	%o3;	\
	clr	%o4;	clr	%o5;	clr	%o6;	clr	%o7;	\
	clr	%l0;	clr	%l1;	clr	%l2;	clr	%l3;	\
	clr	%l4;	clr	%l5;	clr	%l6;	clr	%l7;	\
	rdpr %cleanwin, %g1; 		add %g1, 1, %g1;		\
	wrpr %g1, 0x0, %cleanwin;	retry;				\
	nop;		nop;		nop;		nop;

#define TRAP(routine)					\
	ba,pt	%xcc, etrap;				\
	 rd	%pc, %g7;				\
	call	routine;				\
	 add	%sp, STACK_BIAS + REGWIN_SZ, %o0;	\
	ba,pt	%xcc, rtrap;				\
	 nop;						\
	nop;						\
	nop;

#define TRAP_ARG(routine, arg)				\
	ba,pt	%xcc, etrap;				\
	 rd	%pc, %g7;				\
	add	%sp, STACK_BIAS + REGWIN_SZ, %o0;	\
	call	routine;				\
	 mov	arg, %o1;				\
	ba,pt	%xcc, rtrap;				\
	 nop;						\
	nop;
	
#define SYSCALL_TRAP(routine, systbl)			\
	ba,pt	%xcc, etrap;				\
	 rd	%pc, %g7;				\
	sethi	%hi(systbl), %l7;			\
	call	routine;				\
	 or	%l7, %lo(systbl), %l7;			\
	nop; nop; nop;
	

#define SUNOS_SYSCALL_TRAP SYSCALL_TRAP(linux_sparc_syscall, sunos_sys_table)
#define	LINUX_32BIT_SYSCALL_TRAP SYSCALL_TRAP(linux_sparc_syscall, sys_call_table32)
#define LINUX_64BIT_SYSCALL_TRAP SYSCALL_TRAP(linux_sparc_syscall, sys_call_table64)
/* FIXME: Write these actually */	
#define NETBSD_SYSCALL_TRAP TRAP(netbsd_syscall)
#define SOLARIS_SYSCALL_TRAP TRAP(solaris_syscall)
#define BREAKPOINT_TRAP TRAP(breakpoint_trap)
#define GETCC_TRAP TRAP(getcc)
#define SETCC_TRAP TRAP(setcc)
#define INDIRECT_SOLARIS_SYSCALL(tlvl) TRAP_ARG(indirect_syscall, tlvl)

#define TRAP_IRQ(routine, level) TRAP_ARG(routine, level)

#define BTRAP(lvl) TRAP_ARG(bad_trap, lvl)

#define BTRAPTL1(lvl) TRAP_ARG(bad_trap_tl1, lvl)

#define FLUSH_WINDOW_TRAP					\
	flushw;							\
	done; nop; nop; nop; nop; nop; nop;
	        

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
#define SPILL_1_NORMAL					\
	wr	%g0, ASI_AIUP, %asi;			\
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
	nop; nop; nop; nop; nop; nop; nop;

/* Normal 32bit spill */
#define SPILL_2_NORMAL					\
	wr	%g0, ASI_AIUP, %asi;			\
	srl	%sp, 0, %sp;				\
	stda	%l0, [%sp + 0x00] %asi;			\
	stda	%l2, [%sp + 0x10] %asi;			\
	stda	%l4, [%sp + 0x20] %asi;			\
	stda	%l6, [%sp + 0x30] %asi;			\
	stda	%i0, [%sp + 0x40] %asi;			\
	stda	%i2, [%sp + 0x50] %asi;			\
	stda	%i4, [%sp + 0x60] %asi;			\
	stda	%i6, [%sp + 0x70] %asi;			\
	saved; retry; nop; nop; nop; nop; nop;		\
	nop; nop; nop; nop; nop; nop; nop; nop;		\
	nop; nop; nop; nop; nop; nop; nop; nop;

#define SPILL_3_NORMAL SPILL_0_NORMAL
#define SPILL_4_NORMAL SPILL_0_NORMAL
#define SPILL_5_NORMAL SPILL_0_NORMAL
#define SPILL_6_NORMAL SPILL_0_NORMAL
#define SPILL_7_NORMAL SPILL_0_NORMAL

#define SPILL_0_OTHER SPILL_0_NORMAL
#define SPILL_1_OTHER SPILL_1_NORMAL
#define SPILL_2_OTHER SPILL_2_NORMAL
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
	saved; retry; nop; nop; nop; nop; nop; nop;	\
	nop; nop; nop; nop; nop; nop; nop; nop;

/* Normal 64bit fill */
#define FILL_1_NORMAL					\
	wr	%g0, ASI_AIUP, %asi;			\
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
	saved; retry; nop; nop; nop; nop; nop; nop;	\
	nop; nop; nop; nop; nop; nop; nop;

/* Normal 32bit fill */
#define FILL_2_NORMAL					\
	wr	%g0, ASI_AIUP, %asi;			\
	srl	%sp, 0, %sp;				\
	ldda	[%sp + 0x00] %asi, %l0;			\
	ldda	[%sp + 0x10] %asi, %l2;			\
	ldda	[%sp + 0x20] %asi, %l4;			\
	ldda	[%sp + 0x30] %asi, %l6;			\
	ldda	[%sp + 0x40] %asi, %i0;			\
	ldda	[%sp + 0x50] %asi, %i2;			\
	ldda	[%sp + 0x60] %asi, %i4;			\
	ldda	[%sp + 0x70] %asi, %i6;			\
	saved; retry; nop; nop; nop; nop; nop;		\
	nop; nop; nop; nop; nop; nop; nop; nop;		\
	nop; nop; nop; nop; nop; nop; nop; nop;

#define FILL_3_NORMAL FILL_0_NORMAL
#define FILL_4_NORMAL FILL_0_NORMAL
#define FILL_5_NORMAL FILL_0_NORMAL
#define FILL_6_NORMAL FILL_0_NORMAL
#define FILL_7_NORMAL FILL_0_NORMAL

#define FILL_0_OTHER FILL_0_NORMAL
#define FILL_1_OTHER FILL_1_NORMAL
#define FILL_2_OTHER FILL_2_NORMAL
#define FILL_3_OTHER FILL_3_NORMAL
#define FILL_4_OTHER FILL_4_NORMAL
#define FILL_5_OTHER FILL_5_NORMAL
#define FILL_6_OTHER FILL_6_NORMAL
#define FILL_7_OTHER FILL_7_NORMAL

#endif /* !(_SPARC64_HEAD_H) */
