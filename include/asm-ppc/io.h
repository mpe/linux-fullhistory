#ifndef _PPC_IO_H
#define _PPC_IO_H

#include <linux/config.h>
#include <asm/page.h>
#include <asm/byteorder.h>

/* from the Carolina Technical Spec -- Cort */
#define IBM_ACORN 0x82A
#define SIO_CONFIG_RA	0x398
#define SIO_CONFIG_RD	0x399

#define IBM_HDD_LED       0x808
#define IBM_EQUIP_PRESENT 0x80c	
#define IBM_L2_STATUS     0x80d
#define IBM_L2_INVALIDATE 0x814
#define IBM_SYS_CTL       0x81c

extern unsigned long io_base;
#define SLOW_DOWN_IO
#define _IO_BASE io_base

extern unsigned long pci_dram_offset;
#undef PCI_DRAM_OFFSET
#define PCI_DRAM_OFFSET  pci_dram_offset

#define readb(addr) (*(volatile unsigned char *) (addr))
#define readw(addr) ld_le16((volatile unsigned short *)(addr))
#define readl(addr) ld_le32((volatile unsigned *)addr)
#define writeb(b,addr) ((*(volatile unsigned char *) (addr)) = (b))
#define writew(b,addr) st_le16((volatile unsigned short *)(addr),(b))
#define writel(b,addr) st_le32((volatile unsigned *)(addr),(b))

#define insb(port, buf, ns)	_insb((unsigned char *)((port)+_IO_BASE), (buf), (ns))
#define outsb(port, buf, ns)	_outsb((unsigned char *)((port)+_IO_BASE), (buf), (ns))
#define insw(port, buf, ns)	_insw((unsigned short *)((port)+_IO_BASE), (buf), (ns))
#define outsw(port, buf, ns)	_outsw((unsigned short *)((port)+_IO_BASE), (buf), (ns))
#define insl(port, buf, nl)	_insl((unsigned long *)((port)+_IO_BASE), (buf), (nl))
#define outsl(port, buf, nl)	_outsl((unsigned long *)((port)+_IO_BASE), (buf), (nl))

#define inb(port)		in_8((unsigned char *)((port)+_IO_BASE))
#define outb(val, port)		out_8((unsigned char *)((port)+_IO_BASE), (val))
#define inw(port)		in_le16((unsigned short *)((port)+_IO_BASE))
#define outw(val, port)		out_le16((unsigned short *)((port)+_IO_BASE), (val))
#define inl(port)		in_le32((unsigned *)((port)+_IO_BASE))
#define outl(val, port)		out_le32((unsigned *)((port)+_IO_BASE), (val))

#define inb_p(port)		in_8((unsigned char *)((port)+_IO_BASE))
#define outb_p(val, port)	out_8((unsigned char *)((port)+_IO_BASE), (val))
#define inw_p(port)		in_le16((unsigned short *)((port)+_IO_BASE))
#define outw_p(val, port)	out_le16((unsigned short *)((port)+_IO_BASE), (val))
#define inl_p(port)		in_le32(((unsigned *)(port)+_IO_BASE))
#define outl_p(val, port)	out_le32((unsigned *)((port)+_IO_BASE), (val))

extern void _insb(volatile unsigned char *port, void *buf, int ns);
extern void _outsb(volatile unsigned char *port, const void *buf, int ns);
extern void _insw(volatile unsigned short *port, void *buf, int ns);
extern void _outsw(volatile unsigned short *port, const void *buf, int ns);
extern void _insl(volatile unsigned long *port, void *buf, int nl);
extern void _outsl(volatile unsigned long *port, const void *buf, int nl);

#ifdef __KERNEL__
/*
 * The PCI bus is inherently Little-Endian.  The PowerPC is being
 * run Big-Endian.  Thus all values which cross the [PCI] barrier
 * must be endian-adjusted.  Also, the local DRAM has a different
 * address from the PCI point of view, thus buffer addresses also
 * have to be modified [mapped] appropriately.
 */
extern inline unsigned long virt_to_bus(volatile void * address)
{
        if (address == (void *)0)
		return 0;
        return (unsigned long)address - KERNELBASE + PCI_DRAM_OFFSET;
}

extern inline void * bus_to_virt(unsigned long address)
{
        if (address == 0)
		return 0;
        return (void *)(address - PCI_DRAM_OFFSET + KERNELBASE);
}

/*
 * Map in an area of physical address space, for accessing
 * I/O devices etc.
 */
extern void *ioremap(unsigned long address, unsigned long size);
extern void iounmap(unsigned long *addr);

/*
 * Change virtual addresses to physical addresses and vv, for
 * addresses in the area where the kernel has the RAM mapped.
 */
extern inline unsigned long virt_to_phys(volatile void * address)
{
	return (unsigned long) address - KERNELBASE;
}

extern inline void * phys_to_virt(unsigned long address)
{
	return (void *) (address + KERNELBASE);
}

#endif /* __KERNEL__ */

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
