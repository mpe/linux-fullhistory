#ifndef __ASM_MIPS_IO_H
#define __ASM_MIPS_IO_H

#include <asm/mipsregs.h>
#include <asm/mipsconfig.h>

/*
 * This file contains the definitions for the MIPS counterpart of the
 * x86 in/out instructions. This heap of macros and C results in much
 * better code than the approach of doing it in plain C, though that's
 * probably not needed.
 *
 *   Ralf
 *
 * This file contains the definitions for the x86 IO instructions
 * inb/inw/inl/outb/outw/outl and the "string versions" of the same
 * (insb/insw/insl/outsb/outsw/outsl). You can also use "pausing"
 * versions of the single-IO instructions (inb_p/inw_p/..).
 *
 * This file is not meant to be obfuscating: it's just complicated
 * to (a) handle it all in a way that makes gcc able to optimize it
 * as well as possible and (b) trying to avoid writing the same thing
 * over and over again with slight variations and possibly making a
 * mistake somewhere.
 */

/*
 * Thanks to James van Artsdalen for a better timing-fix than
 * the two short jumps: using outb's to a nonexistent port seems
 * to guarantee better timings even on fast machines.
 *
 * On the other hand, I'd like to be sure of a non-existent port:
 * I feel a bit unsafe about using 0x80 (should be safe, though)
 *
 *		Linus
 */

#define __SLOW_DOWN_IO \
	__asm__ __volatile__( \
		"sb\t$0,0x80(%0)" \
		: : "r" (PORT_BASE));

#ifdef REALLY_SLOW_IO
#define SLOW_DOWN_IO { __SLOW_DOWN_IO; __SLOW_DOWN_IO; __SLOW_DOWN_IO; __SLOW_DOWN_IO; }
#else
#define SLOW_DOWN_IO __SLOW_DOWN_IO
#endif

/*
 * Talk about misusing macros..
 */

#define __OUT1(s) \
extern inline void __out##s(unsigned int value, unsigned int port) {

#define __OUT2(m) \
__asm__ __volatile__ ("s" #m "\t%0,%1(%2)"

#define __OUT(m,s) \
__OUT1(s) __OUT2(m) : : "r" (value), "i" (0), "r" (PORT_BASE+port)); } \
__OUT1(s##c) __OUT2(m) : : "r" (value), "i" (port), "r" (PORT_BASE)); } \
__OUT1(s##_p) __OUT2(m) : : "r" (value), "i" (0), "r" (PORT_BASE+port)); \
	SLOW_DOWN_IO; } \
__OUT1(s##c_p) __OUT2(m) : : "r" (value), "i" (port), "r" (PORT_BASE)); \
	SLOW_DOWN_IO; }

#define __IN1(s) \
extern inline unsigned int __in##s(unsigned int port) { unsigned int _v;

#define __IN2(m) \
__asm__ __volatile__ ("l" #m "u\t%0,%1(%2)\n\t"

#define __IN(m,s) \
__IN1(s) __IN2(m) STR(FILL_LDS) : "=r" (_v) : "i" (0), "r" (PORT_BASE+port)); return _v; } \
__IN1(s##c) __IN2(m) STR(FILL_LDS) : "=r" (_v) : "i" (port), "r" (PORT_BASE)); return _v; } \
__IN1(s##_p) __IN2(m) : "=r" (_v) : "i" (0), "r" (PORT_BASE+port)); SLOW_DOWN_IO; return _v; } \
__IN1(s##c_p) __IN2(m) : "=r" (_v) : "i" (port), "r" (PORT_BASE)); SLOW_DOWN_IO; return _v; }

#define __INS1(s) \
extern inline void __ins##s(unsigned int port, void * addr, unsigned long count) {

#define __INS2(m) \
__asm__ __volatile__ ( \
	".set\tnoreorder\n\t" \
	".set\tnoat\n" \
	"1:\tl" #m "u\t$1,%4(%5)\n\t" \
	"subu\t%1,%1,1\n\t" \
	"s" #m "\t$1,(%0)\n\t" \
	"bne\t$0,%1,1b\n\t" \
	"addiu\t%0,%0,%6\n\t" \
	".set\tat\n\t" \
	".set\treorder"

#define __INS(m,s,i) \
__INS1(s) __INS2(m) \
	: "=r" (addr), "=r" (count) \
	: "0" (addr), "1" (count), "i" (0), "r" (PORT_BASE+port), "I" (i) \
	: "$1");} \
__INS1(s##c) __INS2(m) \
	: "=r" (addr), "=r" (count) \
	: "0" (addr), "1" (count), "i" (port), "r" (PORT_BASE), "I" (i) \
	: "$1");}

#define __OUTS1(s) \
extern inline void __outs##s(unsigned int port, const void * addr, unsigned long count) {

#define __OUTS2(m) \
__asm__ __volatile__ ( \
        ".set\tnoreorder\n\t" \
        ".set\tnoat\n" \
        "1:\tl" #m "u\t$1,(%0)\n\t" \
        "subu\t%1,%1,1\n\t" \
        "s" #m "\t$1,%4(%5)\n\t" \
        "bne\t$0,%1,1b\n\t" \
        "addiu\t%0,%0,%6\n\t" \
        ".set\tat\n\t" \
        ".set\treorder"

#define __OUTS(m,s,i) \
__OUTS1(s) __OUTS2(m) \
	: "=r" (addr), "=r" (count) \
	: "0" (addr), "1" (count), "i" (0), "r" (PORT_BASE+port), "I" (i) \
	: "$1");} \
