/*
 *  linux/arch/arm/lib/semaphore.S
 *
 *  Idea from i386 code, Copyright Linus Torvalds.
 *  Converted for ARM by Russell King
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

/*
 * The semaphore operations have a special calling sequence
 * that allows us to keep the distruption of the main code
 * path to a minimum.  These routines save and restore the
 * registers that will be touched by __down etc.
 */
ENTRY(__down_failed)
	stmfd	sp!, {r0 - r3, ip, lr}
	bl	SYMBOL_NAME(__down)
	LOADREGS(fd, sp!, {r0 - r3, ip, pc})

ENTRY(__down_interruptible_failed)
	stmfd	sp!, {r1 - r3, ip, lr}
	bl	SYMBOL_NAME(__down_interruptible)
	LOADREGS(fd, sp!, {r1 - r3, ip, pc})

ENTRY(__down_trylock_failed)
	stmfd	sp!, {r1 - r3, ip, lr}
	bl	SYMBOL_NAME(__down_trylock)
	LOADREGS(fd, sp!, {r1 - r3, ip, pc})

ENTRY(__up_wakeup)
	stmfd	sp!, {r0 - r3, ip, lr}
	bl	SYMBOL_NAME(__up)
	LOADREGS(fd, sp!, {r0 - r3, ip, pc})
