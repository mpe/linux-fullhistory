#ifndef __ALPHA_MCPCIA__H__
#define __ALPHA_MCPCIA__H__

#include <linux/config.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <asm/compiler.h>

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
**  I/O procedures                                                        **
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

#define MCPCIA_MEM_MASK 0x07ffffff /* SPARSE Mem region mask is 27 bits */

#define MCPCIA_DMA_WIN_BASE_DEFAULT    (2*1024*1024*1024U)
#define MCPCIA_DMA_WIN_SIZE_DEFAULT    (2*1024*1024*1024U)

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_SRM_SETUP)
#define MCPCIA_DMA_WIN_BASE		alpha_mv.dma_win_base
#define MCPCIA_DMA_WIN_SIZE		alpha_mv.dma_win_size
#else
#define MCPCIA_DMA_WIN_BASE		MCPCIA_DMA_WIN_BASE_DEFAULT
#define MCPCIA_DMA_WIN_SIZE		MCPCIA_DMA_WIN_SIZE_DEFAULT
#endif

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
#define MCPCIA_MC_ERR0(h)	(IDENT_ADDR + 0xf9e0000800UL + HOSE(h))
#define MCPCIA_MC_ERR1(h)	(IDENT_ADDR + 0xf9e0000840UL + HOSE(h))
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
#define _MCPCIA_IACK_SC(h)	(IDENT_ADDR + 0xf9f0003f00UL + HOSE(h))

#define MCPCIA_HAE_ADDRESS	MCPCIA_HAE_MEM(0)
#define MCPCIA_IACK_SC		_MCPCIA_IACK_SC(0)

/*
 * Data structure for handling MCPCIA machine checks:
 */
struct el_MCPCIA_uncorrected_frame_mcheck {
        struct el_common header;
        struct el_common_EV5_uncorrectable_mcheck procdata;
};


#ifdef __KERNEL__

#ifndef __EXTERN_INLINE
#define __EXTERN_INLINE extern inline
#define __IO_EXTERN_INLINE
#endif

/*
 * Translate physical memory address as seen on (PCI) bus into
 * a kernel virtual address and vv.
 */

__EXTERN_INLINE unsigned long mcpcia_virt_to_bus(void * address)
{
	return virt_to_phys(address) + MCPCIA_DMA_WIN_BASE;
}

__EXTERN_INLINE void * mcpcia_bus_to_virt(unsigned long address)
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

#define vucp	volatile unsigned char *
#define vusp	volatile unsigned short *
#define vip	volatile int *
#define vuip	volatile unsigned int *
#define vulp	volatile unsigned long *

#if 0 /* BWIO */
__EXTERN_INLINE unsigned int mcpcia_bw_inb(unsigned long addr)
{
	return __kernel_ldbu(*(vucp)(addr+MCPCIA_BW_IO));
}

__EXTERN_INLINE void mcpcia_bw_outb(unsigned char b, unsigned long addr)
{
	__kernel_stb(b, *(vucp)(addr+MCPCIA_BW_IO));
	mb();
}

__EXTERN_INLINE unsigned int mcpcia_bw_inw(unsigned long addr)
{
	return __kernel_ldwu(*(vusp)(addr+MCPCIA_BW_IO));
}

__EXTERN_INLINE void mcpcia_bw_outw(unsigned short b, unsigned long addr)
{
	__kernel_stw(b, *(vusp)(addr+MCPCIA_BW_IO));
	mb();
}

__EXTERN_INLINE unsigned int mcpcia_bw_inl(unsigned long addr)
{
	return *(vuip)(addr+MCPCIA_BW_IO);
}

__EXTERN_INLINE void mcpcia_bw_outl(unsigned int b, unsigned long addr)
{
	*(vuip)(addr+MCPCIA_BW_IO) = b;
	mb();
}
#endif

__EXTERN_INLINE unsigned int mcpcia_inb(unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	long result = *(vip) ((addr << 5) + MCPCIA_IO(hose) + 0x00);
	return __kernel_extbl(result, addr & 3);
}

__EXTERN_INLINE void mcpcia_outb(unsigned char b, unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	unsigned long w;

	w = __kernel_insbl(b, addr & 3);
	*(vuip) ((addr << 5) + MCPCIA_IO(hose) + 0x00) = w;
	mb();
}

__EXTERN_INLINE unsigned int mcpcia_inw(unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	long result = *(vip) ((addr << 5) + MCPCIA_IO(hose) + 0x08);
	return __kernel_extwl(result, addr & 3);
}

__EXTERN_INLINE void mcpcia_outw(unsigned short b, unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	unsigned long w;

	w = __kernel_inswl(b, addr & 3);
	*(vuip) ((addr << 5) + MCPCIA_IO(hose) + 0x08) = w;
	mb();
}

