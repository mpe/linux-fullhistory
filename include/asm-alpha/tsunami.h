#ifndef __ALPHA_TSUNAMI__H__
#define __ALPHA_TSUNAMI__H__

#include <linux/config.h>
#include <linux/types.h>

/*
 * TSUNAMI/TYPHOON are the internal names for the core logic chipset which
 * provides memory controller and PCI access for the 21264 based systems.
 *
 * This file is based on:
 *
 * Tsunami System Programmers Manual
 * Preliminary, Chapters 2-5
 *
 */

#define BYTE_ENABLE_SHIFT 5
#define TRANSFER_LENGTH_SHIFT 3

#ifdef CONFIG_ALPHA_SRM_SETUP
/* if we are using the SRM PCI setup, we'll need to use variables instead */
#define TSUNAMI_DMA_WIN_BASE_DEFAULT    (1024*1024*1024)
#define TSUNAMI_DMA_WIN_SIZE_DEFAULT    (1024*1024*1024)

extern unsigned int TSUNAMI_DMA_WIN_BASE;
extern unsigned int TSUNAMI_DMA_WIN_SIZE;

#else /* SRM_SETUP */
#define TSUNAMI_DMA_WIN_BASE	(1024*1024*1024)
#define TSUNAMI_DMA_WIN_SIZE	(1024*1024*1024)
#endif /* SRM_SETUP */

#ifdef USE_48_BIT_KSEG
#define TS_BIAS 0x80000000000UL
#else
#define TS_BIAS 0x10000000000UL
#endif

/*
 * CChip and DChip registers
 */
#define	TSUNAMI_CSR_CSC		(IDENT_ADDR + TS_BIAS + 0x1A0000000UL)
#define	TSUNAMI_CSR_MTR		(IDENT_ADDR + TS_BIAS + 0x1A0000040UL)
#define	TSUNAMI_CSR_MISC	(IDENT_ADDR + TS_BIAS + 0x1A0000080UL)
#define	TSUNAMI_CSR_MPD		(IDENT_ADDR + TS_BIAS + 0x1A00000C0UL)
#define	TSUNAMI_CSR_AAR0	(IDENT_ADDR + TS_BIAS + 0x1A0000100UL)
#define	TSUNAMI_CSR_AAR1	(IDENT_ADDR + TS_BIAS + 0x1A0000140UL)
#define	TSUNAMI_CSR_AAR2	(IDENT_ADDR + TS_BIAS + 0x1A0000180UL)
#define	TSUNAMI_CSR_AAR3	(IDENT_ADDR + TS_BIAS + 0x1A00001C0UL)
#define	TSUNAMI_CSR_DIM0	(IDENT_ADDR + TS_BIAS + 0x1A0000200UL)
#define	TSUNAMI_CSR_DIM1	(IDENT_ADDR + TS_BIAS + 0x1A0000240UL)
#define	TSUNAMI_CSR_DIR0	(IDENT_ADDR + TS_BIAS + 0x1A0000280UL)
#define	TSUNAMI_CSR_DIR1	(IDENT_ADDR + TS_BIAS + 0x1A00002C0UL)

#define	TSUNAMI_CSR_DRIR	(IDENT_ADDR + TS_BIAS + 0x1A0000300UL)
#define	TSUNAMI_CSR_PRBEN	(IDENT_ADDR + TS_BIAS + 0x1A0000340UL)
#define	TSUNAMI_CSR_IIC	       	(IDENT_ADDR + TS_BIAS + 0x1A0000380UL)
#define	TSUNAMI_CSR_WDR	       	(IDENT_ADDR + TS_BIAS + 0x1A00003C0UL)
#define	TSUNAMI_CSR_MPR0	(IDENT_ADDR + TS_BIAS + 0x1A0000400UL)
#define	TSUNAMI_CSR_MPR1	(IDENT_ADDR + TS_BIAS + 0x1A0000440UL)
#define	TSUNAMI_CSR_MPR2	(IDENT_ADDR + TS_BIAS + 0x1A0000480UL)
#define	TSUNAMI_CSR_MPR3	(IDENT_ADDR + TS_BIAS + 0x1A00004C0UL)
#define	TSUNAMI_CSR_TTR		(IDENT_ADDR + TS_BIAS + 0x1A0000580UL)
#define	TSUNAMI_CSR_TDR		(IDENT_ADDR + TS_BIAS + 0x1A00005C0UL)
#define	TSUNAMI_CSR_DSC	       	(IDENT_ADDR + TS_BIAS + 0x1B0000800UL)
#define	TSUNAMI_CSR_STR		(IDENT_ADDR + TS_BIAS + 0x1B0000840UL)
#define	TSUNAMI_CSR_DREV	(IDENT_ADDR + TS_BIAS + 0x1B0000880UL)

