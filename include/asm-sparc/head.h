#ifndef __SPARC_HEAD_H
#define __SPARC_HEAD_H

#define KERNBASE        0xf0000000  /* First address the kernel will eventually be */
#define LOAD_ADDR       0x4000      /* prom jumps to us here unless this is elf /boot */
#define C_STACK         96
#define SUN4C_SEGSZ     (1 << 18)
#define SRMMU_L1_KBASE_OFFSET ((KERNBASE>>24)<<2)  /* Used in boot remapping. */
#define INTS_ENAB   0x01            /* entry.S uses this. */

#define NCPUS           4            /* Architectual limit of sun4m. */

#define  SUN4_PROM_VECTOR   0xFFE81000    /* To safely die on a SUN4 */
#define  SUN4_PRINTF   0x84               /* Offset into SUN4_PROM_VECTOR */

#define WRITE_PAUSE     nop; nop; nop;

#define NOP_INSN        0x01000000        /* Used to patch sparc_save_state */

/* Here are some trap goodies */

/* Generic trap entry. */
#define TRAP_ENTRY(type, label) \
	mov (type), %l3; b label; rd %psr, %l0; nop;

/* Notice that for the system calls we pull a trick.  We load up a
 * different pointer to the system call vector table in %l7, but call
 * the same generic system call low-level entry point.  The trap table
 * entry sequences are also HyperSparc pipeline friendly ;-)
 */

/* Software trap for Linux system calls. */
#define LINUX_SYSCALL_TRAP \
        sethi %hi(C_LABEL(sys_call_table)), %l7; or %l7, %lo(C_LABEL(sys_call_table)), %l7; b linux_sparc_syscall; mov %psr, %l0;

/* Software trap for SunOS4.1.x system calls. */
#define SUNOS_SYSCALL_TRAP \
        sethi %hi(C_LABEL(sys_call_table)), %l7; or %l7, %lo(C_LABEL(sys_call_table)), %l7; b linux_sparc_syscall; mov %psr, %l0;

/* Software trap for Slowaris system calls. */
#define SOLARIS_SYSCALL_TRAP \
        sethi %hi(C_LABEL(sys_call_table)), %l7; or %l7, %lo(C_LABEL(sys_call_table)), %l7; b linux_sparc_syscall; mov %psr, %l0;

/* Software trap for Sparc-netbsd system calls. */
#define NETBSD_SYSCALL_TRAP \
        sethi %hi(C_LABEL(sys_call_table)), %l7; or %l7, %lo(C_LABEL(sys_call_table)), %l7; b linux_sparc_syscall; mov %psr, %l0;

/* The Get Condition Codes software trap for userland. */
#define GETCC_TRAP \
        b getcc_trap_handler; mov %psr, %l0; nop; nop

/* The Set Condition Codes software trap for userland. */
#define SETCC_TRAP \
        b setcc_trap_handler; mov %psr, %l0; nop; nop

/* This is for hard interrupts from level 1-14, 15 is non-maskable (nmi) and
 * gets handled with another macro.
 */
#define TRAP_ENTRY_INTERRUPT(int_level) \
        mov int_level, %l3; b real_irq_entry; rd %psr, %l0; nop;

/* NMI's (Non Maskable Interrupts) are special, you can't keep them
 * from coming in, and basically if you get one, the shows over. ;(
 */
#define NMI_TRAP \
        b linux_trap_nmi; mov %psr, %l0; nop; nop

/* The above two macros are for generic traps. The following is made
 * especially for timer interrupts at IRQ level 14.
 */
#define TRAP_ENTRY_TIMER \
        rd %psr, %l0; b sparc_timer; nop; nop;

/* Trap entry code in entry.S needs the offsets into task_struct
 * to get at the thread_struct goodies during window craziness.
 *
 * NOTE: We need to keep these values under 0x3ff in order to do
 *       efficient load/stores in the window fill/spill handlers.
 *       See TRAP_WIN_CLEAN in entry.S for details.
 */

/* First generic task_struct offsets */
#define TASK_STATE        0x000
#define TASK_PRI          0x008
#define TASK_KSTACK_PG    0x250

#define THREAD_UWINDOWS   0x3b8
#define THREAD_WIM        0x3bc
#define THREAD_W_SAVED    0x3c0
#define THREAD_KSP        0x3c4
#define THREAD_USP        0x3c8
#define THREAD_PSR        0x3cc
#define THREAD_PC         0x3d0
#define THREAD_NPC        0x3d4
#define THREAD_Y          0x3d8
#define THREAD_REG_WINDOW 0x3e0

/* More fun offset macros. These are for pt_regs. */

#define PT_PSR    0x0
#define PT_PC     0x4
#define PT_NPC    0x8
#define PT_Y      0xc
#define PT_G0     0x10
#define PT_G1     0x14
#define PT_G2     0x18
#define PT_G3     0x1c
#define PT_G4     0x20
#define PT_G5     0x24
#define PT_G6     0x28
#define PT_G7     0x2c
#define PT_I0     0x30
#define PT_I1     0x34
#define PT_I2     0x38
#define PT_I3     0x3c
#define PT_I4     0x40
#define PT_I5     0x44
#define PT_I6     0x48
#define PT_I7     0x4c

#endif __SPARC_HEAD_H
