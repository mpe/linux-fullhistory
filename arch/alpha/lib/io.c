/*
 * Alpha IO and memory functions.. Just expand the inlines in the header
 * files..
 */
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
