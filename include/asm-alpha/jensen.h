#ifndef __ALPHA_JENSEN_H
#define __ALPHA_JENSEN_H

/*
 * Defines for the AlphaPC EISA IO and memory address space.
 */

/*
 * NOTE! The memory operations do not set any memory barriers, as it's
 * not needed for cases like a frame buffer that is essentially memory-like.
 * You need to do them by hand if the operations depend on ordering.
 *
 * Similarly, the port IO operations do a "mb" only after a write operation:
 * if an mb is needed before (as in the case of doing memory mapped IO
 * first, and then a port IO operation to the same device), it needs to be
 * done by hand.
 *
 * After the above has bitten me 100 times, I'll give up and just do the
 * mb all the time, but right now I'm hoping this will work out.  Avoiding
 * mb's may potentially be a noticeable speed improvement, but I can't
 * honestly say I've tested it.
 *
 * Handling interrupts that need to do mb's to synchronize to non-interrupts
 * is another fun race area.  Don't do it (because if you do, I'll have to
 * do *everything* with interrupts disabled, ugh).
 */

/*
 * EISA Interrupt Acknowledge address
 */
#define EISA_INTA		(IDENT_ADDR + 0x100000000UL)

/*
 * FEPROM addresses
 */
#define EISA_FEPROM0		(IDENT_ADDR + 0x180000000UL)
#define EISA_FEPROM1		(IDENT_ADDR + 0x1A0000000UL)

/*
 * VL82C106 base address
 */
#define EISA_VL82C106		(IDENT_ADDR + 0x1C0000000UL)

/*
 * EISA "Host Address Extension" address (bits 25-31 of the EISA address)
 */
#define EISA_HAE		(IDENT_ADDR + 0x1D0000000UL)

/*
 * "SYSCTL" register address
 */
#define EISA_SYSCTL		(IDENT_ADDR + 0x1E0000000UL)

/*
 * "spare" register address
 */
#define EISA_SPARE		(IDENT_ADDR + 0x1F0000000UL)

/*
 * EISA memory address offset
 */
#define EISA_MEM		(IDENT_ADDR + 0x200000000UL)

/*
 * EISA IO address offset
 */
#define EISA_IO			(IDENT_ADDR + 0x300000000UL)

/*
 * Change virtual addresses to bus addresses and vv.
 *
 * NOTE! On the Jensen, the physical address is the same
 * as the bus address, but this is not necessarily true on
 * other alpha hardware.
 */
#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt

#define HAE_ADDRESS	EISA_HAE

/*
 * Handle the "host address register". This needs to be set
 * to the high 7 bits of the EISA address.  This is also needed
 * for EISA IO addresses, which are only 16 bits wide (the
 * hae needs to be set to 0).
 *
 * HAE isn't needed for the local IO operations, though.
 */
#define __HAE_MASK 0x1ffffff
extern inline void __set_hae(unsigned long addr)
{
	/* hae on the Jensen is bits 31:25 shifted right */
	addr >>= 25;
	if (addr != hae.cache)
		set_hae(addr);
}

/*
 * IO functions
 *
 * The "local" functions are those that don't go out to the EISA bus,
 * but instead act on the VL82C106 chip directly.. This is mainly the
 * keyboard, RTC,  printer and first two serial lines..
 *
 * The local stuff makes for some complications, but it seems to be
 * gone in the PCI version. I hope I can get DEC suckered^H^H^H^H^H^H^H^H
 * convinced that I need one of the newer machines.
 */
extern inline unsigned int __local_inb(unsigned long addr)
{
	long result = *(volatile int *) ((addr << 9) + EISA_VL82C106);
	return 0xffUL & result;
}

extern inline void __local_outb(unsigned char b, unsigned long addr)
{
	*(volatile unsigned int *) ((addr << 9) + EISA_VL82C106) = b;
	mb();
}

extern unsigned int _bus_inb(unsigned long addr);

extern inline unsigned int __bus_inb(unsigned long addr)
{
	long result;

	__set_hae(0);
	result = *(volatile int *) ((addr << 7) + EISA_IO + 0x00);
	result >>= (addr & 3) * 8;
	return 0xffUL & result;
}

extern void _bus_outb(unsigned char b, unsigned long addr);