__EXTERN_INLINE unsigned int mcpcia_inl(unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	return *(vuip) ((addr << 5) + MCPCIA_IO(hose) + 0x18);
}

__EXTERN_INLINE void mcpcia_outl(unsigned int b, unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	*(vuip) ((addr << 5) + MCPCIA_IO(hose) + 0x18) = b;
	mb();
}


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

#if 0 /* BWIO */
__EXTERN_INLINE unsigned long mcpcia_bw_readb(unsigned long addr)
{
	return __kernel_ldbu(*(vucp)(addr+MCPCIA_BW_MEM));
}

__EXTERN_INLINE unsigned long mcpcia_bw_readw(unsigned long addr)
{
	return __kernel_ldbw(*(vusp)(addr+MCPCIA_BW_MEM));
}

__EXTERN_INLINE unsigned long mcpcia_bw_readl(unsigned long addr)
{
	return *(vuip)(addr + MCPCIA_BW_MEM);
}

__EXTERN_INLINE unsigned long mcpcia_bw_readq(unsigned long addr)
{
	return *(vulp)(addr + MCPCIA_BW_MEM);
}

__EXTERN_INLINE void mcpcia_bw_writeb(unsigned char b, unsigned long addr)
{
	__kernel_stb(b, *(vucp)(addr+MCPCIA_BW_MEM));
}

__EXTERN_INLINE void mcpcia_bw_writew(unsigned short b, unsigned long addr)
{
	__kernel_stw(b, *(vusp)(addr+MCPCIA_BW_MEM));
}

__EXTERN_INLINE void mcpcia_bw_writel(unsigned int b, unsigned long addr)
{
	*(vuip)(addr+MCPCIA_BW_MEM) = b;
}

__EXTERN_INLINE void mcpcia_bw_writeq(unsigned long b, unsigned long addr)
{
	*(vulp)(addr+MCPCIA_BW_MEM) = b;
}
#endif

__EXTERN_INLINE unsigned long mcpcia_srm_base(unsigned long addr)
{
	unsigned long mask, base;
	unsigned long hose = (addr >> 32) & 3;

	if (addr >= alpha_mv.sm_base_r1
	    && addr <= alpha_mv.sm_base_r1 + MCPCIA_MEM_MASK) {
		mask = MCPCIA_MEM_MASK;
		base = MCPCIA_SPARSE(hose);
	}
	else
	{
#if 0
	  printk("mcpcia: address 0x%lx not covered by HAE\n", addr);
#endif
	  return 0;
	}

	return ((addr & mask) << 5) + base;
}

__EXTERN_INLINE unsigned long mcpcia_srm_readb(unsigned long addr)
{
	unsigned long result, work;

	if ((work = mcpcia_srm_base(addr)) == 0)
		return 0xff;
	work += 0x00;	/* add transfer length */

	result = *(vip) work;
	return __kernel_extbl(result, addr & 3);
}

__EXTERN_INLINE unsigned long mcpcia_srm_readw(unsigned long addr)
{
	unsigned long result, work;

	if ((work = mcpcia_srm_base(addr)) == 0)
		return 0xffff;
	work += 0x08;	/* add transfer length */

	result = *(vip) work;
	return __kernel_extwl(result, addr & 3);
}

__EXTERN_INLINE void mcpcia_srm_writeb(unsigned char b, unsigned long addr)
{
	unsigned long work = mcpcia_srm_base(addr);
	if (work) {
		work += 0x00;	/* add transfer length */
		*(vuip) work = b * 0x01010101;
	}
}

__EXTERN_INLINE void mcpcia_srm_writew(unsigned short b, unsigned long addr)
{
	unsigned long work = mcpcia_srm_base(addr);
	if (work) {
		work += 0x08;	/* add transfer length */
		*(vuip) work = b * 0x00010001;
	}
}

__EXTERN_INLINE unsigned long mcpcia_readb(unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	unsigned long result, msb, work, temp;

	msb = addr & ~MCPCIA_MEM_MASK;
	temp = addr & MCPCIA_MEM_MASK;
	set_hae(msb);

	work = ((temp << 5) + MCPCIA_SPARSE(hose) + 0x00);
	result = *(vip) work;
	return __kernel_extbl(result, addr & 3);
}

__EXTERN_INLINE unsigned long mcpcia_readw(unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	unsigned long result, msb, work, temp;

	msb = addr & ~MCPCIA_MEM_MASK;
	temp = addr & MCPCIA_MEM_MASK ;
	set_hae(msb);

	work = ((temp << 5) + MCPCIA_SPARSE(hose) + 0x08);
	result = *(vip) work;
	return __kernel_extwl(result, addr & 3);
}

