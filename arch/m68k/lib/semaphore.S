/*
 *  linux/arch/m68k/lib/semaphore.S
 *
 *  Copyright (C) 1996  Linus Torvalds
 *
 *  m68k version by Andreas Schwab
 */

#include <linux/linkage.h>

/*
 * "down_failed" is called with the eventual return address
 * in %a0, and the address of the semaphore in %a1. We need
 * to increment the number of waiters on the semaphore,
 * call "__down()", and then eventually return to try again.
 */
ENTRY(down_failed)
	movel %a0,-(%sp)
	movel %a1,-(%sp)
	jbsr SYMBOL_NAME(__down)
	movel (%sp)+,%a1
	rts

ENTRY(up_wakeup)
	movel %a0,-(%sp)
	movel %a1,-(%sp)
	jbsr SYMBOL_NAME(__up)
	movel (%sp)+,%a1
	rts
