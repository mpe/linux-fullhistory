#ifndef __M68K_ENTRY_H
#define __M68K_ENTRY_H

#include <linux/config.h>
#include <asm/setup.h>
#ifdef CONFIG_KGDB
#include <asm/kgdb.h>
#endif

/*
 * Stack layout in 'ret_from_exception':
 *
 *	This allows access to the syscall arguments in registers d1-d5
 *
 *	 0(sp) - d1
 *	 4(sp) - d2
 *	 8(sp) - d3
 *	 C(sp) - d4
 *	10(sp) - d5
 *	14(sp) - a0
 *	18(sp) - a1
 *	1C(sp) - a2
 *	20(sp) - d0
 *	24(sp) - orig_d0
 *	28(sp) - stack adjustment
 *	2C(sp) - sr
 *	2E(sp) - pc
 *	32(sp) - format & vector
 */

/*
 * 97/05/14 Andreas: Register %a2 is now set to the current task throughout
 *		     the whole kernel.
 */

#ifdef __ASSEMBLY__

#define curptr a2

/*
 * these are offsets into the task-struct
 */
LTASK_STATE	 =  0
LTASK_FLAGS	 =  4
LTASK_SIGPENDING =  8
LTASK_ADDRLIMIT	 = 12
LTASK_EXECDOMAIN = 16

LTSS_KSP	= 0
LTSS_USP	= 4
LTSS_SR		= 8
LTSS_FS		= 10
LTSS_CRP	= 12
LTSS_FPCTXT	= 24

/* the following macro is used when enabling interrupts */
#if defined(CONFIG_ATARI_ONLY) && !defined(CONFIG_HADES)
	/* block out HSYNC on the atari */
#define ALLOWINT 0xfbff
#define	MAX_NOINT_IPL	3
#else
	/* portable version */
#define ALLOWINT 0xf8ff
#define	MAX_NOINT_IPL	0
#endif /* machine compilation types */ 

LPT_OFF_D0	  = 0x20
LPT_OFF_ORIG_D0	  = 0x24
LPT_OFF_SR	  = 0x2C
LPT_OFF_FORMATVEC = 0x32

LFLUSH_I_AND_D = 0x00000808
LENOSYS = 38
LSIGTRAP = 5

LPF_TRACESYS_OFF = 3
LPF_TRACESYS_BIT = 5
LPF_PTRACED_OFF = 3
LPF_PTRACED_BIT = 4
LPF_DTRACE_OFF = 1
LPF_DTRACE_BIT = 5

/*
 * This defines the normal kernel pt-regs layout.
 *
 * regs a3-a6 and d6-d7 are preserved by C code
 * the kernel doesn't mess with usp unless it needs to
 */
#ifndef CONFIG_KGDB
/*
 * a -1 in the orig_d0 field signifies
 * that the stack frame is NOT for syscall
 */
#define SAVE_ALL_INT				\
	clrl	%sp@-;		/* stk_adj */	\
	pea	-1:w;		/* orig d0 */	\
	movel	%d0,%sp@-;	/* d0 */	\
	moveml	%d1-%d5/%a0-%a1/%curptr,%sp@-

#define SAVE_ALL_SYS				\
	clrl	%sp@-;		/* stk_adj */	\
	movel	%d0,%sp@-;	/* orig d0 */	\
	movel	%d0,%sp@-;	/* d0 */	\
	moveml  %d1-%d5/%a0-%a1/%curptr,%sp@-
#else
/* Need to save the "missing" registers for kgdb...
 */
#define SAVE_ALL_INT					\
	clrl	%sp@-;		/* stk_adj */		\
	pea	-1:w;		/* orig d0 */		\
	movel	%d0,%sp@-;	/* d0 */		\
	moveml	%d1-%d5/%a0-%a1/%curptr,%sp@-;		\
	moveml	%d6-%d7,kgdb_registers+GDBOFFA_D6;	\
	moveml	%a3-%a6,kgdb_registers+GDBOFFA_A3

#define SAVE_ALL_SYS					\
	clrl	%sp@-;		/* stk_adj */		\
	movel	%d0,%sp@-;	/* orig d0 */		\
	movel	%d0,%sp@-;	/* d0 */		\
	moveml	%d1-%d5/%a0-%a1/%curptr,%sp@-;		\
	moveml	%d6-%d7,kgdb_registers+GDBOFFA_D6;	\
	moveml	%a3-%a6,kgdb_registers+GDBOFFA_A3
#endif

#define RESTORE_ALL			\
	moveml	%sp@+,%a0-%a1/%curptr/%d1-%d5;	\
	movel	%sp@+,%d0;		\
	addql	#4,%sp;	 /* orig d0 */	\
	addl	%sp@+,%sp; /* stk adj */	\
	rte

#define SWITCH_STACK_SIZE (6*4+4)	/* includes return address */

#define SAVE_SWITCH_STACK \
	moveml	%a3-%a6/%d6-%d7,%sp@-

#define RESTORE_SWITCH_STACK \
	moveml	%sp@+,%a3-%a6/%d6-%d7

#define GET_CURRENT(tmp) \
	movel	%sp,tmp; \
	andw	&-8192,tmp; \
	movel	tmp,%curptr;

#else /* C source */

#define STR(X) STR1(X)
#define STR1(X) #X

#define PT_OFF_ORIG_D0	 0x24
#define PT_OFF_FORMATVEC 0x32
#define PT_OFF_SR	 0x2C
#ifndef CONFIG_KGDB
#define SAVE_ALL_INT				\
	"clrl	%%sp@-;"    /* stk_adj */	\
	"pea	-1:w;"	    /* orig d0 = -1 */	\
	"movel	%%d0,%%sp@-;" /* d0 */		\
	"moveml	%%d1-%%d5/%%a0-%%a2,%%sp@-"
#else
#define SAVE_ALL_INT				\
	"clrl	%%sp@-\n\t" /* stk_adj */	\
	"pea	-1:w\n\t"   /* orig d0 = -1 */	\
	"movel	%%d0,%%sp@-\n\t" /* d0 */	\
	"moveml	%%d1-%%d5/%%a0-%%a2,%%sp@-\n\t"	\
	"moveml	%%d6-%%d7,kgdb_registers+"STR(GDBOFFA_D6)"\n\t" \
	"moveml	%%a3-%%a6,kgdb_registers+"STR(GDBOFFA_A3)
#endif
#define GET_CURRENT(tmp) \
	"movel	%%sp,"#tmp"\n\t" \
	"andw	#-8192,"#tmp"\n\t" \
	"movel	"#tmp",%%a2"

#endif

#endif /* __M68K_ENTRY_H */
