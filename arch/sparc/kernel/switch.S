/* $Id: switch.S,v 1.18 1996/04/03 02:15:00 davem Exp $
 * switch.S: Sparc task switch code.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/head.h>
#include <asm/asi.h>
#include <asm/contregs.h>
#include <asm/cprefix.h>
#include <asm/psr.h>
#include <asm/asmmacro.h>
#include <asm/ptrace.h>
#include <asm/winmacro.h>

#define sw_ntask     g1
#define sw_psr       g4
#define sw_wim       g5
#define sw_tmp       g6
#define sw_ctx       g7

/* Context switch code.  The new process's task_struct
 * ptr is passed as the first parameter.
 *
 * First successful task switch 05/13/95 21:52:37
 */
	.align 4
	.globl	C_LABEL(sparc_switch_to)
C_LABEL(sparc_switch_to):
	mov	%o0, %sw_ntask

	/* Save kernel state. */
	FLUSH_ALL_KERNEL_WINDOWS; 
	STORE_WINDOW(sp)
	rd	%psr, %sw_psr
	LOAD_CURRENT(sw_tmp, sw_wim)
	rd	%wim, %sw_wim
	std	%sw_psr, [%sw_tmp + THREAD_KPSR]
	std	%sp, [%sw_tmp + THREAD_KSP]

	/* Load new kernel state. */
	wr	%sw_psr, PSR_ET, %psr
	WRITE_PAUSE
#ifdef __SMP__
	GET_PROCESSOR_OFFSET(sw_psr)
	set	C_LABEL(current_set), %sw_tmp
	st	%sw_ntask, [%sw_tmp + %sw_psr]
#else
	sethi	%hi(C_LABEL(current_set)), %sw_tmp
	st	%sw_ntask, [%sw_tmp + %lo(C_LABEL(current_set))]
#endif
	ldd	[%sw_ntask + THREAD_KPSR], %sw_psr
	wr	%sw_psr, PSR_ET, %psr
	WRITE_PAUSE
	wr	%sw_wim, 0x0, %wim
	WRITE_PAUSE
	ldd	[%sw_ntask + THREAD_KSP], %sp
	LOAD_WINDOW(sp)

	wr	%sw_psr, 0x0, %psr		! traps back on
	WRITE_PAUSE

	retl
	 nop


#ifdef __SMP__
	/* Because of nasty register windows this is the only way
	 * to start a processor into it's cpu_idle() thread.
	 */

	.globl	C_LABEL(sparc_cpusched)
C_LABEL(sparc_cpusched):
	LOAD_CURRENT(g1, g2)
	rd	%psr, %g7

	wr	%g7, PSR_ET, %psr
	WRITE_PAUSE

	ldd	[%g1 + THREAD_KPSR], %g2

	wr	%g2, PSR_ET, %psr
	WRITE_PAUSE

	wr	%g3, 0x0, %wim
	WRITE_PAUSE

	ldd	[%g1 + THREAD_KSP], %sp
	LOAD_WINDOW(sp)

	wr	%g2, 0x0, %psr
	WRITE_PAUSE

	retl
	 nop
#endif
