#ifndef __ALPHA_PYXIS__H__
#define __ALPHA_PYXIS__H__

#include <linux/config.h>
#include <linux/types.h>

/*
 * PYXIS is the internal name for a core logic chipset which provides
 * memory controller and PCI access for the 21164A chip based systems.
 *
 * This file is based on:
 *
 * Pyxis Chipset Spec
 * 14-Jun-96
 * Rev. X2.0
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


/* CIA ADDRESS BIT DEFINITIONS
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
#define PYXIS_DMA_WIN_BASE_DEFAULT    (1024*1024*1024)
#define PYXIS_DMA_WIN_SIZE_DEFAULT    (1024*1024*1024)

extern unsigned int PYXIS_DMA_WIN_BASE;
extern unsigned int PYXIS_DMA_WIN_SIZE;

#else /* SRM_SETUP */
#define PYXIS_DMA_WIN_BASE	(1024*1024*1024)
#define PYXIS_DMA_WIN_SIZE	(1024*1024*1024)
#endif /* SRM_SETUP */

/*
 *  General Registers
 */
#define PYXIS_REV			(IDENT_ADDR + 0x8740000080UL)
#define PYXIS_PCI_LAT			(IDENT_ADDR + 0x87400000C0UL)
#define PYXIS_CTRL			(IDENT_ADDR + 0x8740000100UL)
#define PYXIS_CTRL1			(IDENT_ADDR + 0x8740000140UL)
#define PYXIS_FLASH_CTRL		(IDENT_ADDR + 0x8740000200UL)

#define PYXIS_HAE_MEM			(IDENT_ADDR + 0x8740000400UL)
#define PYXIS_HAE_IO			(IDENT_ADDR + 0x8740000440UL)
#define PYXIS_CFG			(IDENT_ADDR + 0x8740000480UL)

/*
 * Diagnostic Registers
 */
#define PYXIS_DIAG			(IDENT_ADDR + 0x8740002000UL)
#define PYXIS_DIAG_CHECK		(IDENT_ADDR + 0x8740003000UL)

/*
 * Performance Monitor registers
 */
#define PYXIS_PERF_MONITOR		(IDENT_ADDR + 0x8740004000UL)
#define PYXIS_PERF_CONTROL		(IDENT_ADDR + 0x8740004040UL)

/*
 * Error registers
 */
#define PYXIS_ERR			(IDENT_ADDR + 0x8740008200UL)
#define PYXIS_STAT			(IDENT_ADDR + 0x8740008240UL)
#define PYXIS_ERR_MASK			(IDENT_ADDR + 0x8740008280UL)
#define PYXIS_SYN			(IDENT_ADDR + 0x8740008300UL)
#define PYXIS_ERR_DATA			(IDENT_ADDR + 0x8740008308UL)

#define PYXIS_MEAR			(IDENT_ADDR + 0x8740008400UL)
#define PYXIS_MESR			(IDENT_ADDR + 0x8740008440UL)
#define PYXIS_PCI_ERR0			(IDENT_ADDR + 0x8740008800UL)
#define PYXIS_PCI_ERR1			(IDENT_ADDR + 0x8740008840UL)
#define PYXIS_PCI_ERR2			(IDENT_ADDR + 0x8740008880UL)

/*
 * PCI Address Translation Registers.
 */
#define PYXIS_TBIA			(IDENT_ADDR + 0x8760000100UL)

#define PYXIS_W0_BASE			(IDENT_ADDR + 0x8760000400UL)
#define PYXIS_W0_MASK			(IDENT_ADDR + 0x8760000440UL)
#define PYXIS_T0_BASE			(IDENT_ADDR + 0x8760000480UL)

#define PYXIS_W1_BASE			(IDENT_ADDR + 0x8760000500UL)
#define PYXIS_W1_MASK			(IDENT_ADDR + 0x8760000540UL)
#define PYXIS_T1_BASE			(IDENT_ADDR + 0x8760000580UL)

#define PYXIS_W2_BASE			(IDENT_ADDR + 0x8760000600UL)
#define PYXIS_W2_MASK			(IDENT_ADDR + 0x8760000640UL)
#define PYXIS_T2_BASE			(IDENT_ADDR + 0x8760000680UL)

#define PYXIS_W3_BASE			(IDENT_ADDR + 0x8760000700UL)
#define PYXIS_W3_MASK			(IDENT_ADDR + 0x8760000740UL)
#define PYXIS_T3_BASE			(IDENT_ADDR + 0x8760000780UL)

