/* $Id: scatterlist.h,v 1.4 1997/04/10 05:13:32 davem Exp $ */
#ifndef _SPARC64_SCATTERLIST_H
#define _SPARC64_SCATTERLIST_H

#include <asm/page.h>

struct scatterlist {
    char *  address;    /* Location data is to be transferred to */
    char * alt_address; /* Location of actual if address is a 
			 * dma indirect buffer.  NULL otherwise */
    unsigned int length;

    __u32 dvma_address; /* A place to hang host-specific addresses at. */
};

#define ISA_DMA_THRESHOLD ((0xf0000000) + PAGE_OFFSET)

#endif /* !(_SPARC64_SCATTERLIST_H) */