__OUTS1(s##c) __OUTS2(m) \
	: "=r" (addr), "=r" (count) \
	: "0" (addr), "1" (count), "i" (port), "r" (PORT_BASE), "I" (i) \
	: "$1");}

__IN(b,b)
__IN(h,w)
__IN(w,l)

__OUT(b,b)
__OUT(h,w)
__OUT(w,l)

__INS(b,b,1)
__INS(h,w,2)
__INS(w,l,4)

__OUTS(b,b,1)
__OUTS(h,w,2)
__OUTS(w,l,4)

/*
 * Note that due to the way __builtin_constant_p() works, you
 *  - can't use it inside a inline function (it will never be true)
 *  - you don't have to worry about side effects within the __builtin..
 */
#define outb(val,port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__outbc((val),(port)) : \
	__outb((val),(port)))

#define inb(port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__inbc(port) : \
	__inb(port))

#define outb_p(val,port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__outbc_p((val),(port)) : \
	__outb_p((val),(port)))

#define inb_p(port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__inbc_p(port) : \
	__inb_p(port))

#define outw(val,port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__outwc((val),(port)) : \
	__outw((val),(port)))

#define inw(port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__inwc(port) : \
	__inw(port))

#define outw_p(val,port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__outwc_p((val),(port)) : \
	__outw_p((val),(port)))

#define inw_p(port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__inwc_p(port) : \
	__inw_p(port))

#define outl(val,port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__outlc((val),(port)) : \
	__outl((val),(port)))

#define inl(port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__inlc(port) : \
	__inl(port))

#define outl_p(val,port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__outlc_p((val),(port)) : \
	__outl_p((val),(port)))

#define inl_p(port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__inlc_p(port) : \
	__inl_p(port))


#define outsb(port,addr,count) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__outsbc((port),(addr),(count)) : \
	__outsb ((port),(addr),(count)))

#define insb(port,addr,count) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__insbc((port),(addr),(count)) : \
	__insb((port),(addr),(count)))

#define outsw(port,addr,count) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__outswc((port),(addr),(count)) : \
	__outsw ((port),(addr),(count)))

#define insw(port,addr,count) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__inswc((port),(addr),(count)) : \
	__insw((port),(addr),(count)))

#define outsl(port,addr,count) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__outslc((port),(addr),(count)) : \
	__outsl ((port),(addr),(count)))

#define insl(port,addr,count) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__inslc((port),(addr),(count)) : \
	__insl((port),(addr),(count)))

#endif /* __ASM_MIPS_IO_H */
