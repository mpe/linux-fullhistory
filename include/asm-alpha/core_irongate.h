#ifndef __ALPHA_IRONGATE__H__
#define __ALPHA_IRONGATE__H__

#include <linux/types.h>
#include <asm/compiler.h>

/*
 * IRONGATE is the internal name for the AMD-751 K7 core logic chipset
 * which provides memory controller and PCI access for NAUTILUS-based
 * EV6 (21264) systems.
 *
 * This file is based on:
 *
 * IronGate management library, (c) 1999 Alpha Processor, Inc.
 * Copyright (C) 1999 Alpha Processor, Inc.,
 *	(David Daniel, Stig Telfer, Soohoon Lee)
 */

/*
 * The 21264 supports, and internally recognizes, a 44-bit physical
 * address space that is divided equally between memory address space
 * and I/O address space. Memory address space resides in the lower
 * half of the physical address space (PA[43]=0) and I/O address space
 * resides in the upper half of the physical address space (PA[43]=1).
 *
 */

#define IRONGATE_DMA_WIN_BASE		 (0UL)
#define IRONGATE_DMA_WIN_SIZE		 (0UL)


/*
 * Irongate CSR map.  Some of the CSRs are 8 or 16 bits, but all access
 * through the routines given is 32-bit.
 *
 * The first 0x40 bytes are standard as per the PCI spec.
 */

typedef volatile __u32	igcsr32;

typedef struct {
	igcsr32 dev_vendor;		/* 0x00 - device ID, vendor ID */
	igcsr32 stat_cmd;		/* 0x04 - status, command */
	igcsr32 class;			/* 0x08 - class code, rev ID */
	igcsr32 latency;		/* 0x0C - header type, PCI latency */
	igcsr32 bar0;			/* 0x10 - BAR0 - AGP */
	igcsr32 bar1;			/* 0x14 - BAR1 - GART */
	igcsr32 bar2;			/* 0x18 - Power Management reg block */

	igcsr32 rsrvd0[6];		/* 0x1C-0x33 reserved */

	igcsr32 capptr;			/* 0x34 - Capabilities pointer */

	igcsr32 rsrvd1[2];		/* 0x38-0x3F reserved */

	igcsr32 bacsr10;		/* 0x40 - base address chip selects */
	igcsr32 bacsr32;		/* 0x44 - base address chip selects */
	igcsr32 bacsr54;		/* 0x48 - base address chip selects */

	igcsr32 rsrvd2[1];		/* 0x4C-0x4F reserved */

	igcsr32 drammap;		/* 0x50 - address mapping control */
	igcsr32 dramtm;			/* 0x54 - timing, driver strength */
	igcsr32 dramms;			/* 0x58 - ECC, mode/status */

	igcsr32 rsrvd3[1];		/* 0x5C-0x5F reserved */

	igcsr32 biu0;			/* 0x60 - bus interface unit */
	igcsr32 biusip;			/* 0x64 - Serial initialisation pkt */

	igcsr32 rsrvd4[2];		/* 0x68-0x6F reserved */

	igcsr32 mro;			/* 0x70 - memory request optimiser */

	igcsr32 rsrvd5[3];		/* 0x74-0x7F reserved */

	igcsr32 whami;			/* 0x80 - who am I */
	igcsr32 pciarb;			/* 0x84 - PCI arbitration control */
	igcsr32 pcicfg;			/* 0x88 - PCI config status */

	igcsr32 rsrvd6[5];		/* 0x8C-0x9F reserved */

	/* AGP (bus 1) control registers */
	igcsr32 agpcap;			/* 0xA0 - AGP Capability Identifier */
	igcsr32 agpstat;		/* 0xA4 - AGP status register */
	igcsr32 agpcmd;			/* 0xA8 - AGP control register */
	igcsr32 agpva;			/* 0xAC - AGP Virtual Address Space */
	igcsr32 agpmode;		/* 0xB0 - AGP/GART mode control */
} Irongate0;

/* Bitfield and mask register definitions */

/* Device, vendor IDs - offset 0x00 */

