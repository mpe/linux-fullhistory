#ifndef _ASM_IO_H
#define _ASM_IO_H

/*
 * Thanks to James van Artsdalen for a better timing-fix than
 * the two short jumps: using outb's to a nonexistent port seems
 * to guarantee better timings even on fast machines.
 *
 * On the other hand, I'd like to be sure of a non-existent port:
 * I feel a bit unsafe about using 0x80.
 *
 *		Linus
 */

#ifdef SLOW_IO_BY_JUMPING
#define __SLOW_DOWN_IO __asm__ __volatile__("jmp 1f\n1:\tjmp 1f\n1:")
#else
#define __SLOW_DOWN_IO __asm__ __volatile__("outb %al,$0x80")
#endif

#ifdef REALLY_SLOW_IO
#define SLOW_DOWN_IO { __SLOW_DOWN_IO; __SLOW_DOWN_IO; __SLOW_DOWN_IO; __SLOW_DOWN_IO; }
#else
#define SLOW_DOWN_IO __SLOW_DOWN_IO
#endif

/* This is the more general version of outb.. */
extern inline void __outb(unsigned char value, unsigned short port)
{
__asm__ __volatile__ ("outb %b0,%w1"
		: /* no outputs */
		:"a" (value),"d" (port));
}

/* this is used for constant port numbers < 256.. */
extern inline void __outbc(unsigned char value, unsigned short port)
{
__asm__ __volatile__ ("outb %b0,%1"
		: /* no outputs */
		:"a" (value),"i" (port));
}

/* general version of inb */
extern inline unsigned int __inb(unsigned short port)
{
	unsigned int _v;
__asm__ __volatile__ ("inb %w1,%b0"
		:"=a" (_v):"d" (port),"0" (0));
	return _v;
}

/* inb with constant port nr 0-255 */
extern inline unsigned int __inbc(unsigned short port)
{
	unsigned int _v;
__asm__ __volatile__ ("inb %1,%b0"
		:"=a" (_v):"i" (port),"0" (0));
	return _v;
}

extern inline void __outb_p(unsigned char value, unsigned short port)
{
__asm__ __volatile__ ("outb %b0,%w1"
		: /* no outputs */
		:"a" (value),"d" (port));
	SLOW_DOWN_IO;
}

extern inline void __outbc_p(unsigned char value, unsigned short port)
{
__asm__ __volatile__ ("outb %b0,%1"
		: /* no outputs */
		:"a" (value),"i" (port));
	SLOW_DOWN_IO;
}

extern inline unsigned int __inb_p(unsigned short port)
{
	unsigned int _v;
__asm__ __volatile__ ("inb %w1,%b0"
		:"=a" (_v):"d" (port),"0" (0));
	SLOW_DOWN_IO;
	return _v;
}

extern inline unsigned int __inbc_p(unsigned short port)
{
	unsigned int _v;
__asm__ __volatile__ ("inb %1,%b0"
		:"=a" (_v):"i" (port),"0" (0));
	SLOW_DOWN_IO;
	return _v;
}

/*
 * Note that due to the way __builtin_constant_p() works, you
 *  - can't use it inside a inline function (it will never be true)
 *  - you don't have to worry about side effects within the __builtin..
 */
#define outb(val,port) \
((__builtin_constant_p((port)) && (port) < 256) ? \
	__outbc((val),(port)) : \
	__outb((val),(port)))

#define inb(port) \
((__builtin_constant_p((port)) && (port) < 256) ? \
	__inbc(port) : \
	__inb(port))

#define outb_p(val,port) \
((__builtin_constant_p((port)) && (port) < 256) ? \
	__outbc_p((val),(port)) : \
	__outb_p((val),(port)))

#define inb_p(port) \
((__builtin_constant_p((port)) && (port) < 256) ? \
	__inbc_p(port) : \
	__inb_p(port))

#endif
