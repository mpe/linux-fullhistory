#ifndef __ALPHA_POLARIS__H__
#define __ALPHA_POLARIS__H__

#include <linux/config.h>
#include <linux/types.h>
#include <asm/compiler.h>

/*
 * POLARIS is the internal name for a core logic chipset which provides
 * memory controller and PCI access for the 21164PC chip based systems.
 *
 * This file is based on:
 *
 * Polaris System Controller
 * Device Functional Specification
 * 22-Jan-98
 * Rev. 4.2
 *
 */

/* Polaris memory regions */
#define		POLARIS_SPARSE_MEM_BASE		(IDENT_ADDR + 0xf800000000)
#define		POLARIS_DENSE_MEM_BASE		(IDENT_ADDR + 0xf900000000)
#define 	POLARIS_SPARSE_IO_BASE		(IDENT_ADDR + 0xf980000000)
#define		POLARIS_SPARSE_CONFIG_BASE	(IDENT_ADDR + 0xf9c0000000)
#define		POLARIS_IACK_BASE		(IDENT_ADDR + 0xf9f8000000)
#define		POLARIS_DENSE_IO_BASE		(IDENT_ADDR + 0xf9fc000000)
#define		POLARIS_DENSE_CONFIG_BASE	(IDENT_ADDR + 0xf9fe000000)

#define		POLARIS_IACK_SC			POLARIS_IACK_BASE

/* The Polaris command/status registers live in PCI Config space for
 * bus 0/device 0.  As such, they may be bytes, words, or doublewords.
 */
#define		POLARIS_W_VENID		(POLARIS_DENSE_CONFIG_BASE)
#define		POLARIS_W_DEVID		(POLARIS_DENSE_CONFIG_BASE+2)
#define		POLARIS_W_CMD		(POLARIS_DENSE_CONFIG_BASE+4)
#define		POLARIS_W_STATUS	(POLARIS_DENSE_CONFIG_BASE+6)

/* No HAE address.  Polaris has no concept of an HAE, since it
 * supports transfers of all sizes in dense space.
 */

#define POLARIS_DMA_WIN_BASE_DEFAULT	0x80000000	/* fixed, 2G @ 2G */
#define POLARIS_DMA_WIN_SIZE_DEFAULT	0x80000000	/* fixed, 2G @ 2G */

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_SRM_SETUP)
#define POLARIS_DMA_WIN_BASE		alpha_mv.dma_win_base
#define POLARIS_DMA_WIN_SIZE		alpha_mv.dma_win_size
#else
#define POLARIS_DMA_WIN_BASE		POLARIS_DMA_WIN_BASE_DEFAULT
#define POLARIS_DMA_WIN_SIZE		POLARIS_DMA_WIN_SIZE_DEFAULT
#endif

/*
 * Data structure for handling POLARIS machine checks:
 */
struct el_POLARIS_sysdata_mcheck {
    u_long      psc_status;
    u_long	psc_pcictl0;
    u_long	psc_pcictl1;
    u_long	psc_pcictl2;
};

 #ifdef __KERNEL__

#ifndef __EXTERN_INLINE
#define __EXTERN_INLINE extern inline
#define __IO_EXTERN_INLINE
#endif

__EXTERN_INLINE unsigned long polaris_virt_to_bus(void * address)
{
	return virt_to_phys(address) + POLARIS_DMA_WIN_BASE;
}

__EXTERN_INLINE void * polaris_bus_to_virt(unsigned long address)
{
	return phys_to_virt(address - POLARIS_DMA_WIN_BASE);
}

/*
 * I/O functions:
 *
 * POLARIS, the PCI/memory support chipset for the PCA56 (21164PC)
 * processors, can use either a sparse address  mapping scheme, or the 
 * so-called byte-word PCI address space, to get at PCI memory and I/O.
 *
 * However, we will support only the BWX form.
 */

#define vucp	volatile unsigned char *
#define vusp	volatile unsigned short *
#define vuip	volatile unsigned int  *
#define vulp	volatile unsigned long  *

__EXTERN_INLINE unsigned int polaris_inb(unsigned long addr)
{
        return __kernel_ldbu(*(vucp)(addr + POLARIS_DENSE_IO_BASE));
}

__EXTERN_INLINE void polaris_outb(unsigned char b, unsigned long addr)
{
	__kernel_stb(b, *(vucp)(addr + POLARIS_DENSE_IO_BASE));
	mb();
}

