/*
 * arch/arm/kernel/dma.h
 *
 * Copyright (C) 1998 Russell King
 *
 * This header file describes the interface between the generic DMA handler
 * (dma.c) and the architecture-specific DMA backends (dma-*.c)
 */

typedef struct {
	dmasg_t		buf;		/* single DMA			*/
	int		sgcount;	/* number of DMA SG		*/
	dmasg_t		*sg;		/* DMA Scatter-Gather List	*/

	unsigned int	active:1;	/* Transfer active		*/
	unsigned int	invalid:1;	/* Address/Count changed	*/
	dmamode_t	dma_mode;	/* DMA mode			*/

	unsigned int	lock;		/* Device is allocated		*/
	const char	*device_id;	/* Device name			*/

	unsigned int	dma_base;	/* Controller base address	*/
	int		dma_irq;	/* Controller IRQ		*/
	int		state;		/* Controller state		*/
	dmasg_t		cur_sg;		/* Current controller buffer	*/
} dma_t;

/* Prototype: int arch_request_dma(channel, dma, dev_id)
 * Purpose  : Perform architecture specific claiming of a DMA channel
 * Params   : channel - DMA channel number
 *          : dma     - DMA structure (above) for channel
 *          : dev_id  - device ID string passed with request
 * Returns  : 0 on success, E????? number on error
 */ 
int arch_request_dma(dmach_t channel, dma_t *dma, const char *dev_id);

/* Prototype: int arch_free_dma(channel, dma)
 * Purpose  : Perform architecture specific freeing of a DMA channel
 * Params   : channel - DMA channel number
 *          : dma     - DMA structure for channel
 */
void arch_free_dma(dmach_t channel, dma_t *dma);

/* Prototype: void arch_enable_dma(channel, dma)
 * Purpose  : Enable a claimed DMA channel
 * Params   : channel - DMA channel number
 *          : dma     - DMA structure for channel
 */
void arch_enable_dma(dmach_t channel, dma_t *dma);

/* Prototype: void arch_disable_dma(channel, dma)
 * Purpose  : Disable a claimed DMA channel
 * Params   : channel - DMA channel number
 *          : dma     - DMA structure for channel
 */
void arch_disable_dma(dmach_t channel, dma_t *dma);

/* Prototype: int arch_get_dma_residue(channel, dma)
 * Purpose  : Return number of bytes left to DMA
 * Params   : channel - DMA channel number
 *          : dma     - DMA structure for channel
 * Returns  : Number of bytes left to DMA
 */
int arch_get_dma_residue(dmach_t channel, dma_t *dma);

/* Prototype: void arch_dma_init(dma)
 * Purpose  : Initialise architecture specific DMA
 * Params   : dma - pointer to array of DMA structures
 */
void arch_dma_init(dma_t *dma);
