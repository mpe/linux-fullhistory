#ifndef __ALPHA_CIA__H__
#define __ALPHA_CIA__H__

#include <linux/config.h>
#include <linux/types.h>
#include <asm/compiler.h>

/*
 * CIA is the internal name for the 2117x chipset which provides
 * memory controller and PCI access for the 21164 chip based systems.
 *
 * This file is based on:
 *
 * DECchip 21171 Core Logic Chipset 
 * Technical Reference Manual
 *
 * EC-QE18B-TE
 *
 * david.rusling@reo.mts.dec.com Initial Version.
 *
 */

/*------------------------------------------------------------------------**
**                                                                        **
**  EB164 I/O procedures                                                  **
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

#define CIA_MEM_R1_MASK 0x1fffffff  /* SPARSE Mem region 1 mask is 29 bits */
#define CIA_MEM_R2_MASK 0x07ffffff  /* SPARSE Mem region 2 mask is 27 bits */
#define CIA_MEM_R3_MASK 0x03ffffff  /* SPARSE Mem region 3 mask is 26 bits */

#define CIA_DMA_WIN_BASE_DEFAULT	(1024*1024*1024)
#define CIA_DMA_WIN_SIZE_DEFAULT	(1024*1024*1024)

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_SRM_SETUP)
#define CIA_DMA_WIN_BASE		alpha_mv.dma_win_base
#define CIA_DMA_WIN_SIZE		alpha_mv.dma_win_size
#else
#define CIA_DMA_WIN_BASE		CIA_DMA_WIN_SIZE_DEFAULT
#define CIA_DMA_WIN_SIZE		CIA_DMA_WIN_SIZE_DEFAULT
#endif

/*
 * 21171-CA Control and Status Registers (p4-1)
 */
#define CIA_IOC_CIA_REV               (IDENT_ADDR + 0x8740000080UL)
#define CIA_IOC_PCI_LAT               (IDENT_ADDR + 0x87400000C0UL)
#define CIA_IOC_CIA_CTRL              (IDENT_ADDR + 0x8740000100UL)
#define CIA_IOC_CIA_CNFG              (IDENT_ADDR + 0x8740000140UL)
#define CIA_IOC_HAE_MEM               (IDENT_ADDR + 0x8740000400UL)
#define CIA_IOC_HAE_IO                (IDENT_ADDR + 0x8740000440UL)
#define CIA_IOC_CFG                   (IDENT_ADDR + 0x8740000480UL)
#define CIA_IOC_CACK_EN               (IDENT_ADDR + 0x8740000600UL)

/*
 * 21171-CA Diagnostic Registers (p4-2)
 */
#define CIA_IOC_CIA_DIAG              (IDENT_ADDR + 0x8740002000UL)
#define CIA_IOC_DIAG_CHECK            (IDENT_ADDR + 0x8740003000UL)

/*
 * 21171-CA Performance Monitor registers (p4-3)
 */
#define CIA_IOC_PERF_MONITOR          (IDENT_ADDR + 0x8740004000UL)
#define CIA_IOC_PERF_CONTROL          (IDENT_ADDR + 0x8740004040UL)

/*
 * 21171-CA Error registers (p4-3)
 */
#define CIA_IOC_CPU_ERR0              (IDENT_ADDR + 0x8740008000UL)
#define CIA_IOC_CPU_ERR1              (IDENT_ADDR + 0x8740008040UL)
#define CIA_IOC_CIA_ERR               (IDENT_ADDR + 0x8740008200UL)
#define CIA_IOC_CIA_STAT              (IDENT_ADDR + 0x8740008240UL)
#define CIA_IOC_ERR_MASK              (IDENT_ADDR + 0x8740008280UL)
#define CIA_IOC_CIA_SYN               (IDENT_ADDR + 0x8740008300UL)
#define CIA_IOC_MEM_ERR0              (IDENT_ADDR + 0x8740008400UL)
#define CIA_IOC_MEM_ERR1              (IDENT_ADDR + 0x8740008440UL)
#define CIA_IOC_PCI_ERR0              (IDENT_ADDR + 0x8740008800UL)
#define CIA_IOC_PCI_ERR1              (IDENT_ADDR + 0x8740008840UL)
#define CIA_IOC_PCI_ERR3              (IDENT_ADDR + 0x8740008880UL)

