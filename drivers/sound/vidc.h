/*
 * drivers/sound/vidc.h
 *
 * VIDC sound function prototypes
 *
 * Copyright (C) 1997 Russell King <rmk@arm.uk.linux.org>
 */

/* vidc.c */

extern int vidc_busy;

/* vidc_fill.S */

/*
 * Filler routines for different channels and sample sizes
 */

extern unsigned long vidc_fill_1x8_u(unsigned long ibuf, unsigned long iend,
				     unsigned long obuf, int mask);
extern unsigned long vidc_fill_2x8_u(unsigned long ibuf, unsigned long iend,
				     unsigned long obuf, int mask);
extern unsigned long vidc_fill_1x8_s(unsigned long ibuf, unsigned long iend,
				     unsigned long obuf, int mask);
extern unsigned long vidc_fill_2x8_s(unsigned long ibuf, unsigned long iend,
				     unsigned long obuf, int mask);
extern unsigned long vidc_fill_1x16_s(unsigned long ibuf, unsigned long iend,
				      unsigned long obuf, int mask);
extern unsigned long vidc_fill_2x16_s(unsigned long ibuf, unsigned long iend,
				      unsigned long obuf, int mask);

/*
 * DMA Interrupt handler
 */

extern void vidc_sound_dma_irq(int irqnr, void *ref, struct pt_regs *regs);

/*
 * Filler routine pointer
 */

extern unsigned long (*vidc_filler) (unsigned long ibuf, unsigned long iend,
				     unsigned long obuf, int mask);

/*
 * Virtual DMA buffer exhausted
 */

extern void     (*dma_interrupt) (void);

/*
 * Virtual DMA buffer addresses
 */

extern unsigned long dma_start, dma_count, dma_bufsize;
extern unsigned long dma_buf[2], dma_pbuf[2];

/* vidc_audio.c */

extern void     vidc_audio_init(struct address_info *hw_config);
extern int      vidc_audio_get_volume(void);
extern int      vidc_audio_set_volume(int vol);

/* vidc_mixer.c */

extern void     vidc_mixer_init(struct address_info *hw_config);

/* vidc_synth.c */

extern void     vidc_synth_init(struct address_info *hw_config);
extern int      vidc_synth_get_volume(void);
extern int      vidc_synth_set_volume(int vol);
