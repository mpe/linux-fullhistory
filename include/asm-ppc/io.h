#ifndef _PPC_IO_H
#define _PPC_IO_H

#include <linux/config.h>
#include <asm/page.h>
#include <asm/byteorder.h>

#ifdef CONFIG_PREP
/* from the Carolina Technical Spec -- Cort */
#define IBM_ACORN 0x82A
#define SIO_CONFIG_RA	0x398
#define SIO_CONFIG_RD	0x399

#define IBM_HDD_LED       0x808
#define IBM_EQUIP_PRESENT 0x80c	
#define IBM_L2_STATUS     0x80d
#define IBM_L2_INVALIDATE 0x814
#define IBM_SYS_CTL       0x81c

#define SLOW_DOWN_IO

#ifndef PCI_DRAM_OFFSET
#define PCI_DRAM_OFFSET  0x80000000
#endif

#define readb(addr) (*(volatile unsigned char *) (addr))
#define readw(addr) (*(volatile unsigned short *) (addr))
#define readl(addr) (*(volatile unsigned int *) (addr))
#define writeb(b,addr) ((*(volatile unsigned char *) (addr)) = (b))
#define writew(b,addr) ((*(volatile unsigned short *) (addr)) = (b))
#define writel(b,addr) ((*(volatile unsigned int *) (addr)) = (b))

void outsl(int port, long *ptr, int len);

__inline__ unsigned char outb(unsigned char val, int port);
__inline__ unsigned short outw(unsigned short val, int port);
__inline__ unsigned long outl(unsigned long val, int port);
__inline__ unsigned char inb(int port);
__inline__ unsigned short inw(int port);
__inline__ unsigned long inl(int port);

#define inb_p inb
#define inw_p inw
#define inl_p inl
#define outb_p outb
#define outw_p outw
#define outl_p outl

#endif /* CONFIG_PREP */

#ifdef CONFIG_PMAC
/*
 * Read and write the non-volatile RAM.
 */
extern int nvram_readb(int addr);
extern void nvram_writeb(int addr, int val);

#ifndef PCI_DRAM_OFFSET
#define PCI_DRAM_OFFSET  0
#endif

#define inb(port)		in_8((unsigned char *)(port))
#define outb(val, port)		out_8((unsigned char *)(port), (val))
#define inw(port)		in_le16((unsigned short *)(port))
#define outw(val, port)		out_le16((unsigned short *)(port), (val))
#define inl(port)		in_le32((unsigned long *)(port))
#define outl(val, port)		out_le32((unsigned long *)(port), (val))

#define inb_p(port)		in_8((unsigned char *)(port))
#define outb_p(val, port)	out_8((unsigned char *)(port), (val))
#define inw_p(port)		in_le16((unsigned short *)(port))
#define outw_p(val, port)	out_le16((unsigned short *)(port), (val))
#define inl_p(port)		in_le32(((unsigned long *)port))
#define outl_p(val, port)	out_le32((unsigned long *)(port), (val))

#define insw(port, buf, ns)	_insw((unsigned short *)(port), (buf), (ns))
#define outsw(port, buf, ns)	_outsw((unsigned short *)(port), (buf), (ns))
#define insl(port, buf, nl)	_insl((unsigned long *)(port), (buf), (nl))
#define outsl(port, buf, nl)	_outsl((unsigned long *)(port), (buf), (nl))
#endif /* CONFIG_PMAC */

/*
 * The PCI bus is inherently Little-Endian.  The PowerPC is being
 * run Big-Endian.  Thus all values which cross the [PCI] barrier
 * must be endian-adjusted.  Also, the local DRAM has a different
 * address from the PCI point of view, thus buffer addresses also
 * have to be modified [mapped] appropriately.
 */
extern inline unsigned long virt_to_bus(volatile void * address)
{
        if (address == (void *)0) return 0;
        return ((unsigned long)((long)address - KERNELBASE + PCI_DRAM_OFFSET));
}

extern inline void * bus_to_virt(unsigned long address)
{
        if (address == 0) return 0;
        return ((void *)(address - PCI_DRAM_OFFSET + KERNELBASE));
}

/*
 * Map in an area of physical address space, for accessing
 * I/O devices etc.
 */
extern void *ioremap(unsigned long address, unsigned long size);

/*
 * Change virtual addresses to physical addresses and vv.
 * These are trivial on the 1:1 Linux/i386 mapping (but if we ever
 * make the kernel segment mapped at 0, we need to do translation
 * on the i386 as well)
 */
extern inline unsigned long virt_to_phys(volatile void * address)
{
	return (unsigned long) address;
}

extern inline void * phys_to_virt(unsigned long address)
{
	return (void *) address;
}

#define _IO_BASE ((unsigned long)0x80000000)

/*
 * These are much more useful le/be io functions from Paul
 * than leXX_to_cpu() style functions since the ppc has
 * load/store byte reverse instructions
 *  -- Cort
 */

/*
 * Enforce In-order Execution of I/O:
 * Acts as a barrier to ensure all previous I/O accesses have
 * completed before any further ones are issued.
 */
extern inline void eieio(void)
{
	asm volatile ("eieio" : :);
}

/*
 * 8, 16 and 32 bit, big and little endian I/O operations, with barrier.
 */
extern inline int in_8(volatile unsigned char *addr)
{
	int ret;

	ret = *addr;
	eieio();
	return ret;
}

extern inline void out_8(volatile unsigned char *addr, int val)
{
	*addr = val;
	eieio();
}

extern inline int in_le16(volatile unsigned short *addr)
{
	int ret;

	ret = ld_le16(addr);
	eieio();
	return ret;
}

extern inline int in_be16(volatile unsigned short *addr)
{
	int ret;

	ret = *addr;
	eieio();
	return ret;
}

extern inline void out_le16(volatile unsigned short *addr, int val)
{
	st_le16(addr, val);
	eieio();
}

extern inline void out_be16(volatile unsigned short *addr, int val)
{
	*addr = val;
	eieio();
}

extern inline unsigned in_le32(volatile unsigned *addr)
{
	unsigned ret;

	ret = ld_le32(addr);
	eieio();
	return ret;
}

extern inline int in_be32(volatile unsigned *addr)
{
	int ret;

	ret = *addr;
	eieio();
	return ret;
}

extern inline void out_le32(volatile unsigned *addr, int val)
{
	st_le32(addr, val);
	eieio();
}

extern inline void out_be32(volatile unsigned *addr, int val)
{
	*addr = val;
	eieio();
}

#endif