/*
 * 2117A-CA PCI Address Translation Registers.
 */
#define CIA_IOC_PCI_TBIA              (IDENT_ADDR + 0x8760000100UL)

#define CIA_IOC_PCI_W0_BASE           (IDENT_ADDR + 0x8760000400UL)
#define CIA_IOC_PCI_W0_MASK           (IDENT_ADDR + 0x8760000440UL)
#define CIA_IOC_PCI_T0_BASE           (IDENT_ADDR + 0x8760000480UL)

#define CIA_IOC_PCI_W1_BASE           (IDENT_ADDR + 0x8760000500UL)
#define CIA_IOC_PCI_W1_MASK           (IDENT_ADDR + 0x8760000540UL)
#define CIA_IOC_PCI_T1_BASE           (IDENT_ADDR + 0x8760000580UL)

#define CIA_IOC_PCI_W2_BASE           (IDENT_ADDR + 0x8760000600UL)
#define CIA_IOC_PCI_W2_MASK           (IDENT_ADDR + 0x8760000640UL)
#define CIA_IOC_PCI_T2_BASE           (IDENT_ADDR + 0x8760000680UL)

#define CIA_IOC_PCI_W3_BASE           (IDENT_ADDR + 0x8760000700UL)
#define CIA_IOC_PCI_W3_MASK           (IDENT_ADDR + 0x8760000740UL)
#define CIA_IOC_PCI_T3_BASE           (IDENT_ADDR + 0x8760000780UL)

/*
 * 21171-CA System configuration registers (p4-3)
 */
#define CIA_IOC_MCR                   (IDENT_ADDR + 0x8750000000UL)
#define CIA_IOC_MBA0                  (IDENT_ADDR + 0x8750000600UL)
#define CIA_IOC_MBA2                  (IDENT_ADDR + 0x8750000680UL)
#define CIA_IOC_MBA4                  (IDENT_ADDR + 0x8750000700UL)
#define CIA_IOC_MBA6                  (IDENT_ADDR + 0x8750000780UL)
#define CIA_IOC_MBA8                  (IDENT_ADDR + 0x8750000800UL)
#define CIA_IOC_MBAA                  (IDENT_ADDR + 0x8750000880UL)
#define CIA_IOC_MBAC                  (IDENT_ADDR + 0x8750000900UL)
#define CIA_IOC_MBAE                  (IDENT_ADDR + 0x8750000980UL)
#define CIA_IOC_TMG0                  (IDENT_ADDR + 0x8750000B00UL)
#define CIA_IOC_TMG1                  (IDENT_ADDR + 0x8750000B40UL)
#define CIA_IOC_TMG2                  (IDENT_ADDR + 0x8750000B80UL)

/*
 * Memory spaces:
 */
#define CIA_IACK_SC		        (IDENT_ADDR + 0x8720000000UL)
#define CIA_CONF		        (IDENT_ADDR + 0x8700000000UL)
#define CIA_IO				(IDENT_ADDR + 0x8580000000UL)
#define CIA_SPARSE_MEM			(IDENT_ADDR + 0x8000000000UL)
#define CIA_SPARSE_MEM_R2		(IDENT_ADDR + 0x8400000000UL)
#define CIA_SPARSE_MEM_R3		(IDENT_ADDR + 0x8500000000UL)
#define CIA_DENSE_MEM		        (IDENT_ADDR + 0x8600000000UL)

/*
 * ALCOR's GRU ASIC registers
 */
#define GRU_INT_REQ			(IDENT_ADDR + 0x8780000000UL)
#define GRU_INT_MASK			(IDENT_ADDR + 0x8780000040UL)
#define GRU_INT_EDGE			(IDENT_ADDR + 0x8780000080UL)
#define GRU_INT_HILO			(IDENT_ADDR + 0x87800000C0UL)
#define GRU_INT_CLEAR			(IDENT_ADDR + 0x8780000100UL)

