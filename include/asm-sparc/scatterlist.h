/* $Id: scatterlist.h,v 1.1 1996/12/24 07:38:48 davem Exp $ */
#ifndef _SPARC_SCATTERLIST_H
#define _SPARC_SCATTERLIST_H

struct scatterlist {
    char *  address;    /* Location data is to be transferred to */
    char * alt_address; /* Location of actual if address is a 
			 * dma indirect buffer.  NULL otherwise */
    unsigned int length;

    char * dvma_address; /* A place to hang host-specific addresses at. */
};

#define ISA_DMA_THRESHOLD (~0UL)

#endif /* !(_SPARC_SCATTERLIST_H) */
