#ifndef _ASM_IO_H
#define _ASM_IO_H

/*
 * Thanks to James van Artsdalen for a better timing-fix than
 * the two short jumps: using outb's to a nonexistent port seems
 * to guarantee better timings even on fast machines.
 *
 *		Linus
 */

extern void inline outb(char value, unsigned short port)
{
__asm__ __volatile__ ("outb %0,%1"
		::"a" ((char) value),"d" ((unsigned short) port));
}

extern void inline outb_p(char value, unsigned short port)
{
__asm__ __volatile__ ("outb %0,%1\n\t"
#ifdef REALLY_SLOW_IO
		  "outb %0,$0x80\n\t"
		  "outb %0,$0x80\n\t"
		  "outb %0,$0x80\n\t"
#endif
		  "outb %0,$0x80"
		::"a" ((char) value),"d" ((unsigned short) port));
}

extern unsigned char inline inb(unsigned short port)
{
	unsigned char _v;
__asm__ __volatile__ ("inb %1,%0"
		:"=a" (_v):"d" ((unsigned short) port));
	return _v;
}

extern unsigned char inline inb_p(unsigned short port)
{
	unsigned char _v;
__asm__ __volatile__ ("inb %1,%0\n\t"
#ifdef REALLY_SLOW_IO
		  "outb %0,$0x80\n\t"
		  "outb %0,$0x80\n\t"
		  "outb %0,$0x80\n\t"
#endif
		  "outb %0,$0x80"
		:"=a" (_v):"d" ((unsigned short) port));
	return _v;
}

#endif
