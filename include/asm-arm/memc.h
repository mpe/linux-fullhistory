#define VDMA_ALIGNMENT	PAGE_SIZE
#define VDMA_XFERSIZE	16
#define VDMA_INIT	0
#define VDMA_START	1
#define VDMA_END	2

#define video_set_dma(start,end,offset)				\
do {								\
	memc_write (VDMA_START, (start >> 2));			\
	memc_write (VDMA_END, (end - VDMA_XFERSIZE) >> 2);	\
	memc_write (VDMA_INIT, (offset >> 2));			\
} while (0)

