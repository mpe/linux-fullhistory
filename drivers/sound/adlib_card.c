/*
 * sound/adlib_card.c
 *
 * Detection routine for the AdLib card.
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

#if defined(CONFIG_YM3812)

long
attach_adlib_card (long mem_start, struct address_info *hw_config)
{

  mem_start = opl3_init (mem_start, hw_config->io_base, hw_config->osp);
  request_region (hw_config->io_base, 4, "OPL3/OPL2");

  return mem_start;
}

int
probe_adlib (struct address_info *hw_config)
{

  if (check_region (hw_config->io_base, 4))
    {
      printk ("\n\nopl3.c: I/O port %x already in use\n\n", hw_config->io_base);
      return 0;
    }

  return opl3_detect (hw_config->io_base, hw_config->osp);
}

void
unload_adlib (struct address_info *hw_config)
{
  release_region (hw_config->io_base, 4);
}


#endif