typedef union {
	igcsr32 i;			/* integer value of CSR */
	struct {
		unsigned v : 16;
		unsigned d : 16;
	} r;				/* structured interpretation */
} ig_dev_vendor_t;


/* Status, command registers - offset 0x04 */

typedef union {
	igcsr32 i;
	struct {
		unsigned command;
		unsigned status;
	} s;
	struct {
		/* command register fields */
		unsigned iospc : 1;		/* always reads zero */
		unsigned memspc	 : 1;		/* PCI memory space accesses? */
		unsigned iten : 1;		/* always 1: can be bus initiator */
		unsigned scmon : 1;		/* always 0 special cycles not chckd */
		unsigned mwic : 1;		/* always 0 - no mem write & invalid */
		unsigned vgaps : 1;		/* always 0 - palette rds not special */
		unsigned per : 1;		/* parity error resp: always 0 */
		unsigned step : 1;		/* address/data stepping : always 0 */
		unsigned serre : 1;		/* 1 = sys err output driver enable */
		unsigned fbbce : 1;		/* fast back-back cycle : always 0 */
		unsigned zero1 : 6;		/* must be zero */

		/* status register fields */
		unsigned zero2 : 4;	     /* must be zero */
		unsigned cl : 1;	    /* config space capa list: always 1 */
		unsigned pci66 : 1;	    /* 66 MHz PCI support - always 0 */
		unsigned udf : 1;	    /* user defined features - always 0 */
		unsigned fbbc : 1;	    /* back-back transactions - always 0 */
		unsigned ppe : 1;	    /* PCI parity error detected (0) */
		unsigned devsel : 2;	    /* DEVSEL timing (always 01) */
		unsigned sta : 1;	    /* signalled target abort (0) */
		unsigned rta : 1;	    /* recvd target abort */
		unsigned ria : 1;	    /* recvd initiator abort */
		unsigned serr : 1;	    /* SERR has been asserted */
		unsigned dpe : 1;	    /* DRAM parity error (0) */
	} r;
} ig_stat_cmd_t;


/* Revision ID, Programming interface, subclass, baseclass - offset 0x08 */

typedef union {
	igcsr32 i;
	struct {
		/* revision ID */
		unsigned step : 4;		/* stepping Revision ID */
		unsigned die : 4;		/* die Revision ID */
		unsigned pif : 8;		/* programming interface (0x00) */
		unsigned sub : 8;		/* subclass code (0x00) */
		unsigned base: 8;		/* baseclass code (0x06) */
	} r;
} ig_class_t;


/* Latency Timer, PCI Header type - offset 0x0C */

typedef union {
	igcsr32 i;
	struct {
		unsigned zero1:8;		/* reserved */
		unsigned lat : 8;		/* latency in PCI bus clocks */
		unsigned hdr : 8;		/* PCI header type */
		unsigned zero2:8;		/* reserved */
	} r;
} ig_latency_t;


/* Base Address Register 0 - offset 0x10 */

typedef union {
	igcsr32 i;
	struct {
		unsigned mem : 1;		/* Reg pts to memory (always 0) */
		unsigned type: 2;		/* 32 bit register = 0b00 */
		unsigned pref: 1;		/* graphics mem prefetchable=1 */
		unsigned baddrl : 21;		/* 32M = minimum alloc -> all zero */
		unsigned size : 6;		/* size requirements for AGP */
		unsigned zero : 1;		/* reserved=0 */
	} r;
} ig_bar0_t;


/* Base Address Register 1 - offset 0x14 */

typedef union {
	igcsr32 i;
	struct {
		unsigned mem : 1;		/* BAR0 maps to memory -> 0 */
		unsigned type : 2;		/* BAR1 is 32-bit -> 0b00 */
		unsigned pref : 1;		/* graphics mem prefetchable=1 */
		unsigned baddrl : 8;		/* 4K alloc for AGP CSRs -> 0b00 */
		unsigned baddrh : 20;		/* base addr of AGP CSRs A[30:11] */
	} r;
} ig_bar1_t;


