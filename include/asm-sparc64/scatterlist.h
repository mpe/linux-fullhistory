/* $Id: scatterlist.h,v 1.1 1996/12/26 14:22:32 davem Exp $ */
#ifndef _SPARC64_SCATTERLIST_H
#define _SPARC64_SCATTERLIST_H

#include <asm/page.h>

struct scatterlist {
    char *  address;    /* Location data is to be transferred to */
    char * alt_address; /* Location of actual if address is a 
			 * dma indirect buffer.  NULL otherwise */
    unsigned int length;

    char * dvma_address; /* A place to hang host-specific addresses at. */
};

#define ISA_DMA_THRESHOLD ((0x100000000) + PAGE_OFFSET)

#endif /* !(_SPARC64_SCATTERLIST_H) */
