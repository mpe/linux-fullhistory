#ifndef __ALPHA_CIA__H__
#define __ALPHA_CIA__H__

#include <linux/types.h>

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
**  EB164 I/O procedures                                                   **
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
#define MEM_SP1_MASK 0x1fffffff  /* Mem sparse space 1 mask is 29 bits */


#define CIA_DMA_WIN_BASE	(1024UL*1024UL*1024UL)
#define CIA_DMA_WIN_SIZE	(1024*1024*1024)

/*
 * 21171-CA Control and Status Registers (p4-1)
 */
#define CIA_IOC_CIA_REV               (IDENT_ADDR + 0x8740000080UL)
#define CIA_IOC_PCI_LAT               (IDENT_ADDR + 0x87400000C0UL)
#define CIA_IOC_CIA_CTRL              (IDENT_ADDR + 0x8740000100UL)
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
 * 2117A-CA PCI Address Translation Registers.   I've only defined
 * the first window fully as that's the only one that we're currently using.
 * The other window bases are needed to disable the windows.
 */
#define CIA_IOC_PCI_TBIA              (IDENT_ADDR + 0x8760000100UL)
#define CIA_IOC_PCI_W0_BASE           (IDENT_ADDR + 0x8760000400UL)
#define CIA_IOC_PCI_W0_MASK           (IDENT_ADDR + 0x8760000440UL)
#define CIA_IOC_PCI_T0_BASE           (IDENT_ADDR + 0x8760000480UL)

#define CIA_IOC_PCI_W1_BASE           (IDENT_ADDR + 0x8760000500UL)
#define CIA_IOC_PCI_W2_BASE           (IDENT_ADDR + 0x8760000600UL)
#define CIA_IOC_PCI_W3_BASE           (IDENT_ADDR + 0x8760000700UL)

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

#define HAE_ADDRESS	                CIA_IOC_HAE_MEM

#ifdef __KERNEL__

/*
 * Translate physical memory address as seen on (PCI) bus into
 * a kernel virtual address and vv.
 */
extern inline unsigned long virt_to_bus(void * address)
{
	return virt_to_phys(address) + CIA_DMA_WIN_BASE;
}

extern inline void * bus_to_virt(unsigned long address)
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

#define vuip	volatile unsigned int *

extern inline unsigned int __inb(unsigned long addr)
{
	long result = *(vuip) ((addr << 5) + CIA_IO + 0x00);
	result >>= (addr & 3) * 8;
	return 0xffUL & result;
}

extern inline void __outb(unsigned char b, unsigned long addr)
{
	unsigned int w;

	asm ("insbl %2,%1,%0" : "r="(w) : "ri"(addr & 0x3), "r"(b));
	*(vuip) ((addr << 5) + CIA_IO + 0x00) = w;
	mb();
}

extern inline unsigned int __inw(unsigned long addr)
{
	long result = *(vuip) ((addr << 5) + CIA_IO + 0x08);
	result >>= (addr & 3) * 8;
	return 0xffffUL & result;
}

extern inline void __outw(unsigned short b, unsigned long addr)
{
	unsigned int w;

	asm ("inswl %2,%1,%0" : "r="(w) : "ri"(addr & 0x3), "r"(b));
	*(vuip) ((addr << 5) + CIA_IO + 0x08) = w;
	mb();
}

extern inline unsigned int __inl(unsigned long addr)
{
	return *(vuip) ((addr << 5) + CIA_IO + 0x18);
}

extern inline void __outl(unsigned int b, unsigned long addr)
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

extern inline unsigned long __readb(unsigned long addr)
{
	unsigned long result, shift, msb;

	shift = (addr & 0x3) * 8 ;
	msb = addr & 0xE0000000 ;
	addr &= MEM_SP1_MASK ;
	if (msb != hae.cache) {
	  set_hae(msb);
	}
	result = *(vuip) ((addr << 5) + CIA_SPARSE_MEM + 0x00) ;
	result >>= shift;
	return 0xffUL & result;
}

extern inline unsigned long __readw(unsigned long addr)
{
	unsigned long result, shift, msb;

	shift = (addr & 0x3) * 8;
	msb = addr & 0xE0000000 ;
	addr &= MEM_SP1_MASK ;
	if (msb != hae.cache) {
	  set_hae(msb);
	}
	result = *(vuip) ((addr << 5) + CIA_SPARSE_MEM + 0x08);
	result >>= shift;
	return 0xffffUL & result;
}

extern inline unsigned long __readl(unsigned long addr)
{
	return *(vuip) (addr + CIA_DENSE_MEM);
}

extern inline void __writeb(unsigned char b, unsigned long addr)
{
        unsigned long msb ; 

	msb = addr & 0xE0000000 ;
	addr &= MEM_SP1_MASK ;
	if (msb != hae.cache) {
	  set_hae(msb);
	}
	*(vuip) ((addr << 5) + CIA_SPARSE_MEM + 0x00) = b * 0x01010101;
}

extern inline void __writew(unsigned short b, unsigned long addr)
{
        unsigned long msb ; 

	msb = addr & 0xE0000000 ;
	addr &= MEM_SP1_MASK ;
	if (msb != hae.cache) {
	  set_hae(msb);
	}
	*(vuip) ((addr << 5) + CIA_SPARSE_MEM + 0x08) = b * 0x00010001;
}

extern inline void __writel(unsigned int b, unsigned long addr)
{
	*(vuip) (addr + CIA_DENSE_MEM) = b;
}

#define inb(port) \
(__builtin_constant_p((port))?__inb(port):_inb(port))

#define outb(x, port) \
(__builtin_constant_p((port))?__outb((x),(port)):_outb((x),(port)))

#define readl(a)	__readl((unsigned long)(a))
#define writel(v,a)	__writel((v),(unsigned long)(a))

#undef vuip

extern unsigned long cia_init (unsigned long mem_start,
				 unsigned long mem_end);

#endif /* __KERNEL__ */

/*
 * Data structure for handling CIA machine checks:
 */
struct el_CIA_sysdata_mcheck {
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

#endif /* __ALPHA_CIA__H__ */