__EXTERN_INLINE unsigned int polaris_inw(unsigned long addr)
{
        return __kernel_ldwu(*(vusp)(addr + POLARIS_DENSE_IO_BASE));
}

__EXTERN_INLINE void polaris_outw(unsigned short b, unsigned long addr)
{
	__kernel_stw(b, *(vusp)(addr + POLARIS_DENSE_IO_BASE));
	mb();
}

__EXTERN_INLINE unsigned int polaris_inl(unsigned long addr)
{
        return *(vuip)(addr + POLARIS_DENSE_IO_BASE);
}

__EXTERN_INLINE void polaris_outl(unsigned int b, unsigned long addr)
{
        *(vuip)(addr + POLARIS_DENSE_IO_BASE) = b;
        mb();
}

/*
 * Memory functions.  Polaris allows all accesses (byte/word
 * as well as long/quad) to be done through dense space.
 *
 * We will only support DENSE access via BWX insns.
 */

__EXTERN_INLINE unsigned long polaris_readb(unsigned long addr)
{
        return __kernel_ldbu(*(vucp)(addr + POLARIS_DENSE_MEM_BASE));
}

__EXTERN_INLINE unsigned long polaris_readw(unsigned long addr)
{
        return __kernel_ldwu(*(vusp)(addr + POLARIS_DENSE_MEM_BASE));
}

__EXTERN_INLINE unsigned long polaris_readl(unsigned long addr)
{
        return *(vuip)(addr + POLARIS_DENSE_MEM_BASE);
}

__EXTERN_INLINE unsigned long polaris_readq(unsigned long addr)
{
        return *(vulp)(addr + POLARIS_DENSE_MEM_BASE);
}

__EXTERN_INLINE void polaris_writeb(unsigned char b, unsigned long addr)
{
        __kernel_stb(b, *(vucp)(addr + POLARIS_DENSE_MEM_BASE));
        mb();
}

__EXTERN_INLINE void polaris_writew(unsigned short b, unsigned long addr)
{
        __kernel_stw(b, *(vusp)(addr + POLARIS_DENSE_MEM_BASE));
        mb();
}

__EXTERN_INLINE void polaris_writel(unsigned int b, unsigned long addr)
{
        *(vuip)(addr + POLARIS_DENSE_MEM_BASE) = b;
        mb();
}

__EXTERN_INLINE void polaris_writeq(unsigned long b, unsigned long addr)
{
        *(vulp)(addr + POLARIS_DENSE_MEM_BASE) = b;
        mb();
}

/* Find the DENSE memory area for a given bus address.  */

__EXTERN_INLINE unsigned long polaris_dense_mem(unsigned long addr)
{
        return POLARIS_DENSE_MEM_BASE;
}

#undef vucp
#undef vusp
#undef vuip
#undef vulp

#ifdef __WANT_IO_DEF

#define virt_to_bus     polaris_virt_to_bus
#define bus_to_virt     polaris_bus_to_virt

#define __inb           polaris_inb
#define __inw           polaris_inw
#define __inl           polaris_inl
#define __outb          polaris_outb
#define __outw          polaris_outw
#define __outl          polaris_outl
#define __readb         polaris_readb
#define __readw         polaris_readw
#define __writeb        polaris_writeb
#define __writew        polaris_writew
#define __readl         polaris_readl
#define __readq         polaris_readq
#define __writel        polaris_writel
#define __writeq        polaris_writeq
#define dense_mem       polaris_dense_mem

#define inb(port) __inb((port))
#define inw(port) __inw((port))
#define inl(port) __inl((port))

#define outb(v, port) __outb((v),(port))
#define outw(v, port) __outw((v),(port))
#define outl(v, port) __outl((v),(port))

#define readb(a)        __readb((unsigned long)(a))
#define readw(a)        __readw((unsigned long)(a))
#define readl(a)        __readl((unsigned long)(a))
#define readq(a)        __readq((unsigned long)(a))

#define writeb(v,a)     __writeb((v),(unsigned long)(a))
#define writew(v,a)     __writew((v),(unsigned long)(a))
#define writel(v,a)     __writel((v),(unsigned long)(a))
#define writeq(v,a)     __writeq((v),(unsigned long)(a))

#endif /* __WANT_IO_DEF */

#ifdef __IO_EXTERN_INLINE
#undef __EXTERN_INLINE
#undef __IO_EXTERN_INLINE
#endif

#endif /* __KERNEL__ */

#endif /* __ALPHA_POLARIS__H__ */
