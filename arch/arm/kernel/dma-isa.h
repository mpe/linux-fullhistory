/*
 * Request an ISA DMA channel
 */
int isa_request_dma(int channel, dma_t *dma, const char *dev_name);

/*
 * Free an ISA DMA channel
 */
void isa_free_dma(int channel, dma_t *dma);

/*
 * Get ISA DMA channel residue
 */
int isa_get_dma_residue(int channel, dma_t *dma);

/*
 * Enable (and set up) an ISA DMA channel
 */
void isa_enable_dma(int channel, dma_t *dma);

/*
 * Disable an ISA DMA channel
 */
void isa_disable_dma(int channel, dma_t *dma);

