#ifndef __EMU_WRAPPER_H
#define __EMU_WRAPPER_H

#include <linux/wrapper.h>

#define PCI_SET_DMA_MASK(pdev,mask)        (((pdev)->dma_mask) = (mask))

#endif
