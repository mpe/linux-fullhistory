/*
 *  Copyright (c) by James Courtier-Dutton <James@superbug.demon.co.uk>
 *  Driver p16v chips
 *  Version: 0.22
 *
 *  FEATURES currently supported:
 *    Output fixed at S32_LE, 2 channel to hw:0,0
 *    Rates: 44.1, 48, 96, 192.
 *
 *  Changelog:
 *  0.8
 *    Use separate card based buffer for periods table.
 *  0.9
 *    Use 2 channel output streams instead of 8 channel.
 *       (8 channel output streams might be good for ASIO type output)
 *    Corrected speaker output, so Front -> Front etc.
 *  0.10
 *    Fixed missed interrupts.
 *  0.11
 *    Add Sound card model number and names.
 *    Add Analog volume controls.
 *  0.12
 *    Corrected playback interrupts. Now interrupt per period, instead of half period.
 *  0.13
 *    Use single trigger for multichannel.
 *  0.14
 *    Mic capture now works at fixed: S32_LE, 96000Hz, Stereo.
 *  0.15
 *    Force buffer_size / period_size == INTEGER.
 *  0.16
 *    Update p16v.c to work with changed alsa api.
 *  0.17
 *    Update p16v.c to work with changed alsa api. Removed boot_devs.
 *  0.18
 *    Merging with snd-emu10k1 driver.
 *  0.19
 *    One stereo channel at 24bit now works.
 *  0.20
 *    Added better register defines.
 *  0.21
 *    Integrated with snd-emu10k1 driver.
 *  0.22
 *    Removed #if 0 ... #endif
 *
 *
 *  BUGS:
 *    Some stability problems when unloading the snd-p16v kernel module.
 *    --
 *
 *  TODO:
 *    SPDIF out.
 *    Find out how to change capture sample rates. E.g. To record SPDIF at 48000Hz.
 *    Currently capture fixed at 48000Hz.
 *
 *    --
 *  GENERAL INFO:
 *    Model: SB0240
 *    P16V Chip: CA0151-DBS
 *    Audigy 2 Chip: CA0102-IAT
 *    AC97 Codec: STAC 9721
 *    ADC: Philips 1361T (Stereo 24bit)
 *    DAC: CS4382-K (8-channel, 24bit, 192Khz)
 *
 *  This code was initally based on code from ALSA's emu10k1x.c which is:
 *  Copyright (c) by Francisco Moraes <fmoraes@nc.rr.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
#include <sound/driver.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/info.h>
#include <sound/emu10k1.h>
#include "p16v.h"

#define SET_CHANNEL 0  /* Testing channel outputs 0=Front, 1=Center/LFE, 2=Unknown, 3=Rear */
#define PCM_FRONT_CHANNEL 0
#define PCM_REAR_CHANNEL 1
#define PCM_CENTER_LFE_CHANNEL 2
#define PCM_UNKNOWN_CHANNEL 3
#define CONTROL_FRONT_CHANNEL 0
#define CONTROL_REAR_CHANNEL 3
#define CONTROL_CENTER_LFE_CHANNEL 1
#define CONTROL_UNKNOWN_CHANNEL 2

/* Card IDs:
 * Class 0401: 1102:0004 (rev 04) Subsystem: 1102:2002 -> Audigy2 ZS 7.1 Model:SB0350
 * Class 0401: 1102:0004 (rev 04) Subsystem: 1102:1007 -> Audigy2 6.1    Model:SB0240
 * Class 0401: 1102:0004 (rev 04) Subsystem: 1102:1002 -> Audigy2 Platinum  Model:SB msb0240230009266
 * Class 0401: 1102:0004 (rev 04) Subsystem: 1102:2007 -> Audigy4 Pro Model:SB0380 M1SB0380472001901E
 *
 */

 /* hardware definition */
