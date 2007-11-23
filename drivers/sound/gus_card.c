/*
 * linux/kernel/chr_drv/sound/gus_card.c
 * 
 * Detection routine for the Gravis Ultrasound.
 * 
 * Copyright by Hannu Savolainen 1993
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
 * 
 */

#include "sound_config.h"

#if defined(CONFIGURE_SOUNDCARD) && !defined(EXCLUDE_GUS)

#include "gus_hw.h"

void            gusintr (int);

int             gus_base, gus_irq, gus_dma;

static int
set_gus_irq (int interrupt_level)
{
  int             retcode;

#ifdef linux
  struct sigaction sa;

  sa.sa_handler = gusintr;

#ifdef SND_SA_INTERRUPT
  sa.sa_flags = SA_INTERRUPT;
#else
  sa.sa_flags = 0;
#endif

  sa.sa_mask = 0;
  sa.sa_restorer = NULL;

  retcode = irqaction (interrupt_level, &sa);

  if (retcode < 0)
    {
      printk ("GUS: IRQ%d already in use\n", interrupt_level);
    }

#else
  /* #  error Unimplemented for this OS	 */
#endif
  return retcode;
}

int
gus_set_midi_irq (int interrupt_level)
{
  int             retcode;

#ifdef linux
  struct sigaction sa;

  sa.sa_handler = gus_midi_interrupt;

#ifdef SND_SA_INTERRUPT
  sa.sa_flags = SA_INTERRUPT;
#else
  sa.sa_flags = 0;
#endif

  sa.sa_mask = 0;
  sa.sa_restorer = NULL;

  retcode = irqaction (interrupt_level, &sa);

  if (retcode < 0)
    {
      printk ("GUS: IRQ%d already in use\n", interrupt_level);
    }

#else
  /* #  error Unimplemented for this OS	 */
#endif
  return retcode;
}

long
attach_gus_card (long mem_start, struct address_info *hw_config)
{
  int             io_addr;

  set_gus_irq (hw_config->irq);

  if (gus_wave_detect (hw_config->io_base))	/* Try first the default */
    {
      mem_start = gus_wave_init (mem_start, hw_config->irq, hw_config->dma);
#ifndef EXCLUDE_MIDI
      mem_start = gus_midi_init (mem_start);
#endif
      return mem_start;
    }

#ifndef EXCLUDE_GUS_IODETECT

  /*
   * Look at the possible base addresses (0x2X0, X=1, 2, 3, 4, 5, 6)
   */

  for (io_addr = 0x210; io_addr <= 0x260; io_addr += 0x10)
    if (io_addr != hw_config->io_base)	/* Already tested */
      if (gus_wave_detect (io_addr))
	{
	  printk (" WARNING! GUS found at %03x, config was %03x ", io_addr, hw_config->io_base);
	  mem_start = gus_wave_init (mem_start, hw_config->irq, hw_config->dma);
#ifndef EXCLUDE_MIDI
	  mem_start = gus_midi_init (mem_start);
#endif
	  return mem_start;
	}

#endif

  return mem_start;		/* Not detected */
}

int
probe_gus (struct address_info *hw_config)
{
  int             io_addr;

  if (gus_wave_detect (hw_config->io_base))
    return 1;

#ifndef EXCLUDE_GUS_IODETECT

  /*
   * Look at the possible base addresses (0x2X0, X=1, 2, 3, 4, 5, 6)
   */

  for (io_addr = 0x210; io_addr <= 0x260; io_addr += 0x10)
    if (io_addr != hw_config->io_base)	/* Already tested */
      if (gus_wave_detect (io_addr))
	return 1;

#endif

  return 0;
}

void
gusintr (int unit)
{
  unsigned char   src;

  while (1)
    {
      if (!(src = INB (u_IrqStatus)))
	return;

      if (src & DMA_TC_IRQ)
	{
	  guswave_dma_irq ();
	}

      if (src & (MIDI_TX_IRQ | MIDI_RX_IRQ))
	{
#ifndef EXCLUDE_MIDI
	  gus_midi_interrupt (0);
#endif
	}

      if (src & (GF1_TIMER1_IRQ | GF1_TIMER2_IRQ))
	{
	  printk ("T");
	  gus_write8 (0x45, 0);	/* Timer control */
	}

      if (src & (WAVETABLE_IRQ | ENVELOPE_IRQ))
	{
	  gus_voice_irq ();
	}
    }
}

#endif