__EXTERN_INLINE void mcpcia_writeb(unsigned char b, unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
        unsigned long msb; 

	msb = addr & ~MCPCIA_MEM_MASK;
	addr &= MCPCIA_MEM_MASK;
	set_hae(msb);

	*(vuip) ((addr << 5) + MCPCIA_SPARSE(hose) + 0x00) = b * 0x01010101;
}

__EXTERN_INLINE void mcpcia_writew(unsigned short b, unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
        unsigned long msb ; 

	msb = addr & ~MCPCIA_MEM_MASK ;
	addr &= MCPCIA_MEM_MASK ;
	set_hae(msb);

	*(vuip) ((addr << 5) + MCPCIA_SPARSE(hose) + 0x08) = b * 0x00010001;
}

__EXTERN_INLINE unsigned long mcpcia_readl(unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	return *(vuip) (addr + MCPCIA_DENSE(hose));
}

__EXTERN_INLINE unsigned long mcpcia_readq(unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	return *(vulp) (addr + MCPCIA_DENSE(hose));
}

__EXTERN_INLINE void mcpcia_writel(unsigned int b, unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	*(vuip) (addr + MCPCIA_DENSE(hose)) = b;
}

__EXTERN_INLINE void mcpcia_writeq(unsigned long b, unsigned long in_addr)
{
	unsigned long addr = in_addr & 0xffffffffUL;
	unsigned long hose = (in_addr >> 32) & 3;
	*(vulp) (addr + MCPCIA_DENSE(hose)) = b;
}

/* Find the DENSE memory area for a given bus address.  */

__EXTERN_INLINE unsigned long mcpcia_dense_mem(unsigned long addr)
{
	return MCPCIA_DENSE((addr >> 32) & 3);
}

#undef vucp
#undef vusp
#undef vip
#undef vuip
#undef vulp

#ifdef __WANT_IO_DEF

#define virt_to_bus	mcpcia_virt_to_bus
#define bus_to_virt	mcpcia_bus_to_virt

#if 0 /* BWIO */
# define __inb		mcpcia_bw_inb
# define __inw		mcpcia_bw_inw
# define __inl		mcpcia_bw_inl
# define __outb		mcpcia_bw_outb
# define __outw		mcpcia_bw_outw
# define __outl		mcpcia_bw_outl
# define __readb	mcpcia_bw_readb
# define __readw	mcpcia_bw_readw
# define __writeb	mcpcia_bw_writeb
# define __writew	mcpcia_bw_writew
# define __readl	mcpcia_bw_readl
# define __readq	mcpcia_bw_readq
# define __writel	mcpcia_bw_writel
# define __writeq	mcpcia_bw_writeq
#else
# define __inb		mcpcia_inb
# define __inw		mcpcia_inw
# define __inl		mcpcia_inl
# define __outb		mcpcia_outb
# define __outw		mcpcia_outw
# define __outl		mcpcia_outl
# ifdef CONFIG_ALPHA_SRM_SETUP
#  define __readb	mcpcia_srm_readb
#  define __readw	mcpcia_srm_readw
#  define __writeb	mcpcia_srm_writeb
#  define __writew	mcpcia_srm_writew
# else
#  define __readb	mcpcia_readb
#  define __readw	mcpcia_readw
#  define __writeb	mcpcia_writeb
#  define __writew	mcpcia_writew
# endif
# define __readl	mcpcia_readl
# define __readq	mcpcia_readq
# define __writel	mcpcia_writel
# define __writeq	mcpcia_writeq
#endif /* BWIO */

#define dense_mem	mcpcia_dense_mem

#if 0 /* BWIO */
# define inb(port) __inb((port))
# define inw(port) __inw((port))
# define inl(port) __inl((port))
# define outb(x, port) __outb((x),(port))
# define outw(x, port) __outw((x),(port))
# define outl(x, port) __outl((x),(port))
# define readb(addr) __readb((addr))
# define readw(addr) __readw((addr))
# define writeb(b, addr) __writeb((b),(addr))
# define writew(b, addr) __writew((b),(addr))
#else
# define inb(port) \
  (__builtin_constant_p((port))?__inb(port):_inb(port))
# define outb(x, port) \
  (__builtin_constant_p((port))?__outb((x),(port)):_outb((x),(port)))
#endif /* BWIO */

#define readl(a)	__readl((unsigned long)(a))
#define readq(a)	__readq((unsigned long)(a))
#define writel(v,a)	__writel((v),(unsigned long)(a))
#define writeq(v,a)	__writeq((v),(unsigned long)(a))

#endif /* __WANT_IO_DEF */

#ifdef __IO_EXTERN_INLINE
#undef __EXTERN_INLINE
#undef __IO_EXTERN_INLINE
#endif

#endif /* __KERNEL__ */

#endif /* __ALPHA_MCPCIA__H__ */
