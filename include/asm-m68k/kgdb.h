/*
 *  include/asm-m68k/kgdb.h
 *
 *  Copyright (C) 1996 Roman Hodek
 */

#ifndef __ASM_M68k_KGDB_H
#define __ASM_M68k_KGDB_H

/*
 * Structure to save all register values in, already in the order gdb wants
 * it. Note that the upper half of the SR field is recycled for the FORMAT and
 * VECTOR fields. Hope that doesn't confuse gdb... That upper half is ignored
 * on exiting the stub, so gdb can modify it as it likes.
 */

#define GDBREG_A6	14
#define GDBREG_A7	15
#define GDBREG_SP	15
#define GDBREG_SR	16
#define GDBREG_PC	17
#define GDBREG_FP0	18
#define GDBREG_FP7	25
#define GDBREG_FPCR	26
#define GDBREG_FPIAR 28

#define GDBOFFA_D6	(6*4)
#define GDBOFFA_A3	(11*4)

#define NUMREGSBYTES	180

#ifndef __ASSEMBLY__

struct gdb_regs {
    long		   regs[16];		/* d0-a7 */
    unsigned	   format :  4; 	/* frame format specifier */
    unsigned	   vector : 12; 	/* vector offset */
    unsigned short sr;				/* status register */
    unsigned long  pc;				/* program counter */
	unsigned long  fpregs[8*3];		/* fp0-fp7 */
	unsigned long  fpcntl[3];		/* fpcr, fpsr, fpiar */
};

extern struct gdb_regs kgdb_registers;
extern void kgdb_init( void );
struct frame;
extern asmlinkage void enter_kgdb( struct pt_regs *fp );

extern int kgdb_initialized;

/*
 * This function will generate a breakpoint exception.  It is used at the
 * beginning of a program to sync up with a debugger and can be used
 * otherwise as a quick means to stop program execution and "break" into
 * the debugger.
 */
extern inline void breakpoint( void )
{
	if (!kgdb_initialized)
		/* if kgdb not inited, do nothing */
		return;
	
	/* breakpoint instruction is TRAP #15 */
	__asm__ __volatile__ ( "trap #15" );
}

/*
 * This function will report a SIGABORT to gdb.
 */
extern inline void kgdb_abort( void )
{
	if (!kgdb_initialized)
		/* if kgdb not inited, do nothing */
		return;
	
	/* TRAP #14 is reported as SIGABORT */
	__asm__ __volatile__ ( "trap #14" );
}

#endif /* __ASSEMBLY__ */

#endif /* __ASM_M68k_KGDB_H */

