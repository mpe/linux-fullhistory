/*
 * Alpha IO and memory functions.. Just expand the inlines in the header
 * files..
 */
#include <linux/kernel.h>

#include <asm/io.h>

/* 
 * Jensen has a separate "local" and "bus" IO space for
 * byte-wide IO.
 */
#ifdef __is_local

unsigned int _bus_inb(unsigned long addr)
{
	return __bus_inb(addr);
}

void _bus_outb(unsigned char b, unsigned long addr)
{
	__bus_outb(b, addr);
}
#endif

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
 * SRC.  SRC must be at least word aligned.  This is used by the
 * IDE driver to read disk sectors.  Performance is important, but
 * the interfaces seems to be slow: just using the inlined version
 * of the inw() breaks things.
 */
void insl (unsigned long port, void *dst, unsigned long count)
{
	if (((unsigned long)dst) & 0x3) {
		panic("insl: memory not aligned");
	}

	while (count) {
		--count;
		*(unsigned int *) dst = inl(port);
		((unsigned int *) dst)++;
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
 * driver to write disk sectors.  Performance is important, but the
 * interfaces seems to be slow: just using the inlined version of the
 * outw() breaks things.
 */
void outsl (unsigned long port, const void *src, unsigned long count)
{
	if (((unsigned long)src) & 0x3) {
		panic("outsw: memory not aligned");
	}

	while (count) {
		--count;
		outl(*(unsigned int *) src, port);
		((unsigned int *) src)++;
	}
}


/*
 * Copy data from IO memory space to "real" memory space.
 * This needs to be optimized.
 */
void _memcpy_fromio(void * to, unsigned long from, unsigned long count)
{
	while (count) {
		count--;
		*(char *) to = readb(from);
		((char *) to)++;
		from++;
	}
}

/*
 * Copy data from "real" memory space to IO memory space.
 * This needs to be optimized.
 */
void _memcpy_toio(unsigned long to, void * from, unsigned long count)
{
	while (count) {
		count--;
		writeb(*(char *) from, to);
		((char *) from)++;
		to++;
	}
}

/*
 * "memset" on IO memory space.
 * This needs to be optimized.
 */
void _memset_io(unsigned long dst, int c, unsigned long count)
{
	while (count) {
		count--;
		writeb(c, dst);
		dst++;
	}
}
