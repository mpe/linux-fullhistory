#ifndef _ASM_IOW_H
#define _ASM_IOW_H
/* I added a few.  Please add to the distributed files.	-djb. */
/* This file is copied 1:1 from /linux/include/asm/io.h, and changed all
   al to ax, all inb to inw and all outb to outw (to get word in/out)
   the four inlines here should be added to the original, and
   then this one rm'd (and the #include "iow.h" in depca.c removed)... 
   Gruss PB 
*/
/*
 * Thanks to James van Artsdalen for a better timing-fix than
 * the two short jumps: using outb's to a nonexistent port seems
 * to guarantee better timings even on fast machines.
 *
 * On the other hand, I'd like to be sure of a non-existent port:
 * I feel a bit unsafe abou using 0x80.
 *
 *		Linus
 */

/* This is the more general version of outw.. */
extern inline void __outw(unsigned short value, unsigned short port)
{
__asm__ __volatile__ ("outw %w0,%w1"
		: /* no outputs */
		:"a" (value),"d" (port));
}

/* this is used for constant port numbers < 256.. */
extern inline void __outwc(unsigned short value, unsigned short port)
{
__asm__ __volatile__ ("outw %w0,%1"
		: /* no outputs */
		:"a" (value),"i" (port));
}

/* general version of inw */
extern inline unsigned int __inw(unsigned short port)
{
	unsigned int _v;
__asm__ __volatile__ ("inw %w1,%w0"
		:"=a" (_v):"d" (port),"0" (0));
	return _v;
}

/* inw with constant port nr 0-255 */
extern inline unsigned int __inwc(unsigned short port)
{
	unsigned int _v;
__asm__ __volatile__ ("inw %1,%w0"
		:"=a" (_v):"i" (port),"0" (0));
	return _v;
}

extern inline void __outw_p(unsigned short value, unsigned short port)
{
__asm__ __volatile__ ("outw %w0,%w1"
		: /* no outputs */
		:"a" (value),"d" (port));
	SLOW_DOWN_IO;
}

extern inline void __outwc_p(unsigned short value, unsigned short port)
{
__asm__ __volatile__ ("outw %w0,%1"
		: /* no outputs */
		:"a" (value),"i" (port));
	SLOW_DOWN_IO;
}

extern inline unsigned int __inw_p(unsigned short port)
{
	unsigned int _v;
__asm__ __volatile__ ("inw %w1,%w0"
		:"=a" (_v):"d" (port),"0" (0));
	SLOW_DOWN_IO;
	return _v;
}

extern inline unsigned int __inwc_p(unsigned short port)
{
	unsigned int _v;
__asm__ __volatile__ ("inw %1,%w0"
		:"=a" (_v):"i" (port),"0" (0));
	SLOW_DOWN_IO;
	return _v;
}

/*
 * Note that due to the way __builtin_constant_p() works, you
 *  - can't use it inside a inlien function (it will never be true)
 *  - you don't have to worry about side effects within the __builtin..
 */
#define outw(val,port) \
((__builtin_constant_p((port)) && (port) < 256) ? \
	__outwc((val),(port)) : \
	__outw((val),(port)))

#define inw(port) \
((__builtin_constant_p((port)) && (port) < 256) ? \
	__inwc(port) : \
	__inw(port))

#define outw_p(val,port) \
((__builtin_constant_p((port)) && (port) < 256) ? \
	__outwc_p((val),(port)) : \
	__outw_p((val),(port)))

#define inw_p(port) \
((__builtin_constant_p((port)) && (port) < 256) ? \
	__inwc_p(port) : \
	__inw_p(port))

#endif

/* The word-wide I/O operations are more general, but require a halved
   count.  */
#define port_read(port,buf,nr) \
__asm__("cld;rep;insw": :"d" (port),"D" (buf),"c" (nr):"cx","di")
#define port_write(port,buf,nr) \
__asm__("cld;rep;outsw": :"d" (port),"S" (buf),"c" (nr):"cx","si")

#define port_read_b(port,buf,nr) \
__asm__("cld;rep;insb": :"d" (port),"D" (buf),"c" (nr):"cx","di")
#define port_write_b(port,buf,nr) \
__asm__("cld;rep;outsb": :"d" (port),"S" (buf),"c" (nr):"cx","si")
