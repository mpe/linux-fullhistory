#ifndef __ALPHA_MCPCIA__H__
#define __ALPHA_MCPCIA__H__

#include <linux/config.h>
#include <linux/types.h>
#include <linux/pci.h>

/*
 * MCPCIA is the internal name for a core logic chipset which provides
 * PCI access for the RAWHIDE family of systems.
 *
 * This file is based on:
 *
 * RAWHIDE System Programmer's Manual
 * 16-May-96
 * Rev. 1.4
 *
 */

/*------------------------------------------------------------------------**
**                                                                        **
**  I/O procedures                                                   **
**                                                                        **
**      inport[b|w|t|l], outport[b|w|t|l] 8:16:24:32 IO xfers             **
**	inportbxt: 8 bits only                                            **
**      inport:    alias of inportw                                       **
**      outport:   alias of outportw                                      **
**                                                                        **
**      inmem[b|w|t|l], outmem[b|w|t|l] 8:16:24:32 ISA memory xfers       **
**	inmembxt: 8 bits only                                             **
**      inmem:    alias of inmemw                                         **
**      outmem:   alias of outmemw                                        **
**                                                                        **
**------------------------------------------------------------------------*/


/* MCPCIA ADDRESS BIT DEFINITIONS
 *
 *  3 3 3 3|3 3 3 3|3 3 2 2|2 2 2 2|2 2 2 2|1 1 1 1|1 1 1 1|1 1 
 *  9 8 7 6|5 4 3 2|1 0 9 8|7 6 5 4|3 2 1 0|9 8 7 6|5 4 3 2|1 0 9 8|7 6 5 4|3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |1| | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | |0|0|0|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ 
 *  |                                                                        \_/ \_/
 *  |                                                                         |   |
 *  +-- IO space, not cached.                                   Byte Enable --+   |
 *                                                              Transfer Length --+
 *
 *
 *
 *   Byte      Transfer
 *   Enable    Length    Transfer  Byte    Address
 *   adr<6:5>  adr<4:3>  Length    Enable  Adder
 *   ---------------------------------------------
 *      00        00      Byte      1110   0x000
 *      01        00      Byte      1101   0x020
 *      10        00      Byte      1011   0x040
 *      11        00      Byte      0111   0x060
 *
 *      00        01      Word      1100   0x008
 *      01        01      Word      1001   0x028 <= Not supported in this code.
 *      10        01      Word      0011   0x048
 *
 *      00        10      Tribyte   1000   0x010
 *      01        10      Tribyte   0001   0x030
 *
 *      10        11      Longword  0000   0x058
 *
 *      Note that byte enables are asserted low.
 *
 */

#define BYTE_ENABLE_SHIFT 5
#define TRANSFER_LENGTH_SHIFT 3

#define MEM_R1_MASK 0x1fffffff  /* SPARSE Mem region 1 mask is 29 bits */
#define MEM_R2_MASK 0x07ffffff  /* SPARSE Mem region 2 mask is 27 bits */
#define MEM_R3_MASK 0x03ffffff  /* SPARSE Mem region 3 mask is 26 bits */

#ifdef CONFIG_ALPHA_SRM_SETUP
/* if we are using the SRM PCI setup, we'll need to use variables instead */
#define MCPCIA_DMA_WIN_BASE_DEFAULT    (2*1024*1024*1024U)
#define MCPCIA_DMA_WIN_SIZE_DEFAULT    (2*1024*1024*1024U)

extern unsigned int MCPCIA_DMA_WIN_BASE;
extern unsigned int MCPCIA_DMA_WIN_SIZE;

#else /* SRM_SETUP */
#define MCPCIA_DMA_WIN_BASE	(2*1024*1024*1024UL)
#define MCPCIA_DMA_WIN_SIZE	(2*1024*1024*1024UL)
#endif /* SRM_SETUP */

#define HOSE(h) (((unsigned long)(h)) << 33)
/*
 *  General Registers
 */
