/* $Id: sunos_asm.S,v 1.8 1995/11/25 00:58:29 davem Exp $
 * sunos_asm.S: SunOS system calls which must have a low-level
 *              entry point to operate correctly.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *
 * Based upon preliminary work which is:
 *
 * Copyright (C) 1995 Adrian M. Rodriguez (adrian@remus.rutgers.edu)
 */

#include <asm/cprefix.h>
#include <asm/ptrace.h>

	/* Note that for dual value returning system calls we
	 * need to store the second one ourselves in the pt_regs
	 * trap-frame.  linux_sparc_syscall does %o0 for us
	 * however.
	 */

	.text
	.align 4

	/* SunOS getpid() returns pid in %o0 and ppid in %o1 */
	.globl	C_LABEL(sunos_getpid)
C_LABEL(sunos_getpid):
	save	%sp, -STACKFRAME_SZ, %sp
	call	C_LABEL(sys_getpid)
	nop

	mov	%o0, %i0

	call	C_LABEL(sys_getppid)
	nop	

	st	%o0, [%fp + STACKFRAME_SZ + PT_I1]

	ret
	restore

	/* SunOS getuid() returns uid in %o0 and euid in %o1 */
	.globl	C_LABEL(sunos_getuid)
C_LABEL(sunos_getuid):
	save	%sp, -STACKFRAME_SZ, %sp
	call	C_LABEL(sys_getuid)
	nop

	mov	%o0, %i0

	call	C_LABEL(sys_geteuid)
	nop

	st	%o0, [%fp + STACKFRAME_SZ + PT_I1]

	ret
	restore

	/* SunOS getgid() returns gid in %o0 and egid in %o1 */
	.globl	C_LABEL(sunos_getgid)
C_LABEL(sunos_getgid):
	save	%sp, -STACKFRAME_SZ, %sp
	call	C_LABEL(sys_getgid)
	nop

	mov	%o0, %i0

	call	C_LABEL(sys_getegid)
	nop

	st	%o0, [%fp + STACKFRAME_SZ + PT_I1]

	ret
	restore

	/* SunOS's execv() call only specifies the argv argument, the
	 * environment settings are the same as the calling processes.
	 */
	.globl	C_LABEL(sunos_execv)
C_LABEL(sunos_execv):
	save	%sp, -STACKFRAME_SZ, %sp

	st	%g0, [%fp + STACKFRAME_SZ + PT_I2]
	call	C_LABEL(sparc_execve)
	add	%fp, STACKFRAME_SZ, %o0

	st	%o0, [%fp + STACKFRAME_SZ + PT_I1]

	ret
	restore