#define GRU_CACHE_CNFG			(IDENT_ADDR + 0x8780000200UL)
#define GRU_SCR				(IDENT_ADDR + 0x8780000300UL)
#define GRU_LED				(IDENT_ADDR + 0x8780000800UL)
#define GRU_RESET			(IDENT_ADDR + 0x8780000900UL)

#define ALCOR_GRU_INT_REQ_BITS		0x800fffffUL
#define XLT_GRU_INT_REQ_BITS		0x80003fffUL
#define GRU_INT_REQ_BITS		(alpha_mv.sys.cia.gru_int_req_bits+0)


/*
 * Bit definitions for I/O Controller status register 0:
 */
#define CIA_IOC_STAT0_CMD		0xf
#define CIA_IOC_STAT0_ERR		(1<<4)
#define CIA_IOC_STAT0_LOST		(1<<5)
#define CIA_IOC_STAT0_THIT		(1<<6)
#define CIA_IOC_STAT0_TREF		(1<<7)
#define CIA_IOC_STAT0_CODE_SHIFT	8
#define CIA_IOC_STAT0_CODE_MASK		0x7
#define CIA_IOC_STAT0_P_NBR_SHIFT	13
#define CIA_IOC_STAT0_P_NBR_MASK	0x7ffff

#define CIA_HAE_ADDRESS	                CIA_IOC_HAE_MEM

/*
 * Data structure for handling CIA machine checks.
 */

/* EV5-specific info.  */
struct el_CIA_procdata {
	unsigned long shadow[8];	/* PALmode shadow registers */
	unsigned long paltemp[24];	/* PAL temporary registers */
	/* EV5-specific fields */
	unsigned long exc_addr;		/* Address of excepting instruction. */
	unsigned long exc_sum;		/* Summary of arithmetic traps. */
	unsigned long exc_mask;		/* Exception mask (from exc_sum). */
	unsigned long exc_base;		/* PALbase at time of exception. */
	unsigned long isr;		/* Interrupt summary register. */
	unsigned long icsr;		/* Ibox control register. */
	unsigned long ic_perr_stat;
	unsigned long dc_perr_stat;
	unsigned long va;		/* Effective VA of fault or miss. */
	unsigned long mm_stat;
	unsigned long sc_addr;
	unsigned long sc_stat;
	unsigned long bc_tag_addr;
	unsigned long ei_addr;
	unsigned long fill_syn;
	unsigned long ei_stat;
	unsigned long ld_lock;
};

