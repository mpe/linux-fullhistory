#ifndef _PPC_IO_H
#define _PPC_IO_H

/* Define the particulars of outb/outw/outl "instructions" */

#define SLOW_DOWN_IO

#ifndef PCI_DRAM_OFFSET
#define PCI_DRAM_OFFSET  0x80000000
#endif
#ifndef KERNELBASE
#define KERNELBASE 0x90000000
#endif

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
/* #define virt_to_bus(a) ((unsigned long)(((char *)a==(char *) 0) ? ((char *)0) \
			: ((char *)((long)a - KERNELBASE + PCI_DRAM_OFFSET))))
#define bus_to_virt(a) ((void *) (((char *)a==(char *)0) ? ((char *)0) \
			: ((char *)((long)a - PCI_DRAM_OFFSET + KERNELBASE))))
*/

#define readb(addr) (*(volatile unsigned char *) (addr))
#define readw(addr) (*(volatile unsigned short *) (addr))
#define readl(addr) (*(volatile unsigned int *) (addr))

#define writeb(b,addr) ((*(volatile unsigned char *) (addr)) = (b))
#define writew(b,addr) ((*(volatile unsigned short *) (addr)) = (b))
#define writel(b,addr) ((*(volatile unsigned int *) (addr)) = (b))

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

/* from arch/ppc/kernel/port_io.c
 *               -- Cort
 */
unsigned char inb(int port);
unsigned short inw(int port);
unsigned long inl(int port);
unsigned char outb(unsigned char val,int port);
unsigned short outw(unsigned short val,int port);
unsigned long outl(unsigned long val,int port);
void outsl(int port, long *ptr, int len);

static inline unsigned char  inb_p(int port) {return (inb(port)); }
static inline unsigned short inw_p(int port) {return (inw(port)); }
static inline unsigned long  inl_p(int port) {return (inl(port)); }



static inline unsigned char  outb_p(unsigned char val,int port) { return (outb(val,port)); }
static inline unsigned short outw_p(unsigned short val,int port) { return (outw(val,port)); }
static inline unsigned long  outl_p(unsigned long val,int port) { return (outl(val,port)); }



#endif
