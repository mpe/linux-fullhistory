/*
 * linux/arch/arm/lib/copypage.S
 *
 * Copyright (C) 1995-1999 Russell King
 *
 * ASM optimised string functions
 *
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include "constants.h"

		.text 
/*
 * StrongARM optimised copy_page routine
 * now 1.72bytes/cycle, was 1.60 bytes/cycle
 * (50MHz bus -> 86MB/s)
 */

ENTRY(copy_page)
		stmfd	sp!, {r4, lr}			@	2
		mov	r2, #PAGE_SZ/64			@	1
1:		ldmia	r1!, {r3, r4, ip, lr}		@	4
		subs	r2, r2, #1			@	1
		stmia	r0!, {r3, r4, ip, lr}		@	4
		ldmia	r1!, {r3, r4, ip, lr}		@	4+1
		stmia	r0!, {r3, r4, ip, lr}		@	4
		ldmia	r1!, {r3, r4, ip, lr}		@	4+1
		stmia	r0!, {r3, r4, ip, lr}		@	4
		ldmia	r1!, {r3, r4, ip, lr}		@	4+1
		stmia	r0!, {r3, r4, ip, lr}		@	4
		bne	1b				@	1
		LOADREGS(fd, sp!, {r4, pc})		@	3