/* Base Address Register 2 - offset 0x18 */

typedef union {
	igcsr32 i;
	struct {
		unsigned io  : 1;		/* BAR2 maps to I/O space -> 1 */
		unsigned zero1: 1;		/* reserved */
		unsigned addr : 22;		/* BAR2[31:10] - PM2_BLK base */
		unsigned zero2: 8;		/* reserved */
	} r;
} ig_bar2_t;


/* Capabilities Pointer - offset 0x34 */

typedef union {
	igcsr32 i;
	struct {
		unsigned cap : 8;		/* =0xA0, offset of AGP ctrl regs */
		unsigned zero: 24;		/* reserved */
	} r;
} ig_capptr_t;


/* Base Address Chip Select Register 1,0 - offset 0x40 */
/* Base Address Chip Select Register 3,2 - offset 0x44 */
/* Base Address Chip Select Register 5,4 - offset 0x48 */

typedef union {

	igcsr32 i;
	struct {
		/* lower bank */
		unsigned en0 : 1;		/* memory bank enabled */
		unsigned mask0 : 6;		/* Address mask for A[28:23] */
		unsigned base0 : 9;		/* Bank Base Address A[31:23] */

		/* upper bank */
		unsigned en1 : 1;		/* memory bank enabled */
		unsigned mask1 : 6;		/* Address mask for A[28:23] */
		unsigned base1 : 9;		/* Bank Base Address A[31:23] */
	} r;
} ig_bacsr_t, ig_bacsr10_t, ig_bacsr32_t, ig_bacsr54_t;


/* SDRAM Address Mapping Control Register - offset 0x50 */

typedef union {
	igcsr32 i;
	struct {
		unsigned z1 : 1;		/* reserved */
		unsigned bnks0: 1;		/* 0->2 banks in chip select 0 */
		unsigned am0 : 1;		/* row/column addressing */
		unsigned z2 : 1;		/* reserved */

		unsigned z3 : 1;		/* reserved */
		unsigned bnks1: 1;		/* 0->2 banks in chip select 1 */
		unsigned am1 : 1;		/* row/column addressing */
		unsigned z4 : 1;		/* reserved */

		unsigned z5 : 1;		/* reserved */
		unsigned bnks2: 1;		/* 0->2 banks in chip select 2 */
		unsigned am2 : 1;		/* row/column addressing */
		unsigned z6 : 1;		/* reserved */

		unsigned z7 : 1;		/* reserved */
		unsigned bnks3: 1;		/* 0->2 banks in chip select 3 */
		unsigned am3 : 1;		/* row/column addressing */
		unsigned z8 : 1;		/* reserved */

		unsigned z9 : 1;		/* reserved */
		unsigned bnks4: 1;		/* 0->2 banks in chip select 4 */
		unsigned am4 : 1;		/* row/column addressing */
		unsigned z10 : 1;		/* reserved */

		unsigned z11 : 1;		/* reserved */
		unsigned bnks5: 1;		/* 0->2 banks in chip select 5 */
		unsigned am5 : 1;		/* row/column addressing */
		unsigned z12 : 1;		/* reserved */

		unsigned rsrvd: 8;		/* reserved */
	} r;
} ig_drammap_t;


/* DRAM timing and driver strength register - offset 0x54 */

typedef union {
	igcsr32 i;
	struct {
		/* DRAM timing parameters */
		unsigned trcd : 2;
		unsigned tcl : 2;
		unsigned tras: 3;
		unsigned trp : 2;
		unsigned trc : 3;
		unsigned icl: 2;
		unsigned ph : 2;

		/* Chipselect driver strength */
		unsigned adra : 1;
		unsigned adrb : 1;
		unsigned ctrl : 3;
		unsigned dqm : 1;
		unsigned cs : 1;
		unsigned clk: 1;
		unsigned rsrvd:8;
	} r;
} ig_dramtm_t;


/* DRAM Mode / Status and ECC Register - offset 0x58 */

