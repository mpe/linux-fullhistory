#ifndef __ALPHA_LCA__H
#define __ALPHA_LCA__H

/*
 * Low Cost Alpha (LCA) definitions (these apply to 21066 and 21068,
 * for example).
 *
 * This file is based on:
 *
 *	DECchip 21066 and DECchip 21068 Alpha AXP Microprocessors
 *	Hardware Reference Manual; Digital Equipment Corp.; May 1994;
 *	Maynard, MA; Order Number: EC-N2681-71.
 */

/*
 * NOTE! Currently, this never uses the HAE register, so it works only
 * for the low 27 bits of the PCI sparse memory address space.  Dense
 * memory space doesn't require the HAE, but is restricted to aligned
 * 32 and 64 bit accesses.  Special Cycle and Interrupt Acknowledge
 * cycles may also require the use of the HAE.  The LCA limits I/O
 * address space to the bottom 24 bits of address space, but this
 * easily covers the 16 bit ISA I/O address space.
 */

/*
 * NOTE 2! The memory operations do not set any memory barriers, as
 * it's not needed for cases like a frame buffer that is essentially
 * memory-like.  You need to do them by hand if the operations depend
 * on ordering.
 *
 * Similarly, the port I/O operations do a "mb" only after a write
 * operation: if an mb is needed before (as in the case of doing
 * memory mapped I/O first, and then a port I/O operation to the same
 * device), it needs to be done by hand.
 *
 * After the above has bitten me 100 times, I'll give up and just do
 * the mb all the time, but right now I'm hoping this will work out.
 * Avoiding mb's may potentially be a noticeable speed improvement,
 * but I can't honestly say I've tested it.
 *
 * Handling interrupts that need to do mb's to synchronize to
 * non-interrupts is another fun race area.  Don't do it (because if
 * you do, I'll have to do *everything* with interrupts disabled,
 * ugh).
 */

/*
 * Virtual -> physical identity mapping starts at this offset.
 */
#define IDENT_ADDR	(0xfffffc0000000000UL)

/*
 * I/O Controller registers:
 */
#define LCA_IOC_HAE		(IDENT_ADDR + 0x180000000UL)
#define LCA_IOC_CONF		(IDENT_ADDR + 0x180000020UL)
#define LCA_IOC_STAT0		(IDENT_ADDR + 0x180000040UL)
#define LCA_IOC_STAT1		(IDENT_ADDR + 0x180000060UL)
#define LCA_IOC_TBIA		(IDENT_ADDR + 0x180000080UL)
#define LCA_IOC_TB_ENA		(IDENT_ADDR + 0x1800000a0UL)
#define LCA_IOC_SFT_RST		(IDENT_ADDR + 0x1800000c0UL)
#define LCA_IOC_PAR_DIS		(IDENT_ADDR + 0x1800000e0UL)
#define LCA_IOC_W_BASE0		(IDENT_ADDR + 0x180000100UL)
#define LCA_IOC_W_BASE1		(IDENT_ADDR + 0x180000120UL)
#define LCA_IOC_W_MASK0		(IDENT_ADDR + 0x180000140UL)
#define LCA_IOC_W_MASK1		(IDENT_ADDR + 0x180000160UL)
#define LCA_IOC_T_BASE0		(IDENT_ADDR + 0x180000180UL)
#define LCA_IOC_T_BASE1		(IDENT_ADDR + 0x1800001a0UL)
#define LCA_IOC_TB_TAG0		(IDENT_ADDR + 0x188000000UL)
#define LCA_IOC_TB_TAG1		(IDENT_ADDR + 0x188000020UL)
#define LCA_IOC_TB_TAG2		(IDENT_ADDR + 0x188000040UL)
#define LCA_IOC_TB_TAG3		(IDENT_ADDR + 0x188000060UL)
#define LCA_IOC_TB_TAG4		(IDENT_ADDR + 0x188000070UL)
#define LCA_IOC_TB_TAG5		(IDENT_ADDR + 0x1880000a0UL)
#define LCA_IOC_TB_TAG6		(IDENT_ADDR + 0x1880000c0UL)
#define LCA_IOC_TB_TAG7		(IDENT_ADDR + 0x1880000e0UL)

