#ifndef _M68K_SCATTERLIST_H
#define _M68K_SCATTERLIST_H

struct scatterlist {
    char *  address;    /* Location data is to be transferred to */
    char * alt_address; /* Location of actual if address is a 
			 * dma indirect buffer.  NULL otherwise */
    unsigned int length;

#ifdef __sparc__
    char * dvma_address; /* A place to hang host-specific addresses at. */
#endif
};

#define ISA_DMA_THRESHOLD (0x00ffffff)

#endif /* !(_M68K_SCATTERLIST_H) */