typedef union {
	igcsr32 i;
	struct {
		unsigned chipsel : 6;		/* failing ECC chip select */
		unsigned zero1 : 2;		/* always reads zero */
		unsigned status : 2;		/* ECC Detect logic status */
		unsigned zero2 : 6;		/* always reads zero */

		unsigned cycles : 2;		/* cycles per refresh, see table */
		unsigned en : 1;		/* ECC enable */
		unsigned r : 1;			/* Large burst enable (=0) */
		unsigned bre : 1;		/* Burst refresh enable */
		unsigned zero3 : 2;		/* reserved = 0 */
		unsigned mwe : 1;		/* Enable writes to DRAM mode reg */
		unsigned type : 1;		/* SDRAM = 0, default */
		unsigned sdraminit : 1;		/* SDRAM init - set params first! */
		unsigned zero4 : 6;		/* reserved = 0 */
	} r;
} ig_dramms_t;


/*
 * Memory spaces:
 */

/* Irongate is consistent with a subset of the Tsunami memory map */
#ifdef USE_48_BIT_KSEG
#define IRONGATE_BIAS 0x80000000000UL
#else
#define IRONGATE_BIAS 0x10000000000UL
#endif


#define IRONGATE_MEM		(IDENT_ADDR | IRONGATE_BIAS | 0x000000000UL)
#define IRONGATE_IACK_SC	(IDENT_ADDR | IRONGATE_BIAS | 0x1F8000000UL)
#define IRONGATE_IO		(IDENT_ADDR | IRONGATE_BIAS | 0x1FC000000UL)
#define IRONGATE_CONF		(IDENT_ADDR | IRONGATE_BIAS | 0x1FE000000UL)

#define IRONGATE0		((Irongate0 *) IRONGATE_CONF)

/*
 * Data structure for handling IRONGATE machine checks:
 * This is the standard OSF logout frame
 */

#define SCB_Q_SYSERR	0x620			/* OSF definitions */
#define SCB_Q_PROCERR	0x630
#define SCB_Q_SYSMCHK	0x660
#define SCB_Q_PROCMCHK	0x670

