/* atomic.S: Move this stuff here for better ICACHE hit rates.
 *
 * Copyright (C) 1996 David S. Miller (davem@caipfs.rutgers.edu)
 */

#include <asm/cprefix.h>
#include <asm/ptrace.h>
#include <asm/psr.h>

	.text
	.align	4

	/* XXX At boot time patch this with swap [x], y; retl; if
	 * XXX processor is found to have that instruction.
	 */

	.globl	___xchg32
___xchg32:
	rd	%psr, %g3
	andcc	%g3, PSR_PIL, %g0
	bne	1f
	 nop
	wr	%g3, PSR_PIL, %psr
	nop; nop; nop
1:
	andcc	%g3, PSR_PIL, %g0
	ld	[%g1], %g7
	bne	1f
	 st	%g2, [%g1]
	wr	%g3, 0x0, %psr
	nop; nop; nop
1:
	mov	%g7, %g2
	jmpl	%o7, %g0		/* Note, not + 0x8, see call in system.h */
	 mov	%g4, %o7

	.globl	___xchg32_hw
___xchg32_hw:
	swap	[%g1], %g2
	jmpl	%o7, %g0		/* Note, not + 0x8, see call in system.h */
	 mov	%g4, %o7

	/* Atomic add/sub routines.  Returns the final value whether you
	 * want it or not for even _better_ cache hit rates.
	 */
	.globl	___atomic_add
___atomic_add:
	rd	%psr, %g3
	andcc	%g3, PSR_PIL, %g0
	bne	1f
	 nop
	wr	%g3, PSR_PIL, %psr
	nop; nop; nop;
1:
	ld	[%g1], %g7
	andcc	%g3, PSR_PIL, %g0
	add	%g7, %g2, %g2
	bne	1f
	 st	%g2, [%g1]
	wr	%g3, 0x0, %psr
	nop; nop; nop;
1:
	jmpl	%o7, %g0		/* NOTE: not + 8, see callers in atomic.h */
	 mov	%g4, %o7

	.globl	___atomic_sub
___atomic_sub:
	rd	%psr, %g3
	andcc	%g3, PSR_PIL, %g0
	bne	1f
	 nop
	wr	%g3, PSR_PIL, %psr
	nop; nop; nop;
1:
	ld	[%g1], %g7
	andcc	%g3, PSR_PIL, %g0
	sub	%g7, %g2, %g2
	bne	1f
	 st	%g2, [%g1]
	wr	%g3, 0x0, %psr
	nop; nop; nop;
1:
	jmpl	%o7, %g0		/* NOTE: not + 8, see callers in atomic.h */
	 mov	%g4, %o7
