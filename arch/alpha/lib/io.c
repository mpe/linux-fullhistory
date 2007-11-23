/*
 * Alpha IO and memory functions.. Just expand the inlines in the header
 * files..
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/io.h>

unsigned int _inb(unsigned long addr)
{
	return __inb(addr);
}

unsigned int _inw(unsigned long addr)
{
	return __inw(addr);
}

unsigned int _inl(unsigned long addr)
{
	return __inl(addr);
}


void _outb(unsigned char b, unsigned long addr)
{
	__outb(b, addr);
}

void _outw(unsigned short b, unsigned long addr)
{
	__outw(b, addr);
}

void _outl(unsigned int b, unsigned long addr)
{
	__outl(b, addr);
}


unsigned long _readb(unsigned long addr)
{
	return __readb(addr);
}

unsigned long _readw(unsigned long addr)
{
	return __readw(addr);
}

unsigned long _readl(unsigned long addr)
{
	return __readl(addr);
}

unsigned long _readq(unsigned long addr)
{
	return __readq(addr);
}

void _writeb(unsigned char b, unsigned long addr)
{
	__writeb(b, addr);
}

void _writew(unsigned short b, unsigned long addr)
{
	__writew(b, addr);
}

void _writel(unsigned int b, unsigned long addr)
{
	__writel(b, addr);
}

void _writeq(unsigned long b, unsigned long addr)
{
	__writeq(b, addr);
}

/*
 * Read COUNT 8-bit bytes from port PORT into memory starting at
 * SRC.
 */
void insb (unsigned long port, void *dst, unsigned long count)
{
	while (((unsigned long)dst) & 0x3) {
		if (!count)
			return;
		count--;
		*(unsigned char *) dst = inb(port);
		((unsigned char *) dst)++;
	}

	while (count >= 4) {
		unsigned int w;
		count -= 4;
		w = inb(port);
		w |= inb(port) << 8;
		w |= inb(port) << 16;
		w |= inb(port) << 24;
		*(unsigned int *) dst = w;
		((unsigned int *) dst)++;
	}

	while (count) {
		--count;
		*(unsigned char *) dst = inb(port);
		((unsigned char *) dst)++;
	}
}


/*
 * Read COUNT 16-bit words from port PORT into memory starting at
 * SRC.  SRC must be at least short aligned.  This is used by the
 * IDE driver to read disk sectors.  Performance is important, but
 * the interfaces seems to be slow: just using the inlined version
 * of the inw() breaks things.
 */
void insw (unsigned long port, void *dst, unsigned long count)
{
	if (((unsigned long)dst) & 0x3) {
		if (((unsigned long)dst) & 0x1) {
			panic("insw: memory not short aligned");
		}
		if (!count)
			return;
		count--;
		*(unsigned short* ) dst = inw(port);
		((unsigned short *) dst)++;
	}

	while (count >= 2) {
		unsigned int w;
		count -= 2;
		w = inw(port);
		w |= inw(port) << 16;
		*(unsigned int *) dst = w;
		((unsigned int *) dst)++;
	}

	if (count) {
		*(unsigned short*) dst = inw(port);
	}
}


/*
 * Read COUNT 32-bit words from port PORT into memory starting at
 * SRC. Now works with any alignment in SRC. Performance is important,
 * but the interfaces seems to be slow: just using the inlined version
 * of the inl() breaks things.
 */