static snd_pcm_hardware_t snd_p16v_playback_hw = {
	.info =			(SNDRV_PCM_INFO_MMAP | 
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S32_LE, /* Only supports 24-bit samples padded to 32 bits. */
	.rates =		SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_48000 ,
	.rate_min =		48000,
	.rate_max =		192000,
	.channels_min =		8, 
	.channels_max =		8,
	.buffer_bytes_max =	(32*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(16*1024),
	.periods_min =		2,
	.periods_max =		8,
	.fifo_size =		0,
};

static void snd_p16v_pcm_free_substream(snd_pcm_runtime_t *runtime)
{
	snd_pcm_t *epcm = runtime->private_data;
  
	if (epcm) {
        	//snd_printk("epcm free: %p\n", epcm);
		kfree(epcm);
	}
}

/* open_playback callback */
static int snd_p16v_pcm_open_playback_channel(snd_pcm_substream_t *substream, int channel_id)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
        emu10k1_voice_t *channel = &(emu->p16v_voices[channel_id]);
	emu10k1_pcm_t *epcm;
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	epcm = kcalloc(1, sizeof(*epcm), GFP_KERNEL);
        //snd_printk("epcm kcalloc: %p\n", epcm);

	if (epcm == NULL)
		return -ENOMEM;
	epcm->emu = emu;
	epcm->substream = substream;
        //snd_printk("epcm device=%d, channel_id=%d\n", substream->pcm->device, channel_id);
  
	runtime->private_data = epcm;
	runtime->private_free = snd_p16v_pcm_free_substream;
  
	runtime->hw = snd_p16v_playback_hw;

        channel->emu = emu;
        channel->number = channel_id;

        channel->use=1;
	//snd_printk("p16v: open channel_id=%d, channel=%p, use=0x%x\n", channel_id, channel, channel->use);
        //printk("open:channel_id=%d, chip=%p, channel=%p\n",channel_id, chip, channel);
        //channel->interrupt = snd_p16v_pcm_channel_interrupt;
        channel->epcm=epcm;
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
                return err;

	return 0;
}

/* close callback */
static int snd_p16v_pcm_close_playback(snd_pcm_substream_t *substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	//snd_pcm_runtime_t *runtime = substream->runtime;
        //emu10k1_pcm_t *epcm = runtime->private_data;
        emu->p16v_voices[substream->pcm->device - emu->p16v_device_offset].use=0;
/* FIXME: maybe zero others */
	return 0;
}

static int snd_p16v_pcm_open_playback_front(snd_pcm_substream_t *substream)
{
	return snd_p16v_pcm_open_playback_channel(substream, PCM_FRONT_CHANNEL);
}

/* hw_params callback */
static int snd_p16v_pcm_hw_params_playback(snd_pcm_substream_t *substream,
				      snd_pcm_hw_params_t * hw_params)
{
	int result;
        //snd_printk("hw_params alloc: substream=%p\n", substream);
	result = snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
        //snd_printk("hw_params alloc: result=%d\n", result);
	//dump_stack();
	return result;
}

/* hw_free callback */
static int snd_p16v_pcm_hw_free_playback(snd_pcm_substream_t *substream)
{
	int result;
        //snd_printk("hw_params free: substream=%p\n", substream);
	result = snd_pcm_lib_free_pages(substream);
        //snd_printk("hw_params free: result=%d\n", result);
	//dump_stack();
	return result;
}

/* prepare playback callback */
static int snd_p16v_pcm_prepare_playback(snd_pcm_substream_t *substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	//emu10k1_pcm_t *epcm = runtime->private_data;
	int channel = substream->pcm->device - emu->p16v_device_offset;
	u32 *table_base = (u32 *)(emu->p16v_buffer.area+(8*16*channel));
	u32 period_size_bytes = frames_to_bytes(runtime, runtime->period_size);
	int i;
	u32 tmp;
	
        //snd_printk("prepare:channel_number=%d, rate=%d, format=0x%x, channels=%d, buffer_size=%ld, period_size=%ld, periods=%u, frames_to_bytes=%d\n",channel, runtime->rate, runtime->format, runtime->channels, runtime->buffer_size, runtime->period_size, runtime->periods, frames_to_bytes(runtime, 1));
        //snd_printk("dma_addr=%x, dma_area=%p, table_base=%p\n",runtime->dma_addr, runtime->dma_area, table_base);
	//snd_printk("dma_addr=%x, dma_area=%p, dma_bytes(size)=%x\n",emu->p16v_buffer.addr, emu->p16v_buffer.area, emu->p16v_buffer.bytes);
	tmp = snd_emu10k1_ptr_read(emu, A_SPDIF_SAMPLERATE, channel);
        switch (runtime->rate) {
	case 44100:
	  snd_emu10k1_ptr_write(emu, A_SPDIF_SAMPLERATE, channel, (tmp & ~0xe000) | 0x8000); /* FIXME: This will change the capture rate as well! */
	  break;
	case 48000:
	  snd_emu10k1_ptr_write(emu, A_SPDIF_SAMPLERATE, channel, (tmp & ~0xe000) | 0x0000); /* FIXME: This will change the capture rate as well! */
	  break;
	case 96000:
	  snd_emu10k1_ptr_write(emu, A_SPDIF_SAMPLERATE, channel, (tmp & ~0xe000) | 0x4000); /* FIXME: This will change the capture rate as well! */
	  break;
	case 192000:
	  snd_emu10k1_ptr_write(emu, A_SPDIF_SAMPLERATE, channel, (tmp & ~0xe000) | 0x2000); /* FIXME: This will change the capture rate as well! */
	  break;
	default:
	  snd_emu10k1_ptr_write(emu, A_SPDIF_SAMPLERATE, channel, 0x0000); /* FIXME: This will change the capture rate as well! */
	  break;
	}
	/* FIXME: Check emu->buffer.size before actually writing to it. */
        for(i=0; i < runtime->periods; i++) {
		table_base[i*2]=runtime->dma_addr+(i*period_size_bytes);
		table_base[(i*2)+1]=period_size_bytes<<16;
	}
 
	snd_emu10k1_ptr20_write(emu, PLAYBACK_LIST_ADDR, channel, emu->p16v_buffer.addr+(8*16*channel));
	snd_emu10k1_ptr20_write(emu, PLAYBACK_LIST_SIZE, channel, (runtime->periods - 1) << 19);
	snd_emu10k1_ptr20_write(emu, PLAYBACK_LIST_PTR, channel, 0);
	snd_emu10k1_ptr20_write(emu, PLAYBACK_DMA_ADDR, channel, runtime->dma_addr);
	snd_emu10k1_ptr20_write(emu, PLAYBACK_PERIOD_SIZE, channel, frames_to_bytes(runtime, runtime->period_size)<<16); // buffer size in bytes
	snd_emu10k1_ptr20_write(emu, PLAYBACK_POINTER, channel, 0);
	snd_emu10k1_ptr20_write(emu, 0x07, channel, 0x0);
	snd_emu10k1_ptr20_write(emu, 0x08, channel, 0);

	return 0;
}

static void snd_p16v_intr_enable(emu10k1_t *emu, unsigned int intrenb)
{
	unsigned long flags;
	unsigned int enable;

	spin_lock_irqsave(&emu->emu_lock, flags);
	enable = inl(emu->port + INTE2) | intrenb;
	outl(enable, emu->port + INTE2);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

static void snd_p16v_intr_disable(emu10k1_t *emu, unsigned int intrenb)
{
	unsigned long flags;
	unsigned int disable;

	spin_lock_irqsave(&emu->emu_lock, flags);
	disable = inl(emu->port + INTE2) & (~intrenb);
	outl(disable, emu->port + INTE2);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

/* trigger_playback callback */
static int snd_p16v_pcm_trigger_playback(snd_pcm_substream_t *substream,
				    int cmd)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime;
	emu10k1_pcm_t *epcm;
	int channel;
	int result = 0;
	struct list_head *pos;
        snd_pcm_substream_t *s;
	u32 basic = 0;
	u32 inte = 0;
	int running=0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		running=1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	default:
		running=0;
		break;
	}
        snd_pcm_group_for_each(pos, substream) {
                s = snd_pcm_group_substream_entry(pos);
		runtime = s->runtime;
		epcm = runtime->private_data;
		channel = substream->pcm->device-emu->p16v_device_offset;
		//snd_printk("p16v channel=%d\n",channel);
		epcm->running = running;
		basic |= (0x1<<channel);
		inte |= (INTE2_PLAYBACK_CH_0_LOOP<<channel);
                snd_pcm_trigger_done(s, substream);
        }
	//snd_printk("basic=0x%x, inte=0x%x\n",basic, inte);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		snd_p16v_intr_enable(emu, inte);
		snd_emu10k1_ptr20_write(emu, BASIC_INTERRUPT, 0, snd_emu10k1_ptr20_read(emu, BASIC_INTERRUPT, 0)| (basic));
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		snd_emu10k1_ptr20_write(emu, BASIC_INTERRUPT, 0, snd_emu10k1_ptr20_read(emu, BASIC_INTERRUPT, 0) & ~(basic));
		snd_p16v_intr_disable(emu, inte);
		break;
	default:
		result = -EINVAL;
		break;
	}
	return result;
}

