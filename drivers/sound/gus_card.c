/*
 * sound/gus_card.c
 *
 * Detection routine for the Gravis Ultrasound.
 */
/*
 * Copyright by Hannu Savolainen 1993-1996
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer. 2.
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <linux/config.h>


#include "sound_config.h"

#if defined(CONFIG_GUS)

#include "gus_hw.h"

void            gusintr (int irq, void *dev_id, struct pt_regs *dummy);

int             gus_base, gus_irq, gus_dma;
extern int      gus_wave_volume;
extern int      gus_pcm_volume;
extern int      have_gus_max;

int            *gus_osp;

long
attach_gus_card (long mem_start, struct address_info *hw_config)
{
  int             io_addr;

  gus_osp = hw_config->osp;
  snd_set_irq_handler (hw_config->irq, gusintr, "Gravis Ultrasound", hw_config->osp);

  if (gus_wave_detect (hw_config->io_base))	/*
						 * Try first the default
						 */
    {
      mem_start = gus_wave_init (mem_start, hw_config);

      request_region (hw_config->io_base, 16, "GUS");
      request_region (hw_config->io_base + 0x100, 12, "GUS");	/* 0x10c-> is MAX */

      if (sound_alloc_dma (hw_config->dma, "GUS"))
	printk ("gus_card.c: Can't allocate DMA channel\n");
      if (hw_config->dma2 != -1 && hw_config->dma2 != hw_config->dma)
	if (sound_alloc_dma (hw_config->dma2, "GUS(2)"))
	  printk ("gus_card.c: Can't allocate DMA channel2\n");
#ifdef CONFIG_MIDI
      mem_start = gus_midi_init (mem_start);
#endif
      return mem_start;
    }

#ifndef EXCLUDE_GUS_IODETECT

  /*
   * Look at the possible base addresses (0x2X0, X=1, 2, 3, 4, 5, 6)
   */

  for (io_addr = 0x210; io_addr <= 0x260; io_addr += 0x10)
    if (io_addr != hw_config->io_base)	/*
					 * Already tested
					 */
      if (gus_wave_detect (io_addr))
	{
	  hw_config->io_base = io_addr;

	  printk (" WARNING! GUS found at %x, config was %x ", io_addr, hw_config->io_base);
	  mem_start = gus_wave_init (mem_start, hw_config);
	  request_region (io_addr, 16, "GUS");
	  request_region (io_addr + 0x100, 12, "GUS");	/* 0x10c-> is MAX */
	  if (sound_alloc_dma (hw_config->dma, "GUS"))
	    printk ("gus_card.c: Can't allocate DMA channel\n");
	  if (hw_config->dma2 != -1 && hw_config->dma2 != hw_config->dma)
	    if (sound_alloc_dma (hw_config->dma2, "GUS"))
	      printk ("gus_card.c: Can't allocate DMA channel2\n");
#ifdef CONFIG_MIDI
	  mem_start = gus_midi_init (mem_start);
#endif
	  return mem_start;
	}

#endif

  return mem_start;		/*
				 * Not detected
				 */
}

int
probe_gus (struct address_info *hw_config)
{
  int             io_addr, irq;

  gus_osp = hw_config->osp;

  irq = hw_config->irq;

  if (hw_config->card_subtype == 0)	/* GUS/MAX/ACE */
    if (irq != 3 && irq != 5 && irq != 7 && irq != 9 &&
	irq != 11 && irq != 12 && irq != 15)
      {
	printk ("GUS: Unsupported IRQ %d\n", irq);
	return 0;
      }

  if (!check_region (hw_config->io_base, 16))
    if (!check_region (hw_config->io_base + 0x100, 16))
      if (gus_wave_detect (hw_config->io_base))
	return 1;

#ifndef EXCLUDE_GUS_IODETECT

  /*
   * Look at the possible base addresses (0x2X0, X=1, 2, 3, 4, 5, 6)
   */

  for (io_addr = 0x210; io_addr <= 0x260; io_addr += 0x10)
    if (io_addr != hw_config->io_base)	/*
					 * Already tested
					 */
      if (!check_region (io_addr, 16))
	if (!check_region (io_addr + 0x100, 16))
	  if (gus_wave_detect (io_addr))
	    {
	      hw_config->io_base = io_addr;
	      return 1;
	    }

#endif

  return 0;
}

void
unload_gus (struct address_info *hw_config)
{
  DDB (printk ("unload_gus(%x)\n", hw_config->io_base));

  gus_wave_unload ();

  release_region (hw_config->io_base, 16);
  release_region (hw_config->io_base + 0x100, 12);	/* 0x10c-> is MAX */
  snd_release_irq (hw_config->irq);

  sound_free_dma (hw_config->dma);

  if (hw_config->dma2 != -1 && hw_config->dma2 != hw_config->dma)
    sound_free_dma (hw_config->dma2);
}

void
gusintr (int irq, void *dev_id, struct pt_regs *dummy)
{
  unsigned char   src;
  extern int      gus_timer_enabled;

  sti ();

#ifdef CONFIG_GUSMAX
  if (have_gus_max)
    ad1848_interrupt (irq, NULL, NULL);
#endif

  while (1)
    {
      if (!(src = inb (u_IrqStatus)))
	return;

      if (src & DMA_TC_IRQ)
	{
	  guswave_dma_irq ();
	}

      if (src & (MIDI_TX_IRQ | MIDI_RX_IRQ))
	{
#ifdef CONFIG_MIDI
	  gus_midi_interrupt (0);
#endif
	}

      if (src & (GF1_TIMER1_IRQ | GF1_TIMER2_IRQ))
	{
#ifdef CONFIG_SEQUENCER
	  if (gus_timer_enabled)
	    {
	      sound_timer_interrupt ();
	    }

	  gus_write8 (0x45, 0);	/* Ack IRQ */
	  gus_timer_command (4, 0x80);	/* Reset IRQ flags */

#else
	  gus_write8 (0x45, 0);	/* Stop timers */
#endif
	}

      if (src & (WAVETABLE_IRQ | ENVELOPE_IRQ))
	{
	  gus_voice_irq ();
	}
    }
}

#endif

/*
 * Some extra code for the 16 bit sampling option
 */
#if defined(CONFIG_GUS16)

int
probe_gus_db16 (struct address_info *hw_config)
{
  return ad1848_detect (hw_config->io_base, NULL, hw_config->osp);
}

long
attach_gus_db16 (long mem_start, struct address_info *hw_config)
{
#ifdef CONFIG_GUS
  gus_pcm_volume = 100;
  gus_wave_volume = 90;
#endif

  ad1848_init ("GUS 16 bit sampling", hw_config->io_base,
	       hw_config->irq,
	       hw_config->dma,
	       hw_config->dma, 0,
	       hw_config->osp);
  return mem_start;
}

void
unload_gus_db16 (struct address_info *hw_config)
{

  ad1848_unload (hw_config->io_base,
		 hw_config->irq,
		 hw_config->dma,
		 hw_config->dma, 0);
}
#endif
