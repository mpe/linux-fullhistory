/* $Id: scatterlist.h,v 1.2 1997/04/10 05:13:24 davem Exp $ */
#ifndef _SPARC_SCATTERLIST_H
#define _SPARC_SCATTERLIST_H

struct scatterlist {
    char *  address;    /* Location data is to be transferred to */
    char * alt_address; /* Location of actual if address is a 
			 * dma indirect buffer.  NULL otherwise */
    unsigned int length;

    __u32 dvma_address; /* A place to hang host-specific addresses at. */
};

#define ISA_DMA_THRESHOLD (~0UL)

#endif /* !(_SPARC_SCATTERLIST_H) */