/* pointer_playback callback */
static snd_pcm_uframes_t
snd_p16v_pcm_pointer_playback(snd_pcm_substream_t *substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	emu10k1_pcm_t *epcm = runtime->private_data;
	snd_pcm_uframes_t ptr, ptr1, ptr2,ptr3,ptr4 = 0;
	int channel = substream->pcm->device - emu->p16v_device_offset;
	if (!epcm->running)
		return 0;

	ptr3 = snd_emu10k1_ptr20_read(emu, PLAYBACK_LIST_PTR, channel);
	ptr1 = snd_emu10k1_ptr20_read(emu, PLAYBACK_POINTER, channel);
	ptr4 = snd_emu10k1_ptr20_read(emu, PLAYBACK_LIST_PTR, channel);
	if (ptr3 != ptr4) ptr1 = snd_emu10k1_ptr20_read(emu, PLAYBACK_POINTER, channel);
	ptr2 = bytes_to_frames(runtime, ptr1);
	ptr2+= (ptr4 >> 3) * runtime->period_size;
	ptr=ptr2;
        if (ptr >= runtime->buffer_size)
		ptr -= runtime->buffer_size;

	return ptr;
}

/* operators */
static snd_pcm_ops_t snd_p16v_playback_front_ops = {
	.open =        snd_p16v_pcm_open_playback_front,
	.close =       snd_p16v_pcm_close_playback,
	.ioctl =       snd_pcm_lib_ioctl,
	.hw_params =   snd_p16v_pcm_hw_params_playback,
	.hw_free =     snd_p16v_pcm_hw_free_playback,
	.prepare =     snd_p16v_pcm_prepare_playback,
	.trigger =     snd_p16v_pcm_trigger_playback,
	.pointer =     snd_p16v_pcm_pointer_playback,
};