#define MCPCIA_REV(h)		(IDENT_ADDR + 0xf9e0000000UL + HOSE(h))
#define MCPCIA_WHOAMI(h)	(IDENT_ADDR + 0xf9e0000040UL + HOSE(h))
#define MCPCIA_PCI_LAT(h)	(IDENT_ADDR + 0xf9e0000080UL + HOSE(h))
#define MCPCIA_CAP_CTRL(h)	(IDENT_ADDR + 0xf9e0000100UL + HOSE(h))
#define MCPCIA_HAE_MEM(h)	(IDENT_ADDR + 0xf9e0000400UL + HOSE(h))
#define MCPCIA_HAE_IO(h)	(IDENT_ADDR + 0xf9e0000440UL + HOSE(h))
#if 0
#define MCPCIA_IACK_SC(h)	(IDENT_ADDR + 0xf9e0000480UL + HOSE(h))
#endif
#define MCPCIA_HAE_DENSE(h)	(IDENT_ADDR + 0xf9e00004c0UL + HOSE(h))

/*
 * Interrupt Control registers
 */
#define MCPCIA_INT_CTL(h)	(IDENT_ADDR + 0xf9e0000500UL + HOSE(h))
#define MCPCIA_INT_REQ(h)	(IDENT_ADDR + 0xf9e0000540UL + HOSE(h))
#define MCPCIA_INT_TARG(h)	(IDENT_ADDR + 0xf9e0000580UL + HOSE(h))
#define MCPCIA_INT_ADR(h)	(IDENT_ADDR + 0xf9e00005c0UL + HOSE(h))
#define MCPCIA_INT_ADR_EXT(h)	(IDENT_ADDR + 0xf9e0000600UL + HOSE(h))
#define MCPCIA_INT_MASK0(h)	(IDENT_ADDR + 0xf9e0000640UL + HOSE(h))
#define MCPCIA_INT_MASK1(h)	(IDENT_ADDR + 0xf9e0000680UL + HOSE(h))
#define MCPCIA_INT_ACK0(h)	(IDENT_ADDR + 0xf9f0003f00UL + HOSE(h))
#define MCPCIA_INT_ACK1(h)	(IDENT_ADDR + 0xf9e0003f40UL + HOSE(h))

/*
 * Performance Monitor registers
 */
#define MCPCIA_PERF_MONITOR(h)	(IDENT_ADDR + 0xf9e0000300UL + HOSE(h))
#define MCPCIA_PERF_CONTROL(h)	(IDENT_ADDR + 0xf9e0000340UL + HOSE(h))

/*
 * Diagnostic Registers
 */
#define MCPCIA_CAP_DIAG(h)	(IDENT_ADDR + 0xf9e0000700UL + HOSE(h))
#define MCPCIA_TOP_OF_MEM(h)	(IDENT_ADDR + 0xf9e00007c0UL + HOSE(h))

/*
 * Error registers
 */
#define MCPCIA_CAP_ERR(h)	(IDENT_ADDR + 0xf9e0000880UL + HOSE(h))
#define MCPCIA_PCI_ERR1(h)	(IDENT_ADDR + 0xf9e0001040UL + HOSE(h))

/*
 * PCI Address Translation Registers.
 */
#define MCPCIA_SG_TBIA(h)	(IDENT_ADDR + 0xf9e0001300UL + HOSE(h))
#define MCPCIA_HBASE(h)		(IDENT_ADDR + 0xf9e0001340UL + HOSE(h))

#define MCPCIA_W0_BASE(h)	(IDENT_ADDR + 0xf9e0001400UL + HOSE(h))
#define MCPCIA_W0_MASK(h)	(IDENT_ADDR + 0xf9e0001440UL + HOSE(h))
#define MCPCIA_T0_BASE(h)	(IDENT_ADDR + 0xf9e0001480UL + HOSE(h))

#define MCPCIA_W1_BASE(h)	(IDENT_ADDR + 0xf9e0001500UL + HOSE(h))
#define MCPCIA_W1_MASK(h)	(IDENT_ADDR + 0xf9e0001540UL + HOSE(h))
#define MCPCIA_T1_BASE(h)	(IDENT_ADDR + 0xf9e0001580UL + HOSE(h))

#define MCPCIA_W2_BASE(h)	(IDENT_ADDR + 0xf9e0001600UL + HOSE(h))
#define MCPCIA_W2_MASK(h)	(IDENT_ADDR + 0xf9e0001640UL + HOSE(h))
#define MCPCIA_T2_BASE(h)	(IDENT_ADDR + 0xf9e0001680UL + HOSE(h))

