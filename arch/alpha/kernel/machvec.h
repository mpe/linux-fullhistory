/*
 *	linux/arch/alpha/kernel/machvec.h
 *
 *	Copyright (C) 1997, 1998  Richard Henderson
 *
 * This file has goodies to help simplify instantiation of machine vectors.
 */

#include <linux/config.h>

/* Whee.  TSUNAMI doesn't have an HAE.  Fix things up for the GENERIC
   kernel by defining the HAE address to be that of the cache.  Now
   we can read and write it as we like.  ;-)  */
#define TSUNAMI_HAE_ADDRESS	(&alpha_mv.hae_cache)

/* Whee.  POLARIS doesn't have an HAE.  Fix things up for the GENERIC
   kernel by defining the HAE address to be that of the cache.  Now
   we can read and write it as we like.  ;-)  */
#define POLARIS_HAE_ADDRESS	(&alpha_mv.hae_cache)

/* Only a few systems don't define IACK_SC, handling all interrupts through
   the SRM console.  But splitting out that one case from IO() below
   seems like such a pain.  Define this to get things to compile.  */
#define JENSEN_IACK_SC		1
#define T2_IACK_SC		1


/*
 * Some helpful macros for filling in the blanks.
 */

#define CAT1(x,y)  x##y
#define CAT(x,y)   CAT1(x,y)

#define DO_DEFAULT_RTC rtc_port: 0x70

#define DO_EV4_MMU							\
	max_asn:			EV4_MAX_ASN,			\
	mmu_context_mask:		~0UL,				\
	mv_get_mmu_context:		ev4_get_mmu_context,		\
	mv_flush_tlb_current:		ev4_flush_tlb_current,		\
	mv_flush_tlb_other:		ev4_flush_tlb_other,		\
	mv_flush_tlb_current_page:	ev4_flush_tlb_current_page

#define DO_EV5_MMU							\
	max_asn:			EV5_MAX_ASN,			\
	mmu_context_mask:		~0UL,				\
	mv_get_mmu_context:		ev5_get_mmu_context,		\
	mv_flush_tlb_current:		ev5_flush_tlb_current,		\
	mv_flush_tlb_other:		ev5_flush_tlb_other,		\
	mv_flush_tlb_current_page:	ev5_flush_tlb_current_page

#define DO_EV6_MMU							\
	max_asn:			EV6_MAX_ASN,			\
	mmu_context_mask:		0xfffffffffful,			\
	mv_get_mmu_context:		ev5_get_mmu_context,		\
	mv_flush_tlb_current:		ev5_flush_tlb_current,		\
	mv_flush_tlb_other:		ev5_flush_tlb_other,		\
	mv_flush_tlb_current_page:	ev5_flush_tlb_current_page

#define IO_LITE(UP,low1,low2)						\
	hae_register:		(unsigned long *) CAT(UP,_HAE_ADDRESS),	\
	iack_sc:		CAT(UP,_IACK_SC),			\
	mv_inb:			CAT(low1,_inb),				\
	mv_inw:			CAT(low1,_inw),				\
	mv_inl:			CAT(low1,_inl),				\
	mv_outb:		CAT(low1,_outb),			\
	mv_outw:		CAT(low1,_outw),			\
	mv_outl:		CAT(low1,_outl),			\
	mv_readb:		CAT(low1,_readb),			\
	mv_readw:		CAT(low1,_readw),			\
	mv_readl:		CAT(low1,_readl),			\
	mv_readq:		CAT(low1,_readq),			\
	mv_writeb:		CAT(low1,_writeb),			\
	mv_writew:		CAT(low1,_writew),			\
	mv_writel:		CAT(low1,_writel),			\
	mv_writeq:		CAT(low1,_writeq),			\
	mv_dense_mem:		CAT(low2,_dense_mem)

#define IO(UP,low1,low2)						\
	IO_LITE(UP,low1,low2),						\
	hose_read_config_byte:	CAT(low2,_hose_read_config_byte),	\
	hose_read_config_word:	CAT(low2,_hose_read_config_word),	\
	hose_read_config_dword:	CAT(low2,_hose_read_config_dword),	\
	hose_write_config_byte:	CAT(low2,_hose_write_config_byte),	\
	hose_write_config_word:	CAT(low2,_hose_write_config_word),	\
	hose_write_config_dword: CAT(low2,_hose_write_config_dword),	\
	dma_win_base:		CAT(UP,_DMA_WIN_BASE_DEFAULT),		\
        dma_win_size:		CAT(UP,_DMA_WIN_SIZE_DEFAULT)

/* Any assembler that can generate a GENERIC kernel can generate BWX
   instructions.  So always use them for PYXIS I/O.  */

#define DO_APECS_IO	IO(APECS,apecs,apecs)
#define DO_CIA_IO	IO(CIA,cia,cia)
#define DO_LCA_IO	IO(LCA,lca,lca)
#define DO_MCPCIA_IO	IO(MCPCIA,mcpcia,mcpcia)
#define DO_PYXIS_IO	IO(PYXIS,pyxis_bw,pyxis)
#define DO_POLARIS_IO	IO(POLARIS,polaris,polaris)
#define DO_T2_IO	IO(T2,t2,t2)
#define DO_TSUNAMI_IO	IO(TSUNAMI,tsunami,tsunami)

#define BUS(which)					\
	mv_virt_to_bus:	CAT(which,_virt_to_bus),	\
	mv_bus_to_virt:	CAT(which,_bus_to_virt)

#define DO_APECS_BUS	BUS(apecs)
#define DO_CIA_BUS	BUS(cia)
#define DO_LCA_BUS	BUS(lca)
#define DO_MCPCIA_BUS	BUS(mcpcia)
#define DO_PYXIS_BUS	BUS(pyxis)
#define DO_POLARIS_BUS	BUS(polaris)
#define DO_T2_BUS	BUS(t2)
#define DO_TSUNAMI_BUS	BUS(tsunami)


/*
 * In a GENERIC kernel, we have lots of these vectors floating about,
 * all but one of which we want to go away.  In a non-GENERIC kernel,
 * we want only one, ever.
 *
 * Accomplish this in the GENERIC kernel by puting all of the vectors
 * in the .init.data section where they'll go away.  We'll copy the
 * one we want to the real alpha_mv vector in setup_arch.
 *
 * Accomplish this in a non-GENERIC kernel by ifdef'ing out all but
 * one of the vectors, which will not reside in .init.data.  We then
 * alias this one vector to alpha_mv, so no copy is needed.
 *
 * Upshot: set __initdata to nothing for non-GENERIC kernels.
 */

#ifdef CONFIG_ALPHA_GENERIC
#define __initmv __initdata
#define ALIAS_MV(x)
#else
#define __initmv

/* GCC actually has a syntax for defining aliases, but is under some
   delusion that you shouldn't be able to declare it extern somewhere
   else beforehand.  Fine.  We'll do it ourselves.  */
#define ALIAS_MV(system) asm(".global alpha_mv\nalpha_mv = " #system "_mv");
#endif