/*
 * PChip registers
 */
#define	TSUNAMI_PCHIP0_WSBA0  	(IDENT_ADDR + TS_BIAS + 0x180000000UL)
#define	TSUNAMI_PCHIP0_WSBA1  	(IDENT_ADDR + TS_BIAS + 0x180000040UL)
#define	TSUNAMI_PCHIP0_WSBA2  	(IDENT_ADDR + TS_BIAS + 0x180000080UL)
#define	TSUNAMI_PCHIP0_WSBA3  	(IDENT_ADDR + TS_BIAS + 0x1800000C0UL)

#define	TSUNAMI_PCHIP0_WSM0  	(IDENT_ADDR + TS_BIAS + 0x180000100UL)
#define	TSUNAMI_PCHIP0_WSM1  	(IDENT_ADDR + TS_BIAS + 0x180000140UL)
#define	TSUNAMI_PCHIP0_WSM2  	(IDENT_ADDR + TS_BIAS + 0x180000180UL)
#define	TSUNAMI_PCHIP0_WSM3  	(IDENT_ADDR + TS_BIAS + 0x1800001C0UL)
#define	TSUNAMI_PCHIP0_TBA0  	(IDENT_ADDR + TS_BIAS + 0x180000200UL)
#define	TSUNAMI_PCHIP0_TBA1  	(IDENT_ADDR + TS_BIAS + 0x180000240UL)
#define	TSUNAMI_PCHIP0_TBA2  	(IDENT_ADDR + TS_BIAS + 0x180000280UL)
#define	TSUNAMI_PCHIP0_TBA3  	(IDENT_ADDR + TS_BIAS + 0x1800002C0UL)

#define	TSUNAMI_PCHIP0_PCTL  	(IDENT_ADDR + TS_BIAS + 0x180000300UL)
#define	TSUNAMI_PCHIP0_PLAT  	(IDENT_ADDR + TS_BIAS + 0x180000340UL)
#define	TSUNAMI_PCHIP0_RESERVED	(IDENT_ADDR + TS_BIAS + 0x180000380UL)
#define	TSUNAMI_PCHIP0_PERROR	(IDENT_ADDR + TS_BIAS + 0x1800003c0UL)
#define	TSUNAMI_PCHIP0_PERRMASK	(IDENT_ADDR + TS_BIAS + 0x180000400UL)
#define	TSUNAMI_PCHIP0_PERRSET 	(IDENT_ADDR + TS_BIAS + 0x180000440UL)
#define	TSUNAMI_PCHIP0_TLBIV  	(IDENT_ADDR + TS_BIAS + 0x180000480UL)
#define	TSUNAMI_PCHIP0_TLBIA 	(IDENT_ADDR + TS_BIAS + 0x1800004C0UL)
#define	TSUNAMI_PCHIP0_PMONCTL	(IDENT_ADDR + TS_BIAS + 0x180000500UL)
#define	TSUNAMI_PCHIP0_PMONCNT	(IDENT_ADDR + TS_BIAS + 0x180000540UL)