/* System-specific info.  */
struct el_CIA_sysdata_mcheck {
	unsigned long      coma_gcr;                       
	unsigned long      coma_edsr;                      
	unsigned long      coma_ter;                       
	unsigned long      coma_elar;                      
	unsigned long      coma_ehar;                      
	unsigned long      coma_ldlr;                      
	unsigned long      coma_ldhr;                      
	unsigned long      coma_base0;                     
	unsigned long      coma_base1;                     
	unsigned long      coma_base2;                     
	unsigned long      coma_cnfg0;                     
	unsigned long      coma_cnfg1;                     
	unsigned long      coma_cnfg2;                     
	unsigned long      epic_dcsr;                      
	unsigned long      epic_pear;                      
	unsigned long      epic_sear;                      
	unsigned long      epic_tbr1;                      
	unsigned long      epic_tbr2;                      
	unsigned long      epic_pbr1;                      
	unsigned long      epic_pbr2;                      
	unsigned long      epic_pmr1;                      
	unsigned long      epic_pmr2;                      
	unsigned long      epic_harx1;                     
	unsigned long      epic_harx2;                     
	unsigned long      epic_pmlt;                      
	unsigned long      epic_tag0;                      
	unsigned long      epic_tag1;                      
	unsigned long      epic_tag2;                      
	unsigned long      epic_tag3;                      
	unsigned long      epic_tag4;                      
	unsigned long      epic_tag5;                      
	unsigned long      epic_tag6;                      
	unsigned long      epic_tag7;                      
	unsigned long      epic_data0;                     
	unsigned long      epic_data1;                     
	unsigned long      epic_data2;                     
	unsigned long      epic_data3;                     
	unsigned long      epic_data4;                     
	unsigned long      epic_data5;                     
	unsigned long      epic_data6;                     
	unsigned long      epic_data7;                     
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

__EXTERN_INLINE unsigned long cia_virt_to_bus(void * address)
{
	return virt_to_phys(address) + CIA_DMA_WIN_BASE;
}

__EXTERN_INLINE void * cia_bus_to_virt(unsigned long address)
{
	return phys_to_virt(address - CIA_DMA_WIN_BASE);
}

/*
 * I/O functions:
 *
 * CIA (the 2117x PCI/memory support chipset for the EV5 (21164)
 * series of processors uses a sparse address mapping scheme to
 * get at PCI memory and I/O.
 */

#define vip	volatile int *
#define vuip	volatile unsigned int *
#define vulp	volatile unsigned long *

__EXTERN_INLINE unsigned int cia_inb(unsigned long addr)
{
	long result;
	result = *(vip) ((addr << 5) + CIA_IO + 0x00);
	return __kernel_extbl(result, addr & 3);
}

__EXTERN_INLINE void cia_outb(unsigned char b, unsigned long addr)
{
	unsigned long w = __kernel_insbl(b, addr & 3);
	*(vuip) ((addr << 5) + CIA_IO + 0x00) = w;
	mb();
}

__EXTERN_INLINE unsigned int cia_inw(unsigned long addr)
{
	long result;
	result = *(vip) ((addr << 5) + CIA_IO + 0x08);
	return __kernel_extwl(result, addr & 3);
}

__EXTERN_INLINE void cia_outw(unsigned short b, unsigned long addr)
{
	unsigned long w = __kernel_inswl(b, addr & 3);
	*(vuip) ((addr << 5) + CIA_IO + 0x08) = w;
	mb();
}

__EXTERN_INLINE unsigned int cia_inl(unsigned long addr)
{
	return *(vuip) ((addr << 5) + CIA_IO + 0x18);
}

__EXTERN_INLINE void cia_outl(unsigned int b, unsigned long addr)
{
	*(vuip) ((addr << 5) + CIA_IO + 0x18) = b;
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

__EXTERN_INLINE unsigned long cia_srm_base(unsigned long addr)
{
	unsigned long mask, base;

	if (addr >= alpha_mv.sm_base_r1
	    && addr <= alpha_mv.sm_base_r1 + CIA_MEM_R1_MASK) {
		mask = CIA_MEM_R1_MASK;
		base = CIA_SPARSE_MEM;
	}
	else if (addr >= alpha_mv.sm_base_r2
		 && addr <= alpha_mv.sm_base_r2 + CIA_MEM_R2_MASK) {
		mask = CIA_MEM_R2_MASK;
		base = CIA_SPARSE_MEM_R2;
	}
	else if (addr >= alpha_mv.sm_base_r3
		 && addr <= alpha_mv.sm_base_r3 + CIA_MEM_R3_MASK) {
		mask = CIA_MEM_R3_MASK;
		base = CIA_SPARSE_MEM_R3;
	}
	else
	{
#if 0
		printk("cia: address 0x%lx not covered by HAE\n", addr);
#endif
		return 0;
	}

	return ((addr & mask) << 5) + base;
}

__EXTERN_INLINE unsigned long cia_srm_readb(unsigned long addr)
{
	unsigned long result, work;

	if ((work = cia_srm_base(addr)) == 0)
		return 0xff;
	work += 0x00;	/* add transfer length */

	result = *(vip) work;
	return __kernel_extbl(result, addr & 3);
}

__EXTERN_INLINE unsigned long cia_srm_readw(unsigned long addr)
{
	unsigned long result, work;

	if ((work = cia_srm_base(addr)) == 0)
		return 0xffff;
	work += 0x08;	/* add transfer length */

	result = *(vip) work;
	return __kernel_extwl(result, addr & 3);
}

__EXTERN_INLINE void cia_srm_writeb(unsigned char b, unsigned long addr)
{
	unsigned long work = cia_srm_base(addr), w;
	if (work) {
		work += 0x00;	/* add transfer length */
		w = __kernel_insbl(b, addr & 3);
		*(vuip) work = w;
	}
}

__EXTERN_INLINE void cia_srm_writew(unsigned short b, unsigned long addr)
{
	unsigned long work = cia_srm_base(addr), w;
	if (work) {
		work += 0x08;	/* add transfer length */
		w = __kernel_inswl(b, addr & 3);
		*(vuip) work = w;
	}
}

__EXTERN_INLINE unsigned long cia_readb(unsigned long addr)
{
	unsigned long result, msb;

	msb = addr & 0xE0000000;
	addr &= CIA_MEM_R1_MASK;
	set_hae(msb);

	result = *(vip) ((addr << 5) + CIA_SPARSE_MEM + 0x00);
	return __kernel_extbl(result, addr & 3);
}

__EXTERN_INLINE unsigned long cia_readw(unsigned long addr)
{
	unsigned long result, msb;

	msb = addr & 0xE0000000;
	addr &= CIA_MEM_R1_MASK;
	set_hae(msb);

	result = *(vip) ((addr << 5) + CIA_SPARSE_MEM + 0x08);
	return __kernel_extwl(result, addr & 3);
}

__EXTERN_INLINE void cia_writeb(unsigned char b, unsigned long addr)
{
        unsigned long msb, w; 

	msb = addr & 0xE0000000;
	addr &= CIA_MEM_R1_MASK;
	set_hae(msb);

	w = __kernel_insbl(b, addr & 3);
	*(vuip) ((addr << 5) + CIA_SPARSE_MEM + 0x00) = w;
}

__EXTERN_INLINE void cia_writew(unsigned short b, unsigned long addr)
{
        unsigned long msb, w; 

	msb = addr & 0xE0000000;
	addr &= CIA_MEM_R1_MASK;
	set_hae(msb);

	w = __kernel_inswl(b, addr & 3);
	*(vuip) ((addr << 5) + CIA_SPARSE_MEM + 0x08) = w;
}

__EXTERN_INLINE unsigned long cia_readl(unsigned long addr)
{
	return *(vuip) (addr + CIA_DENSE_MEM);
}

__EXTERN_INLINE unsigned long cia_readq(unsigned long addr)
{
	return *(vulp) (addr + CIA_DENSE_MEM);
}

__EXTERN_INLINE void cia_writel(unsigned int b, unsigned long addr)
{
	*(vuip) (addr + CIA_DENSE_MEM) = b;
}

__EXTERN_INLINE void cia_writeq(unsigned long b, unsigned long addr)
{
	*(vulp) (addr + CIA_DENSE_MEM) = b;
}

/* Find the DENSE memory area for a given bus address.  */

__EXTERN_INLINE unsigned long cia_dense_mem(unsigned long addr)
{
	return CIA_DENSE_MEM;
}

#undef vip
#undef vuip
#undef vulp

#ifdef __WANT_IO_DEF

#define virt_to_bus	cia_virt_to_bus
#define bus_to_virt	cia_bus_to_virt
#define __inb		cia_inb
#define __inw		cia_inw
#define __inl		cia_inl
#define __outb		cia_outb
#define __outw		cia_outw
#define __outl		cia_outl

#ifdef CONFIG_ALPHA_SRM_SETUP
#define __readb		cia_srm_readb
#define __readw		cia_srm_readw
#define __writeb	cia_srm_writeb
#define __writew	cia_srm_writew
#else
#define __readb		cia_readb
#define __readw		cia_readw
#define __writeb	cia_writeb
#define __writew	cia_writew
#endif

#define __readl		cia_readl
#define __readq		cia_readq
#define __writel	cia_writel
#define __writeq	cia_writeq
#define dense_mem	cia_dense_mem

#define inb(port) \
(__builtin_constant_p((port))?__inb(port):_inb(port))
#define outb(x, port) \
(__builtin_constant_p((port))?__outb((x),(port)):_outb((x),(port)))

#define inw(port) \
(__builtin_constant_p((port))?__inw(port):_inw(port))
#define outw(x, port) \
(__builtin_constant_p((port))?__outw((x),(port)):_outw((x),(port)))

#define inl(port)	__inl(port)
#define outl(x,port)	__outl((x),(port))

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

#endif /* __ALPHA_CIA__H__ */
