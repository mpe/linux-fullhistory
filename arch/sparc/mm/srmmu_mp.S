/* srmmu_mp.S:	Low level invalidates for MP SRMMU modules.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */
#include <asm/cprefix.h>
#include <asm/asi.h>

	/* Protocol is:
	 *
	 * %l0 --> %l3  NO TOUCH
	 * %l7          NO TOUCH
	 * %l6		return address - 0x8
	 * %l4, %l5     what we can use
	 */

	.globl	C_LABEL(hyper_invalidate_low)
C_LABEL(hyper_invalidate_low):
	/* First the on-chip cache. */
	set	C_LABEL(hyper_cache_size), %l5
	ld	[%l5], %l5
1:
	subcc	%l5, 32, %l5
	bne	1b
	 sta	%g0, [%l5] ASI_M_FLUSH_CTX

	/* Now the on-chip ICACHE. */
	sta	%g0, [%g0] ASI_M_FLUSH_IWHOLE

	/* Flush the TLB and return. */
	mov	0x400, %l4
	sta	%g0, [%l4] ASI_M_FLUSH_PROBE

	jmpl	%l6 + 0x8, %g0
	 nop

	.globl	C_LABEL(viking_invalidate_low)
C_LABEL(viking_invalidate_low):
	/* Flash clear the I/D caches. */
	sta	%g0, [%g0] ASI_M_IC_FLCLEAR
	sta	%g0, [%g0] ASI_M_DC_FLCLEAR

	/* Flush the TLB and return. */
	mov	0x400, %l4
	sta	%g0, [%l4] ASI_M_FLUSH_PROBE

	jmpl	%l6 + 0x8, %g0
	 nop

	/* XXX Write the Cypress when I get access to
	 * XXX some modules, poke mossip real hard until
	 * XXX he takes care of the Nubis upgrade.
	 */