#define	TSUNAMI_PCHIP1_WSBA0  	(IDENT_ADDR + TS_BIAS + 0x380000000UL)
#define	TSUNAMI_PCHIP1_WSBA1  	(IDENT_ADDR + TS_BIAS + 0x380000040UL)
#define	TSUNAMI_PCHIP1_WSBA2  	(IDENT_ADDR + TS_BIAS + 0x380000080UL)
#define	TSUNAMI_PCHIP1_WSBA3  	(IDENT_ADDR + TS_BIAS + 0x3800000C0UL)
#define	TSUNAMI_PCHIP1_WSM0  	(IDENT_ADDR + TS_BIAS + 0x380000100UL)
#define	TSUNAMI_PCHIP1_WSM1  	(IDENT_ADDR + TS_BIAS + 0x380000140UL)
#define	TSUNAMI_PCHIP1_WSM2  	(IDENT_ADDR + TS_BIAS + 0x380000180UL)
#define	TSUNAMI_PCHIP1_WSM3  	(IDENT_ADDR + TS_BIAS + 0x3800001C0UL)

#define	TSUNAMI_PCHIP1_TBA0  	(IDENT_ADDR + TS_BIAS + 0x380000200UL)
#define	TSUNAMI_PCHIP1_TBA1  	(IDENT_ADDR + TS_BIAS + 0x380000240UL)
#define	TSUNAMI_PCHIP1_TBA2  	(IDENT_ADDR + TS_BIAS + 0x380000280UL)
#define	TSUNAMI_PCHIP1_TBA3  	(IDENT_ADDR + TS_BIAS + 0x3800002C0UL)

#define	TSUNAMI_PCHIP1_PCTL  	(IDENT_ADDR + TS_BIAS + 0x380000300UL)
#define	TSUNAMI_PCHIP1_PLAT  	(IDENT_ADDR + TS_BIAS + 0x380000340UL)
#define	TSUNAMI_PCHIP1_RESERVED	(IDENT_ADDR + TS_BIAS + 0x380000380UL)
#define	TSUNAMI_PCHIP1_PERROR	(IDENT_ADDR + TS_BIAS + 0x3800003c0UL)
#define	TSUNAMI_PCHIP1_PERRMASK	(IDENT_ADDR + TS_BIAS + 0x380000400UL)
#define	TSUNAMI_PCHIP1_PERRSET	(IDENT_ADDR + TS_BIAS + 0x380000440UL)
#define	TSUNAMI_PCHIP1_TLBIV  	(IDENT_ADDR + TS_BIAS + 0x380000480UL)
#define	TSUNAMI_PCHIP1_TLBIA	(IDENT_ADDR + TS_BIAS + 0x3800004C0UL)
#define	TSUNAMI_PCHIP1_PMONCTL	(IDENT_ADDR + TS_BIAS + 0x380000500UL)
#define	TSUNAMI_PCHIP1_PMONCNT	(IDENT_ADDR + TS_BIAS + 0x380000540UL)

/*                                                                          */
/* TSUNAMI Pchip Error register.                                            */
/*                                                                          */
#define perror_m_lost 0x1
#define perror_m_serr 0x2
#define perror_m_perr 0x4
#define perror_m_dcrto 0x8
#define perror_m_sge 0x10
#define perror_m_ape 0x20
#define perror_m_ta 0x40
#define perror_m_rdpe 0x80
#define perror_m_nds 0x100
#define perror_m_rto 0x200
#define perror_m_uecc 0x400
#define perror_m_cre 0x800
#define perror_m_addrl 0xFFFFFFFF0000UL
#define perror_m_addrh 0x7000000000000UL
#define perror_m_cmd 0xF0000000000000UL
#define perror_m_syn 0xFF00000000000000UL
union TPchipPERROR {   
    struct  {
        unsigned int perror_v_lost : 1;
        unsigned perror_v_serr : 1;
        unsigned perror_v_perr : 1;
        unsigned perror_v_dcrto : 1;
        unsigned perror_v_sge : 1;
        unsigned perror_v_ape : 1;
        unsigned perror_v_ta : 1;
        unsigned perror_v_rdpe : 1;
        unsigned perror_v_nds : 1;
        unsigned perror_v_rto : 1;
        unsigned perror_v_uecc : 1;
        unsigned perror_v_cre : 1;                 
        unsigned perror_v_rsvd1 : 4;
        unsigned perror_v_addrl : 32;
        unsigned perror_v_addrh : 3;
        unsigned perror_v_rsvd2 : 1;
        unsigned perror_v_cmd : 4;
        unsigned perror_v_syn : 8;
        } perror_r_bits;
    int perror_q_whole [2];
    } ;                       
