/*
 * splx.c - SYSV DDI/DKI ipl manipulation functions
 *
 * Internally, many unices use a range of different interrupt 
 * privilege levels, ie from "allow all interrupts" (7) to 
 * "allow no interrupts." (0) under SYSV.
 *
 * This a simple splx() function behaves as the SYSV DDI/DKI function does,
 * although since Linux only implements the equivalent of level 0 (cli) and
 * level 7 (sti), this implementation only implements those levels.
 * 
 * Also, unlike the current Linux routines, splx() also returns the 
 * old privilege level so that it can be restored.
 */

#include <asm/system.h>

int splx (int new_level) {
    register int old_level, tmp;
    save_flags(tmp);
    old_level = (tmp & 0x200) ? 7 : 0;
    if (new_level)
	sti();
    else 
	cli();
    return old_level;
}
