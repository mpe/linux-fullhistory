/* asmmacro.h: Assembler macros.
 *
 * Copyright (C) 1996 David S. Miller (davem@caipfs.rutgers.edu)
 */

#ifndef _SPARC_ASMMACRO_H
#define _SPARC_ASMMACRO_H

/* #define SMP_DEBUG */

#define GET_PROCESSOR_ID(reg) \
	rd	%tbr, %reg; \
	srl	%reg, 12, %reg; \
	and	%reg, 3, %reg;

#define GET_PROCESSOR_MID(reg, tmp) \
	GET_PROCESSOR_ID(reg) \
	set	C_LABEL(mid_xlate), %tmp; \
	ldub	[%tmp + %reg], %reg;

#define GET_PROCESSOR_OFFSET(reg) \
	rd	%tbr, %reg; \
	srl	%reg, 10, %reg; \
	and	%reg, 0xc, %reg;

#define PROCESSOR_OFFSET_TO_ID(reg) \
	srl	%reg, 2, %reg;

#define PROCESSOR_ID_TO_OFFSET(reg) \
	sll	%reg, 2, %reg;

/* All trap entry points _must_ begin with this macro or else you
 * lose.  It makes sure the kernel has a proper window so that
 * c-code can be called.
 */
#define SAVE_ALL_HEAD \
	sethi	%hi(trap_setup), %l4; \
	jmpl	%l4 + %lo(trap_setup), %l6;
#define SAVE_ALL \
	SAVE_ALL_HEAD \
	 nop;

/* All traps low-level code here must end with this macro. */
#define RESTORE_ALL b ret_trap_entry; clr %l6;

#endif /* !(_SPARC_ASMMACRO_H) */