void insl (unsigned long port, void *dst, unsigned long count)
{
	unsigned int l = 0, l2;
	
	if (!count)
		return;
	
	switch (((unsigned long) dst) & 0x3)
	{
	 case 0x00:			/* Buffer 32-bit aligned */
		while (count--)
		{
			*(unsigned int *) dst = inl(port);
			((unsigned int *) dst)++;
		}
		break;
	
	/* Assuming little endian Alphas in cases 0x01 -- 0x03 ... */
	
	 case 0x02:			/* Buffer 16-bit aligned */
		--count;
		
		l = inl(port);
		*(unsigned short *) dst = l;
		((unsigned short *) dst)++;
		
		while (count--)
		{
			l2 = inl(port);
			*(unsigned int *) dst = l >> 16 | l2 << 16;
			((unsigned int *) dst)++;
			l = l2;
		}
		*(unsigned short *) dst = l >> 16;
		break;
	 case 0x01:			/* Buffer 8-bit aligned */
		--count;
		
		l = inl(port);
		*(unsigned char *) dst = l;
		((unsigned char *) dst)++;
		*(unsigned short *) dst = l >> 8;
		((unsigned short *) dst)++;
		while (count--)
		{
			l2 = inl(port);
			*(unsigned int *) dst = l >> 24 | l2 << 8;
			((unsigned int *) dst)++;
			l = l2;
		}
		*(unsigned char *) dst = l >> 24;
		break;
	 case 0x03:			/* Buffer 8-bit aligned */
		--count;
		
		l = inl(port);
		*(unsigned char *) dst = l;
		((unsigned char *) dst)++;
		while (count--)
		{
			l2 = inl(port);
			*(unsigned int *) dst = l << 24 | l2 >> 8;
			((unsigned int *) dst)++;
			l = l2;
		}
		*(unsigned short *) dst = l >> 8;
		((unsigned short *) dst)++;
		*(unsigned char *) dst = l >> 24;
		break;
	}
}


/*
 * Like insb but in the opposite direction.
 * Don't worry as much about doing aligned memory transfers:
 * doing byte reads the "slow" way isn't nearly as slow as
 * doing byte writes the slow way (no r-m-w cycle).
 */
void outsb(unsigned long port, const void * src, unsigned long count)
{
	while (count) {
		count--;
		outb(*(char *)src, port);
		((char *) src)++;
	}
}

/*
 * Like insw but in the opposite direction.  This is used by the IDE
 * driver to write disk sectors.  Performance is important, but the
 * interfaces seems to be slow: just using the inlined version of the
 * outw() breaks things.
 */
void outsw (unsigned long port, const void *src, unsigned long count)
{
	if (((unsigned long)src) & 0x3) {
		if (((unsigned long)src) & 0x1) {
			panic("outsw: memory not short aligned");
		}
		outw(*(unsigned short*)src, port);
		((unsigned short *) src)++;
		--count;
	}

	while (count >= 2) {
		unsigned int w;
		count -= 2;
		w = *(unsigned int *) src;
		((unsigned int *) src)++;
		outw(w >>  0, port);
		outw(w >> 16, port);
	}

	if (count) {
		outw(*(unsigned short *) src, port);
	}
}


/*
 * Like insl but in the opposite direction.  This is used by the IDE
 * driver to write disk sectors.  Works with any alignment in SRC.
 *  Performance is important, but the interfaces seems to be slow:
 * just using the inlined version of the outl() breaks things.
 */
void outsl (unsigned long port, const void *src, unsigned long count)
{
	unsigned int l = 0, l2;
	
	if (!count)
		return;
	
	switch (((unsigned long) src) & 0x3)
	{
	 case 0x00:			/* Buffer 32-bit aligned */
		while (count--)
		{
			outl(*(unsigned int *) src, port);
			((unsigned int *) src)++;
		}
		break;
	
	/* Assuming little endian Alphas in cases 0x01 -- 0x03 ... */
	
	 case 0x02:			/* Buffer 16-bit aligned */
		--count;
		
		l = *(unsigned short *) src << 16;
		((unsigned short *) src)++;
		
		while (count--)
		{
			l2 = *(unsigned int *) src;
			((unsigned int *) src)++;
			outl (l >> 16 | l2 << 16, port);
			l = l2;
		}
		l2 = *(unsigned short *) src;
		outl (l >> 16 | l2 << 16, port);
		break;
	 case 0x01:			/* Buffer 8-bit aligned */
		--count;
		
		l  = *(unsigned char *) src << 8;
		((unsigned char *) src)++;
		l |= *(unsigned short *) src << 16;
		((unsigned short *) src)++;
		while (count--)
		{
			l2 = *(unsigned int *) src;
			((unsigned int *) src)++;
			outl (l >> 8 | l2 << 24, port);
			l = l2;
		}
		l2 = *(unsigned char *) src;
		outl (l >> 8 | l2 << 24, port);
		break;
	 case 0x03:			/* Buffer 8-bit aligned */
		--count;
		
		l  = *(unsigned char *) src << 24;
		((unsigned char *) src)++;
		while (count--)
		{
			l2 = *(unsigned int *) src;
			((unsigned int *) src)++;
			outl (l >> 24 | l2 << 8, port);
			l = l2;
		}
		l2  = *(unsigned short *) src;
		((unsigned short *) src)++;
		l2 |= *(unsigned char *) src << 16;
		outl (l >> 24 | l2 << 8, port);
		break;
	}
}