/*                                                                          */
/* TSUNAMI Pchip Window Space Base Address register.                        */
/*                                                                          */
#define wsba_m_ena 0x1                
#define wsba_m_sg 0x2
#define wsba_m_ptp 0x4
#define wsba_m_addr 0xFFF00000  
#define wmask_k_sz1gb 0x3FF00000                   
union TPchipWSBA {
    struct  {
        unsigned wsba_v_ena : 1;
        unsigned wsba_v_sg : 1;
        unsigned wsba_v_ptp : 1;
        unsigned wsba_v_rsvd1 : 17;
        unsigned wsba_v_addr : 12;
        unsigned wsba_v_rsvd2 : 32;
        } wsba_r_bits;
    int wsba_q_whole [2];
    } ;
/*									    */
/* TSUNAMI Pchip Control Register					    */
/*									    */
#define pctl_m_fdsc 0x1
#define pctl_m_fbtb 0x2
#define pctl_m_thdis 0x4
#define pctl_m_chaindis 0x8
#define pctl_m_tgtlat 0x10
#define pctl_m_hole 0x20
#define pctl_m_mwin 0x40
#define pctl_m_arbena 0x80
#define pctl_m_prigrp 0x7F00
#define pctl_m_ppri 0x8000
#define pctl_m_rsvd1 0x30000
#define pctl_m_eccen 0x40000
#define pctl_m_padm 0x80000
#define pctl_m_cdqmax 0xF00000
#define pctl_m_rev 0xFF000000
#define pctl_m_crqmax 0xF00000000UL
#define pctl_m_ptpmax 0xF000000000UL
#define pctl_m_pclkx 0x30000000000UL
#define pctl_m_fdsdis 0x40000000000UL
#define pctl_m_fdwdis 0x80000000000UL
#define pctl_m_ptevrfy 0x100000000000UL
#define pctl_m_rpp 0x200000000000UL
#define pctl_m_pid 0xC00000000000UL
#define pctl_m_rsvd2 0xFFFF000000000000UL

union TPchipPCTL {
    struct {
	unsigned pctl_v_fdsc : 1;
	unsigned pctl_v_fbtb : 1;
	unsigned pctl_v_thdis : 1;
	unsigned pctl_v_chaindis : 1;
	unsigned pctl_v_tgtlat : 1;
	unsigned pctl_v_hole : 1;
	unsigned pctl_v_mwin : 1;
	unsigned pctl_v_arbena : 1;
	unsigned pctl_v_prigrp : 7;
	unsigned pctl_v_ppri : 1;
	unsigned pctl_v_rsvd1 : 2;
	unsigned pctl_v_eccen : 1;
	unsigned pctl_v_padm : 1;
	unsigned pctl_v_cdqmax : 4;
	unsigned pctl_v_rev : 8;
	unsigned pctl_v_crqmax : 4;
	unsigned pctl_v_ptpmax : 4;
	unsigned pctl_v_pclkx : 2;
	unsigned pctl_v_fdsdis : 1;
	unsigned pctl_v_fdwdis : 1;
	unsigned pctl_v_ptevrfy : 1;
	unsigned pctl_v_rpp : 1;
	unsigned pctl_v_pid : 2;
	unsigned pctl_v_rsvd2 : 16;
	} pctl_r_bits;
    int pctl_q_whole [2];
} ;
/*                                                                          */
/* TSUNAMI Pchip Error Mask Register.                                       */
/*                                                                          */
#define perrmask_m_lost 0x1
#define perrmask_m_serr 0x2
#define perrmask_m_perr 0x4
#define perrmask_m_dcrto 0x8
#define perrmask_m_sge 0x10
#define perrmask_m_ape 0x20
#define perrmask_m_ta 0x40
#define perrmask_m_rdpe 0x80
#define perrmask_m_nds 0x100
#define perrmask_m_rto 0x200
#define perrmask_m_uecc 0x400
#define perrmask_m_cre 0x800
#define perrmask_m_rsvd 0xFFFFFFFFFFFFF000UL
union TPchipPERRMASK {   
    struct  {
        unsigned int perrmask_v_lost : 1;
        unsigned perrmask_v_serr : 1;
        unsigned perrmask_v_perr : 1;
        unsigned perrmask_v_dcrto : 1;
        unsigned perrmask_v_sge : 1;
        unsigned perrmask_v_ape : 1;
        unsigned perrmask_v_ta : 1;
        unsigned perrmask_v_rdpe : 1;
        unsigned perrmask_v_nds : 1;
        unsigned perrmask_v_rto : 1;
        unsigned perrmask_v_uecc : 1;
        unsigned perrmask_v_cre : 1;                 
        unsigned perrmask_v_rsvd1 : 20;
	unsigned perrmask_v_rsvd2 : 32;
        } perrmask_r_bits;
    int perrmask_q_whole [2];
    } ;                       