#define MCPCIA_W3_BASE(h)	(IDENT_ADDR + 0xf9e0001700UL + HOSE(h))
#define MCPCIA_W3_MASK(h)	(IDENT_ADDR + 0xf9e0001740UL + HOSE(h))
#define MCPCIA_T3_BASE(h)	(IDENT_ADDR + 0xf9e0001780UL + HOSE(h))

/*
 * Memory spaces:
 */
#define MCPCIA_CONF(h)		(IDENT_ADDR + 0xf9c0000000UL + HOSE(h))
#define MCPCIA_IO(h)		(IDENT_ADDR + 0xf980000000UL + HOSE(h))
#define MCPCIA_SPARSE(h)	(IDENT_ADDR + 0xf800000000UL + HOSE(h))
#define MCPCIA_DENSE(h)		(IDENT_ADDR + 0xf900000000UL + HOSE(h))
#define MCPCIA_IACK_SC(h)	(IDENT_ADDR + 0xf9f0003f00UL + HOSE(h))

#define DENSE_MEM(addr)		MCPCIA_DENSE(((unsigned long)(addr) >> 32) & 3)

#define HAE_ADDRESS		MCPCIA_HAE_MEM(0)

#ifdef __KERNEL__

/*
 * Translate physical memory address as seen on (PCI) bus into
 * a kernel virtual address and vv.
 */
extern inline unsigned long virt_to_bus(void * address)
{
	return virt_to_phys(address) + MCPCIA_DMA_WIN_BASE;
}

extern inline void * bus_to_virt(unsigned long address)
{
	return phys_to_virt(address - MCPCIA_DMA_WIN_BASE);
}

/*
 * I/O functions:
 *
 * MCPCIA, the RAWHIDE family PCI/memory support chipset for the EV5 (21164)
 * and EV56 (21164a) processors, can use either a sparse address mapping
 * scheme, or the so-called byte-word PCI address space, to get at PCI memory
 * and I/O.
 *
 * Unfortunately, we can't use BWIO with EV5, so for now, we always use SPARSE.
 */

#define vuip	volatile unsigned int *
#define vulp	volatile unsigned long *

#ifdef DISABLE_BWIO_ENABLED

extern inline unsigned int __inb(unsigned long addr)
{
	register unsigned long result;

	__asm__ __volatile__ (
		 "ldbu %0,%1"
		 : "=r" (result)
		 : "m"  (*(unsigned char *)(addr+MCPCIA_BW_IO)));

	return result;
}

extern inline void __outb(unsigned char b, unsigned long addr)
{
	__asm__ __volatile__ (
		 "stb %1,%0\n\t"
		 "mb"
		 : : "m" (*(unsigned char *)(addr+MCPCIA_BW_IO)), "r" (b));
}

extern inline unsigned int __inw(unsigned long addr)
{
	register unsigned long result;

	__asm__ __volatile__ (
		 "ldwu %0,%1"
		 : "=r" (result)
		 : "m"  (*(unsigned short *)(addr+MCPCIA_BW_IO)));

	return result;
}

extern inline void __outw(unsigned short b, unsigned long addr)
{
	__asm__ __volatile__ (
		 "stw %1,%0\n\t"
		 "mb"
		 : : "m" (*(unsigned short *)(addr+MCPCIA_BW_IO)), "r" (b));
}

extern inline unsigned int __inl(unsigned long addr)
{
	register unsigned long result;

	__asm__ __volatile__ (
		 "ldl %0,%1"
		 : "=r" (result)
		 : "m"  (*(unsigned int *)(addr+MCPCIA_BW_IO)));

	return result;
}

extern inline void __outl(unsigned int b, unsigned long addr)
{
	__asm__ __volatile__ (
		 "stl %1,%0\n\t"
		 "mb"
		 : : "m" (*(unsigned int *)(addr+MCPCIA_BW_IO)), "r" (b));
}

#define inb(port) __inb((port))
#define inw(port) __inw((port))
#define inl(port) __inl((port))

#define outb(x, port) __outb((x),(port))
#define outw(x, port) __outw((x),(port))
#define outl(x, port) __outl((x),(port))

#else /* BWIO_ENABLED */

extern inline unsigned int __inb(unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	long result = *(vuip) ((addr << 5) + MCPCIA_IO(hose) + 0x00);
	result >>= (addr & 3) * 8;
	return 0xffUL & result;
}

