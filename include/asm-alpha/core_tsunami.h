#ifndef __ALPHA_TSUNAMI__H__
#define __ALPHA_TSUNAMI__H__

#include <linux/config.h>
#include <linux/types.h>
#include <asm/compiler.h>

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

#define TSUNAMI_DMA_WIN_BASE_DEFAULT    (1024*1024*1024U)
#define TSUNAMI_DMA_WIN_SIZE_DEFAULT    (1024*1024*1024U)

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_SRM_SETUP)
#define TSUNAMI_DMA_WIN_BASE		alpha_mv.dma_win_base
#define TSUNAMI_DMA_WIN_SIZE		alpha_mv.dma_win_size
#else
#define TSUNAMI_DMA_WIN_BASE		TSUNAMI_DMA_WIN_BASE_DEFAULT
#define TSUNAMI_DMA_WIN_SIZE		TSUNAMI_DMA_WIN_SIZE_DEFAULT
#endif

/* XXX: Do we need to conditionalize on this?  */
#ifdef USE_48_BIT_KSEG
#define TS_BIAS 0x80000000000UL
#else
#define TS_BIAS 0x10000000000UL
#endif

/*
 * CChip, DChip, and PChip registers
 */

typedef struct {
	volatile unsigned long csr __attribute__((aligned(64)));
} tsunami_64;

typedef struct {
	tsunami_64	csc;
	tsunami_64	mtr;
	tsunami_64	misc;
	tsunami_64	mpd;
	tsunami_64	aar0;
	tsunami_64	aar1;
	tsunami_64	aar2;
	tsunami_64	aar3;
	tsunami_64	dim0;
	tsunami_64	dim1;
	tsunami_64	dir0;
	tsunami_64	dir1;
	tsunami_64	drir;
	tsunami_64	prben;
	tsunami_64	iic;	/* a.k.a. iic0 */
	tsunami_64	wdr;	/* a.k.a. iic1 */
	tsunami_64	mpr0;
	tsunami_64	mpr1;
	tsunami_64	mpr2;
	tsunami_64	mpr3;
	tsunami_64	mctl;
	tsunami_64	ttr;
	tsunami_64	tdr;
	tsunami_64	dim2;
	tsunami_64	dim3;
	tsunami_64	dir2;
	tsunami_64	dir3;
	tsunami_64	iic2;
	tsunami_64	iic3;
} tsunami_cchip;

typedef struct {
	tsunami_64	dsc;
	tsunami_64	str;
	tsunami_64	drev;
} tsunami_dchip;

typedef struct {
	tsunami_64	wsba[4];
	tsunami_64	wsm[4];
	tsunami_64	tba[4];
	tsunami_64	pctl;
	tsunami_64	plat;
	tsunami_64	reserved;
	tsunami_64	perror;
	tsunami_64	perrmask;
	tsunami_64	perrset;
	tsunami_64	tlbiv;
	tsunami_64	tlbia;
	tsunami_64	pmonctl;
	tsunami_64	pmoncnt;
} tsunami_pchip;

#define TSUNAMI_cchip  ((tsunami_cchip *)(IDENT_ADDR+TS_BIAS+0x1A0000000UL))
#define TSUNAMI_dchip  ((tsunami_dchip *)(IDENT_ADDR+TS_BIAS+0x1B0000800UL))
#define TSUNAMI_pchip0 ((tsunami_pchip *)(IDENT_ADDR+TS_BIAS+0x180000000UL))
#define TSUNAMI_pchip1 ((tsunami_pchip *)(IDENT_ADDR+TS_BIAS+0x380000000UL))
extern int TSUNAMI_bootcpu;

/*
 * TSUNAMI Pchip Error register.
 */

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
};                       

/*
 * TSUNAMI Pchip Window Space Base Address register.
 */
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
};

/*
 * TSUNAMI Pchip Control Register
 */
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
};

/*
 * TSUNAMI Pchip Error Mask Register.
 */
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
};                       

/*
 * Memory spaces:
 */
#define HOSE(h) (((unsigned long)(h)) << 33)

#define TSUNAMI_MEM(h)	     (IDENT_ADDR + TS_BIAS + 0x000000000UL + HOSE(h))
#define _TSUNAMI_IACK_SC(h)  (IDENT_ADDR + TS_BIAS + 0x1F8000000UL + HOSE(h))
#define TSUNAMI_IO(h)	     (IDENT_ADDR + TS_BIAS + 0x1FC000000UL + HOSE(h))
#define TSUNAMI_CONF(h)	     (IDENT_ADDR + TS_BIAS + 0x1FE000000UL + HOSE(h))

#define TSUNAMI_IACK_SC	     _TSUNAMI_IACK_SC(0) /* hack! */


/*
 * Data structure for handling TSUNAMI machine checks:
 */