extern inline void __bus_outb(unsigned char b, unsigned long addr)
{
	__set_hae(0);
	*(volatile unsigned int *) ((addr << 7) + EISA_IO + 0x00) = b * 0x01010101;
	mb();
}

/*
 * It seems gcc is not very good at optimizing away logical
 * operations that result in operations across inline functions.
 * Which is why this is a macro.
 */
#define __is_local(addr) ( \
/* keyboard */	(addr == 0x60 || addr == 0x64) || \
/* RTC */	(addr == 0x170 || addr == 0x171) || \
/* mb COM2 */	(addr >= 0x2f8 && addr <= 0x2ff) || \
/* mb LPT1 */	(addr >= 0x3bc && addr <= 0x3be) || \
/* mb COM2 */	(addr >= 0x3f8 && addr <= 0x3ff))

extern inline unsigned int __inb(unsigned long addr)
{
	if (__is_local(addr))
		return __local_inb(addr);
	return _bus_inb(addr);
}

extern inline void __outb(unsigned char b, unsigned long addr)
{
	if (__is_local(addr))
		__local_outb(b, addr);
	else
		_bus_outb(b, addr);
}

extern inline unsigned int __inw(unsigned long addr)
{
	long result;

	__set_hae(0);
	result = *(volatile int *) ((addr << 7) + EISA_IO + 0x20);
	result >>= (addr & 3) * 8;
	return 0xffffUL & result;
}

extern inline unsigned int __inl(unsigned long addr)
{
	__set_hae(0);
	return *(volatile unsigned int *) ((addr << 7) + EISA_IO + 0x60);
}

extern inline void __outw(unsigned short b, unsigned long addr)
{
	__set_hae(0);
	*(volatile unsigned int *) ((addr << 7) + EISA_IO + 0x20) = b * 0x00010001;
	mb();
}

extern inline void __outl(unsigned int b, unsigned long addr)
{
	__set_hae(0);
	*(volatile unsigned int *) ((addr << 7) + EISA_IO + 0x60) = b;
	mb();
}

/*
 * Memory functions.
 */
extern inline unsigned long __readb(unsigned long addr)
{
	long result;

	__set_hae(addr);
	addr &= __HAE_MASK;
	result = *(volatile int *) ((addr << 7) + EISA_MEM + 0x00);
	result >>= (addr & 3) * 8;
	return 0xffUL & result;
}

extern inline unsigned long __readw(unsigned long addr)
{
	long result;

	__set_hae(addr);
	addr &= __HAE_MASK;
	result = *(volatile int *) ((addr << 7) + EISA_MEM + 0x20);
	result >>= (addr & 3) * 8;
	return 0xffffUL & result;
}

extern inline unsigned long __readl(unsigned long addr)
{
	__set_hae(addr);
	addr &= __HAE_MASK;
	return *(volatile unsigned int *) ((addr << 7) + EISA_MEM + 0x60);
}

extern inline void __writeb(unsigned short b, unsigned long addr)
{
	__set_hae(addr);
	addr &= __HAE_MASK;
	*(volatile unsigned int *) ((addr << 7) + EISA_MEM + 0x00) = b * 0x01010101;
}

extern inline void __writew(unsigned short b, unsigned long addr)
{
	__set_hae(addr);
	addr &= __HAE_MASK;
	*(volatile unsigned int *) ((addr << 7) + EISA_MEM + 0x20) = b * 0x00010001;
}

extern inline void __writel(unsigned int b, unsigned long addr)
{
	__set_hae(addr);
	addr &= __HAE_MASK;
	*(volatile unsigned int *) ((addr << 7) + EISA_MEM + 0x60) = b;
}

/*
 * The above have so much overhead that it probably doesn't make
 * sense to have them inlined (better icache behaviour).
 */
#define inb(port) \
(__builtin_constant_p((port))?__inb(port):_inb(port))

#define outb(x, port) \
(__builtin_constant_p((port))?__outb((x),(port)):_outb((x),(port)))

/*
 * The Alpha Jensen hardware for some rather strange reason puts
 * the RTC clock at 0x170 instead of 0x70. Probably due to some
 * misguided idea about using 0x70 for NMI stuff.
 *
 * These defines will override the defaults when doing RTC queries
 */
#define RTC_PORT(x)	(0x170+(x))
#define RTC_ADDR(x)	(x)
#define RTC_ALWAYS_BCD	0

#endif