extern inline void __outb(unsigned char b, unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	unsigned int w;

	w = __kernel_insbl(b, addr & 3);
	*(vuip) ((addr << 5) + MCPCIA_IO(hose) + 0x00) = w;
	mb();
}

extern inline unsigned int __inw(unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	long result = *(vuip) ((addr << 5) + MCPCIA_IO(hose) + 0x08);
	result >>= (addr & 3) * 8;
	return 0xffffUL & result;
}

extern inline void __outw(unsigned short b, unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	unsigned int w;

	w = __kernel_inswl(b, addr & 3);
	*(vuip) ((addr << 5) + MCPCIA_IO(hose) + 0x08) = w;
	mb();
}

extern inline unsigned int __inl(unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	return *(vuip) ((addr << 5) + MCPCIA_IO(hose) + 0x18);
}

extern inline void __outl(unsigned int b, unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	*(vuip) ((addr << 5) + MCPCIA_IO(hose) + 0x18) = b;
	mb();
}

#define inb(port) \
(__builtin_constant_p((port))?__inb(port):_inb(port))

#define outb(x, port) \
(__builtin_constant_p((port))?__outb((x),(port)):_outb((x),(port)))

#endif /* BWIO_ENABLED */


/*
 * Memory functions.  64-bit and 32-bit accesses are done through
 * dense memory space, everything else through sparse space.
 * 
 * For reading and writing 8 and 16 bit quantities we need to 
 * go through one of the three sparse address mapping regions
 * and use the HAE_MEM CSR to provide some bits of the address.
 * The following few routines use only sparse address region 1
 * which gives 1Gbyte of accessible space which relates exactly
 * to the amount of PCI memory mapping *into* system address space.
 * See p 6-17 of the specification but it looks something like this:
 *
 * 21164 Address:
 * 
 *          3         2         1
 * 9876543210987654321098765432109876543210
 * 1ZZZZ0.PCI.QW.Address............BBLL                 
 *
 * ZZ = SBZ
 * BB = Byte offset
 * LL = Transfer length
 *
 * PCI Address:
 *
 * 3         2         1
 * 10987654321098765432109876543210
 * HHH....PCI.QW.Address........ 00
 *
 * HHH = 31:29 HAE_MEM CSR
 * 
 */

#ifdef DISABLE_BWIO_ENABLED

extern inline unsigned long __readb(unsigned long addr)
{
	register unsigned long result;

	__asm__ __volatile__ (
		 "ldbu %0,%1"
		 : "=r" (result)
		 : "m"  (*(volatile unsigned char *)(addr+MCPCIA_BW_MEM)));

	return result;
}

extern inline unsigned long __readw(unsigned long addr)
{
	register unsigned long result;

	__asm__ __volatile__ (
		 "ldwu %0,%1"
		 : "=r" (result)
		 : "m"  (*(volatile unsigned short *)(addr+MCPCIA_BW_MEM)));

	return result;
}

extern inline unsigned long __readl(unsigned long addr)
{
	return *(vuip)(addr + MCPCIA_BW_MEM);
}

extern inline unsigned long __readq(unsigned long addr)
{
	return *(vulp)(addr + MCPCIA_BW_MEM);
}

extern inline void __writeb(unsigned char b, unsigned long addr)
{
	__asm__ __volatile__ (
		 "stb %1,%0\n\t"
		 "mb"
		 : "m" (*(volatile unsigned char *)(addr+MCPCIA_BW_MEM))
		 : "r" (b));
}

extern inline void __writew(unsigned short b, unsigned long addr)
{
	__asm__ __volatile__ (
		 "stw %1,%0\n\t"
		 "mb"
		 : "m" (*(volatile unsigned short *)(addr+MCPCIA_BW_MEM))
		 : "r" (b));
}

extern inline void __writel(unsigned int b, unsigned long addr)
{
	*(vuip)(addr+MCPCIA_BW_MEM) = b;
	mb();
}

extern inline void __writeq(unsigned long b, unsigned long addr)
{
	*(vulp)(addr+MCPCIA_BW_MEM) = b;
	mb();
}

#define readb(addr) __readb((addr))
#define readw(addr) __readw((addr))

#define writeb(b, addr) __writeb((b),(addr))
#define writew(b, addr) __writew((b),(addr))

