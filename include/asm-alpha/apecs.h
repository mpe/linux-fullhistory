#ifndef __ALPHA_APECS__H__
#define __ALPHA_APECS__H__

#include <linux/types.h>

/*
 * APECS is the internal name for the 2107x chipset which provides
 * memory controller and PCI access for the 21064 chip based systems.
 *
 * This file is based on:
 *
 * DECchip 21071-AA and DECchip 21072-AA Core Logic Chipsets
 * Data Sheet
 *
 * EC-N0648-72
 *
 *
 * david.rusling@reo.mts.dec.com Initial Version.
 *
 */

#define APECS_DMA_WIN_BASE	(1024*1024*1024)
#define APECS_DMA_WIN_SIZE	(1024*1024*1024)

/*
 * 21071-DA Control and Status registers.
 * These are used for PCI memory access.
 */
#define APECS_IOC_DCSR                  (IDENT_ADDR + 0x1A0000000UL)
#define APECS_IOC_PEAR                  (IDENT_ADDR + 0x1A0000020UL)
#define APECS_IOC_SEAR                  (IDENT_ADDR + 0x1A0000040UL)
#define APECS_IOC_DR1                   (IDENT_ADDR + 0x1A0000060UL)
#define APECS_IOC_DR2                   (IDENT_ADDR + 0x1A0000080UL)
#define APECS_IOC_DR3                   (IDENT_ADDR + 0x1A00000A0UL)

#define APECS_IOC_TB1R                  (IDENT_ADDR + 0x1A00000C0UL)
#define APECS_IOC_TB2R                  (IDENT_ADDR + 0x1A00000E0UL)

#define APECS_IOC_PB1R                  (IDENT_ADDR + 0x1A0000100UL)
#define APECS_IOC_PB2R                  (IDENT_ADDR + 0x1A0000120UL)

#define APECS_IOC_PM1R                  (IDENT_ADDR + 0x1A0000140UL)
#define APECS_IOC_PM2R                  (IDENT_ADDR + 0x1A0000160UL)

#define APECS_IOC_HAXR0                 (IDENT_ADDR + 0x1A0000180UL)
#define APECS_IOC_HAXR1                 (IDENT_ADDR + 0x1A00001A0UL)
#define APECS_IOC_HAXR2                 (IDENT_ADDR + 0x1A00001C0UL)

#define APECS_IOC_PMLT                  (IDENT_ADDR + 0x1A00001E0UL)

#define APECS_IOC_TLBTAG0               (IDENT_ADDR + 0x1A0000200UL)
#define APECS_IOC_TLBTAG1               (IDENT_ADDR + 0x1A0000220UL)
#define APECS_IOC_TLBTAG2               (IDENT_ADDR + 0x1A0000240UL)
#define APECS_IOC_TLBTAG3               (IDENT_ADDR + 0x1A0000260UL)
#define APECS_IOC_TLBTAG4               (IDENT_ADDR + 0x1A0000280UL)
#define APECS_IOC_TLBTAG5               (IDENT_ADDR + 0x1A00002A0UL)
#define APECS_IOC_TLBTAG6               (IDENT_ADDR + 0x1A00002C0UL)
#define APECS_IOC_TLBTAG7               (IDENT_ADDR + 0x1A00002E0UL)

#define APECS_IOC_TLBDATA0              (IDENT_ADDR + 0x1A0000300UL)
#define APECS_IOC_TLBDATA1              (IDENT_ADDR + 0x1A0000320UL)
#define APECS_IOC_TLBDATA2              (IDENT_ADDR + 0x1A0000340UL)
#define APECS_IOC_TLBDATA3              (IDENT_ADDR + 0x1A0000360UL)
#define APECS_IOC_TLBDATA4              (IDENT_ADDR + 0x1A0000380UL)
#define APECS_IOC_TLBDATA5              (IDENT_ADDR + 0x1A00003A0UL)
#define APECS_IOC_TLBDATA6              (IDENT_ADDR + 0x1A00003C0UL)
#define APECS_IOC_TLBDATA7              (IDENT_ADDR + 0x1A00003E0UL)

#define APECS_IOC_TBIA                  (IDENT_ADDR + 0x1A0000400UL)


/*
 * 21071-CA Control and Status registers.
 * These are used to program memory timing,
 *  configure memory and initialise the B-Cache.
 */
#define APECS_IOC_GCR		        (IDENT_ADDR + 0x180000000UL)
#define APECS_IOC_EDSR		        (IDENT_ADDR + 0x180000040UL)
#define APECS_IOC_TAR  		        (IDENT_ADDR + 0x180000060UL)
#define APECS_IOC_ELAR		        (IDENT_ADDR + 0x180000080UL)
#define APECS_IOC_EHAR  		(IDENT_ADDR + 0x1800000a0UL)
#define APECS_IOC_SFT_RST		(IDENT_ADDR + 0x1800000c0UL)
#define APECS_IOC_LDxLAR 		(IDENT_ADDR + 0x1800000e0UL)
#define APECS_IOC_LDxHAR 		(IDENT_ADDR + 0x180000100UL)
#define APECS_IOC_GTR    		(IDENT_ADDR + 0x180000200UL)
#define APECS_IOC_RTR    		(IDENT_ADDR + 0x180000220UL)
#define APECS_IOC_VFPR   		(IDENT_ADDR + 0x180000240UL)
#define APECS_IOC_PDLDR  		(IDENT_ADDR + 0x180000260UL)
#define APECS_IOC_PDhDR  		(IDENT_ADDR + 0x180000280UL)

/* Bank x Base Address Register */
#define APECS_IOC_B0BAR  		(IDENT_ADDR + 0x180000800UL)
#define APECS_IOC_B1BAR  		(IDENT_ADDR + 0x180000820UL)
#define APECS_IOC_B2BAR  		(IDENT_ADDR + 0x180000840UL)
#define APECS_IOC_B3BAR  		(IDENT_ADDR + 0x180000860UL)
#define APECS_IOC_B4BAR  		(IDENT_ADDR + 0x180000880UL)
#define APECS_IOC_B5BAR  		(IDENT_ADDR + 0x1800008A0UL)
#define APECS_IOC_B6BAR  		(IDENT_ADDR + 0x1800008C0UL)
#define APECS_IOC_B7BAR  		(IDENT_ADDR + 0x1800008E0UL)
#define APECS_IOC_B8BAR  		(IDENT_ADDR + 0x180000900UL)

/* Bank x Configuration Register */
#define APECS_IOC_B0BCR  		(IDENT_ADDR + 0x180000A00UL)
#define APECS_IOC_B1BCR  		(IDENT_ADDR + 0x180000A20UL)
#define APECS_IOC_B2BCR  		(IDENT_ADDR + 0x180000A40UL)
#define APECS_IOC_B3BCR  		(IDENT_ADDR + 0x180000A60UL)
#define APECS_IOC_B4BCR  		(IDENT_ADDR + 0x180000A80UL)
#define APECS_IOC_B5BCR  		(IDENT_ADDR + 0x180000AA0UL)
#define APECS_IOC_B6BCR  		(IDENT_ADDR + 0x180000AC0UL)
#define APECS_IOC_B7BCR  		(IDENT_ADDR + 0x180000AE0UL)
#define APECS_IOC_B8BCR  		(IDENT_ADDR + 0x180000B00UL)

/* Bank x Timing Register A */
#define APECS_IOC_B0TRA  		(IDENT_ADDR + 0x180000C00UL)
#define APECS_IOC_B1TRA  		(IDENT_ADDR + 0x180000C20UL)
#define APECS_IOC_B2TRA  		(IDENT_ADDR + 0x180000C40UL)
#define APECS_IOC_B3TRA  		(IDENT_ADDR + 0x180000C60UL)
#define APECS_IOC_B4TRA  		(IDENT_ADDR + 0x180000C80UL)
#define APECS_IOC_B5TRA  		(IDENT_ADDR + 0x180000CA0UL)
#define APECS_IOC_B6TRA  		(IDENT_ADDR + 0x180000CC0UL)
#define APECS_IOC_B7TRA  		(IDENT_ADDR + 0x180000CE0UL)
#define APECS_IOC_B8TRA  		(IDENT_ADDR + 0x180000D00UL)

/* Bank x Timing Register B */
#define APECS_IOC_B0TRB                 (IDENT_ADDR + 0x180000E00UL)
#define APECS_IOC_B1TRB  		(IDENT_ADDR + 0x180000E20UL)
#define APECS_IOC_B2TRB  		(IDENT_ADDR + 0x180000E40UL)
#define APECS_IOC_B3TRB  		(IDENT_ADDR + 0x180000E60UL)
#define APECS_IOC_B4TRB  		(IDENT_ADDR + 0x180000E80UL)
#define APECS_IOC_B5TRB  		(IDENT_ADDR + 0x180000EA0UL)
#define APECS_IOC_B6TRB  		(IDENT_ADDR + 0x180000EC0UL)
#define APECS_IOC_B7TRB  		(IDENT_ADDR + 0x180000EE0UL)
#define APECS_IOC_B8TRB  		(IDENT_ADDR + 0x180000F00UL)


/*
 * Memory spaces:
 */
#define APECS_IACK_SC		        (IDENT_ADDR + 0x1b0000000UL)
#define APECS_CONF		        (IDENT_ADDR + 0x1e0000000UL)
#define APECS_IO			(IDENT_ADDR + 0x1c0000000UL)
#define APECS_SPARSE_MEM		(IDENT_ADDR + 0x200000000UL)
#define APECS_DENSE_MEM		        (IDENT_ADDR + 0x300000000UL)

/*
 * Bit definitions for I/O Controller status register 0:
 */
#define APECS_IOC_STAT0_CMD		0xf
#define APECS_IOC_STAT0_ERR		(1<<4)
#define APECS_IOC_STAT0_LOST		(1<<5)
#define APECS_IOC_STAT0_THIT		(1<<6)
#define APECS_IOC_STAT0_TREF		(1<<7)
#define APECS_IOC_STAT0_CODE_SHIFT	8
#define APECS_IOC_STAT0_CODE_MASK	0x7
#define APECS_IOC_STAT0_P_NBR_SHIFT	13
#define APECS_IOC_STAT0_P_NBR_MASK	0x7ffff

#define HAE_ADDRESS	APECS_IOC_HAXR1

#ifdef __KERNEL__

/*
 * Translate physical memory address as seen on (PCI) bus into
 * a kernel virtual address and vv.
 */
extern inline unsigned long virt_to_bus(void * address)
{
	return virt_to_phys(address) + APECS_DMA_WIN_BASE;
}

extern inline void * bus_to_virt(unsigned long address)
{
	return phys_to_virt(address - APECS_DMA_WIN_BASE);
}

/*
 * I/O functions:
 *
 * Unlike Jensen, the APECS machines have no concept of local
 * I/O---everything goes over the PCI bus.
 *
 * There is plenty room for optimization here.  In particular,
 * the Alpha's insb/insw/extb/extw should be useful in moving
 * data to/from the right byte-lanes.
 */

#define vuip	volatile unsigned int *

extern inline unsigned int __inb(unsigned long addr)
{
	long result = *(vuip) ((addr << 5) + APECS_IO + 0x00);
	result >>= (addr & 3) * 8;
	return 0xffUL & result;
}

extern inline void __outb(unsigned char b, unsigned long addr)
{
	unsigned int w;

	asm ("insbl %2,%1,%0" : "r="(w) : "ri"(addr & 0x3), "r"(b));
	*(vuip) ((addr << 5) + APECS_IO + 0x00) = w;
	mb();
}

extern inline unsigned int __inw(unsigned long addr)
{
	long result = *(vuip) ((addr << 5) + APECS_IO + 0x08);
	result >>= (addr & 3) * 8;
	return 0xffffUL & result;
}

extern inline void __outw(unsigned short b, unsigned long addr)
{
	unsigned int w;

	asm ("inswl %2,%1,%0" : "r="(w) : "ri"(addr & 0x3), "r"(b));
	*(vuip) ((addr << 5) + APECS_IO + 0x08) = w;
	mb();
}

extern inline unsigned int __inl(unsigned long addr)
{
	return *(vuip) ((addr << 5) + APECS_IO + 0x18);
}

extern inline void __outl(unsigned int b, unsigned long addr)
{
	*(vuip) ((addr << 5) + APECS_IO + 0x18) = b;
	mb();
}


/*
 * Memory functions.  64-bit and 32-bit accesses are done through
 * dense memory space, everything else through sparse space.
 */
extern inline unsigned long __readb(unsigned long addr)
{
	unsigned long result, shift, msb;

	shift = (addr & 0x3) * 8;
	if (addr >= (1UL << 24)) {
		msb = addr & 0xf8000000;
		addr -= msb;
		if (msb != hae.cache) {
			set_hae(msb);
		}
	}
	result = *(vuip) ((addr << 5) + APECS_SPARSE_MEM + 0x00);
	result >>= shift;
	return 0xffUL & result;
}

extern inline unsigned long __readw(unsigned long addr)
{
	unsigned long result, shift, msb;

	shift = (addr & 0x3) * 8;
	if (addr >= (1UL << 24)) {
		msb = addr & 0xf8000000;
		addr -= msb;
		if (msb != hae.cache) {
			set_hae(msb);
		}
	}
	result = *(vuip) ((addr << 5) + APECS_SPARSE_MEM + 0x08);
	result >>= shift;
	return 0xffffUL & result;
}

extern inline unsigned long __readl(unsigned long addr)
{
	return *(vuip) (addr + APECS_DENSE_MEM);
}

extern inline void __writeb(unsigned char b, unsigned long addr)
{
	unsigned long msb;

	if (addr >= (1UL << 24)) {
		msb = addr & 0xf8000000;
		addr -= msb;
		if (msb != hae.cache) {
			set_hae(msb);
		}
	}
	*(vuip) ((addr << 5) + APECS_SPARSE_MEM + 0x00) = b * 0x01010101;
}

extern inline void __writew(unsigned short b, unsigned long addr)
{
	unsigned long msb;

	if (addr >= (1UL << 24)) {
		msb = addr & 0xf8000000;
		addr -= msb;
		if (msb != hae.cache) {
			set_hae(msb);
		}
	}
	*(vuip) ((addr << 5) + APECS_SPARSE_MEM + 0x08) = b * 0x00010001;
}

extern inline void __writel(unsigned int b, unsigned long addr)
{
	*(vuip) (addr + APECS_DENSE_MEM) = b;
}

/*
 * Most of the above have so much overhead that it probably doesn't
 * make sense to have them inlined (better icache behavior).
 */
extern unsigned int inb(unsigned long addr);
extern unsigned int inw(unsigned long addr);
extern unsigned int inl(unsigned long addr);

extern void outb(unsigned char b, unsigned long addr);
extern void outw(unsigned short b, unsigned long addr);
extern void outl(unsigned int b, unsigned long addr);

extern unsigned long readb(unsigned long addr);
extern unsigned long readw(unsigned long addr);

extern void writeb(unsigned char b, unsigned long addr);
extern void writew(unsigned short b, unsigned long addr);

#define inb(port) \
(__builtin_constant_p((port))?__inb(port):(inb)(port))

#define outb(x, port) \
(__builtin_constant_p((port))?__outb((x),(port)):(outb)((x),(port)))

#define inb_p inb
#define outb_p outb

extern inline unsigned long readl(unsigned long addr)
{
	return __readl(addr);
}

extern inline void writel(unsigned int b, unsigned long addr)
{
	__writel(b, addr);
}

#undef vuip

extern unsigned long apecs_init (unsigned long mem_start,
				 unsigned long mem_end);

#endif /* __KERNEL__ */

/*
 * data structures for handling APECS machine checks
 */

struct el_common_logout_header {
        u_int   elfl_size;              /* size in bytes of logout area. */
        int     elfl_sbz1:31;           /* Should be zero. */
        char    elfl_retry:1;           /* Retry flag. */
        u_int   elfl_procoffset;        /* Processor-specific offset. */
        u_int   elfl_sysoffset;         /* Offset of system-specific. */
};

struct el_apecs_sysdata_mcheck {
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

#endif /* __ALPHA_APECS__H__ */