struct el_IRONGATE_sysdata_mcheck {
	__u32 FrameSize;                 /* Bytes, including this field */
	__u32 FrameFlags;                /* <31> = Retry, <30> = Second Error */
	__u32 CpuOffset;                 /* Offset to CPU-specific into */
	__u32 SystemOffset;              /* Offset to system-specific info */
	__u32 MCHK_Code;
	__u32 MCHK_Frame_Rev;
	__u64 I_STAT;
	__u64 DC_STAT;
	__u64 C_ADDR;
	__u64 DC1_SYNDROME;
	__u64 DC0_SYNDROME;
	__u64 C_STAT;
	__u64 C_STS;
	__u64 RESERVED0;
	__u64 EXC_ADDR;
	__u64 IER_CM;
	__u64 ISUM;
	__u64 MM_STAT;
	__u64 PAL_BASE;
	__u64 I_CTL;
	__u64 PCTX;
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

__EXTERN_INLINE unsigned long irongate_virt_to_bus(void * address)
{
	return virt_to_phys(address) + IRONGATE_DMA_WIN_BASE;
}

__EXTERN_INLINE void * irongate_bus_to_virt(unsigned long address)
{
	return phys_to_virt(address - IRONGATE_DMA_WIN_BASE);
}

/*
 * I/O functions:
 *
 * IRONGATE (AMD-751) PCI/memory support chip for the EV6 (21264) and
 * K7 can only use linear accesses to get at PCI memory and I/O spaces.
 */

#define vucp	volatile unsigned char *
#define vusp	volatile unsigned short *
#define vuip	volatile unsigned int *
#define vulp	volatile unsigned long *

__EXTERN_INLINE unsigned int irongate_inb(unsigned long addr)
{
	return __kernel_ldbu(*(vucp)(addr + IRONGATE_IO));
}

__EXTERN_INLINE void irongate_outb(unsigned char b, unsigned long addr)
{
        __kernel_stb(b, *(vucp)(addr + IRONGATE_IO));
	mb();
}

__EXTERN_INLINE unsigned int irongate_inw(unsigned long addr)
{
	return __kernel_ldwu(*(vusp)(addr + IRONGATE_IO));
}

__EXTERN_INLINE void irongate_outw(unsigned short b, unsigned long addr)
{
        __kernel_stw(b, *(vusp)(addr + IRONGATE_IO));
	mb();
}

__EXTERN_INLINE unsigned int irongate_inl(unsigned long addr)
{
	return *(vuip)(addr + IRONGATE_IO);
}

__EXTERN_INLINE void irongate_outl(unsigned int b, unsigned long addr)
{
        *(vuip)(addr + IRONGATE_IO) = b;
	mb();
}

/*
 * Memory functions.  All accesses are done through linear space.
 */

__EXTERN_INLINE unsigned long irongate_readb(unsigned long addr)
{
	return __kernel_ldbu(*(vucp)addr);
}

__EXTERN_INLINE unsigned long irongate_readw(unsigned long addr)
{
	return __kernel_ldwu(*(vusp)addr);
}

__EXTERN_INLINE unsigned long irongate_readl(unsigned long addr)
{
	return *(vuip)addr;
}

__EXTERN_INLINE unsigned long irongate_readq(unsigned long addr)
{
	return *(vulp)addr;
}

__EXTERN_INLINE void irongate_writeb(unsigned char b, unsigned long addr)
{
	__kernel_stb(b, *(vucp)addr);
}

__EXTERN_INLINE void irongate_writew(unsigned short b, unsigned long addr)
{
	__kernel_stw(b, *(vusp)addr);
}

__EXTERN_INLINE void irongate_writel(unsigned int b, unsigned long addr)
{
	*(vuip)addr = b;
}

__EXTERN_INLINE void irongate_writeq(unsigned long b, unsigned long addr)
{
	*(vulp)addr = b;
}

__EXTERN_INLINE unsigned long irongate_ioremap(unsigned long addr)
{
	return addr + IRONGATE_MEM;
}

__EXTERN_INLINE int irongate_is_ioaddr(unsigned long addr)
{
	return addr >= IRONGATE_MEM;
}

#undef vucp
#undef vusp
#undef vuip
#undef vulp

#ifdef __WANT_IO_DEF

#define virt_to_bus	irongate_virt_to_bus
#define bus_to_virt	irongate_bus_to_virt

#define __inb		irongate_inb
#define __inw		irongate_inw
#define __inl		irongate_inl
#define __outb		irongate_outb
#define __outw		irongate_outw
#define __outl		irongate_outl
#define __readb		irongate_readb
#define __readw		irongate_readw
#define __writeb	irongate_writeb
#define __writew	irongate_writew
#define __readl		irongate_readl
#define __readq		irongate_readq
#define __writel	irongate_writel
#define __writeq	irongate_writeq
#define __ioremap	irongate_ioremap
#define __is_ioaddr	irongate_is_ioaddr

#define inb(port)	__inb((port))
#define inw(port)	__inw((port))
#define inl(port)	__inl((port))
#define outb(v, port)	__outb((v),(port))
#define outw(v, port)	__outw((v),(port))
#define outl(v, port)	__outl((v),(port))

#define __raw_readb(a)		__readb((unsigned long)(a))
#define __raw_readw(a)		__readw((unsigned long)(a))
#define __raw_readl(a)		__readl((unsigned long)(a))
#define __raw_readq(a)		__readq((unsigned long)(a))
#define __raw_writeb(v,a)	__writeb((v),(unsigned long)(a))
#define __raw_writew(v,a)	__writew((v),(unsigned long)(a))
#define __raw_writel(v,a)	__writel((v),(unsigned long)(a))
#define __raw_writeq(v,a)	__writeq((v),(unsigned long)(a))

#endif /* __WANT_IO_DEF */

#ifdef __IO_EXTERN_INLINE
#undef __EXTERN_INLINE
#undef __IO_EXTERN_INLINE
#endif

#endif /* __KERNEL__ */

#endif /* __ALPHA_IRONGATE__H__ */