/*
 * Memory spaces:
 */
#define LCA_IACK_SC		(IDENT_ADDR + 0x1a0000000UL)
#define LCA_CONF		(IDENT_ADDR + 0x1e0000000UL)
#define LCA_IO			(IDENT_ADDR + 0x1c0000000UL)
#define LCA_SPARSE_MEM		(IDENT_ADDR + 0x200000000UL)
#define LCA_DENSE_MEM		(IDENT_ADDR + 0x300000000UL)

/*
 * Bit definitions for I/O Controller status register 0:
 */
#define LCA_IOC_STAT0_CMD		0xf
#define LCA_IOC_STAT0_ERR		(1<<4)
#define LCA_IOC_STAT0_LOST		(1<<5)
#define LCA_IOC_STAT0_THIT		(1<<6)
#define LCA_IOC_STAT0_TREF		(1<<7)
#define LCA_IOC_STAT0_CODE_SHIFT	8
#define LCA_IOC_STAT0_CODE_MASK		0x7
#define LCA_IOC_STAT0_P_NBR_SHIFT	13
#define LCA_IOC_STAT0_P_NBR_MASK	0x7ffff

/*
 * I/O functions:
 *
 * Unlike Jensen, the Noname machines have no concept of local
 * I/O---everything goes over the PCI bus.
 *
 * There is plenty room for optimization here.  In particular,
 * the Alpha's insb/insw/extb/extw should be useful in moving
 * data to/from the right byte-lanes.
 */

extern inline unsigned int
inb(unsigned long addr)
{
    long result = *(volatile int *) ((addr << 5) + LCA_IO + 0x00);
    result >>= (addr & 3) * 8;
    return 0xffUL & result;
}

extern inline unsigned int
inw(unsigned long addr)
{
    long result = *(volatile int *) ((addr << 5) + LCA_IO + 0x08);
    result >>= (addr & 3) * 8;
    return 0xffffUL & result;
}

extern inline unsigned int
inl(unsigned long addr)
{
    return *(volatile unsigned int *) ((addr << 5) + LCA_IO + 0x18);
}

extern inline void
outb(unsigned char b, unsigned long addr)
{
    *(volatile unsigned int *) ((addr << 5) + LCA_IO + 0x00) = b * 0x01010101;
    mb();
}

extern inline void
outw(unsigned char b, unsigned long addr)
{
    *(volatile unsigned int *) ((addr << 5) + LCA_IO + 0x08) = b * 0x00010001;
    mb();
}

extern inline void
outl(unsigned char b, unsigned long addr)
{
    *(volatile unsigned int *) ((addr << 5) + LCA_IO + 0x18) = b;
    mb();
}


/*
 * Memory functions.  64-bit and 32-bit accesses are done through
 * dense memory space, everything else through sparse space.
 */

extern inline unsigned long
readb(unsigned long addr)
{
    long result = *(volatile int *) ((addr << 5) + LCA_SPARSE_MEM + 0x00);
    result >>= (addr & 3) * 8;
    return 0xffUL & result;
}

extern inline unsigned long
readw(unsigned long addr)
{
    long result = *(volatile int *) ((addr << 5) + LCA_SPARSE_MEM + 0x08);
    result >>= (addr & 3) * 8;
    return 0xffffUL & result;
}

extern inline unsigned long
readl(unsigned long addr)
{
    return *(volatile int *) (addr + LCA_DENSE_MEM);
}

extern inline void
writeb(unsigned short b, unsigned long addr)
{
    *(volatile unsigned int *) ((addr << 5) + LCA_SPARSE_MEM + 0x00) =
      b * 0x01010101;
}

extern inline void
writew(unsigned short b, unsigned long addr)
{
    *(volatile unsigned int *) ((addr << 5) + LCA_SPARSE_MEM + 0x08) =
      b * 0x00010001;
}

extern inline void
writel(unsigned short b, unsigned long addr)
{
    *(volatile unsigned int *) (addr + LCA_DENSE_MEM) = b;
}

#define inb_local inb
#define outb_local outb
#define inb_p inb
#define outb_p outb

#endif