#else /* BWIO_ENABLED */

#ifdef CONFIG_ALPHA_SRM_SETUP

extern unsigned long mcpcia_sm_base_r1, mcpcia_sm_base_r2, mcpcia_sm_base_r3;

extern inline unsigned long __readb(unsigned long addr)
{
	unsigned long result, shift, work;

	if ((addr >= mcpcia_sm_base_r1) &&
	    (addr <= (mcpcia_sm_base_r1 + MEM_R1_MASK)))
	  work = (((addr & MEM_R1_MASK) << 5) + MCPCIA_SPARSE_MEM + 0x00);
	else
	if ((addr >= mcpcia_sm_base_r2) &&
	    (addr <= (mcpcia_sm_base_r2 + MEM_R2_MASK)))
	  work = (((addr & MEM_R2_MASK) << 5) + MCPCIA_SPARSE_MEM_R2 + 0x00);
	else
	if ((addr >= mcpcia_sm_base_r3) &&
	    (addr <= (mcpcia_sm_base_r3 + MEM_R3_MASK)))
	  work = (((addr & MEM_R3_MASK) << 5) + MCPCIA_SPARSE_MEM_R3 + 0x00);
	else
	{
#if 0
	  printk("__readb: address 0x%lx not covered by HAE\n", addr);
#endif
	  return 0x0ffUL;
	}
	shift = (addr & 0x3) << 3;
	result = *(vuip) work;
	result >>= shift;
	return 0x0ffUL & result;
}

extern inline unsigned long __readw(unsigned long addr)
{
	unsigned long result, shift, work;

	if ((addr >= mcpcia_sm_base_r1) &&
	    (addr <= (mcpcia_sm_base_r1 + MEM_R1_MASK)))
	  work = (((addr & MEM_R1_MASK) << 5) + MCPCIA_SPARSE_MEM + 0x08);
	else
	if ((addr >= mcpcia_sm_base_r2) &&
	    (addr <= (mcpcia_sm_base_r2 + MEM_R2_MASK)))
	  work = (((addr & MEM_R2_MASK) << 5) + MCPCIA_SPARSE_MEM_R2 + 0x08);
	else
	if ((addr >= mcpcia_sm_base_r3) &&
	    (addr <= (mcpcia_sm_base_r3 + MEM_R3_MASK)))
	  work = (((addr & MEM_R3_MASK) << 5) + MCPCIA_SPARSE_MEM_R3 + 0x08);
	else
	{
#if 0
	  printk("__readw: address 0x%lx not covered by HAE\n", addr);
#endif
	  return 0x0ffffUL;
	}
	shift = (addr & 0x3) << 3;
	result = *(vuip) work;
	result >>= shift;
	return 0x0ffffUL & result;
}

extern inline void __writeb(unsigned char b, unsigned long addr)
{
	unsigned long work;

	if ((addr >= mcpcia_sm_base_r1) &&
	    (addr <= (mcpcia_sm_base_r1 + MEM_R1_MASK)))
	  work = (((addr & MEM_R1_MASK) << 5) + MCPCIA_SPARSE_MEM + 0x00);
	else
	if ((addr >= mcpcia_sm_base_r2) &&
	    (addr <= (mcpcia_sm_base_r2 + MEM_R2_MASK)))
	  work = (((addr & MEM_R2_MASK) << 5) + MCPCIA_SPARSE_MEM_R2 + 0x00);
	else
	if ((addr >= mcpcia_sm_base_r3) &&
	    (addr <= (mcpcia_sm_base_r3 + MEM_R3_MASK)))
	  work = (((addr & MEM_R3_MASK) << 5) + MCPCIA_SPARSE_MEM_R3 + 0x00);
	else
	{
#if 0
	  printk("__writeb: address 0x%lx not covered by HAE\n", addr);
#endif
	  return;
	}
	*(vuip) work = b * 0x01010101;
}

