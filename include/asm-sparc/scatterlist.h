/* $Id: scatterlist.h,v 1.3 1999/10/18 01:47:13 zaitcev Exp $ */
#ifndef _SPARC_SCATTERLIST_H
#define _SPARC_SCATTERLIST_H

struct scatterlist {
    char *  address;    /* Location data is to be transferred to */
    char * alt_address; /* Location of actual if address is a 
			 * dma indirect buffer.  NULL otherwise */
    unsigned int length;

    __u32 dvma_address; /* A place to hang host-specific addresses at. */
    __u32 dvma_length;
};

#define ISA_DMA_THRESHOLD (~0UL)

#endif /* !(_SPARC_SCATTERLIST_H) */