/*
 * Memory Control registers
 */
#define PYXIS_MCR			(IDENT_ADDR + 0x8750000000UL)

/*
 * Memory spaces:
 */
#define PYXIS_IACK_SC		        (IDENT_ADDR + 0x8720000000UL)
#define PYXIS_CONF		        (IDENT_ADDR + 0x8700000000UL)
#define PYXIS_IO			(IDENT_ADDR + 0x8580000000UL)
#define PYXIS_SPARSE_MEM		(IDENT_ADDR + 0x8000000000UL)
#define PYXIS_SPARSE_MEM_R2		(IDENT_ADDR + 0x8400000000UL)
#define PYXIS_SPARSE_MEM_R3		(IDENT_ADDR + 0x8500000000UL)
#define PYXIS_DENSE_MEM		        (IDENT_ADDR + 0x8600000000UL)

/*
 * Byte/Word PCI Memory Spaces:
 */
#define PYXIS_BW_MEM			(IDENT_ADDR + 0x8800000000UL)
#define PYXIS_BW_IO			(IDENT_ADDR + 0x8900000000UL)
#define PYXIS_BW_CFG_0			(IDENT_ADDR + 0x8a00000000UL)
#define PYXIS_BW_CFG_1			(IDENT_ADDR + 0x8b00000000UL)

/*
 * Interrupt Control registers
 */
#define PYXIS_INT_REQ			(IDENT_ADDR + 0x87A0000000UL)
#define PYXIS_INT_MASK			(IDENT_ADDR + 0x87A0000040UL)
#define PYXIS_INT_HILO			(IDENT_ADDR + 0x87A00000C0UL)
#define PYXIS_INT_ROUTE			(IDENT_ADDR + 0x87A0000140UL)
#define PYXIS_GPO			(IDENT_ADDR + 0x87A0000180UL)
#define PYXIS_INT_CNFG			(IDENT_ADDR + 0x87A00001C0UL)
#define PYXIS_RT_COUNT			(IDENT_ADDR + 0x87A0000200UL)
#define PYXIS_INT_TIME			(IDENT_ADDR + 0x87A0000240UL)
#define PYXIS_IIC_CTRL			(IDENT_ADDR + 0x87A00002C0UL)

/*
 * Bit definitions for I/O Controller status register 0:
 */
#define PYXIS_STAT0_CMD			0xf
#define PYXIS_STAT0_ERR			(1<<4)
#define PYXIS_STAT0_LOST		(1<<5)
#define PYXIS_STAT0_THIT		(1<<6)
#define PYXIS_STAT0_TREF		(1<<7)
#define PYXIS_STAT0_CODE_SHIFT		8
#define PYXIS_STAT0_CODE_MASK		0x7
#define PYXIS_STAT0_P_NBR_SHIFT		13
#define PYXIS_STAT0_P_NBR_MASK		0x7ffff

#define HAE_ADDRESS	                PYXIS_HAE_MEM

#ifdef __KERNEL__

/*
 * Translate physical memory address as seen on (PCI) bus into
 * a kernel virtual address and vv.
 */
#if defined(CONFIG_ALPHA_RUFFIAN)
/* Ruffian doesn't do 1G PCI window */

extern inline unsigned long virt_to_bus(void * address)
{
	return virt_to_phys(address);
}

extern inline void * bus_to_virt(unsigned long address)
{
	return phys_to_virt(address);
}
#else /* RUFFIAN */
extern inline unsigned long virt_to_bus(void * address)
{
	return virt_to_phys(address) + PYXIS_DMA_WIN_BASE;
}

extern inline void * bus_to_virt(unsigned long address)
{
	return phys_to_virt(address - PYXIS_DMA_WIN_BASE);
}
#endif /* RUFFIAN */

/*
 * I/O functions:
 *
 * PYXIS, the 21174 PCI/memory support chipset for the EV56 (21164A)
 * and PCA56 (21164PC) processors, can use either a sparse address
 * mapping scheme, or the so-called byte-word PCI address space, to
 * get at PCI memory and I/O.
 */

#define vuip	volatile unsigned int *

#ifdef BWIO_ENABLED

