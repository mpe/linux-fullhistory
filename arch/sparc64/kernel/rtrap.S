/* $Id: rtrap.S,v 1.2 1997/02/26 11:09:25 jj Exp $
 * rtrap.S: Preparing for entry into the kernel on Sparc V9.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/pstate.h>
#include <asm/ptrace.h>

	.text
	.align	4
	.globl	rtrap
rtrap:
	/*not*/ done /*yet*/