int snd_p16v_free(emu10k1_t *chip)
{
	// release the data
	if (chip->p16v_buffer.area) {
		snd_dma_free_pages(&chip->p16v_buffer);
		//snd_printk("period lables free: %p\n", &chip->p16v_buffer);
	}
	return 0;
}

static void snd_p16v_pcm_free(snd_pcm_t *pcm)
{
	emu10k1_t *emu = pcm->private_data;
	//snd_printk("snd_p16v_pcm_free pcm: called\n");
	snd_pcm_lib_preallocate_free_for_all(pcm);
	emu->pcm = NULL;
}

int snd_p16v_pcm(emu10k1_t *emu, int device, snd_pcm_t **rpcm)
{
	snd_pcm_t *pcm;
	snd_pcm_substream_t *substream;
	int err;
        int capture=0;
  
	//snd_printk("snd_p16v_pcm called. device=%d\n", device);
	emu->p16v_device_offset = device;
	if (rpcm)
		*rpcm = NULL;
        //if (device == 0) capture=1; 
	if ((err = snd_pcm_new(emu->card, "p16v", device, 1, capture, &pcm)) < 0)
		return err;
  
	pcm->private_data = emu;
	pcm->private_free = snd_p16v_pcm_free;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_p16v_playback_front_ops);

	pcm->info_flags = 0;
	pcm->dev_subclass = SNDRV_PCM_SUBCLASS_GENERIC_MIX;
	strcpy(pcm->name, "p16v");
	emu->pcm = pcm;

	for(substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream; 
	    substream; 
	    substream = substream->next) {
		if ((err = snd_pcm_lib_preallocate_pages(substream, 
							 SNDRV_DMA_TYPE_DEV, 
							 snd_dma_pci_data(emu->pci), 
							 64*1024, 64*1024)) < 0) /* FIXME: 32*1024 for sound buffer, between 32and64 for Periods table. */
			return err;
		//snd_printk("preallocate playback substream: err=%d\n", err);
	}

	for (substream = pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream; 
	      substream; 
	      substream = substream->next) {
 		if ((err = snd_pcm_lib_preallocate_pages(substream, 
	                                           SNDRV_DMA_TYPE_DEV, 
	                                           snd_dma_pci_data(emu->pci), 
	                                           64*1024, 64*1024)) < 0)
			return err;
		//snd_printk("preallocate capture substream: err=%d\n", err);
	}
  
	if (rpcm)
		*rpcm = pcm;
  
	return 0;
}

