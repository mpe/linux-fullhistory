/* io-unit.h: Definitions for the sun4d IO-UNIT.
 *
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */
#ifndef _SPARC_IO_UNIT_H
#define _SPARC_IO_UNIT_H

#include <asm/page.h>
#include <asm/spinlock.h>
#include <asm/pgtable.h>

/* The io-unit handles all virtual to physical address translations
 * that occur between the SBUS and physical memory.  Access by
 * the cpu to IO registers and similar go over the xdbus so are
 * translated by the on chip SRMMU.  The io-unit and the srmmu do
 * not need to have the same translations at all, in fact most
 * of the time the translations they handle are a disjunct set.
 * Basically the io-unit handles all dvma sbus activity.
 */
 
/* AIEEE, unlike the nice sun4m, these monsters have 
   fixed DMA range 64M */
 
#define IOUNIT_DMA_BASE	    0xfc000000 /* TOP - 64M */
#define IOUNIT_DMA_SIZE	    0x04000000 /* 64M */
/* We use last 1M for sparc_dvma_malloc */
#define IOUNIT_DVMA_SIZE    0x00100000 /* 1M */

/* The format of an iopte in the external page tables */
#define IOUPTE_PAGE          0xffffff00 /* Physical page number (PA[35:12]) */
#define IOUPTE_CACHE         0x00000080 /* Cached (in Viking/MXCC) */
#define IOUPTE_STREAM        0x00000040
#define IOUPTE_INTRA	     0x00000008 /* Not regular memory - probably direct sbus<->sbus dma 
					   FIXME: Play with this and find out how we can make use of this */
#define IOUPTE_WRITE         0x00000004 /* Writeable */
#define IOUPTE_VALID         0x00000002 /* IOPTE is valid */
#define IOUPTE_PARITY        0x00000001

struct iounit_struct {
	unsigned int		bmap[(IOUNIT_DMA_SIZE >> (PAGE_SHIFT + 3)) / sizeof(unsigned int)];
	spinlock_t		lock;
	iopte_t			*page_table;
	unsigned long		rotor[3];
	unsigned long		limit[4];
};

#define IOUNIT_BMAP1_START	0x00000000
#define IOUNIT_BMAP1_END	(IOUNIT_DMA_SIZE >> (PAGE_SHIFT + 1))
#define IOUNIT_BMAP2_START	IOUNIT_BMAP1_END
#define IOUNIT_BMAP2_END	IOUNIT_BMAP2_START + (IOUNIT_DMA_SIZE >> (PAGE_SHIFT + 2))
#define IOUNIT_BMAPM_START	IOUNIT_BMAP2_END
#define IOUNIT_BMAPM_END	((IOUNIT_DMA_SIZE - IOUNIT_DVMA_SIZE) >> PAGE_SHIFT)

extern __u32 iounit_map_dma_init(struct linux_sbus *, int);
#define iounit_map_dma_finish(sbus, addr, len) mmu_release_scsi_one(addr, len, sbus)
extern __u32 iounit_map_dma_page(__u32, void *, struct linux_sbus *);

#endif /* !(_SPARC_IO_UNIT_H) */