/*
 * Memory spaces:
 */
#define TSUNAMI_PCI0_MEM		(IDENT_ADDR + TS_BIAS + 0x000000000UL)
#define TSUNAMI_PCI0_IACK_SC		(IDENT_ADDR + TS_BIAS + 0x1F8000000UL)
#define TSUNAMI_PCI0_IO			(IDENT_ADDR + TS_BIAS + 0x1FC000000UL)
#define TSUNAMI_PCI0_CONF		(IDENT_ADDR + TS_BIAS + 0x1FE000000UL)

#define TSUNAMI_PCI1_MEM		(IDENT_ADDR + TS_BIAS + 0x200000000UL)
#define TSUNAMI_PCI1_IACK_SC		(IDENT_ADDR + TS_BIAS + 0x3F8000000UL)
#define TSUNAMI_PCI1_IO			(IDENT_ADDR + TS_BIAS + 0x3FC000000UL)
#define TSUNAMI_PCI1_CONF		(IDENT_ADDR + TS_BIAS + 0x3FE000000UL)

#define HAE_ADDRESS 0

#ifdef __KERNEL__

/*
 * Translate physical memory address as seen on (PCI) bus into
 * a kernel virtual address and vv.
 */
extern inline unsigned long virt_to_bus(void * address)
{
	return virt_to_phys(address) + TSUNAMI_DMA_WIN_BASE;
}

extern inline void * bus_to_virt(unsigned long address)
{
	return phys_to_virt(address - TSUNAMI_DMA_WIN_BASE);
}

/*
 * I/O functions:
 *
 * TSUNAMI, the 21??? PCI/memory support chipset for the EV6 (21264)
 * can only use linear accesses to get at PCI memory and I/O spaces.
 */

/* HACK ALERT! HACK ALERT! */
/* HACK ALERT! HACK ALERT! */

/* only using PCI bus 0 for now in all routines */

/* HACK ALERT! HACK ALERT! */
/* HACK ALERT! HACK ALERT! */


#define vuip	volatile unsigned int *

extern inline unsigned int __inb(unsigned long addr)
{
	register unsigned long result;

	__asm__ __volatile__ (
		 "ldbu %0,%1"
		 : "=r" (result)
		 : "m"  (*(unsigned char *)(addr+TSUNAMI_PCI0_IO)));

	return result;
}

extern inline void __outb(unsigned char b, unsigned long addr)
{
	__asm__ __volatile__ (
		 "stb %1,%0\n\t"
		 "mb"
		 : : "m" (*(unsigned char *)(addr+TSUNAMI_PCI0_IO)), "r" (b));
}

