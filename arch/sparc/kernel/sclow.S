/* sclow.S: Low level special syscall handling.
 *          Basically these are cases where we can completely
 *          handle the system call without saving any state
 *          because we know that the process will not sleep.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/cprefix.h>
#include <asm/ptrace.h>
#include <asm/errno.h>
#include <asm/winmacro.h>
#include <asm/psr.h>

#define CC_AND_RETT  \
	set	PSR_C, %l4; \
	andn	%l0, %l4, %l4; \
	wr	%l4, 0x0, %psr; \
	nop; nop; nop; \
	jmp	%l2; \
	rett	%l2 + 4;

#define SC_AND_RETT  \
	set	PSR_C, %l4; \
	or	%l0, %l4, %l4; \
	wr	%l4, 0x0, %psr; \
	nop; nop; nop; \
	jmp	%l2; \
	rett	%l2 + 4;

#define LABEL(func)  CONCAT(func, _low)

	.globl	LABEL(sunosnop)
LABEL(sunosnop):
	CC_AND_RETT

	.globl	LABEL(sunosgetpid)
LABEL(sunosgetpid):
	LOAD_CURRENT(l4, l5)
	ld	[%l4 + 108], %i0
	ld	[%l4 + 256], %l5
	ld	[%l5 + 108], %i1
	CC_AND_RETT

	.globl	LABEL(sunosgetuid)
LABEL(sunosgetuid):
	LOAD_CURRENT(l4, l5)
	lduh	[%l4 + 280], %i0
	lduh	[%l4 + 282], %i1
	CC_AND_RETT

	.globl	LABEL(sunosgetgid)
LABEL(sunosgetgid):
	LOAD_CURRENT(l4, l5)
	lduh	[%l4 + 288], %i0
	lduh	[%l4 + 290], %i1
	CC_AND_RETT

	.globl	LABEL(sunosmctl)
LABEL(sunosmctl):
	mov	0, %i0
	CC_AND_RETT

	.globl	LABEL(sunosgdtsize)
LABEL(sunosgdtsize):	
	mov	256, %i0
	CC_AND_RETT

	.globl	LABEL(sunossblock)
LABEL(sunossblock):
	LOAD_CURRENT(l4, l5)
	set	-65793, %l5
	and	%i0, %l5, %l5
	ld	[%l4 + TASK_BLOCKED], %i0
	or	%i0, %l5, %l5
	st	%l5, [%l4 + TASK_BLOCKED]
	CC_AND_RETT

	.globl	LABEL(sunossmask)
LABEL(sunossmask):
	LOAD_CURRENT(l4, l5)
	set	-65793, %l5
	and	%i0, %l5, %l5
	ld	[%l4 + TASK_BLOCKED], %i0
	st	%l5, [%l4 + TASK_BLOCKED]
	CC_AND_RETT

	.globl	LABEL(getpagesize)
LABEL(getpagesize):
	set	4096, %i0
	CC_AND_RETT

	.globl	LABEL(umask)
LABEL(umask):
	LOAD_CURRENT(l4, l5)
	ld	[%l4 + 1560], %l5
	and	%i0, 511, %l4
	lduh	[%l5 + 4], %i0
	sth	%l4, [%l5 + 4]
	CC_AND_RETT

#if 0
	.globl	LABEL(write)
LABEL(write):
	cmp	%i0, 255		/* fd >= NR_OPEN */
	bgu,a	write_error_return
	 mov	EBADF, %i0

	LOAD_CURRENT(l4, l5)
	ld	[%l4 + 1564], %l5
	sll	%i0, 2, %l6
	add	%l5, %l6, %l5
	ld	[%l5 + 36], %l6
	cmp	%l6, 0			/* !(file=current->files->fd[fd]) */
	be,a	write_error_return
	 mov	EBADF, %i0	

	ld	[%l6 + 36], %l5
	cmp	%l5, 0			/* !(inode=file->f_inode) */
	be,a	write_error_return
	 mov	EBADF, %i0

	lduh	[%l6], %l5		/* !(file->f_mode & 2) */
	andcc	%l5, 2, %g0
	be,a	write_error_return
	 mov	EBADF, %i0

	ld	[%l6 + 40], %l5
	cmp	%l5, 0			/* !file->f_op */
	be,a	write_error_return
	 mov	EINVAL, %i0

	ld	[%l5 + 8], %l5		/* !file->f_op->write */
	cmp	%l5, 0
	be,a	write_error_return
	 mov	EINVAL, %i0

	cmp	%i2, 0			/* count == 0 */
	bne	1f
	 nop

	mov	0, %i0
	CC_AND_RETT

1:
	/* See if we can do the optimization... */
	ld	[%l6 + 36], %l5
	lduh	[%l5 + 16], %l5
	srl	%l5, 8, %l6
	cmp	%l6, 1			/* MEM_MAJOR */
	bne,a	write_is_too_hard
	 sethi	%hi(C_LABEL(quick_sys_write)), %l7

	and	%l5, 0xff, %l5
	cmp	%l5, 3			/* NULL_MINOR */
	bne,a	write_is_too_hard
	 sethi	%hi(C_LABEL(quick_sys_write)), %l7

	/* We only optimize for the /dev/null case currently,
	 * however to stay POSIX4 compliant we must check the
	 * validity of the passed buffer.  Blowlaris2.x does not
	 * do this and is therefore not POSIX4 compliant!
	 * If you are going to optimize for benchmarks, fine,
	 * but to break behavior of a system call in the process
	 * is complete brain damage...
	 */

	/* XXX write verify_area thingy for full POSIX conformance! XXX */

	mov	%i2, %i0
	CC_AND_RETT

write_is_too_hard:
	b	syscall_is_too_hard
	 or	%l7, %lo(C_LABEL(quick_sys_write)), %l7

write_error_return:
	SC_AND_RETT
#endif

	/* XXX sys_nice() XXX */
	/* XXX sys_setpriority() XXX */
	/* XXX sys_getpriority() XXX */
	/* XXX sys_setregid() XXX */
	/* XXX sys_setgid() XXX */
	/* XXX sys_setreuid() XXX */
	/* XXX sys_setuid() XXX */
	/* XXX sys_setfsuid() XXX */
	/* XXX sys_setfsgid() XXX */
	/* XXX sys_setpgid() XXX */
	/* XXX sys_getpgid() XXX */
	/* XXX sys_setsid() XXX */
	/* XXX sys_getsid() XXX */