static int snd_p16v_volume_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
        uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
        uinfo->count = 2;
        uinfo->value.integer.min = 0;
        uinfo->value.integer.max = 255;
        return 0;
}

static int snd_p16v_volume_get(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol, int reg, int high_low)
{
        emu10k1_t *emu = snd_kcontrol_chip(kcontrol);
        u32 value;

        value = snd_emu10k1_ptr20_read(emu, reg, high_low);
	if (high_low == 1) {
        	ucontrol->value.integer.value[0] = 0xff - ((value >> 24) & 0xff); /* Left */
        	ucontrol->value.integer.value[1] = 0xff - ((value >> 16) & 0xff); /* Right */
	} else {
        	ucontrol->value.integer.value[0] = 0xff - ((value >> 8) & 0xff); /* Left */
        	ucontrol->value.integer.value[1] = 0xff - ((value >> 0) & 0xff); /* Right */
	}
        return 0;
}

static int snd_p16v_volume_get_spdif_front(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int high_low = 0;
	int reg = PLAYBACK_VOLUME_MIXER7;
        return snd_p16v_volume_get(kcontrol, ucontrol, reg, high_low);
}

static int snd_p16v_volume_get_spdif_center_lfe(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int high_low = 1;
	int reg = PLAYBACK_VOLUME_MIXER7;
        return snd_p16v_volume_get(kcontrol, ucontrol, reg, high_low);
}
static int snd_p16v_volume_get_spdif_unknown(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int high_low = 0;
	int reg = PLAYBACK_VOLUME_MIXER8;
        return snd_p16v_volume_get(kcontrol, ucontrol, reg, high_low);
}
static int snd_p16v_volume_get_spdif_rear(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int high_low = 1;
	int reg = PLAYBACK_VOLUME_MIXER8;
        return snd_p16v_volume_get(kcontrol, ucontrol, reg, high_low);
}

static int snd_p16v_volume_get_analog_front(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int high_low = 0;
	int reg = PLAYBACK_VOLUME_MIXER9;
        return snd_p16v_volume_get(kcontrol, ucontrol, reg, high_low);
}