extern inline void __writew(unsigned short b, unsigned long addr)
{
	unsigned long work;

	if ((addr >= mcpcia_sm_base_r1) &&
	    (addr <= (mcpcia_sm_base_r1 + MEM_R1_MASK)))
	  work = (((addr & MEM_R1_MASK) << 5) + MCPCIA_SPARSE_MEM + 0x00);
	else
	if ((addr >= mcpcia_sm_base_r2) &&
	    (addr <= (mcpcia_sm_base_r2 + MEM_R2_MASK)))
	  work = (((addr & MEM_R2_MASK) << 5) + MCPCIA_SPARSE_MEM_R2 + 0x00);
	else
	if ((addr >= mcpcia_sm_base_r3) &&
	    (addr <= (mcpcia_sm_base_r3 + MEM_R3_MASK)))
	  work = (((addr & MEM_R3_MASK) << 5) + MCPCIA_SPARSE_MEM_R3 + 0x00);
	else
	{
#if 0
	  printk("__writew: address 0x%lx not covered by HAE\n", addr);
#endif
	  return;
	}
	*(vuip) work = b * 0x00010001;
}

#else /* SRM_SETUP */

extern inline unsigned long __readb(unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	unsigned long result, shift, msb, work, temp;

	shift = (addr & 0x3) << 3;
	msb = addr & 0xE0000000UL;
	temp = addr & MEM_R1_MASK;
	if (msb != hae.cache) {
	  set_hae(msb);
	}
	work = ((temp << 5) + MCPCIA_SPARSE(hose) + 0x00);
	result = *(vuip) work;
	result >>= shift;
	return 0x0ffUL & result;
}

extern inline unsigned long __readw(unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	unsigned long result, shift, msb, work, temp;

	shift = (addr & 0x3) << 3;
	msb = addr & 0xE0000000UL;
	temp = addr & MEM_R1_MASK ;
	if (msb != hae.cache) {
	  set_hae(msb);
	}
	work = ((temp << 5) + MCPCIA_SPARSE(hose) + 0x08);
	result = *(vuip) work;
	result >>= shift;
	return 0x0ffffUL & result;
}

extern inline void __writeb(unsigned char b, unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
        unsigned long msb; 

	msb = addr & 0xE0000000;
	addr &= MEM_R1_MASK;
	if (msb != hae.cache) {
	  set_hae(msb);
	}
	*(vuip) ((addr << 5) + MCPCIA_SPARSE(hose) + 0x00) = b * 0x01010101;
}

extern inline void __writew(unsigned short b, unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
        unsigned long msb ; 

	msb = addr & 0xE0000000 ;
	addr &= MEM_R1_MASK ;
	if (msb != hae.cache) {
	  set_hae(msb);
	}
	*(vuip) ((addr << 5) + MCPCIA_SPARSE(hose) + 0x08) = b * 0x00010001;
}
#endif /* SRM_SETUP */

extern inline unsigned long __readl(unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	return *(vuip) (addr + MCPCIA_DENSE(hose));
}

extern inline unsigned long __readq(unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	return *(vulp) (addr + MCPCIA_DENSE(hose));
}

extern inline void __writel(unsigned int b, unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	*(vuip) (addr + MCPCIA_DENSE(hose)) = b;
}

extern inline void __writeq(unsigned long b, unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	*(vulp) (addr + MCPCIA_DENSE(hose)) = b;
}

#endif /* BWIO_ENABLED */

#define readl(a)	__readl((unsigned long)(a))
#define readq(a)	__readq((unsigned long)(a))
#define writel(v,a)	__writel((v),(unsigned long)(a))
#define writeq(v,a)	__writeq((v),(unsigned long)(a))

#undef vuip
#undef vulp

struct linux_hose_info {
        struct pci_bus                  pci_bus;
	struct linux_hose_info         *next;
	unsigned long                   pci_io_space;
	unsigned long                   pci_mem_space;
	unsigned long                   pci_config_space;
	unsigned long                   pci_sparse_space;
        unsigned int                    pci_first_busno;
        unsigned int                    pci_last_busno;
	unsigned int                    pci_hose_index;
};

extern unsigned long mcpcia_init (unsigned long, unsigned long);
extern void mcpcia_fixup (void);

#endif /* __KERNEL__ */

/*
 * Data structure for handling MCPCIA machine checks:
 */
struct el_MCPCIA_uncorrected_frame_mcheck {
        struct el_common header;
        struct el_common_EV5_uncorrectable_mcheck procdata;
};

#define RTC_PORT(x)	(0x70 + (x))
#define RTC_ADDR(x)	(0x80 | (x))
#define RTC_ALWAYS_BCD	0

#endif /* __ALPHA_MCPCIA__H__ */
