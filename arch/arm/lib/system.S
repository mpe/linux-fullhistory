/* 
 * linux/arch/arm/lib/system.S
 *
 * Copyright (C) 1995, 1996 Russell King
 *
 * 07/06/96: Now support tasks running in SVC mode.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

		.text

ENTRY(abort)
		adr	r0, .abort_msg
		mov	r1, lr
		b	SYMBOL_NAME(panic)

.abort_msg:	.ascii	"Eek! Got to an abort() from %p! "
		.ascii	"(Please report to rmk@arm.uk.linux.org)\n\0"
		.align