static int snd_p16v_volume_get_analog_center_lfe(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int high_low = 1;
	int reg = PLAYBACK_VOLUME_MIXER9;
        return snd_p16v_volume_get(kcontrol, ucontrol, reg, high_low);
}
static int snd_p16v_volume_get_analog_rear(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int high_low = 1;
	int reg = PLAYBACK_VOLUME_MIXER10;
        return snd_p16v_volume_get(kcontrol, ucontrol, reg, high_low);
}

static int snd_p16v_volume_get_analog_unknown(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int high_low = 0;
	int reg = PLAYBACK_VOLUME_MIXER10;
        return snd_p16v_volume_get(kcontrol, ucontrol, reg, high_low);
}

static int snd_p16v_volume_put(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol, int reg, int high_low)
{
        emu10k1_t *emu = snd_kcontrol_chip(kcontrol);
        u32 value;
        value = snd_emu10k1_ptr20_read(emu, reg, 0);
        //value = value & 0xffff;
	if (high_low == 1) {
		value &= 0xffff;
	        value = value | ((0xff - ucontrol->value.integer.value[0]) << 24) | ((0xff - ucontrol->value.integer.value[1]) << 16);
	} else {
		value &= 0xffff0000;
        	value = value | ((0xff - ucontrol->value.integer.value[0]) << 8) | ((0xff - ucontrol->value.integer.value[1]) );
	}
        	snd_emu10k1_ptr20_write(emu, reg, 0, value);
        return 1;
}

static int snd_p16v_volume_put_spdif_front(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int high_low = 0;
	int reg = PLAYBACK_VOLUME_MIXER7;
        return snd_p16v_volume_put(kcontrol, ucontrol, reg, high_low);
}

static int snd_p16v_volume_put_spdif_center_lfe(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int high_low = 1;
	int reg = PLAYBACK_VOLUME_MIXER7;
        return snd_p16v_volume_put(kcontrol, ucontrol, reg, high_low);
}

static int snd_p16v_volume_put_spdif_unknown(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int high_low = 0;
	int reg = PLAYBACK_VOLUME_MIXER8;
        return snd_p16v_volume_put(kcontrol, ucontrol, reg, high_low);
}

static int snd_p16v_volume_put_spdif_rear(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int high_low = 1;
	int reg = PLAYBACK_VOLUME_MIXER8;
        return snd_p16v_volume_put(kcontrol, ucontrol, reg, high_low);
}

static int snd_p16v_volume_put_analog_front(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int high_low = 0;
	int reg = PLAYBACK_VOLUME_MIXER9;
        return snd_p16v_volume_put(kcontrol, ucontrol, reg, high_low);
}

static int snd_p16v_volume_put_analog_center_lfe(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int high_low = 1;
	int reg = PLAYBACK_VOLUME_MIXER9;
        return snd_p16v_volume_put(kcontrol, ucontrol, reg, high_low);
}

static int snd_p16v_volume_put_analog_rear(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int high_low = 1;
	int reg = PLAYBACK_VOLUME_MIXER10;
        return snd_p16v_volume_put(kcontrol, ucontrol, reg, high_low);
}

static int snd_p16v_volume_put_analog_unknown(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int high_low = 0;
	int reg = PLAYBACK_VOLUME_MIXER10;
        return snd_p16v_volume_put(kcontrol, ucontrol, reg, high_low);
}

static snd_kcontrol_new_t snd_p16v_volume_control_analog_front =
{
        .iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
        .name =         "HD Analog Front Volume",
        .info =         snd_p16v_volume_info,
        .get =          snd_p16v_volume_get_analog_front,
        .put =          snd_p16v_volume_put_analog_front
};

static snd_kcontrol_new_t snd_p16v_volume_control_analog_center_lfe =
{
        .iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
        .name =         "HD Analog Center/LFE Volume",
        .info =         snd_p16v_volume_info,
        .get =          snd_p16v_volume_get_analog_center_lfe,
        .put =          snd_p16v_volume_put_analog_center_lfe
};

