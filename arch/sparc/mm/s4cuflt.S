/* s4cuflt.S:  Quick in window user tlb faults on the sun4c.
 *
 * Copyright (C) 1995 David S. Miller (davem@caipfs.rutgers.edu)
 */

	.data

	.align	8
.g0g1:	.quad	0
.g2g3:	.quad	0
.g4g5:	.quad	0
.g6g7:	.quad	0

#include "srclow.h"

	.text

	.align	8
	.globl	sun4c_quick_user_fault
sun4c_quick_user_fault:
