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
#include <linux/config.h>

#ifdef CONFIG_ALPHA_XL
/*
   An AVANTI *might* be an XL, and an XL has only 27 bits of ISA address
   that get passed through the PCI<->ISA bridge chip. So we've gotta use
   both windows to max out the physical memory we can DMA to. Sigh...

   If we try a window at 0 for 1GB as a work-around, we run into conflicts
   with ISA/PCI bus memory which can't be relocated, like VGA aperture and
   BIOS ROMs. So we must put the windows high enough to avoid these areas.

   We put window 1 at BUS 64Mb for 64Mb, mapping physical 0 to 64Mb-1,
   and window 2 at BUS 512Mb for 512Mb, mapping physical 0 to 512Mb-1.
   Yes, this does map 0 to 64Mb-1 twice, but only window 1 will actually
   be used for that range (via virt_to_bus()).

   Note that we actually fudge the window 1 maximum as 48Mb instead of 64Mb,
   to keep virt_to_bus() from returning an address in the first window, for
   a data area that goes beyond the 64Mb first DMA window.  Sigh...
   The fudge factor MUST match with <asm/dma.h> MAX_DMA_ADDRESS, but
   we can't just use that here, because of header file looping... :-(

   Window 1 will be used for all DMA from the ISA bus; yes, that does
   limit what memory an ISA floppy or soundcard or Ethernet can touch, but
   it's also a known limitation on other platforms as well. We use the
   same technique that is used on INTEL platforms with similar limitation:
   set MAX_DMA_ADDRESS and clear some pages' DMAable flags during mem_init().
   We trust that any ISA bus device drivers will *always* ask for DMAable
   memory explicitly via kmalloc()/get_free_pages() flags arguments.

   Note that most PCI bus devices' drivers do *not* explicitly ask for
   DMAable memory; they count on being able to DMA to any memory they
   get from kmalloc()/get_free_pages(). They will also use window 1 for
   any physical memory accesses below 64Mb; the rest will be handled by
   window 2, maxing out at 512Mb of memory. I trust this is enough... :-)

   Finally, the reason we make window 2 start at 512Mb for 512Mb, is so that
   we can allocate PCI bus devices' memory starting at 1Gb and up, to ensure
   that no conflicts occur and bookkeeping is simplified (ie we don't
   try to fill the gap between the two windows, we just go above the top).

   Note that the XL is treated differently from the AVANTI, even though
   for most other things they are identical. It didn't seem reasonable to
   make the AVANTI support pay for the limitations of the XL. It is true,
   however, that an XL kernel will run on an AVANTI without problems.

*/
#define APECS_XL_DMA_WIN1_BASE		(64*1024*1024)
#define APECS_XL_DMA_WIN1_SIZE		(64*1024*1024)
#define APECS_XL_DMA_WIN1_SIZE_PARANOID	(48*1024*1024)
#define APECS_XL_DMA_WIN2_BASE		(512*1024*1024)
#define APECS_XL_DMA_WIN2_SIZE		(512*1024*1024)

#else /* CONFIG_ALPHA_XL */

/* these are for normal APECS family machines, AVANTI/MUSTANG/EB64/PC64 */
#define APECS_DMA_WIN_BASE	(1024*1024*1024)
#define APECS_DMA_WIN_SIZE	(1024*1024*1024)

#endif /* CONFIG_ALPHA_XL */

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
#define APECS_MEM_GCR		        (IDENT_ADDR + 0x180000000UL)
#define APECS_MEM_EDSR		        (IDENT_ADDR + 0x180000040UL)
#define APECS_MEM_TAR  		        (IDENT_ADDR + 0x180000060UL)
#define APECS_MEM_ELAR		        (IDENT_ADDR + 0x180000080UL)
#define APECS_MEM_EHAR  		(IDENT_ADDR + 0x1800000a0UL)
#define APECS_MEM_SFT_RST		(IDENT_ADDR + 0x1800000c0UL)
#define APECS_MEM_LDxLAR 		(IDENT_ADDR + 0x1800000e0UL)
#define APECS_MEM_LDxHAR 		(IDENT_ADDR + 0x180000100UL)
#define APECS_MEM_GTR    		(IDENT_ADDR + 0x180000200UL)
#define APECS_MEM_RTR    		(IDENT_ADDR + 0x180000220UL)
#define APECS_MEM_VFPR   		(IDENT_ADDR + 0x180000240UL)
#define APECS_MEM_PDLDR  		(IDENT_ADDR + 0x180000260UL)
#define APECS_MEM_PDhDR  		(IDENT_ADDR + 0x180000280UL)

/* Bank x Base Address Register */
#define APECS_MEM_B0BAR  		(IDENT_ADDR + 0x180000800UL)
#define APECS_MEM_B1BAR  		(IDENT_ADDR + 0x180000820UL)
#define APECS_MEM_B2BAR  		(IDENT_ADDR + 0x180000840UL)
#define APECS_MEM_B3BAR  		(IDENT_ADDR + 0x180000860UL)
#define APECS_MEM_B4BAR  		(IDENT_ADDR + 0x180000880UL)
#define APECS_MEM_B5BAR  		(IDENT_ADDR + 0x1800008A0UL)
#define APECS_MEM_B6BAR  		(IDENT_ADDR + 0x1800008C0UL)
#define APECS_MEM_B7BAR  		(IDENT_ADDR + 0x1800008E0UL)
#define APECS_MEM_B8BAR  		(IDENT_ADDR + 0x180000900UL)

/* Bank x Configuration Register */
#define APECS_MEM_B0BCR  		(IDENT_ADDR + 0x180000A00UL)
#define APECS_MEM_B1BCR  		(IDENT_ADDR + 0x180000A20UL)
#define APECS_MEM_B2BCR  		(IDENT_ADDR + 0x180000A40UL)
#define APECS_MEM_B3BCR  		(IDENT_ADDR + 0x180000A60UL)
#define APECS_MEM_B4BCR  		(IDENT_ADDR + 0x180000A80UL)
#define APECS_MEM_B5BCR  		(IDENT_ADDR + 0x180000AA0UL)
#define APECS_MEM_B6BCR  		(IDENT_ADDR + 0x180000AC0UL)
#define APECS_MEM_B7BCR  		(IDENT_ADDR + 0x180000AE0UL)
#define APECS_MEM_B8BCR  		(IDENT_ADDR + 0x180000B00UL)

/* Bank x Timing Register A */
#define APECS_MEM_B0TRA  		(IDENT_ADDR + 0x180000C00UL)
#define APECS_MEM_B1TRA  		(IDENT_ADDR + 0x180000C20UL)
#define APECS_MEM_B2TRA  		(IDENT_ADDR + 0x180000C40UL)
#define APECS_MEM_B3TRA  		(IDENT_ADDR + 0x180000C60UL)
#define APECS_MEM_B4TRA  		(IDENT_ADDR + 0x180000C80UL)
#define APECS_MEM_B5TRA  		(IDENT_ADDR + 0x180000CA0UL)
#define APECS_MEM_B6TRA  		(IDENT_ADDR + 0x180000CC0UL)
#define APECS_MEM_B7TRA  		(IDENT_ADDR + 0x180000CE0UL)
#define APECS_MEM_B8TRA  		(IDENT_ADDR + 0x180000D00UL)

/* Bank x Timing Register B */
#define APECS_MEM_B0TRB                 (IDENT_ADDR + 0x180000E00UL)
#define APECS_MEM_B1TRB  		(IDENT_ADDR + 0x180000E20UL)
#define APECS_MEM_B2TRB  		(IDENT_ADDR + 0x180000E40UL)
#define APECS_MEM_B3TRB  		(IDENT_ADDR + 0x180000E60UL)
#define APECS_MEM_B4TRB  		(IDENT_ADDR + 0x180000E80UL)
#define APECS_MEM_B5TRB  		(IDENT_ADDR + 0x180000EA0UL)
#define APECS_MEM_B6TRB  		(IDENT_ADDR + 0x180000EC0UL)
#define APECS_MEM_B7TRB  		(IDENT_ADDR + 0x180000EE0UL)
#define APECS_MEM_B8TRB  		(IDENT_ADDR + 0x180000F00UL)


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
/* NOTE: we fudge the window 1 maximum as 48Mb instead of 64Mb, to prevent 
   virt_to_bus() from returning an address in the first window, for a
   data area that goes beyond the 64Mb first DMA window. Sigh...
   This MUST match with <asm/dma.h> MAX_DMA_ADDRESS for consistency, but
   we can't just use that here, because of header file looping... :-(
*/
extern inline unsigned long virt_to_bus(void * address)
{
	unsigned long paddr = virt_to_phys(address);
#ifdef CONFIG_ALPHA_XL
	if (paddr < APECS_XL_DMA_WIN1_SIZE_PARANOID)
	  return paddr + APECS_XL_DMA_WIN1_BASE;
	else
	  return paddr + APECS_XL_DMA_WIN2_BASE; /* win 2 xlates to 0 also */
#else /* CONFIG_ALPHA_XL */
	return paddr + APECS_DMA_WIN_BASE;
#endif /* CONFIG_ALPHA_XL */
}

extern inline void * bus_to_virt(unsigned long address)
{
	/*
	 * This check is a sanity check but also ensures that bus
	 * address 0 maps to virtual address 0 which is useful to
	 * detect null "pointers" (the NCR driver is much simpler if
	 * NULL pointers are preserved).
	 */
#ifdef CONFIG_ALPHA_XL
        if (address < APECS_XL_DMA_WIN1_BASE)
                return 0;
        else if (address < (APECS_XL_DMA_WIN1_BASE + APECS_XL_DMA_WIN1_SIZE))
                return phys_to_virt(address - APECS_XL_DMA_WIN1_BASE);
	else /* should be more checking here, maybe? */
                return phys_to_virt(address - APECS_XL_DMA_WIN2_BASE);
#else /* CONFIG_ALPHA_XL */
	if (address < APECS_DMA_WIN_BASE)
		return 0;
	return phys_to_virt(address - APECS_DMA_WIN_BASE);
#endif /* CONFIG_ALPHA_XL */
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

#define inb(port) \
(__builtin_constant_p((port))?__inb(port):_inb(port))

#define outb(x, port) \
(__builtin_constant_p((port))?__outb((x),(port)):_outb((x),(port)))

#define readl(a)	__readl((unsigned long)(a))
#define writel(v,a)	__writel((v),(unsigned long)(a))

#undef vuip

extern unsigned long apecs_init (unsigned long mem_start,
				 unsigned long mem_end);

#endif /* __KERNEL__ */

/*
 * Data structure for handling APECS machine checks:
 */
struct el_apecs_sysdata_mcheck {
    unsigned long coma_gcr;
    unsigned long coma_edsr;
    unsigned long coma_ter;
    unsigned long coma_elar;
    unsigned long coma_ehar;
    unsigned long coma_ldlr;
    unsigned long coma_ldhr;
    unsigned long coma_base0;
    unsigned long coma_base1;
    unsigned long coma_base2;
    unsigned long coma_cnfg0;
    unsigned long coma_cnfg1;
    unsigned long coma_cnfg2;
    unsigned long epic_dcsr;
    unsigned long epic_pear;
    unsigned long epic_sear;
    unsigned long epic_tbr1;
    unsigned long epic_tbr2;
    unsigned long epic_pbr1;
    unsigned long epic_pbr2;
    unsigned long epic_pmr1;
    unsigned long epic_pmr2;
    unsigned long epic_harx1;
    unsigned long epic_harx2;
    unsigned long epic_pmlt;
    unsigned long epic_tag0;
    unsigned long epic_tag1;
    unsigned long epic_tag2;
    unsigned long epic_tag3;
    unsigned long epic_tag4;
    unsigned long epic_tag5;
    unsigned long epic_tag6;
    unsigned long epic_tag7;
    unsigned long epic_data0;
    unsigned long epic_data1;
    unsigned long epic_data2;
    unsigned long epic_data3;
    unsigned long epic_data4;
    unsigned long epic_data5;
    unsigned long epic_data6;
    unsigned long epic_data7;
};

#define RTC_PORT(x)	(0x70 + (x))
#define RTC_ADDR(x)	(0x80 | (x))
#define RTC_ALWAYS_BCD	0

#endif /* __ALPHA_APECS__H__ */
