#ifndef _M68K_DMA_H
#define _M68K_DMA_H 1

/* Don't define MAX_DMA_ADDRESS; it's useless on the m68k and any
   occurrence should be flagged as an error.  */

#define MAX_DMA_CHANNELS 8

extern int request_dma(unsigned int dmanr, const char * device_id);	/* reserve a DMA channel */
extern void free_dma(unsigned int dmanr);	/* release it again */

#endif /* _M68K_DMA_H */
