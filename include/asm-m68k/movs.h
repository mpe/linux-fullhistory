#ifndef  __MOVS_H__
#define __MOVS_H__

/*
** movs.h
**
** Inline assembly macros to generate movs & related instructions
*/

/* Set DFC register value */

#define SET_DFC(x) \
        __asm__ __volatile__ ("movec %0,%%dfc" : : "r" (x))

/* Get DFC register value */

#define GET_DFC(x) \
        __asm__ __volatile__ ("movec %%dfc,%0" : "=r" (x))

/* Set SFC register value */

#define SET_SFC(x) \
        __asm__ __volatile__ ("movec %0,%%sfc" : : "r" (x))

/* Get SFC register value */

#define GET_SFC(x) \
        __asm__ __volatile__ ("movec %%sfc,%0" : "=r" (x))

#define SET_VBR(x) \
        __asm__ __volatile__ ("movec %0,%%vbr" : : "r" (x))

#define GET_VBR(x) \
        __asm__ __volatile__ ("movec %%vbr,%0" : "=r" (x))

/* Set a byte using the "moves" instruction */

#define SET_CONTROL_BYTE(addr,value) \
        __asm__ __volatile__ ("movesb %1,%0" : "=m" (addr) : "d" (value))

/* Get a byte using the "moves" instruction */

#define GET_CONTROL_BYTE(addr,value) \
        __asm__ __volatile__ ("movesb %1,%0" : "=d" (value) : "m" (addr))

/* Set a (long)word using the "moves" instruction */

#define SET_CONTROL_WORD(addr,value) \
        __asm__ __volatile__ ("movesl %1,%0" : "=m" (addr) : "r" (value))

/* Get a (long)word using the "moves" instruction */

#define GET_CONTROL_WORD(addr,value) \
        __asm__ __volatile__ ("movesl %1,%0" : "=d" (value) : "m" (addr))
#endif