struct el_TSUNAMI_sysdata_mcheck {
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
__EXTERN_INLINE unsigned long tsunami_virt_to_bus(void * address)
{
	return virt_to_phys(address) + TSUNAMI_DMA_WIN_BASE;
}

__EXTERN_INLINE void * tsunami_bus_to_virt(unsigned long address)
{
	return phys_to_virt(address - TSUNAMI_DMA_WIN_BASE);
}

/*
 * I/O functions:
 *
 * TSUNAMI, the 21??? PCI/memory support chipset for the EV6 (21264)
 * can only use linear accesses to get at PCI memory and I/O spaces.
 */

#define vucp	volatile unsigned char *
#define vusp	volatile unsigned short *
#define vuip	volatile unsigned int *
#define vulp	volatile unsigned long *

#define XADDR	((addr) & 0xffffffffUL)
#define XHOSE	(((addr) >> 32) & 3UL)

__EXTERN_INLINE unsigned int tsunami_inb(unsigned long addr)
{
	return __kernel_ldbu(*(vucp)(XADDR + TSUNAMI_IO(XHOSE)));
}

__EXTERN_INLINE void tsunami_outb(unsigned char b, unsigned long addr)
{
	__kernel_stb(b, *(vucp)(XADDR + TSUNAMI_IO(XHOSE)));
	mb();
}

__EXTERN_INLINE unsigned int tsunami_inw(unsigned long addr)
{
	return __kernel_ldwu(*(vusp)(XADDR + TSUNAMI_IO(XHOSE)));
}

__EXTERN_INLINE void tsunami_outw(unsigned short b, unsigned long addr)
{
	__kernel_stw(b, *(vusp)(XADDR + TSUNAMI_IO(XHOSE)));
	mb();
}

__EXTERN_INLINE unsigned int tsunami_inl(unsigned long addr)
{
	return *(vuip)(XADDR + TSUNAMI_IO(XHOSE));
}

__EXTERN_INLINE void tsunami_outl(unsigned int b, unsigned long addr)
{
	*(vuip)(XADDR + TSUNAMI_IO(XHOSE)) = b;
	mb();
}

/*
 * Memory functions.  all accesses are done through linear space.
 */

__EXTERN_INLINE unsigned long tsunami_readb(unsigned long addr)
{
	return __kernel_ldbu(*(vucp)(XADDR + TSUNAMI_MEM(XHOSE)));
}

__EXTERN_INLINE unsigned long tsunami_readw(unsigned long addr)
{
	return __kernel_ldwu(*(vusp)(XADDR + TSUNAMI_MEM(XHOSE)));
}

__EXTERN_INLINE unsigned long tsunami_readl(unsigned long addr)
{
	return *(vuip)(XADDR + TSUNAMI_MEM(XHOSE));
}

__EXTERN_INLINE unsigned long tsunami_readq(unsigned long addr)
{
	return *(vulp)(XADDR + TSUNAMI_MEM(XHOSE));
}

__EXTERN_INLINE void tsunami_writeb(unsigned char b, unsigned long addr)
{
	__kernel_stb(b, *(vucp)(XADDR + TSUNAMI_MEM(XHOSE)));
	mb();
}

__EXTERN_INLINE void tsunami_writew(unsigned short b, unsigned long addr)
{
	__kernel_stw(b, *(vusp)(XADDR + TSUNAMI_MEM(XHOSE)));
	mb();
}

__EXTERN_INLINE void tsunami_writel(unsigned int b, unsigned long addr)
{
	*(vuip)(XADDR + TSUNAMI_MEM(XHOSE)) = b;
	mb();
}

__EXTERN_INLINE void tsunami_writeq(unsigned long b, unsigned long addr)
{
	*(vulp)(XADDR + TSUNAMI_MEM(XHOSE)) = b;
	mb();
}

/* Find the DENSE memory area for a given bus address.  */

__EXTERN_INLINE unsigned long tsunami_dense_mem(unsigned long addr)
{
	return TSUNAMI_MEM(XHOSE);
}

#undef vucp
#undef vusp
#undef vuip
#undef vulp

#undef XADDR
#undef XHOSE

#ifdef __WANT_IO_DEF

#define virt_to_bus	tsunami_virt_to_bus
#define bus_to_virt	tsunami_bus_to_virt

#define __inb		tsunami_inb
#define __inw		tsunami_inw
#define __inl		tsunami_inl
#define __outb		tsunami_outb
#define __outw		tsunami_outw
#define __outl		tsunami_outl
#define __readb		tsunami_readb
#define __readw		tsunami_readw
#define __writeb	tsunami_writeb
#define __writew	tsunami_writew
#define __readl		tsunami_readl
#define __readq		tsunami_readq
#define __writel	tsunami_writel
#define __writeq	tsunami_writeq
#define dense_mem	tsunami_dense_mem

#define inb(port) __inb((port))
#define inw(port) __inw((port))
#define inl(port) __inl((port))

#define outb(v, port) __outb((v),(port))
#define outw(v, port) __outw((v),(port))
#define outl(v, port) __outl((v),(port))

#define readb(a)	__readb((unsigned long)(a))
#define readw(a)	__readw((unsigned long)(a))
#define readl(a)	__readl((unsigned long)(a))
#define readq(a)	__readq((unsigned long)(a))

#define writeb(v,a)	__writeb((v),(unsigned long)(a))
#define writew(v,a)	__writew((v),(unsigned long)(a))
#define writel(v,a)	__writel((v),(unsigned long)(a))
#define writeq(v,a)	__writeq((v),(unsigned long)(a))

#endif /* __WANT_IO_DEF */

#ifdef __IO_EXTERN_INLINE
#undef __EXTERN_INLINE
#undef __IO_EXTERN_INLINE
#endif

#endif /* __KERNEL__ */

#endif /* __ALPHA_TSUNAMI__H__ */