extern inline unsigned int __inb(unsigned long addr)
{
	register unsigned long result;

	__asm__ __volatile__ (
		 "ldbu %0,%1"
		 : "=r" (result)
		 : "m"  (*(unsigned char *)(addr+PYXIS_BW_IO)));

	return result;
}

extern inline void __outb(unsigned char b, unsigned long addr)
{
	__asm__ __volatile__ (
		 "stb %1,%0\n\t"
		 "mb"
		 : : "m" (*(unsigned char *)(addr+PYXIS_BW_IO)), "r" (b));
}

extern inline unsigned int __inw(unsigned long addr)
{
	register unsigned long result;

	__asm__ __volatile__ (
		 "ldwu %0,%1"
		 : "=r" (result)
		 : "m"  (*(unsigned short *)(addr+PYXIS_BW_IO)));

	return result;
}

extern inline void __outw(unsigned short b, unsigned long addr)
{
	__asm__ __volatile__ (
		 "stw %1,%0\n\t"
		 "mb"
		 : : "m" (*(unsigned short *)(addr+PYXIS_BW_IO)), "r" (b));
}

extern inline unsigned int __inl(unsigned long addr)
{
	register unsigned long result;

	__asm__ __volatile__ (
		 "ldl %0,%1"
		 : "=r" (result)
		 : "m"  (*(unsigned int *)(addr+PYXIS_BW_IO)));

	return result;
}

extern inline void __outl(unsigned int b, unsigned long addr)
{
	__asm__ __volatile__ (
		 "stl %1,%0\n\t"
		 "mb"
		 : : "m" (*(unsigned int *)(addr+PYXIS_BW_IO)), "r" (b));
}

#define inb(port) __inb((port))
#define inw(port) __inw((port))
#define inl(port) __inl((port))

#define outb(x, port) __outb((x),(port))
#define outw(x, port) __outw((x),(port))
#define outl(x, port) __outl((x),(port))

#else /* BWIO_ENABLED */

extern inline unsigned int __inb(unsigned long addr)
{
	long result = *(vuip) ((addr << 5) + PYXIS_IO + 0x00);
	result >>= (addr & 3) * 8;
	return 0xffUL & result;
}

extern inline void __outb(unsigned char b, unsigned long addr)
{
	unsigned int w;

	asm ("insbl %2,%1,%0" : "r="(w) : "ri"(addr & 0x3), "r"(b));
	*(vuip) ((addr << 5) + PYXIS_IO + 0x00) = w;
	mb();
}

extern inline unsigned int __inw(unsigned long addr)
{
	long result = *(vuip) ((addr << 5) + PYXIS_IO + 0x08);
	result >>= (addr & 3) * 8;
	return 0xffffUL & result;
}

extern inline void __outw(unsigned short b, unsigned long addr)
{
	unsigned int w;

	asm ("inswl %2,%1,%0" : "r="(w) : "ri"(addr & 0x3), "r"(b));
	*(vuip) ((addr << 5) + PYXIS_IO + 0x08) = w;
	mb();
}

extern inline unsigned int __inl(unsigned long addr)
{
	return *(vuip) ((addr << 5) + PYXIS_IO + 0x18);
}

