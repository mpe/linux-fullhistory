#ifndef __EMU_WRAPPER_H
#define __EMU_WRAPPER_H

#include <linux/wrapper.h>

#define PCI_SET_DMA_MASK(pdev,mask)        (((pdev)->dma_mask) = (mask))

#ifndef PCI_GET_DRIVER_DATA
  #define PCI_GET_DRIVER_DATA(pdev)             ((pdev)->driver_data)
  #define PCI_SET_DRIVER_DATA(pdev,data)        (((pdev)->driver_data) = (data))
#endif /* PCI_GET_DRIVER_DATA */

#endif
