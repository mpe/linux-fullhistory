#ifndef _PPC_SCATTERLIST_H
#define _PPC_SCATTERLIST_H

#include <linux/config.h>

struct scatterlist {
    char *  address;    /* Location data is to be transferred to */
    char * alt_address; /* Location of actual if address is a 
			 * dma indirect buffer.  NULL otherwise */
    unsigned int length;
};

#ifdef CONFIG_PMAC
/*
 * This is used in the scsi code to decide if bounce buffers are needed.
 * Fortunately the dma controllers on the PowerMac are a bit better
 * than on PCs...
 */
#define ISA_DMA_THRESHOLD (~0UL)
#endif

#ifdef CONFIG_PREP
/* PReP systems are like PCs */
#define ISA_DMA_THRESHOLD (0x00ffffff)
#endif

#endif /* !(_PPC_SCATTERLIST_H) */