/*
 * Copy data from IO memory space to "real" memory space.
 * This needs to be optimized.
 */
void _memcpy_fromio(void * to, unsigned long from, long count)
{
	/* Optimize co-aligned transfers.  Everything else gets handled
	   a byte at a time. */

	if (count >= 8 && ((long)to & 7) == (from & 7)) {
		count -= 8;
		do {
			*(u64 *)to = readq(from);
			count -= 8;
			to += 8;
			from += 8;
		} while (count >= 0);
		count += 8;
	}

	if (count >= 4 && ((long)to & 3) == (from & 3)) {
		count -= 4;
		do {
			*(u32 *)to = readl(from);
			count -= 4;
			to += 4;
			from += 4;
		} while (count >= 0);
		count += 4;
	}
		
	if (count >= 2 && ((long)to & 1) == (from & 1)) {
		count -= 2;
		do {
			*(u16 *)to = readw(from);
			count -= 2;
			to += 2;
			from += 2;
		} while (count >= 0);
		count += 2;
	}

	while (count > 0) {
		*(u8 *) to = readb(from);
		count--;
		to++;
		from++;
	}
}

/*
 * Copy data from "real" memory space to IO memory space.
 * This needs to be optimized.
 */
void _memcpy_toio(unsigned long to, const void * from, long count)
{
	/* Optimize co-aligned transfers.  Everything else gets handled
	   a byte at a time. */
	/* FIXME -- align FROM.  */

	if (count >= 8 && (to & 7) == ((long)from & 7)) {
		count -= 8;
		do {
			writeq(*(const u64 *)from, to);
			count -= 8;
			to += 8;
			from += 8;
		} while (count >= 0);
		count += 8;
	}

	if (count >= 4 && (to & 3) == ((long)from & 3)) {
		count -= 4;
		do {
			writel(*(const u32 *)from, to);
			count -= 4;
			to += 4;
			from += 4;
		} while (count >= 0);
		count += 4;
	}
		
	if (count >= 2 && (to & 1) == ((long)from & 1)) {
		count -= 2;
		do {
			writew(*(const u16 *)from, to);
			count -= 2;
			to += 2;
			from += 2;
		} while (count >= 0);
		count += 2;
	}

	while (count > 0) {
		writeb(*(const u8 *) from, to);
		count--;
		to++;
		from++;
	}
}

/*
 * "memset" on IO memory space.
 */
void _memset_c_io(unsigned long to, unsigned long c, long count)
{
	/* Handle any initial odd byte */
	if (count > 0 && (to & 1)) {
		writeb(c, to);
		to++;
		count--;
	}

	/* Handle any initial odd halfword */
	if (count >= 2 && (to & 2)) {
		writew(c, to);
		to += 2;
		count -= 2;
	}

	/* Handle any initial odd word */
	if (count >= 4 && (to & 4)) {
		writel(c, to);
		to += 4;
		count -= 4;
	}

	/* Handle all full-sized quadwords: we're aligned
	   (or have a small count) */
	count -= 8;
	if (count >= 0) {
		do {
			writeq(c, to);
			to += 8;
			count -= 8;
		} while (count >= 0);
	}
	count += 8;

	/* The tail is word-aligned if we still have count >= 4 */
	if (count >= 4) {
		writel(c, to);
		to += 4;
		count -= 4;
	}

	/* The tail is half-word aligned if we have count >= 2 */
	if (count >= 2) {
		writew(c, to);
		to += 2;
		count -= 2;
	}

	/* And finally, one last byte.. */
	if (count) {
		writeb(c, to);
	}
}
