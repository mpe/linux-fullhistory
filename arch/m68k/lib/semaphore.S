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
 * to call "__down()", and then re-try until we succeed..
 */
ENTRY(__down_failed)
	moveml %a0/%d0/%d1,-(%sp)
1:	movel %a1,-(%sp)
	jbsr SYMBOL_NAME(__down)
	movel (%sp)+,%a1
	subql #1,(%a1)
	jmi 1b
	movel (%sp)+,%d0
	movel (%sp)+,%d1
	rts

ENTRY(__up_wakeup)
	moveml %a0/%d0/%d1,-(%sp)
	movel %a1,-(%sp)
	jbsr SYMBOL_NAME(__up)
	movel (%sp)+,%a1
	movel (%sp)+,%d0
	movel (%sp)+,%d1
	rts
