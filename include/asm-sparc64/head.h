/* $Id: head.h,v 1.2 1996/12/28 18:39:49 davem Exp $ */
#ifndef _SPARC64_HEAD_H
#define _SPARC64_HEAD_H

#define BOOT_KERNEL b sparc64_boot; nop; nop; nop; nop; nop; nop; nop;

#define BTRAP(lvl)

#define BTRAPTL1(lvl)

#define CLEAN_WINDOW							\
	clr	%o0;	clr	%o1;	clr	%o2;	clr	%o3;	\
	clr	%o4;	clr	%o5;	clr	%o6;	clr	%o7;	\
	clr	%l0;	clr	%l1;	clr	%l2;	clr	%l3;	\
	clr	%l4;	clr	%l5;	clr	%l6;	clr	%l7;	\
	rdpr %cleanwin, %g1; 		add %g1, 1, %g1;		\
	wrpr %g1, 0x0, %cleanwin;	retry;				\
	nop;		nop;		nop;		nop;

#define TRAP(routine)			\
	b	etrap;			\
	 rd	%pc, %g7;		\
	call	routine;		\
	 add	%sp, REGWIN_SZ, %o0;	\
	b	rtrap;			\
	 subcc	%g0, %o0, %g0;		\
	nop;				\
	nop;

#define TRAP_IRQ(routine, level)	\
	b	etrap;			\
	 rd	%pc, %g7;		\
	add	%sp, REGWIN_SZ, %o0;	\
	call	routine;		\
	 mov	level, %o1;		\
	b	rtrap;			\
	 subcc	%g0, %o0, %g0;		\
	nop;

#endif /* !(_SPARC64_HEAD_H) */
