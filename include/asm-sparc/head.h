#ifndef __SPARC_HEAD_H
#define __SPARC_HEAD_H

#define KERNSIZE	134217728   /* this is how much of a mapping the prom promises */
#define PAGESIZE	4096        /* luckily this is the same on sun4c's and sun4m's */
#define PAGESHIFT       12
#define PROM_BASE	-1568768    /* casa 'de PROM */
#define LOAD_ADDR       0x4000      /* prom jumps to us here */
#define C_STACK         96
#define SUN4C_SEGSZ     (1 << 18)
#define USRSTACK        0x0         /* no joke, this is temporary, trust me */
#define INT_ENABLE_REG_PHYSADR      0xf5000000
#define INTS_ENAB   0x01

#define WRITE_PAUSE     nop; nop; nop;

#define PAGE_SIZE       4096

/* Here are some trap goodies */


/* Generic trap entry. */

#define TRAP_ENTRY(type, label) \
	mov (type), %l3; b label; mov %psr, %l0; nop;

/* This is for hard interrupts from level 1-14, 15 is non-maskable (nmi) and
 * gets handled with another macro.
 */

#define TRAP_ENTRY_INTERRUPT(int_level) \
        mov int_level, %l4; b trap_entry; mov %psr, %l0; nop;

/* Here is the macro for soft interrupts (ie. not as urgent as hard ones)
   which need to jump to a different handler.
*/

#define TRAP_ENTRY_INTERRUPT_SOFT(int_level, ident) \
        mov int_level, %l4; mov %psr, %l0; b trap_entry; mov ident, %l3;

/* Non-maskable interrupt entry macro. You have to turn off all interrupts
   to not receive this. This is usually due to a asynchronous memory error.
   All we can really do is stop the show. :-(
*/
#define TRAP_ENTRY_INTERRUPT_NMI(t_type, jmp_to) \
        mov t_type, %l3; b jmp_to; mov %psr, %l0; nop;

/* Trap entry code in entry.S needs the offsets into task_struct
 * to get at the thread_struct goodies during window craziness.
 *
 * NOTE: We need to keep these values under 0x3ff in order to do
 *       efficient load/stores in the window fill/spill handlers.
 *       See TRAP_WIN_CLEAN in entry.S for details.
 */

#define THREAD_UWINDOWS 0x3a8
#define THREAD_WIM 0x3ac
#define THREAD_W_SAVED 0x3b0
#define THREAD_KSP 0x3b4
#define THREAD_USP 0x3b8
#define THREAD_REG_WINDOW 0x3c0

#endif __SPARC_HEAD_H