extern inline void __outl(unsigned int b, unsigned long addr)
{
	*(vuip) ((addr << 5) + PYXIS_IO + 0x18) = b;
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

#ifdef BWIO_ENABLED

extern inline unsigned long __readb(unsigned long addr)
{
	register unsigned long result;

	__asm__ __volatile__ (
		 "ldbu %0,%1"
		 : "=r" (result)
		 : "m"  (*(unsigned char *)(addr+PYXIS_BW_MEM)));

	return result;
}

extern inline unsigned long __readw(unsigned long addr)
{
	register unsigned long result;

	__asm__ __volatile__ (
		 "ldwu %0,%1"
		 : "=r" (result)
		 : "m"  (*(unsigned short *)(addr+PYXIS_BW_MEM)));

	return result;
}

extern inline unsigned long __readl(unsigned long addr)
{
	register unsigned long result;

	__asm__ __volatile__ (
		 "ldl %0,%1"
		 : "=r" (result)
		 : "m"  (*(unsigned int *)(addr+PYXIS_BW_MEM)));

	return result;
}

extern inline void __writeb(unsigned char b, unsigned long addr)
{
	__asm__ __volatile__ (
		 "stb %1,%0\n\t"
		 "mb"
		 : : "m" (*(unsigned char *)(addr+PYXIS_BW_MEM)), "r" (b));
}

extern inline void __writew(unsigned short b, unsigned long addr)
{
	__asm__ __volatile__ (
		 "stw %1,%0\n\t"
		 "mb"
		 : : "m" (*(unsigned short *)(addr+PYXIS_BW_MEM)), "r" (b));
}

extern inline void __writel(unsigned int b, unsigned long addr)
{
	__asm__ __volatile__ (
		 "stl %1,%0\n\t"
		 "mb"
		 : : "m" (*(unsigned int *)(addr+PYXIS_BW_MEM)), "r" (b));
}

#define readb(addr) __readb((addr))
#define readw(addr) __readw((addr))

#define writeb(b, addr) __writeb((b),(addr))
#define writew(b, addr) __writew((b),(addr))

#else /* BWIO_ENABLED */

#ifdef CONFIG_ALPHA_SRM_SETUP

extern unsigned long pyxis_sm_base_r1, pyxis_sm_base_r2, pyxis_sm_base_r3;

extern inline unsigned long __readb(unsigned long addr)
{
	unsigned long result, shift, work;

	if ((addr >= pyxis_sm_base_r1) &&
	    (addr <= (pyxis_sm_base_r1 + MEM_R1_MASK)))
	  work = (((addr & MEM_R1_MASK) << 5) + PYXIS_SPARSE_MEM + 0x00);
	else
	if ((addr >= pyxis_sm_base_r2) &&
	    (addr <= (pyxis_sm_base_r2 + MEM_R2_MASK)))
	  work = (((addr & MEM_R2_MASK) << 5) + PYXIS_SPARSE_MEM_R2 + 0x00);
	else
	if ((addr >= pyxis_sm_base_r3) &&
	    (addr <= (pyxis_sm_base_r3 + MEM_R3_MASK)))
	  work = (((addr & MEM_R3_MASK) << 5) + PYXIS_SPARSE_MEM_R3 + 0x00);
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

	if ((addr >= pyxis_sm_base_r1) &&
	    (addr <= (pyxis_sm_base_r1 + MEM_R1_MASK)))
	  work = (((addr & MEM_R1_MASK) << 5) + PYXIS_SPARSE_MEM + 0x08);
	else
	if ((addr >= pyxis_sm_base_r2) &&
	    (addr <= (pyxis_sm_base_r2 + MEM_R2_MASK)))
	  work = (((addr & MEM_R2_MASK) << 5) + PYXIS_SPARSE_MEM_R2 + 0x08);
	else
	if ((addr >= pyxis_sm_base_r3) &&
	    (addr <= (pyxis_sm_base_r3 + MEM_R3_MASK)))
	  work = (((addr & MEM_R3_MASK) << 5) + PYXIS_SPARSE_MEM_R3 + 0x08);
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

	if ((addr >= pyxis_sm_base_r1) &&
	    (addr <= (pyxis_sm_base_r1 + MEM_R1_MASK)))
	  work = (((addr & MEM_R1_MASK) << 5) + PYXIS_SPARSE_MEM + 0x00);
	else
	if ((addr >= pyxis_sm_base_r2) &&
	    (addr <= (pyxis_sm_base_r2 + MEM_R2_MASK)))
	  work = (((addr & MEM_R2_MASK) << 5) + PYXIS_SPARSE_MEM_R2 + 0x00);
	else
	if ((addr >= pyxis_sm_base_r3) &&
	    (addr <= (pyxis_sm_base_r3 + MEM_R3_MASK)))
	  work = (((addr & MEM_R3_MASK) << 5) + PYXIS_SPARSE_MEM_R3 + 0x00);
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

	if ((addr >= pyxis_sm_base_r1) &&
	    (addr <= (pyxis_sm_base_r1 + MEM_R1_MASK)))
	  work = (((addr & MEM_R1_MASK) << 5) + PYXIS_SPARSE_MEM + 0x00);
	else
	if ((addr >= pyxis_sm_base_r2) &&
	    (addr <= (pyxis_sm_base_r2 + MEM_R2_MASK)))
	  work = (((addr & MEM_R2_MASK) << 5) + PYXIS_SPARSE_MEM_R2 + 0x00);
	else
	if ((addr >= pyxis_sm_base_r3) &&
	    (addr <= (pyxis_sm_base_r3 + MEM_R3_MASK)))
	  work = (((addr & MEM_R3_MASK) << 5) + PYXIS_SPARSE_MEM_R3 + 0x00);
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

extern inline unsigned long __readb(unsigned long addr)
{
	unsigned long result, shift, msb, work, temp;

	shift = (addr & 0x3) << 3;
	msb = addr & 0xE0000000UL;
	temp = addr & MEM_R1_MASK ;
	if (msb != hae.cache) {
	  set_hae(msb);
	}
	work = ((temp << 5) + PYXIS_SPARSE_MEM + 0x00);
	result = *(vuip) work;
	result >>= shift;
	return 0x0ffUL & result;
}

extern inline unsigned long __readw(unsigned long addr)
{
	unsigned long result, shift, msb, work, temp;

	shift = (addr & 0x3) << 3;
	msb = addr & 0xE0000000UL;
	temp = addr & MEM_R1_MASK ;
	if (msb != hae.cache) {
	  set_hae(msb);
	}
	work = ((temp << 5) + PYXIS_SPARSE_MEM + 0x08);
	result = *(vuip) work;
	result >>= shift;
	return 0x0ffffUL & result;
}

extern inline void __writeb(unsigned char b, unsigned long addr)
{
        unsigned long msb ; 

	msb = addr & 0xE0000000 ;
	addr &= MEM_R1_MASK ;
	if (msb != hae.cache) {
	  set_hae(msb);
	}
	*(vuip) ((addr << 5) + PYXIS_SPARSE_MEM + 0x00) = b * 0x01010101;
}

extern inline void __writew(unsigned short b, unsigned long addr)
{
        unsigned long msb ; 

	msb = addr & 0xE0000000 ;
	addr &= MEM_R1_MASK ;
	if (msb != hae.cache) {
	  set_hae(msb);
	}
	*(vuip) ((addr << 5) + PYXIS_SPARSE_MEM + 0x08) = b * 0x00010001;
}
#endif /* SRM_SETUP */

extern inline unsigned long __readl(unsigned long addr)
{
	return *(vuip) (addr + PYXIS_DENSE_MEM);
}

extern inline void __writel(unsigned int b, unsigned long addr)
{
	*(vuip) (addr + PYXIS_DENSE_MEM) = b;
}

#endif /* BWIO_ENABLED */

#define readl(a)	__readl((unsigned long)(a))
#define writel(v,a)	__writel((v),(unsigned long)(a))

#undef vuip

extern unsigned long pyxis_init (unsigned long mem_start,
				 unsigned long mem_end);

#endif /* __KERNEL__ */

/*
 * Data structure for handling PYXIS machine checks:
 */
struct el_PYXIS_sysdata_mcheck {
    u_long      coma_gcr;                       
    u_long      coma_edsr;                      
    u_long      coma_ter;                       
    u_long      coma_elar;                      
    u_long      coma_ehar;                      
    u_long      coma_ldlr;                      
    u_long      coma_ldhr;                      
    u_long      coma_base0;                     
    u_long      coma_base1;                     
    u_long      coma_base2;                     
    u_long      coma_cnfg0;                     
    u_long      coma_cnfg1;                     
    u_long      coma_cnfg2;                     
    u_long      epic_dcsr;                      
    u_long      epic_pear;                      
    u_long      epic_sear;                      
    u_long      epic_tbr1;                      
    u_long      epic_tbr2;                      
    u_long      epic_pbr1;                      
    u_long      epic_pbr2;                      
    u_long      epic_pmr1;                      
    u_long      epic_pmr2;                      
    u_long      epic_harx1;                     
    u_long      epic_harx2;                     
    u_long      epic_pmlt;                      
    u_long      epic_tag0;                      
    u_long      epic_tag1;                      
    u_long      epic_tag2;                      
    u_long      epic_tag3;                      
    u_long      epic_tag4;                      
    u_long      epic_tag5;                      
    u_long      epic_tag6;                      
    u_long      epic_tag7;                      
    u_long      epic_data0;                     
    u_long      epic_data1;                     
    u_long      epic_data2;                     
    u_long      epic_data3;                     
    u_long      epic_data4;                     
    u_long      epic_data5;                     
    u_long      epic_data6;                     
    u_long      epic_data7;                     
};

#define RTC_PORT(x)	(0x70 + (x))
#define RTC_ADDR(x)	(0x80 | (x))
#define RTC_ALWAYS_BCD	0

#endif /* __ALPHA_PYXIS__H__ */
