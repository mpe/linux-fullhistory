/* $Id: scatterlist.h,v 1.7 1999/08/30 10:15:01 davem Exp $ */
#ifndef _SPARC64_SCATTERLIST_H
#define _SPARC64_SCATTERLIST_H

#include <asm/page.h>

struct scatterlist {
    char *  address;    /* Location data is to be transferred to */
    char * alt_address; /* Location of actual if address is a 
			 * dma indirect buffer.  NULL otherwise */
    unsigned int length;

    __u32 dvma_address; /* A place to hang host-specific addresses at. */
    __u32 dvma_length;
};

extern unsigned long phys_base;
#define ISA_DMA_THRESHOLD (phys_base + (0xfe000000UL) + PAGE_OFFSET)

#endif /* !(_SPARC64_SCATTERLIST_H) */