extern inline unsigned int __inw(unsigned long addr)
{
	register unsigned long result;

	__asm__ __volatile__ (
		 "ldwu %0,%1"
		 : "=r" (result)
		 : "m"  (*(unsigned short *)(addr+TSUNAMI_PCI0_IO)));

	return result;
}

extern inline void __outw(unsigned short b, unsigned long addr)
{
	__asm__ __volatile__ (
		 "stw %1,%0\n\t"
		 "mb"
		 : : "m" (*(unsigned short *)(addr+TSUNAMI_PCI0_IO)), "r" (b));
}

extern inline unsigned int __inl(unsigned long addr)
{
	register unsigned long result;

	__asm__ __volatile__ (
		 "ldl %0,%1"
		 : "=r" (result)
		 : "m"  (*(unsigned int *)(addr+TSUNAMI_PCI0_IO)));

	return result;
}

extern inline void __outl(unsigned int b, unsigned long addr)
{
	__asm__ __volatile__ (
		 "stl %1,%0\n\t"
		 "mb"
		 : : "m" (*(unsigned int *)(addr+TSUNAMI_PCI0_IO)), "r" (b));
}

/*
 * Memory functions.  all accesses are done through linear space.
 */

extern inline unsigned long __readb(unsigned long addr)
{
	register unsigned long result;

	__asm__ __volatile__ (
		 "ldbu %0,%1"
		 : "=r" (result)
		 : "m"  (*(unsigned char *)(addr+TSUNAMI_PCI0_MEM)));

	return result;
}

extern inline unsigned long __readw(unsigned long addr)
{
	register unsigned long result;

	__asm__ __volatile__ (
		 "ldwu %0,%1"
		 : "=r" (result)
		 : "m"  (*(unsigned short *)(addr+TSUNAMI_PCI0_MEM)));

	return result;
}

extern inline unsigned long __readl(unsigned long addr)
{
	register unsigned long result;

	__asm__ __volatile__ (
		 "ldl %0,%1"
		 : "=r" (result)
		 : "m"  (*(unsigned int *)(addr+TSUNAMI_PCI0_MEM)));

	return result;
}

extern inline void __writeb(unsigned char b, unsigned long addr)
{
	__asm__ __volatile__ (
		 "stb %1,%0\n\t"
		 "mb"
		 : : "m" (*(unsigned char *)(addr+TSUNAMI_PCI0_MEM)), "r" (b));
}

extern inline void __writew(unsigned short b, unsigned long addr)
{
	__asm__ __volatile__ (
		 "stw %1,%0\n\t"
		 "mb"
		 : : "m" (*(unsigned short *)(addr+TSUNAMI_PCI0_MEM)), "r" (b));
}

extern inline void __writel(unsigned int b, unsigned long addr)
{
	__asm__ __volatile__ (
		 "stl %1,%0\n\t"
		 "mb"
		 : : "m" (*(unsigned int *)(addr+TSUNAMI_PCI0_MEM)), "r" (b));
}

#define inb(port) __inb((port))
#define inw(port) __inw((port))
#define inl(port) __inl((port))

#define outb(v, port) __outb((v),(port))
#define outw(v, port) __outw((v),(port))
#define outl(v, port) __outl((v),(port))

#define readb(a)	__readb((unsigned long)(a))
#define readw(a)	__readw((unsigned long)(a))
#define readl(a)	__readl((unsigned long)(a))

#define writeb(v,a)	__writeb((v),(unsigned long)(a))
#define writew(v,a)	__writew((v),(unsigned long)(a))
#define writel(v,a)	__writel((v),(unsigned long)(a))

#undef vuip

extern unsigned long tsunami_init (unsigned long mem_start,
				 unsigned long mem_end);

#endif /* __KERNEL__ */

/*
 * Data structure for handling TSUNAMI machine checks:
 */
struct el_TSUNAMI_sysdata_mcheck {
};

#define RTC_PORT(x)	(0x70 + (x))
#define RTC_ADDR(x)	(0x80 | (x))
#define RTC_ALWAYS_BCD	0

#endif /* __ALPHA_TSUNAMI__H__ */
