/* V9head.S: Initial boot code for the V9 Sparc under Linux.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *
 */

#include <asm/cprefix.h>

	.text

	.globl	start
	.globl	_start
	.globl	C_LABEL(trapbase)
_start:
start:
C_LABEL(trapbase):
