/*---------------------------------------------------------------------------+
 |  fpu_emu.h                                                                |
 |                                                                           |
 | Copyright (C) 1992,1993                                                   |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail apm233m@vaxc.cc.monash.edu.au    |
 |                                                                           |
 +---------------------------------------------------------------------------*/


#ifndef _FPU_EMU_H_
#define _FPU_EMU_H_

/*
 * Define DENORM_OPERAND to make the emulator detect denormals
 * and use the denormal flag of the status word. Note: this only
 * affects the flag and corresponding interrupt, the emulator
 * will always generate denormals and operate upon them as required.
 */
#define DENORM_OPERAND

/*
 * Define PECULIAR_486 to get a closer approximation to 80486 behaviour,
 * rather than behaviour which appears to be cleaner.
 * This is a matter of opinion: for all I know, the 80486 may simply
 * be complying with the IEEE spec. Maybe one day I'll get to see the
 * spec...
 */
#define PECULIAR_486

#ifdef __ASSEMBLER__
#include "fpu_asm.h"
#define	Const(x)	$##x
#else
#define	Const(x)	x
#endif

#define EXP_BIAS	Const(0)
#define EXP_OVER	Const(0x4000)    /* smallest invalid large exponent */
#define	EXP_UNDER	Const(-0x3fff)   /* largest invalid small exponent */

#define SIGN_POS	Const(0)
#define SIGN_NEG	Const(1)

/* Keep the order TW_Valid, TW_Zero, TW_Denormal */
#define TW_Valid	Const(0)	/* valid */
#define TW_Zero		Const(1)	/* zero */
/* The following fold to 2 (Special) in the Tag Word */
#define TW_Denormal     Const(4)        /* De-normal */
#define TW_Infinity	Const(5)	/* + or - infinity */
#define	TW_NaN		Const(6)	/* Not a Number */

#define TW_Empty	Const(7)	/* empty */

/* #define TW_FPU_Interrupt Const(0x80) */    /* Signals an interrupt */


#ifndef __ASSEMBLER__

#include <linux/math_emu.h>

#ifdef PARANOID
extern char emulating;
#  define RE_ENTRANT_CHECK_OFF emulating = 0;
#  define RE_ENTRANT_CHECK_ON emulating = 1;
#else
#  define RE_ENTRANT_CHECK_OFF
#  define RE_ENTRANT_CHECK_ON
#endif PARANOID

typedef void (*FUNC)(void);
typedef struct fpu_reg FPU_REG;

#define	st(x)	( regs[((top+x) &7 )] )

#define	STACK_OVERFLOW	(st_new_ptr = &st(-1), st_new_ptr->tag != TW_Empty)
#define	NOT_EMPTY(i)	(st(i).tag != TW_Empty)
#define	NOT_EMPTY_0	(FPU_st0_tag ^ TW_Empty)

extern unsigned char FPU_rm;

extern	char	FPU_st0_tag;
extern	FPU_REG	*FPU_st0_ptr;

extern void  *FPU_data_address;

extern  FPU_REG  FPU_loaded_data;

#define pop()	{ FPU_st0_ptr->tag = TW_Empty; top++; }

/* push() does not affect the tags */
#define push()	{ top--; FPU_st0_ptr = st_new_ptr; }


#define reg_move(x, y) { \
		 *(short *)&((y)->sign) = *(short *)&((x)->sign); \
		 *(long *)&((y)->exp) = *(long *)&((x)->exp); \
		 *(long long *)&((y)->sigl) = *(long long *)&((x)->sigl); }


/*----- Prototypes for functions written in assembler -----*/
/* extern "C" void reg_move(FPU_REG *a, FPU_REG *b); */

extern "C" void mul64(long long *a, long long *b, long long *result);
extern "C" void poly_div2(long long *x);
extern "C" void poly_div4(long long *x);
extern "C" void poly_div16(long long *x);
extern "C" void polynomial(unsigned accum[], unsigned x[],
		       unsigned short terms[][4], int n);
extern "C" void normalize(FPU_REG *x);
extern "C" void normalize_nuo(FPU_REG *x);
extern "C" void reg_div(FPU_REG *arg1, FPU_REG *arg2, FPU_REG *answ,
		    unsigned int control_w);
extern "C" void reg_u_sub(FPU_REG *arg1, FPU_REG *arg2, FPU_REG *answ,
		      unsigned int control_w);
extern "C" void reg_u_mul(FPU_REG *arg1, FPU_REG *arg2, FPU_REG *answ,
		      unsigned int control_w);
extern "C" void reg_u_div(FPU_REG *arg1, FPU_REG *arg2, FPU_REG *answ,
		      unsigned int control_w);
extern "C" void reg_u_add(FPU_REG *arg1, FPU_REG *arg2, FPU_REG *answ,
		      unsigned int control_w);
extern "C" void wm_sqrt(FPU_REG *n, unsigned int control_w);
extern "C" unsigned	shrx(void *l, unsigned x);
extern "C" unsigned	shrxs(void *v, unsigned x);
extern "C" unsigned long div_small(unsigned long long *x, unsigned long y);
extern "C" void round_reg(FPU_REG *arg, unsigned int extent,
		      unsigned int control_w);

#ifndef MAKING_PROTO
#include "fpu_proto.h"
#endif

#endif __ASSEMBLER__

#endif _FPU_EMU_H_
