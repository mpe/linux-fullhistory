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
#undef __bus_inb
unsigned int __bus_inb(unsigned long addr)
{
	return ___bus_inb(addr);
}

#undef __bus_outb
void __bus_outb(unsigned char b, unsigned long addr)
{
	___bus_outb(b, addr);
}
#endif

#undef inb
unsigned int inb(unsigned long addr)
{
	return __inb(addr);
}

#undef inw
unsigned int inw(unsigned long addr)
{
	return __inw(addr);
}

#undef inl
unsigned int inl(unsigned long addr)
{
	return __inl(addr);
}


#undef outb
void outb(unsigned char b, unsigned long addr)
{
	__outb(b, addr);
}

#undef outw
void outw(unsigned short b, unsigned long addr)
{
	__outw(b, addr);
}

#undef outl
void outl(unsigned int b, unsigned long addr)
{
	__outl(b, addr);
}


#undef readb
unsigned long readb(unsigned long addr)
{
	return __readb(addr);
}

#undef readw
unsigned long readw(unsigned long addr)
{
	return __readw(addr);
}

#undef readl
unsigned long readl(unsigned long addr)
{
	return __readl(addr);
}


#undef writeb
void writeb(unsigned short b, unsigned long addr)
{
	__writeb(b, addr);
}

#undef writew
void writew(unsigned short b, unsigned long addr)
{
	__writew(b, addr);
}

#undef writel
void writel(unsigned int b, unsigned long addr)
{
	__writel(b, addr);
}

/*
 * Read COUNT 16-bit words from port PORT into memory starting at
 * SRC.  SRC must be at least short aligned.  This is used by the
 * IDE driver to read disk sectors.  Performance is important, but
 * the interfaces seems to be slow: just using the inlined version
 * of the inw() breaks things.
 */
#undef insw
void insw (unsigned long port, void *dst, unsigned long count)
{
	unsigned int *ip, w;

	if (((unsigned long)dst) & 0x3) {
		if (((unsigned long)dst) & 0x1) {
			panic("insw: memory not short aligned");
		}
		*(unsigned short*)dst = inw(port);
		dst += 2;
		--count;
	}

	ip = dst;
	while (count >= 2) {
		w  = inw(port);
		w |= inw(port) << 16;
		count -= 2;
		*ip++ = w;
	}

	if (count) {
		w = inw(port);
		*(unsigned short*)ip = w;
	}
}


/*
 * Read COUNT 32-bit words from port PORT into memory starting at
 * SRC.  SRC must be at least word aligned.  This is used by the
 * IDE driver to read disk sectors.  Performance is important, but
 * the interfaces seems to be slow: just using the inlined version
 * of the inw() breaks things.
 */
#undef insl
void insl (unsigned long port, void *dst, unsigned long count)
{
	unsigned int *ip, w;

	if (((unsigned long)dst) & 0x3) {
		panic("insl: memory not aligned");
	}

	ip = dst;
	while (count > 0) {
		w  = inw(port);
		--count;
		*ip++ = w;
	}
}


/*
 * Like insw but in the opposite direction.  This is used by the IDE
 * driver to write disk sectors.  Performance is important, but the
 * interfaces seems to be slow: just using the inlined version of the
 * outw() breaks things.
 */
#undef outsw
void outsw (unsigned long port, void *src, unsigned long count)
{
	unsigned int *ip, w;

	if (((unsigned long)src) & 0x3) {
		if (((unsigned long)src) & 0x1) {
			panic("outsw: memory not short aligned");
		}
		outw(*(unsigned short*)src, port);
		src += 2;
		--count;
	}

	ip = src;
	while (count >= 2) {
		w = *ip++;
		count -= 2;
		outw(w >>  0, port);
		outw(w >> 16, port);
	}

	if (count) {
		outw(*(unsigned short*)ip, port);
	}
}


/*
 * Like insl but in the opposite direction.  This is used by the IDE
 * driver to write disk sectors.  Performance is important, but the
 * interfaces seems to be slow: just using the inlined version of the
 * outw() breaks things.
 */
#undef outsw
void outsl (unsigned long port, void *src, unsigned long count)
{
	unsigned int *ip, w;

	if (((unsigned long)src) & 0x3) {
		panic("outsw: memory not aligned");
	}

	ip = src;
	while (count > 0) {
		w = *ip++;
		--count;
		outw(w, port);
	}
}