static snd_kcontrol_new_t snd_p16v_volume_control_analog_unknown =
{
        .iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
        .name =         "HD Analog Unknown Volume",
        .info =         snd_p16v_volume_info,
        .get =          snd_p16v_volume_get_analog_unknown,
        .put =          snd_p16v_volume_put_analog_unknown
};

static snd_kcontrol_new_t snd_p16v_volume_control_analog_rear =
{
        .iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
        .name =         "HD Analog Rear Volume",
        .info =         snd_p16v_volume_info,
        .get =          snd_p16v_volume_get_analog_rear,
        .put =          snd_p16v_volume_put_analog_rear
};

static snd_kcontrol_new_t snd_p16v_volume_control_spdif_front =
{
        .iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
        .name =         "HD SPDIF Front Volume",
        .info =         snd_p16v_volume_info,
        .get =          snd_p16v_volume_get_spdif_front,
        .put =          snd_p16v_volume_put_spdif_front
};

static snd_kcontrol_new_t snd_p16v_volume_control_spdif_center_lfe =
{
        .iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
        .name =         "HD SPDIF Center/LFE Volume",
        .info =         snd_p16v_volume_info,
        .get =          snd_p16v_volume_get_spdif_center_lfe,
        .put =          snd_p16v_volume_put_spdif_center_lfe
};

static snd_kcontrol_new_t snd_p16v_volume_control_spdif_unknown =
{
        .iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
        .name =         "HD SPDIF Unknown Volume",
        .info =         snd_p16v_volume_info,
        .get =          snd_p16v_volume_get_spdif_unknown,
        .put =          snd_p16v_volume_put_spdif_unknown
};

static snd_kcontrol_new_t snd_p16v_volume_control_spdif_rear =
{
        .iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
        .name =         "HD SPDIF Rear Volume",
        .info =         snd_p16v_volume_info,
        .get =          snd_p16v_volume_get_spdif_rear,
        .put =          snd_p16v_volume_put_spdif_rear
};

int snd_p16v_mixer(emu10k1_t *emu)
{
        int err;
        snd_kcontrol_t *kctl;
        snd_card_t *card = emu->card;
        if ((kctl = snd_ctl_new1(&snd_p16v_volume_control_analog_front, emu)) == NULL)
                return -ENOMEM;
        if ((err = snd_ctl_add(card, kctl)))
                return err;
        if ((kctl = snd_ctl_new1(&snd_p16v_volume_control_analog_rear, emu)) == NULL)
                return -ENOMEM;
        if ((err = snd_ctl_add(card, kctl)))
                return err;
        if ((kctl = snd_ctl_new1(&snd_p16v_volume_control_analog_center_lfe, emu)) == NULL)
                return -ENOMEM;
        if ((err = snd_ctl_add(card, kctl)))
                return err;
        if ((kctl = snd_ctl_new1(&snd_p16v_volume_control_analog_unknown, emu)) == NULL)
                return -ENOMEM;
        if ((err = snd_ctl_add(card, kctl)))
                return err;
        if ((kctl = snd_ctl_new1(&snd_p16v_volume_control_spdif_front, emu)) == NULL)
                return -ENOMEM;
        if ((err = snd_ctl_add(card, kctl)))
                return err;
        if ((kctl = snd_ctl_new1(&snd_p16v_volume_control_spdif_rear, emu)) == NULL)
                return -ENOMEM;
        if ((err = snd_ctl_add(card, kctl)))
                return err;
        if ((kctl = snd_ctl_new1(&snd_p16v_volume_control_spdif_center_lfe, emu)) == NULL)
                return -ENOMEM;
        if ((err = snd_ctl_add(card, kctl)))
                return err;
        if ((kctl = snd_ctl_new1(&snd_p16v_volume_control_spdif_unknown, emu)) == NULL)
                return -ENOMEM;
        if ((err = snd_ctl_add(card, kctl)))
                return err;
        return 0;
}

