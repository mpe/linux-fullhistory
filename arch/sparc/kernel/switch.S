/* $Id: switch.S,v 1.14 1995/12/29 21:47:22 davem Exp $
 * switch.S: Sparc task switch code.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/head.h>
#include <asm/asi.h>
#include <asm/contregs.h>
#include <asm/cprefix.h>
#include <asm/psr.h>
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
	sethi	%hi(C_LABEL(current_set)), %sw_tmp
	st	%sw_ntask, [%sw_tmp + %lo(C_LABEL(current_set))]
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
