/*
 * sound/awe_wave.c
 *
 * The low level driver for the AWE32/Sound Blaster 32 wave table synth.
 *   version 0.3.1b; Jan. 21, 1997
 *
 * Copyright (C) 1996,1997 Takashi Iwai
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define AWEDRV_VERSION		"0.3.1b"
#ifdef __FreeBSD__
#  include <i386/isa/sound/awe_config.h>
#else
#  include "awe_config.h"
#endif

/*----------------------------------------------------------------*/

#ifdef CONFIG_AWE32_SYNTH

#ifdef __FreeBSD__
#  include <i386/isa/sound/awe_hw.h>
#  include <i386/isa/sound/awe_voice.h>
#else
#  include "awe_hw.h"
#  include <linux/awe_voice.h>
#endif

#ifdef AWE_OBSOLETE_VOXWARE
#  ifdef __FreeBSD__
#    define SEQUENCER_C
#    include <i386/isa/sound/tuning.h>
#  else
#    include "tuning.h"
#  endif
#else
#  include "../tuning.h"
#endif

#ifdef linux
#  include <linux/ultrasound.h>
#elif defined(__FreeBSD__)
#  include <machine/ultrasound.h>
#endif


/*----------------------------------------------------------------
 * debug message
 *----------------------------------------------------------------*/

static int debug_mode = 0;
#ifdef AWE_DEBUG_ON
#define DEBUG(LVL,XXX)	{if (debug_mode > LVL) { XXX; }}
#define ERRMSG(XXX)	{if (debug_mode) { XXX; }}
#define FATALERR(XXX)	XXX
#else
#define DEBUG(LVL,XXX) /**/
#define ERRMSG(XXX)	XXX
#define FATALERR(XXX)	XXX
#endif

/*----------------------------------------------------------------
 * bank and voice record
 *----------------------------------------------------------------*/

/* bank record */
typedef struct _awe_voice_list {
	unsigned char bank, instr;
	awe_voice_info v;
	struct _awe_voice_list *next_instr;
	struct _awe_voice_list *next_bank;
} awe_voice_list;

/* sample and information table */
static awe_sample_info *samples;
static awe_voice_list *infos;

#define AWE_MAX_PRESETS		256
#define AWE_DEFAULT_BANK	0
#define AWE_DRUM_BANK		128

/* preset table index */
static awe_voice_list *preset_table[AWE_MAX_PRESETS];

/*----------------------------------------------------------------
 * voice table
 *----------------------------------------------------------------*/

/* effects table */
#define AWE_FX_NBYTES	((AWE_FX_END+7)/8)
typedef	struct FX_Rec { /* channel effects */
	unsigned char flags[AWE_FX_NBYTES];
	short val[AWE_FX_END];
} FX_Rec;


/* channel parameters */
typedef struct _awe_chan_info {
	int bank;		/* current tone bank */
	int instr;		/* current program */
	int bender;		/* midi pitchbend (-8192 - 8192) */
	int bender_range;	/* midi bender range (x100) */
	int panning;		/* panning (0-127) */
	int main_vol;		/* channel volume (0-127) */
	int expression_vol;	/* midi expression (0-127) */
	awe_voice_list *vrec;	/* instrument list */
	awe_voice_list *def_vrec; /* default instrument list */
	FX_Rec fx;		/* effects */
	int sustained;		/* sustain status in MIDI */
} awe_chan_info;

/* voice parameters */
typedef struct _voice_info {
	int state;
#define AWE_ST_OFF		0	/* no sound */
#define AWE_ST_ON		1	/* playing */
#define AWE_ST_STANDBY		2	/* stand by for playing */
#define AWE_ST_SUSTAINED	3	/* sustained */
#define AWE_ST_MARK		4	/* marked for allocation */

	int ch;			/* midi channel */
	int key;		/* internal key for search */
	int time;		/* allocated time */
	awe_chan_info	*cinfo;	/* channel info */

	int note;		/* midi key (0-127) */
	int velocity;		/* midi velocity (0-127) */
	awe_voice_info *sample;	/* assigned voice */

	/* EMU8000 parameters */
	int apitch;		/* pitch parameter */
	int avol;		/* volume parameter */
	int apan;		/* panning parameter */
} voice_info;

/* voice information */
static voice_info voices[AWE_MAX_VOICES];

#define IS_NO_SOUND(v)	(voices[v].state == AWE_ST_OFF || voices[v].state == AWE_ST_STANDBY)
#define IS_NO_EFFECT(v)	(voices[v].state != AWE_ST_ON)
#define IS_PLAYING(v)	(!IS_NO_SOUND(v))


/* MIDI channel effects information (for hw control) */
#if AWE_MAX_CHANNELS < AWE_MAX_VOICES
static awe_chan_info channels[AWE_MAX_VOICES];
#else
static awe_chan_info channels[AWE_MAX_CHANNELS];
#endif


/*----------------------------------------------------------------
 * global variables
 *----------------------------------------------------------------*/

/* awe32 base address (overwritten at initialization) */
static int awe_base = 0;
/* memory byte size (overwritten at initialization) */
static long awe_mem_size = 0;

/* maximum channels for playing */
static int awe_max_voices = AWE_MAX_VOICES;

static long free_mem_ptr = 0;		/* free word byte size */
static int free_info = 0;		/* free info tables */
static int last_info = 0;		/* last loaded info index */
static int free_sample = 0;		/* free sample tables */
static int last_sample = 0;		/* last loaded sample index */
static int loaded_once = 0;		/* samples are loaded after init? */
static unsigned short current_sf_id = 0;	/* internal id */

static int reverb_mode = 0;		/* reverb mode */
static int chorus_mode = 0;		/* chorus mode */
static unsigned short init_atten = AWE_DEFAULT_ATTENUATION; /* 12dB below */

static int awe_present = FALSE;		/* awe device present? */
static int awe_busy = FALSE;		/* awe device opened? */

#define DEFAULT_DRUM_FLAGS	(1 << 9)
#define IS_DRUM_CHANNEL(c)	(drum_flags & (1 << (c)))
static unsigned long drum_flags = DEFAULT_DRUM_FLAGS; /* channel flags */

static int awe_channel_mode = 0;		/* channel control mode */

static int current_alloc_time = 0;		/* voice allocation time */

static int awe_gus_bank = AWE_DEFAULT_BANK;	/* GUS default bank number */
static int awe_exclusive_sound = TRUE;		/* exclusive sound on */


static struct synth_info awe_info = {
	"AWE32 Synth",		/* name */
	0,			/* device */
	SYNTH_TYPE_SAMPLE,	/* synth_type */
	SAMPLE_TYPE_AWE32,	/* synth_subtype */
	0,			/* perc_mode (obsolete) */
	AWE_MAX_VOICES,		/* nr_voices */
	0,			/* nr_drums (obsolete) */
	AWE_MAX_INFOS		/* instr_bank_size */
};


static struct voice_alloc_info *voice_alloc;	/* set at initialization */


/*----------------------------------------------------------------
 * function prototypes
 *----------------------------------------------------------------*/

#ifndef AWE_OBSOLETE_VOXWARE
static int awe_check_port(void);
static void awe_request_region(void);
static void awe_release_region(void);
#endif

static void awe_reset_samples(void);
/* emu8000 chip i/o access */
static void awe_poke(unsigned short cmd, unsigned short port, unsigned short data);
static void awe_poke_dw(unsigned short cmd, unsigned short port, unsigned long data);
static unsigned short awe_peek(unsigned short cmd, unsigned short port);
static unsigned long awe_peek_dw(unsigned short cmd, unsigned short port);
static void awe_wait(unsigned short delay);

/* initialize emu8000 chip */
static void awe_initialize(void);

/* set voice parameters */
static void awe_init_voice_info(awe_voice_info *vp);
static void awe_init_voice_parm(awe_voice_parm *pp);
#ifdef AWE_HAS_GUS_COMPATIBILITY
static int freq_to_note(int freq);
static int calc_rate_offset(int Hz);
/*static int calc_parm_delay(int msec);*/
static int calc_parm_hold(int msec);
static int calc_parm_attack(int msec);
static int calc_parm_decay(int msec);
static int calc_parm_search(int msec, short *table);
#endif

/* turn on/off note */
static void awe_note_on(int voice);
static void awe_note_off(int voice);
static void awe_terminate(int voice);
static void awe_exclusive_off(int voice, int exclass);

/* calculate voice parameters */
typedef void (*fx_affect_func)(int voice, int forced);
static void awe_set_pitch(int voice, int forced);
static void awe_set_voice_pitch(int voice, int forced);
static void awe_set_volume(int voice, int forced);
static void awe_set_voice_vol(int voice, int forced);
static void awe_set_pan(int voice, int forced);
static void awe_fx_fmmod(int voice, int forced);
static void awe_fx_tremfrq(int voice, int forced);
static void awe_fx_fm2frq2(int voice, int forced);
static void awe_fx_cutoff(int voice, int forced);
static void awe_calc_pitch(int voice);
#ifdef AWE_HAS_GUS_COMPATIBILITY
static void awe_calc_pitch_from_freq(int voice, int freq);
#endif
static void awe_calc_volume(int voice);
static void awe_voice_init(int voice);

/* sequencer interface */
static int awe_open(int dev, int mode);
static void awe_close(int dev);
static int awe_ioctl(int dev, unsigned int cmd, caddr_t arg);
static int awe_kill_note(int dev, int voice, int note, int velocity);
static int awe_start_note(int dev, int v, int note_num, int volume);
static int awe_set_instr(int dev, int voice, int instr_no);
static int awe_set_instr_2(int dev, int voice, int instr_no);
static void awe_reset(int dev);
static void awe_hw_control(int dev, unsigned char *event);
static int awe_load_patch(int dev, int format, const char *addr,
			  int offs, int count, int pmgr_flag);
static void awe_aftertouch(int dev, int voice, int pressure);
static void awe_controller(int dev, int voice, int ctrl_num, int value);
static void awe_panning(int dev, int voice, int value);
static void awe_volume_method(int dev, int mode);
#ifndef AWE_NO_PATCHMGR
static int awe_patchmgr(int dev, struct patmgr_info *rec);
#endif
static void awe_bender(int dev, int voice, int value);
static int awe_alloc(int dev, int chn, int note, struct voice_alloc_info *alloc);
static void awe_setup_voice(int dev, int voice, int chn);

/* hardware controls */
#ifdef AWE_HAS_GUS_COMPATIBILITY
static void awe_hw_gus_control(int dev, int cmd, unsigned char *event);
#endif
static void awe_hw_awe_control(int dev, int cmd, unsigned char *event);
static void awe_voice_change(int voice, fx_affect_func func);
static void awe_sustain_off(int voice, int forced);

/* voice search */
static awe_voice_list *awe_search_instr(int bank, int preset);
static int awe_search_multi_voices(awe_voice_list *rec, int note, int velocity, awe_voice_info **vlist);
static void awe_alloc_multi_voices(int ch, int note, int velocity);
static void awe_alloc_one_voice(int voice, int note, int velocity);
static int awe_clear_voice(void);

/* load / remove patches */
static void awe_check_loaded(void);
static int awe_load_info(awe_patch_info *patch, const char *addr);
static int awe_load_data(awe_patch_info *patch, const char *addr);
#ifdef AWE_HAS_GUS_COMPATIBILITY
static int awe_load_guspatch(const char *addr, int offs, int size, int pmgr_flag);
#endif
static int awe_write_wave_data(const char *addr, long offset, int size);
static awe_voice_list *awe_get_removed_list(awe_voice_list *curp);
static void awe_remove_samples(void);
static short awe_set_sample(awe_voice_info *vp);

/* lowlevel functions */
static void awe_init_audio(void);
static void awe_init_dma(void);
static void awe_init_array(void);
static void awe_send_array(unsigned short *data);
static void awe_tweak_voice(int voice);
static void awe_tweak(void);
static void awe_init_fm(void);
static int awe_open_dram_for_write(int offset);
static int awe_open_dram_for_read(int offset);
static void awe_open_dram_for_check(void);
static void awe_close_dram(void);
static void awe_close_dram_for_read(void);
static void awe_write_dram(unsigned short c);
static int awe_detect_base(int addr);
static int awe_detect(void);
static int awe_check_dram(void);
static void awe_set_chorus_mode(int mode);
static void awe_set_reverb_mode(int mode);

#ifdef AWE_OBSOLETE_VOXWARE

#define awe_check_port()	0	/* always false */
#define awe_request_region()	/* nothing */
#define awe_release_region()	/* nothing */

#else /* AWE_OBSOLETE_VOXWARE */

/* the following macros are osbolete */

#define PERMANENT_MALLOC(type,var,size,memptr) \
	var = (type)(sound_mem_blocks[sound_nblocks++] = vmalloc(size))
#define RET_ERROR(err)			-err

#endif /* AWE_OBSOLETE_VOXWARE */


/* macros for Linux and FreeBSD compatibility */

#undef OUTW
#undef COPY_FROM_USER
#undef GET_BYTE_FROM_USER
#undef GET_SHORT_FROM_USER
#undef IOCTL_TO_USER
  
#ifdef linux
#  define NO_DATA_ERR                 ENODATA
#  define OUTW(data, addr)            outw(data, addr)

#ifdef AWE_NEW_KERNEL_INTERFACE
#  define COPY_FROM_USER(target, source, offs, count) \
              copy_from_user( ((caddr_t)(target)),(source)+(offs),(count) )
#  define GET_BYTE_FROM_USER(target, addr, offs)      \
		get_user(target, (unsigned char*)&((addr)[offs]))
#  define GET_SHORT_FROM_USER(target, addr, offs)     \
		get_user(target, (unsigned short*)&((addr)[offs]))
#  define IOCTL_TO_USER(target, offs, source, count)  \
              copy_to_user  ( ((caddr_t)(target)),(source)+(offs),(count) )
#else /* AWE_NEW_KERNEL_INTERFACE */
#  define COPY_FROM_USER(target, source, offs, count) \
              memcpy_fromfs( ((caddr_t)(target)),(source)+(offs),(count) )
#  define GET_BYTE_FROM_USER(target, addr, offs)      \
              *((char  *)&(target)) = get_fs_byte( (addr)+(offs) )
#  define GET_SHORT_FROM_USER(target, addr, offs)     \
              *((short *)&(target)) = get_fs_word( (addr)+(offs) )
#  define IOCTL_TO_USER(target, offs, source, count)  \
              memcpy_tofs  ( ((caddr_t)(target)),(source)+(offs),(count) )
#endif /* AWE_NEW_KERNEL_INTERFACE */

#  define BZERO(target,len)                           \
              memset( (caddr_t)target, '\0', len )
#  define MEMCPY(dst,src,len) \
              memcpy((caddr_t)dst, (caddr_t)src, len)

#elif defined(__FreeBSD__)
#  define NO_DATA_ERR                 EINVAL
#  define OUTW(data, addr)            outw(addr, data)
#  define COPY_FROM_USER(target, source, offs, count) \
              uiomove( ((caddr_t)(target)),(count),((struct uio *)(source)) )
#  define GET_BYTE_FROM_USER(target, addr, offs)      \
              uiomove( ((char*)&(target)), 1, ((struct uio *)(addr)) )
#  define GET_SHORT_FROM_USER(target, addr, offs)     \
              uiomove( ((char*)&(target)), 2, ((struct uio *)(addr)) )
#  define IOCTL_TO_USER(target, offs, source, count)  \
              memcpy( &((target)[offs]), (source), (count) )
#  define BZERO(target,len)                           \
              bzero( (caddr_t)target, len )
#  define MEMCPY(dst,src,len) \
              bcopy((caddr_t)src, (caddr_t)dst, len)
#endif


/*----------------------------------------------------------------
 * synth operation table
 *----------------------------------------------------------------*/

static struct synth_operations awe_operations =
{
	"EMU8K",
	&awe_info,
	0,
	SYNTH_TYPE_SAMPLE,
	SAMPLE_TYPE_AWE32,
	awe_open,
	awe_close,
	awe_ioctl,
	awe_kill_note,
	awe_start_note,
	awe_set_instr_2,
	awe_reset,
	awe_hw_control,
	awe_load_patch,
	awe_aftertouch,
	awe_controller,
	awe_panning,
	awe_volume_method,
#ifndef AWE_NO_PATCHMGR
	awe_patchmgr,
#endif
	awe_bender,
	awe_alloc,
	awe_setup_voice
};



/*================================================================
 * attach / unload interface
 *================================================================*/

#ifdef AWE_OBSOLETE_VOXWARE
long attach_awe_obsolete(long mem_start, struct address_info *hw_config)
#else
int attach_awe(void)
#endif
{
	/* check presence of AWE32 card */
	if (! awe_detect()) {
		printk("AWE32: not detected\n");
		return 0;
	}

	/* check AWE32 ports are available */
	if (awe_check_port()) {
		printk("AWE32: I/O area already used.\n");
		return 0;
	}

	/* allocate sample tables */
	PERMANENT_MALLOC(awe_sample_info *, samples,
			 AWE_MAX_SAMPLES * AWE_SAMPLE_INFO_SIZE, mem_start);
	PERMANENT_MALLOC(awe_voice_list *, infos,
			 AWE_MAX_INFOS * sizeof(awe_voice_list), mem_start);
	if (samples == NULL || infos == NULL) {
		printk("AWE32: can't allocate sample tables\n");
		return 0;
	}

	if (num_synths >= MAX_SYNTH_DEV)
		printk("AWE32 Error: too many synthesizers\n");
	else {
		voice_alloc = &awe_operations.alloc;
		voice_alloc->max_voice = awe_max_voices;
		synth_devs[num_synths++] = &awe_operations;
	}

	/* reserve I/O ports for awedrv */
	awe_request_region();

	/* clear all samples */
	awe_reset_samples();

	/* intialize AWE32 hardware */
	awe_initialize();

#ifndef __FreeBSD__
	printk("AWE32 Sound Driver v%s (DRAM %dk)\n",
	       AWEDRV_VERSION, (int)awe_mem_size/1024);
#else
	DEBUG(0,printk("AWE32 Sound Driver v%s (DRAM %dk)\n",
                   AWEDRV_VERSION, (int)awe_mem_size/1024));
#endif
	sprintf(awe_info.name, "AWE32 Driver v%s (DRAM %dk)",
		AWEDRV_VERSION, (int)awe_mem_size/1024);

	/* set reverb & chorus modes */
	awe_set_reverb_mode(reverb_mode);
	awe_set_chorus_mode(chorus_mode);

	awe_present = TRUE;

#ifdef AWE_OBSOLETE_VOXWARE
	return mem_start;
#else
	return 1;
#endif
}


void unload_awe(void)
{
	if (awe_present) {
		awe_reset_samples();
		awe_release_region();
	}
}


#ifdef AWE_OBSOLETE_VOXWARE
int probe_awe_obsolete(struct address_info *hw_config)
{
	return 1;
	/*return awe_detect();*/
}

#endif

/*================================================================
 * clear sample tables 
 *================================================================*/

static void
awe_reset_samples(void)
{
	int i;

	/* free all bank tables */
	for (i = 0; i < AWE_MAX_PRESETS; i++) {
		preset_table[i] = NULL;
	}

	free_mem_ptr = 0;
	last_sample = free_sample = 0;
	last_info = free_info = 0;
	current_sf_id = 0;
	loaded_once = 0;
}


/*================================================================
 * EMU register access
 *================================================================*/

/* select a given AWE32 pointer */
static int awe_cur_cmd = -1;
#define awe_set_cmd(cmd) \
if (awe_cur_cmd != cmd) { OUTW(cmd, awe_base + 0x802); awe_cur_cmd = cmd; }
#define awe_port(port)		(awe_base - 0x620 + port)

/* write 16bit data */
INLINE static void
awe_poke(unsigned short cmd, unsigned short port, unsigned short data)
{
	awe_set_cmd(cmd);
	OUTW(data, awe_port(port));
}

/* write 32bit data */
INLINE static void
awe_poke_dw(unsigned short cmd, unsigned short port, unsigned long data)
{
	awe_set_cmd(cmd);
	OUTW(data, awe_port(port));		/* write lower 16 bits */
	OUTW(data >> 16, awe_port(port)+2);	/* write higher 16 bits */
}

/* read 16bit data */
INLINE static unsigned short
awe_peek(unsigned short cmd, unsigned short port)
{
	unsigned short k;
	awe_set_cmd(cmd);
	k = inw(awe_port(port));
	return k;
}

/* read 32bit data */
INLINE static unsigned long
awe_peek_dw(unsigned short cmd, unsigned short port)
{
	unsigned long k1, k2;
	awe_set_cmd(cmd);
	k1 = inw(awe_port(port));
	k2 = inw(awe_port(port)+2);
	k1 |= k2 << 16;
	return k1;
}

/* wait delay number of AWE32 44100Hz clocks */
static void
awe_wait(unsigned short delay)
{
	unsigned short clock, target;
	unsigned short port = awe_port(AWE_WC_Port);
	int counter;
  
	/* sample counter */
	awe_set_cmd(AWE_WC_Cmd);
	clock = (unsigned short)inw(port);
	target = clock + delay;
	counter = 0;
	if (target < clock) {
		for (; (unsigned short)inw(port) > target; counter++)
			if (counter > 65536)
				break;
	}
	for (; (unsigned short)inw(port) < target; counter++)
		if (counter > 65536)
			break;
}


#ifndef AWE_OBSOLETE_VOXWARE

/*================================================================
 * port check / request
 *  0x620-622, 0xA20-A22, 0xE20-E22
 *================================================================*/

static int
awe_check_port(void)
{
	return (check_region(awe_port(Data0), 4) ||
		check_region(awe_port(Data1), 4) ||
		check_region(awe_port(Data3), 4));
}

static void
awe_request_region(void)
{
	request_region(awe_port(Data0), 4, "sound driver (AWE32)");
	request_region(awe_port(Data1), 4, "sound driver (AWE32)");
	request_region(awe_port(Data3), 4, "sound driver (AWE32)");
}

static void
awe_release_region(void)
{
	release_region(awe_port(Data0), 4);
	release_region(awe_port(Data1), 4);
	release_region(awe_port(Data3), 4);
}

#endif /* !AWE_OBSOLETE_VOXWARE */


/*================================================================
 * AWE32 initialization
 *================================================================*/
static void
awe_initialize(void)
{
	DEBUG(0,printk("AWE32: initializing..\n"));

	/* initialize hardware configuration */
	awe_poke(AWE_HWCF1, 0x0059);
	awe_poke(AWE_HWCF2, 0x0020);

	/* disable audio; this seems to reduce a clicking noise a bit.. */
	awe_poke(AWE_HWCF3, 0);

	/* initialize audio channels */
	awe_init_audio();

	/* initialize DMA */
	awe_init_dma();

	/* initialize init array */
	awe_init_array();

	/* check DRAM memory size */
	awe_mem_size = awe_check_dram();

	/* initialize the FM section of the AWE32 */
	awe_init_fm();

	/* set up voice envelopes */
	awe_tweak();

	/* enable audio */
	awe_poke(AWE_HWCF3, 0x0004);
}


/*================================================================
 * AWE32 voice parameters
 *================================================================*/

/* initialize voice_info record */
static void
awe_init_voice_info(awe_voice_info *vp)
{
	vp->sf_id = 0;
	vp->sample = 0;
	vp->rate_offset = 0;

	vp->start = 0;
	vp->end = 0;
	vp->loopstart = 0;
	vp->loopend = 0;
	vp->mode = 0;
	vp->root = 60;
	vp->tune = 0;
	vp->low = 0;
	vp->high = 127;
	vp->vellow = 0;
	vp->velhigh = 127;

	vp->fixkey = -1;
	vp->fixvel = -1;
	vp->fixpan = -1;
	vp->pan = -1;

	vp->exclusiveClass = 0;
	vp->amplitude = 127;
	vp->attenuation = 0;
	vp->scaleTuning = 100;

	awe_init_voice_parm(&vp->parm);
}

/* initialize voice_parm record:
 * Env1/2: delay=0, attack=0, hold=0, sustain=0, decay=0, release=0.
 * Vibrato and Tremolo effects are zero.
 * Cutoff is maximum.
 * Chorus and Reverb effects are zero.
 */
static void
awe_init_voice_parm(awe_voice_parm *pp)
{
	pp->moddelay = 0x8000;
	pp->modatkhld = 0x7f7f;
	pp->moddcysus = 0x7f7f;
	pp->modrelease = 0x807f;
	pp->modkeyhold = 0;
	pp->modkeydecay = 0;

	pp->voldelay = 0x8000;
	pp->volatkhld = 0x7f7f;
	pp->voldcysus = 0x7f7f;
	pp->volrelease = 0x807f;
	pp->volkeyhold = 0;
	pp->volkeydecay = 0;

	pp->lfo1delay = 0x8000;
	pp->lfo2delay = 0x8000;
	pp->pefe = 0;

	pp->fmmod = 0;
	pp->tremfrq = 0;
	pp->fm2frq2 = 0;

	pp->cutoff = 0xff;
	pp->filterQ = 0;

	pp->chorus = 0;
	pp->reverb = 0;
}	


#ifdef AWE_HAS_GUS_COMPATIBILITY

/* convert frequency mHz to abstract cents (= midi key * 100) */
static int
freq_to_note(int mHz)
{
	/* abscents = log(mHz/8176) / log(2) * 1200 */
	unsigned long max_val = (unsigned long)0xffffffff / 10000;
	int i, times;
	unsigned long base;
	unsigned long freq;
	int note, tune;

	if (mHz == 0)
		return 0;
	if (mHz < 0)
		return 12799; /* maximum */

	freq = mHz;
	note = 0;
	for (base = 8176 * 2; freq >= base; base *= 2) {
		note += 12;
		if (note >= 128) /* over maximum */
			return 12799;
	}
	base /= 2;

	/* to avoid overflow... */
	times = 10000;
	while (freq > max_val) {
		max_val *= 10;
		times /= 10;
		base /= 10;
	}

	freq = freq * times / base;
	for (i = 0; i < 12; i++) {
		if (freq < semitone_tuning[i+1])
			break;
		note++;
	}

	tune = 0;
	freq = freq * 10000 / semitone_tuning[i];
	for (i = 0; i < 100; i++) {
		if (freq < cent_tuning[i+1])
			break;
		tune++;
	}

	return note * 100 + tune;
}


/* convert Hz to AWE32 rate offset:
 * sample pitch offset for the specified sample rate
 * rate=44100 is no offset, each 4096 is 1 octave (twice).
 * eg, when rate is 22050, this offset becomes -4096.
 */
static int
calc_rate_offset(int Hz)
{
	/* offset = log(Hz / 44100) / log(2) * 4096 */
	int freq, base, i;

	/* maybe smaller than max (44100Hz) */
	if (Hz <= 0 || Hz >= 44100) return 0;

	base = 0;
	for (freq = Hz * 2; freq < 44100; freq *= 2)
		base++;
	base *= 1200;

	freq = 44100 * 10000 / (freq/2);
	for (i = 0; i < 12; i++) {
		if (freq < semitone_tuning[i+1])
			break;
		base += 100;
	}
	freq = freq * 10000 / semitone_tuning[i];
	for (i = 0; i < 100; i++) {
		if (freq < cent_tuning[i+1])
			break;
		base++;
	}
	return -base * 4096 / 1200;
}


/*----------------------------------------------------------------
 * convert envelope time parameter to AWE32 raw parameter
 *----------------------------------------------------------------*/

/* attack & decay/release time table (msec) */
static short attack_time_tbl[128] = {
32767, 11878, 5939, 3959, 2969, 2375, 1979, 1696, 1484, 1319, 1187, 1079, 989, 913, 848, 791, 742,
 698, 659, 625, 593, 565, 539, 516, 494, 475, 456, 439, 424, 409, 395, 383, 371,
 359, 344, 330, 316, 302, 290, 277, 266, 255, 244, 233, 224, 214, 205, 196, 188,
 180, 173, 165, 158, 152, 145, 139, 133, 127, 122, 117, 112, 107, 103, 98, 94,
 90, 86, 83, 79, 76, 73, 69, 67, 64, 61, 58, 56, 54, 51, 49, 47,
 45, 43, 41, 39, 38, 36, 35, 33, 32, 30, 29, 28, 27, 25, 24, 23,
 22, 21, 20, 20, 19, 18, 17, 16, 16, 15, 14, 14, 13, 13, 12, 11,
 11, 10, 10, 10, 9, 9, 8, 8, 8, 7, 7, 7, 6, 6, 0,
};

static short decay_time_tbl[128] = {
32767, 32766, 4589, 4400, 4219, 4045, 3879, 3719, 3566, 3419, 3279, 3144, 3014, 2890, 2771, 2657,
 2548, 2443, 2343, 2246, 2154, 2065, 1980, 1899, 1820, 1746, 1674, 1605, 1539, 1475, 1415, 1356,
 1301, 1247, 1196, 1146, 1099, 1054, 1011, 969, 929, 891, 854, 819, 785, 753, 722, 692,
 664, 636, 610, 585, 561, 538, 516, 494, 474, 455, 436, 418, 401, 384, 368, 353,
 339, 325, 311, 298, 286, 274, 263, 252, 242, 232, 222, 213, 204, 196, 188, 180,
 173, 166, 159, 152, 146, 140, 134, 129, 123, 118, 113, 109, 104, 100, 96, 92,
 88, 84, 81, 77, 74, 71, 68, 65, 63, 60, 58, 55, 53, 51, 49, 47,
 45, 43, 41, 39, 38, 36, 35, 33, 32, 30, 29, 28, 27, 26, 25, 24,
};

/*
static int
calc_parm_delay(int msec)
{
	return (0x8000 - msec * 1000 / 725);
}
*/

/* delay time = 0x8000 - msec/92 */
static int
calc_parm_hold(int msec)
{
	int val = 0x7f - (unsigned char)(msec / 92);
	if (val < 1) val = 1;
	if (val > 127) val = 127;
	return val;
}

/* attack time: search from time table */
static int
calc_parm_attack(int msec)
{
	return calc_parm_search(msec, attack_time_tbl);
}

/* decay/release time: search from time table */
static int
calc_parm_decay(int msec)
{
	return calc_parm_search(msec, decay_time_tbl);
}

/* search an index for specified time from given time table */
static int
calc_parm_search(int msec, short *table)
{
	int left = 1, right = 127, mid;
	while (left < right) {
		mid = (left + right) / 2;
		if (msec < (int)table[mid])
			left = mid + 1;
		else
			right = mid;
	}
	return left;
}
#endif /* AWE_HAS_GUS_COMPATIBILITY */


/*================================================================
 * effects table
 *================================================================*/

/* set an effect value */
#define FX_SET(rec,type,value) \
	((rec)->flags[(type)/8] |= (1 << ((type) % 8)), \
	 (rec)->val[type] = (value))

/* check the effect value is set */
#define FX_ON(rec,type)	((rec)->flags[(type)/8] & (1<<((type)%8)))

/* get byte effect value */
#define FX_BYTE(rec,type,value) \
	(unsigned char)(FX_ON(rec,type) ? (rec)->val[type] : (value))
/* get word effect value */
#define FX_WORD(rec,type,value) \
	(unsigned short)(FX_ON(rec,type) ? (rec)->val[type] : (value))

/* get word (upper=type1/lower=type2) effect value */
static unsigned short
FX_COMB(FX_Rec *rec, int type1, int type2, unsigned short value)
{
	unsigned short tmp;
	if (FX_ON(rec, type1))
		tmp = (unsigned short)(rec->val[type1]) << 8;
	else
		tmp = value & 0xff00;
	if (FX_ON(rec, type2))
		tmp |= (unsigned short)(rec->val[type2]) & 0xff;
	else
		tmp |= value & 0xff;
	return tmp;
}

/* address offset */
static long
FX_OFFSET(FX_Rec *rec, int lo, int hi, int mode)
{
	long addr = 0;
	if (FX_ON(rec, hi)) {
		addr = (short)rec->val[hi];
		addr = addr << 15;
	}
	if (FX_ON(rec, lo))
		addr += (short)rec->val[lo];
	if (!(mode & AWE_SAMPLE_8BITS))
		addr /= 2;
	return addr;
}


/*================================================================
 * turn on/off sample
 *================================================================*/

static void
awe_note_on(int voice)
{
	unsigned long temp;
	long addr;
	awe_voice_info *vp;
	FX_Rec *fx = &voices[voice].cinfo->fx;

	/* A voice sample must assigned before calling */
	if ((vp = voices[voice].sample) == NULL || vp->index < 0)
		return;

	/* channel to be silent and idle */
	awe_poke(AWE_DCYSUSV(voice), 0x0080);
	awe_poke(AWE_VTFT(voice), 0);
	awe_poke(AWE_CVCF(voice), 0);
	awe_poke(AWE_PTRX(voice), 0);
	awe_poke(AWE_CPF(voice), 0);

	/* modulation & volume envelope */
	awe_poke(AWE_ENVVAL(voice),
		 FX_WORD(fx, AWE_FX_ENV1_DELAY, vp->parm.moddelay));
	awe_poke(AWE_ATKHLD(voice),
		 FX_COMB(fx, AWE_FX_ENV1_HOLD, AWE_FX_ENV1_ATTACK,
			 vp->parm.modatkhld));
	awe_poke(AWE_DCYSUS(voice),
		 FX_COMB(fx, AWE_FX_ENV1_SUSTAIN, AWE_FX_ENV1_DECAY,
			  vp->parm.moddcysus));
	awe_poke(AWE_ENVVOL(voice),
		 FX_WORD(fx, AWE_FX_ENV2_DELAY, vp->parm.voldelay));
	awe_poke(AWE_ATKHLDV(voice),
		 FX_COMB(fx, AWE_FX_ENV2_HOLD, AWE_FX_ENV2_ATTACK,
			 vp->parm.volatkhld));
	/* decay/sustain parameter for volume envelope must be set at last */

	/* pitch offset */
	awe_set_pitch(voice, TRUE);

	/* cutoff and volume */
	awe_set_volume(voice, TRUE);

	/* modulation envelope heights */
	awe_poke(AWE_PEFE(voice),
		 FX_COMB(fx, AWE_FX_ENV1_PITCH, AWE_FX_ENV1_CUTOFF,
			 vp->parm.pefe));

	/* lfo1/2 delay */
	awe_poke(AWE_LFO1VAL(voice),
		 FX_WORD(fx, AWE_FX_LFO1_DELAY, vp->parm.lfo1delay));
	awe_poke(AWE_LFO2VAL(voice),
		 FX_WORD(fx, AWE_FX_LFO2_DELAY, vp->parm.lfo2delay));

	/* lfo1 pitch & cutoff shift */
	awe_fx_fmmod(voice, TRUE);
	/* lfo1 volume & freq */
	awe_fx_tremfrq(voice, TRUE);
	/* lfo2 pitch & freq */
	awe_fx_fm2frq2(voice, TRUE);
	/* pan & loop start */
	 awe_set_pan(voice, 1);

	/* chorus & loop end (chorus 8bit, MSB) */
	addr = vp->loopend - 1;
	addr += FX_OFFSET(fx, AWE_FX_LOOP_END,
			  AWE_FX_COARSE_LOOP_END, vp->mode);
	temp = FX_BYTE(fx, AWE_FX_CHORUS, vp->parm.chorus);
	temp = (temp <<24) | (unsigned long)addr;
	awe_poke_dw(AWE_CSL(voice), temp);
	DEBUG(4,printk("AWE32: [-- loopend=%x/%x]\n",
		       (int)vp->loopend, (int)addr));

	/* Q & current address (Q 4bit value, MSB) */
	addr = vp->start - 1;
	addr += FX_OFFSET(fx, AWE_FX_SAMPLE_START,
			  AWE_FX_COARSE_SAMPLE_START, vp->mode);
	temp = FX_BYTE(fx, AWE_FX_FILTERQ, vp->parm.filterQ);
	temp = (temp<<28) | (unsigned long)addr;
	awe_poke_dw(AWE_CCCA(voice), temp);
	DEBUG(4,printk("AWE32: [-- startaddr=%x/%x]\n",
		       (int)vp->start, (int)addr));

	/* reset volume */
	awe_poke_dw(AWE_VTFT(voice), 0x0000FFFF);
	awe_poke_dw(AWE_CVCF(voice), 0x0000FFFF);

	/* turn on envelope */
	awe_poke(AWE_DCYSUSV(voice),
		 FX_COMB(fx, AWE_FX_ENV2_SUSTAIN, AWE_FX_ENV2_DECAY,
			  vp->parm.voldcysus));
	/* set chorus */
	temp = FX_BYTE(fx, AWE_FX_REVERB, vp->parm.reverb);
	temp = (awe_peek_dw(AWE_PTRX(voice)) & 0xffff0000) | (temp<<8);
	awe_poke_dw(AWE_PTRX(voice), temp);
	awe_poke_dw(AWE_CPF(voice), 0x40000000);
}


/* turn off the voice */
static void
awe_note_off(int voice)
{
	awe_voice_info *vp;
	unsigned short tmp;
	FX_Rec *fx = &voices[voice].cinfo->fx;
	if ((vp = voices[voice].sample) == NULL) {
		awe_voice_init(voice);
		return;
	}

	if (FX_ON(fx, AWE_FX_ENV1_RELEASE))
		tmp = 0x8000 | fx->val[AWE_FX_ENV1_RELEASE];
	else
		tmp = vp->parm.modrelease;
	awe_poke(AWE_DCYSUS(voice), tmp);
	if (FX_ON(fx, AWE_FX_ENV2_RELEASE))
		tmp = 0x8000 | fx->val[AWE_FX_ENV2_RELEASE];
	else
		tmp = vp->parm.volrelease;
	awe_poke(AWE_DCYSUSV(voice), tmp);
	voices[voice].state = AWE_ST_OFF;

	awe_voice_init(voice);
}

/* force to terminate the voice (no releasing echo) */
static void
awe_terminate(int voice)
{
	awe_poke(AWE_DCYSUSV(voice), 0x807F);
	awe_tweak_voice(voice);
	awe_voice_init(voice);
}


/* turn off other voices with the same exclusive class (for drums) */
static void
awe_exclusive_off(int voice, int exclass)
{
	int i;

	if (exclass == 0) /* not exclusive */
		return;

	/* turn off voices with the same class */
	for (i = 0; i < awe_max_voices; i++) {
		if (i != voice && IS_PLAYING(voice) &&
		    voices[i].sample &&
		    voices[i].sample->exclusiveClass == exclass) {
			DEBUG(4,printk("AWE32: [exoff(%d)]\n", i));
			awe_note_off(i);
		}
	}
}


/*================================================================
 * change the parameters of an audible voice
 *================================================================*/

/* change pitch */
static void
awe_set_pitch(int voice, int forced)
{
	if (IS_NO_EFFECT(voice) && !forced) return;
	awe_poke(AWE_IP(voice), voices[voice].apitch);
	DEBUG(3,printk("AWE32: [-- pitch=%x]\n", voices[voice].apitch));
}

/* calculate & change pitch */
static void
awe_set_voice_pitch(int voice, int forced)
{
	awe_calc_pitch(voice);
	awe_set_pitch(voice, forced);
}

/* change volume */
static void
awe_set_volume(int voice, int forced)
{
	awe_voice_info *vp;
	unsigned short tmp2;
	FX_Rec *fx = &voices[voice].cinfo->fx;

	if (IS_NO_EFFECT(voice) && !forced) return;
	if ((vp = voices[voice].sample) == NULL || vp->index < 0)
		return;

	tmp2 = FX_BYTE(fx, AWE_FX_CUTOFF, vp->parm.cutoff);
	tmp2 = (tmp2 << 8) | voices[voice].avol;
	awe_poke(AWE_IFATN(voice), tmp2);
}

/* calculate & change volume */
static void
awe_set_voice_vol(int voice, int forced)
{
	awe_calc_volume(voice);
	awe_set_volume(voice, forced);
}


/* change pan; this could make a click noise.. */
static void
awe_set_pan(int voice, int forced)
{
	unsigned long temp;
	long addr;
	awe_voice_info *vp;
	FX_Rec *fx = &voices[voice].cinfo->fx;

	if (IS_NO_EFFECT(voice) && !forced) return;
	if ((vp = voices[voice].sample) == NULL || vp->index < 0)
		return;

	/* pan & loop start (pan 8bit, MSB, 0:right, 0xff:left) */
	if (vp->fixpan > 0)	/* 0-127 */
		temp = 255 - (int)vp->fixpan * 2;
	else {
		int pos = 0;
		if (vp->pan >= 0) /* 0-127 */
			pos = (int)vp->pan * 2 - 128;
		pos += voices[voice].cinfo->panning; /* -128 - 127 */
		pos = 127 - pos;
		if (pos < 0)
			temp = 0;
		else if (pos > 255)
			temp = 255;
		else
			temp = pos;
	}
	if (forced || temp != voices[voice].apan) {
		addr = vp->loopstart - 1;
		addr += FX_OFFSET(fx, AWE_FX_LOOP_START,
				  AWE_FX_COARSE_LOOP_START, vp->mode);
		temp = (temp<<24) | (unsigned long)addr;
		awe_poke_dw(AWE_PSST(voice), temp);
		voices[voice].apan = temp;
		DEBUG(4,printk("AWE32: [-- loopstart=%x/%x]\n",
			       (int)vp->loopstart, (int)addr));
	}
}

/* effects change during playing */
static void
awe_fx_fmmod(int voice, int forced)
{
	awe_voice_info *vp;
	FX_Rec *fx = &voices[voice].cinfo->fx;
	if (IS_NO_EFFECT(voice) && !forced) return;
	if ((vp = voices[voice].sample) == NULL || vp->index < 0)
		return;
	awe_poke(AWE_FMMOD(voice),
		 FX_COMB(fx, AWE_FX_LFO1_PITCH, AWE_FX_LFO1_CUTOFF,
			 vp->parm.fmmod));
}

/* set tremolo (lfo1) volume & frequency */
static void
awe_fx_tremfrq(int voice, int forced)
{
	awe_voice_info *vp;
	FX_Rec *fx = &voices[voice].cinfo->fx;
	if (IS_NO_EFFECT(voice) && !forced) return;
	if ((vp = voices[voice].sample) == NULL || vp->index < 0)
		return;
	awe_poke(AWE_TREMFRQ(voice),
		 FX_COMB(fx, AWE_FX_LFO1_VOLUME, AWE_FX_LFO1_FREQ,
			 vp->parm.tremfrq));
}

/* set lfo2 pitch & frequency */
static void
awe_fx_fm2frq2(int voice, int forced)
{
	awe_voice_info *vp;
	FX_Rec *fx = &voices[voice].cinfo->fx;
	if (IS_NO_EFFECT(voice) && !forced) return;
	if ((vp = voices[voice].sample) == NULL || vp->index < 0)
		return;
	awe_poke(AWE_FM2FRQ2(voice),
		 FX_COMB(fx, AWE_FX_LFO2_PITCH, AWE_FX_LFO2_FREQ,
			 vp->parm.fm2frq2));
}

/* set total cutoff  & current pitch */
static void
awe_fx_cutoff(int voice, int forced)
{
	unsigned short tmp2;
	awe_voice_info *vp;
	FX_Rec *fx = &voices[voice].cinfo->fx;
	if (IS_NO_EFFECT(voice)) return;
	if ((vp = voices[voice].sample) == NULL || vp->index < 0)
		return;
	tmp2 = FX_BYTE(fx, AWE_FX_CUTOFF, vp->parm.cutoff);
	tmp2 = (tmp2 << 8) | voices[voice].avol;
	awe_poke(AWE_IFATN(voice), tmp2);
}


/*================================================================
 * calculate pitch offset
 *----------------------------------------------------------------
 * 0xE000 is no pitch offset at 44100Hz sample.
 * Every 4096 is one octave.
 *================================================================*/

static void
awe_calc_pitch(int voice)
{
	voice_info *vp = &voices[voice];
	awe_voice_info *ap;
	awe_chan_info *cp = voices[voice].cinfo;
	int offset;

	/* search voice information */
	if ((ap = vp->sample) == NULL)
			return;
	if (ap->index < 0) {
		if (awe_set_sample(ap) < 0)
			return;
	}

	/* calculate offset */
	if (ap->fixkey >= 0) {
		DEBUG(3,printk("AWE32: p-> fixkey(%d) tune(%d)\n", ap->fixkey, ap->tune));
		offset = (ap->fixkey - ap->root) * 4096 / 12;
	} else {
		DEBUG(3,printk("AWE32: p(%d)-> root(%d) tune(%d)\n", vp->note, ap->root, ap->tune));
		offset = (vp->note - ap->root) * 4096 / 12;
		DEBUG(4,printk("AWE32: p-> ofs=%d\n", offset));
	}
	offset += ap->tune * 4096 / 1200;
	DEBUG(4,printk("AWE32: p-> tune+ ofs=%d\n", offset));
	if (cp->bender != 0) {
		DEBUG(3,printk("AWE32: p-> bend(%d) %d\n", voice, cp->bender));
		/* (819200: 1 semitone) ==> (4096: 12 semitones) */
		offset += cp->bender * cp->bender_range / 2400;
	}
	offset = (offset * ap->scaleTuning) / 100;
	DEBUG(4,printk("AWE32: p-> scale* ofs=%d\n", offset));

	/* add initial pitch correction */
	if (FX_ON(&cp->fx, AWE_FX_INIT_PITCH)) {
		DEBUG(3,printk("AWE32: fx_pitch(%d) %d\n", voice, cp->fx.val[AWE_FX_INIT_PITCH]));
		offset += cp->fx.val[AWE_FX_INIT_PITCH];
	}

	/* 0xe000: root pitch */
	vp->apitch = 0xe000 + ap->rate_offset + offset;
	DEBUG(4,printk("AWE32: p-> sum aofs=%x, rate_ofs=%d\n", vp->apitch, ap->rate_offset));
	if (vp->apitch > 0xffff)
		vp->apitch = 0xffff;
	if (vp->apitch < 0)
		vp->apitch = 0;
}


#ifdef AWE_HAS_GUS_COMPATIBILITY
/* calculate MIDI key and semitone from the specified frequency */
static void
awe_calc_pitch_from_freq(int voice, int freq)
{
	voice_info *vp = &voices[voice];
	awe_voice_info *ap;
	FX_Rec *fx = &voices[voice].cinfo->fx;
	int offset;
	int note;

	/* search voice information */
	if ((ap = vp->sample) == NULL)
		return;
	if (ap->index < 0) {
		if (awe_set_sample(ap) < 0)
			return;
	}
	note = freq_to_note(freq);
	offset = (note - ap->root * 100 + ap->tune) * 4096 / 1200;
	offset = (offset * ap->scaleTuning) / 100;
	if (FX_ON(fx, AWE_FX_INIT_PITCH))
		offset += fx->val[AWE_FX_INIT_PITCH];
	vp->apitch = 0xe000 + ap->rate_offset + offset;
	if (vp->apitch > 0xffff)
		vp->apitch = 0xffff;
	if (vp->apitch < 0)
		vp->apitch = 0;
}
#endif /* AWE_HAS_GUS_COMPATIBILITY */


/*================================================================
 * calculate volume attenuation
 *----------------------------------------------------------------
 * Voice volume is controlled by volume attenuation parameter.
 * So volume becomes maximum when avol is 0 (no attenuation), and
 * minimum when 255 (-96dB or silence).
 *================================================================*/

static int vol_table[128] = {
	255,111,95,86,79,74,70,66,63,61,58,56,54,52,50,49,
	47,46,45,43,42,41,40,39,38,37,36,35,34,34,33,32,
	31,31,30,29,29,28,27,27,26,26,25,24,24,23,23,22,
	22,21,21,21,20,20,19,19,18,18,18,17,17,16,16,16,
	15,15,15,14,14,14,13,13,13,12,12,12,11,11,11,10,
	10,10,10,9,9,9,8,8,8,8,7,7,7,7,6,6,
	6,6,5,5,5,5,5,4,4,4,4,3,3,3,3,3,
	2,2,2,2,2,1,1,1,1,1,0,0,0,0,0,0,
};

static void
awe_calc_volume(int voice)
{
	voice_info *vp = &voices[voice];
	awe_voice_info *ap;
	awe_chan_info *cp = voices[voice].cinfo;
	int vol;

	/* search voice information */
	if ((ap = vp->sample) == NULL)
		return;

	ap = vp->sample;
	if (ap->index < 0) {
		if (awe_set_sample(ap) < 0)
			return;
	}
	
	/* 0 - 127 */
	vol = (vp->velocity * cp->main_vol * cp->expression_vol) / (127*127);
	vol = vol * ap->amplitude / 127;
	if (vol < 0) vol = 0;
	if (vol > 127) vol = 127;

	/* calc to attenuation */
	vol = vol_table[vol];
	vol = vol + (int)ap->attenuation + init_atten;
	if (vol > 255) vol = 255;

	vp->avol = vol;
	DEBUG(3,printk("AWE32: [-- voice(%d) vol=%x]\n", voice, vol));
}


/*================================================================
 * synth operation routines
 *================================================================*/

#define AWE_VOICE_KEY(v)	(0x8000 | (v))
#define AWE_CHAN_KEY(c,n)	(((c) << 8) | ((n) + 1))

/* initialize the voice */
static void
awe_voice_init(int voice)
{
	voices[voice].note = -1;
	voices[voice].velocity = 0;
	voices[voice].sample = NULL;
	voices[voice].state = AWE_ST_OFF;
	voices[voice].cinfo = &channels[voice];
	voices[voice].ch = -1;
	voices[voice].key = AWE_VOICE_KEY(voice);
	voices[voice].time = current_alloc_time;

	/* clear voice mapping */
	voice_alloc->map[voice] = 0;

	/* emu8000 parameters */
	voices[voice].apitch = 0;
	voices[voice].avol = 255;
	voices[voice].apan = -1;

	/* clear effects */
	if (! awe_channel_mode)
		BZERO(&voices[voice].cinfo->fx, sizeof(FX_Rec));
}

/* initialize channel info */
static void awe_channel_init(int ch)
{
	channels[ch].panning = 0; /* zero center */
	channels[ch].bender = 0; /* zero tune skew */
	channels[ch].bender_range = 200; /* sense * 100 */
	channels[ch].main_vol = 127;
	channels[ch].expression_vol = 127;
	if (awe_channel_mode && IS_DRUM_CHANNEL(ch))
		channels[ch].bank = AWE_DRUM_BANK;
	else
		channels[ch].bank = AWE_DEFAULT_BANK;
	channels[ch].instr = 0;
	channels[ch].vrec = NULL;
	channels[ch].def_vrec = NULL;
	channels[ch].sustained = 0;
	BZERO(&channels[ch].fx, sizeof(FX_Rec));
}


/*----------------------------------------------------------------
 * device open / close
 *----------------------------------------------------------------*/

/* open device:
 *   reset status of all voices, and clear sample position flag
 */
static int
awe_open(int dev, int mode)
{
	if (awe_busy)
		return RET_ERROR(EBUSY);

	awe_busy = TRUE;
	awe_reset(dev);

	/* clear sample position flag */
	loaded_once = 0;

	/* set default mode */
	init_atten = AWE_DEFAULT_ATTENUATION; /* 12dB below */
	awe_gus_bank = AWE_DEFAULT_BANK;
	drum_flags = DEFAULT_DRUM_FLAGS;
	awe_exclusive_sound = TRUE;
	awe_channel_mode = 0;

	return 0;
}


/* close device:
 *   reset all voices again (terminate sounds)
 */
static void
awe_close(int dev)
{
	awe_reset(dev);
	awe_busy = FALSE;
}


/* sequencer I/O control:
 */
static int
awe_ioctl(int dev, unsigned int cmd, caddr_t arg)
{
	switch (cmd) {
	case SNDCTL_SYNTH_INFO:
		awe_info.nr_voices = awe_max_voices;
		IOCTL_TO_USER((char*)arg, 0, &awe_info, sizeof(awe_info));
		return 0;
		break;

	case SNDCTL_SEQ_RESETSAMPLES:
		awe_reset_samples();
		awe_reset(dev);
		return 0;
		break;

	case SNDCTL_SEQ_PERCMODE:
		/* what's this? */
		return 0;
		break;

	case SNDCTL_SYNTH_MEMAVL:
		DEBUG(0,printk("AWE32: [ioctl memavl = %d]\n", (int)free_mem_ptr));
		return awe_mem_size - free_mem_ptr*2;

	default:
		ERRMSG(printk("AWE32: unsupported ioctl %d\n", cmd));
		return RET_ERROR(EINVAL);
	}
}


/* kill a voice:
 *   not terminate, just release the voice.
 */
static int
awe_kill_note(int dev, int voice, int note, int velocity)
{
	int i, key, besttime;

	DEBUG(2,printk("AWE32: [off(%d) nt=%d vl=%d]\n", voice, note, velocity));
	if (awe_channel_mode) {
		if (awe_channel_mode == 2) { /* get channel */
			int v2 = voice_alloc->map[voice] >> 8;
			voice_alloc->map[voice] = 0;
			voice = v2;
		}
		if (voice < 0 || voice >= AWE_MAX_CHANNELS)
			return RET_ERROR(EINVAL);
		if (channels[voice].instr > 128)
			note = channels[voice].instr - 128;
		key = AWE_CHAN_KEY(voice, note);
	} else {
		if (voice < 0 || voice >= awe_max_voices)
			return RET_ERROR(EINVAL);
		key = AWE_VOICE_KEY(voice);
	}

	besttime = current_alloc_time + 1;
	for (i = 0; i < awe_max_voices; i++) {
		if (voices[i].key == key) {
			if (voices[i].time < besttime)
				besttime = voices[i].time;
		}
	}
	for (i = 0; i < awe_max_voices; i++) {
		if (voices[i].key == key &&
		    (!awe_exclusive_sound || voices[i].time == besttime)) {
			if (voices[i].cinfo->sustained)
				voices[i].state = AWE_ST_SUSTAINED;
			else
				awe_note_off(i);
		}
	}
	return 0;
}


/* start a voice:
 *   if note is 255, identical with aftertouch function.
 *   Otherwise, start a voice with specified not and volume.
 */
static int
awe_start_note(int dev, int voice, int note, int velocity)
{
	int i, key, state, volonly;

	DEBUG(2,printk("AWE32: [on(%d) nt=%d vl=%d]\n", voice, note, velocity));
	volonly = 0;
	if (awe_channel_mode) {
		if (awe_channel_mode == 2) /* get channel */
			voice = voice_alloc->map[voice] >> 8;
		else if (voice & 0x80) { /* channel volume mode */
			voice &= ~0x80;
			volonly = 2;
		}
		if (voice < 0 || voice >= AWE_MAX_CHANNELS)
			return RET_ERROR(EINVAL);
		if (channels[voice].instr > 128)
			note = channels[voice].instr - 128;
		if (note >= 128) { /* key volume mode */
			note -= 128;
			volonly = 1;
		}
		key = AWE_CHAN_KEY(voice, note);
	} else {
		if (voice < 0 || voice >= awe_max_voices)
		      return RET_ERROR(EINVAL);
		if (voices[voice].cinfo->instr > 128)
			note = voices[voice].cinfo->instr - 128;
		key = AWE_VOICE_KEY(voice);
		if (note == 255)
			volonly = 1;
	}

	/* dynamic volume change */
	if (volonly) {
		for (i = 0; i < awe_max_voices; i++) {
			if ((volonly == 2 && voices[i].ch == voice) ||
			    voices[i].key == key) {
				voices[i].velocity = velocity;
				if (velocity == 0) /* for GUS compatibility */
					return awe_kill_note(dev, voice, note, velocity);
				awe_calc_volume(i);
				state = voices[i].state;
				voices[i].state = AWE_ST_ON;
				if (state == AWE_ST_STANDBY)
					awe_note_on(i);
				else
					awe_set_volume(i, FALSE);
			}
		}
		return 0;
	}

	/* stop the sound if still playing */
	if (awe_exclusive_sound) {
		for (i = 0; i < awe_max_voices; i++)
			if (voices[i].key == key &&
			    voices[i].state != AWE_ST_OFF)
				awe_note_off(i);
	}

	/* allocate voices */
	if (awe_channel_mode)
		awe_alloc_multi_voices(voice, note, velocity);
	else
		awe_alloc_one_voice(voice, note, velocity);

	/* turn off other voices (for drums) */
	for (i = 0; i < awe_max_voices; i++)
		if (voices[i].key == key && voices[i].sample)
			awe_exclusive_off(i, voices[i].sample->exclusiveClass);

	if (velocity == 0)
		state = AWE_ST_STANDBY; /* stand by for playing */
	else
		state = AWE_ST_ON;	/* really play */

	/* set up pitch and volume parameters */
	for (i = 0; i < awe_max_voices; i++)
		if (voices[i].key == key) {
			/* calculate pitch & volume parameters */
			voices[i].state = state;
			voices[i].note = note;
			voices[i].velocity = velocity;
			awe_calc_pitch(i);
			awe_calc_volume(i);
			if (state == AWE_ST_ON)
				awe_note_on(i);
		}

	return 0;
}


/* search instrument from preset table with the specified bank */
static awe_voice_list *
awe_search_instr(int bank, int preset)
{
	awe_voice_list *p;
	int maxc;

	for (maxc = AWE_MAX_INFOS, p = preset_table[preset];
	     p && maxc; p = p->next_bank, maxc--) {
		if (p->bank == bank)
			return p;
	}
	return NULL;
}


/* assign the instrument to a voice */
static int
awe_set_instr_2(int dev, int voice, int instr_no)
{
	if (awe_channel_mode == 2)
		voice = voice_alloc->map[voice] >> 8;
	return awe_set_instr(dev, voice, instr_no);
}

/* assign the instrument to a voice */
static int
awe_set_instr(int dev, int voice, int instr_no)
{
	awe_chan_info *cinfo;
	int def_bank;

	if (awe_channel_mode) {
		/* skip the percussion instr in SEQ2 mode */
		if (voice < 0 || voice >= AWE_MAX_CHANNELS)
			return RET_ERROR(EINVAL);
		cinfo = &channels[voice];
		if (IS_DRUM_CHANNEL(voice))
			def_bank = AWE_DRUM_BANK;
		else
			def_bank = cinfo->bank;
	} else {
		if (voice < 0 || voice >= awe_max_voices)
			return RET_ERROR(EINVAL);
		cinfo = voices[voice].cinfo;
		def_bank = cinfo->bank;
	}

	if (instr_no < 0 || instr_no >= AWE_MAX_PRESETS)
		return RET_ERROR(EINVAL);

	cinfo->vrec = NULL;
	cinfo->def_vrec = NULL;
	if (instr_no > 128) {
		cinfo->vrec = awe_search_instr(128, cinfo->bank);
		if (cinfo->bank != 0)
			cinfo->def_vrec = awe_search_instr(128, 0);
	} else {
		cinfo->vrec = awe_search_instr(def_bank, instr_no);
		if (def_bank == AWE_DRUM_BANK)
			cinfo->def_vrec = awe_search_instr(def_bank, 0);
		else
			cinfo->def_vrec = awe_search_instr(AWE_DEFAULT_BANK, instr_no);
	}
	if (cinfo->vrec == NULL && cinfo->def_vrec == NULL) {
		DEBUG(1,printk("AWE32 Warning: can't find instrument %d\n", instr_no));
		return 0;
	}

	cinfo->instr = instr_no;

	return 0;
}


/* reset all voices; terminate sounds and initialize parameters */
static void
awe_reset(int dev)
{
	int i;
	current_alloc_time = 0;
	/* don't turn off voice 31 and 32.  they are used also for FM voices */
	for (i = 0; i < AWE_NORMAL_VOICES; i++)
		awe_terminate(i);
	for (i = 0; i < AWE_MAX_CHANNELS; i++)
		awe_channel_init(i);
	for (i = 0; i < 16; i++) {
		awe_operations.chn_info[i].controllers[CTL_MAIN_VOLUME] = 127;
		awe_operations.chn_info[i].controllers[CTL_EXPRESSION] = 127;
	}
	awe_init_fm();
	awe_tweak();
}


/* hardware specific control:
 *   GUS specific and AWE32 specific controls are available.
 */
static void
awe_hw_control(int dev, unsigned char *event)
{
	int cmd = event[2];
	if (cmd & _AWE_MODE_FLAG)
		awe_hw_awe_control(dev, cmd & _AWE_MODE_VALUE_MASK, event);
#ifdef AWE_HAS_GUS_COMPATIBILITY
	else
		awe_hw_gus_control(dev, cmd & _AWE_MODE_VALUE_MASK, event);
#endif
}


#ifdef AWE_HAS_GUS_COMPATIBILITY

/* GUS compatible controls */
static void
awe_hw_gus_control(int dev, int cmd, unsigned char *event)
{
	int voice;
	unsigned short p1;
	short p2;
	int plong;

	voice = event[3];
	p1 = *(unsigned short *) &event[4];
	p2 = *(short *) &event[6];
	plong = *(int*) &event[4];

	switch (cmd) {
	case _GUS_NUMVOICES:
		if (p1 >= awe_max_voices)
			DEBUG(0,printk("AWE32: num_voices: voices out of range %d\n", p1));
		break;
	case _GUS_VOICESAMPLE:
		if (voice < awe_max_voices)
			awe_set_instr(dev, voice, p1);
		break;

	case _GUS_VOICEON:
		if (voice < awe_max_voices)
			awe_note_on(voice);
		break;
		
	case _GUS_VOICEOFF:
		if (voice < awe_max_voices)
			awe_note_off(voice);
		break;
		
	case _GUS_VOICEMODE:
		/* not supported */
		break;

	case _GUS_VOICEBALA:
		/* 0 to 15 --> -128 to 127 */
		if (voice < awe_max_voices)
			awe_panning(dev, voice, ((int)p1 << 4) - 128);
		break;

	case _GUS_VOICEFREQ:
		if (voice < awe_max_voices)
			awe_calc_pitch_from_freq(voice, plong);
		break;
		
	case _GUS_VOICEVOL:
	case _GUS_VOICEVOL2:
		/* not supported yet */
		break;

	case _GUS_RAMPRANGE:
	case _GUS_RAMPRATE:
	case _GUS_RAMPMODE:
	case _GUS_RAMPON:
	case _GUS_RAMPOFF:
		/* volume ramping not supported */
		break;

	case _GUS_VOLUME_SCALE:
		break;

	case _GUS_VOICE_POS:
		if (voice < awe_max_voices) {
			FX_SET(&voices[voice].cinfo->fx, AWE_FX_SAMPLE_START,
			       (short)(plong & 0x7fff));
			FX_SET(&voices[voice].cinfo->fx, AWE_FX_COARSE_SAMPLE_START,
			       (plong >> 15) & 0xffff);
		}
		break;
	}
}

#endif


/* converter function table for realtime paramter change */

static fx_affect_func fx_realtime[] = {
	/* env1: delay, attack, hold, decay, release, sustain, pitch, cutoff*/
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* env2: delay, attack, hold, decay, release, sustain */
	NULL, NULL, NULL, NULL, NULL, NULL,
	/* lfo1: delay, freq, volume, pitch, cutoff */
	NULL, awe_fx_tremfrq, awe_fx_tremfrq, awe_fx_fmmod, awe_fx_fmmod,
	/* lfo2: delay, freq, pitch */
	NULL, awe_fx_fm2frq2, awe_fx_fm2frq2,
	/* global: initpitch, chorus, reverb, cutoff, filterQ */
	awe_set_voice_pitch, NULL, NULL, awe_fx_cutoff, NULL,
	/* sample: start, loopstart, loopend */
	NULL, NULL, NULL,
};


/* AWE32 specific controls */
static void
awe_hw_awe_control(int dev, int cmd, unsigned char *event)
{
	int voice;
	unsigned short p1;
	short p2;
	int chn;
	awe_chan_info *cinfo;
	int i;

	chn = event[1];
	voice = event[3];
	p1 = *(unsigned short *) &event[4];
	p2 = *(short *) &event[6];
	
	if (awe_channel_mode) {
		if (voice < 0 || voice >= AWE_MAX_CHANNELS)
			return;
		cinfo = &channels[voice];
	} else {
		if (voice < 0 || voice >= awe_max_voices)
			return;
		cinfo = voices[voice].cinfo;
	}

	switch (cmd) {
	case _AWE_DEBUG_MODE:
		debug_mode = p1;
		printk("AWE32: debug mode = %d\n", debug_mode);
		break;
	case _AWE_REVERB_MODE:
		if (p1 <= 7) {
			reverb_mode = p1;
			DEBUG(0,printk("AWE32: reverb mode %d\n", reverb_mode));
			awe_set_reverb_mode(reverb_mode);
		}
		break;

	case _AWE_CHORUS_MODE:
		if (p1 <= 7) {
			chorus_mode = p1;
			DEBUG(0,printk("AWE32: chorus mode %d\n", chorus_mode));
			awe_set_chorus_mode(chorus_mode);
		}
		break;
		      
	case _AWE_REMOVE_LAST_SAMPLES:
		DEBUG(0,printk("AWE32: remove last samples\n"));
		awe_remove_samples();
		break;

	case _AWE_INITIALIZE_CHIP:
		awe_initialize();
		break;

	case _AWE_SEND_EFFECT:
		if (p1 < AWE_FX_END) {
			FX_SET(&cinfo->fx, p1, p2);
			DEBUG(0,printk("AWE32: effects (%d) %d %d\n", voice, p1, cinfo->fx.val[p1]));
			FX_SET(&cinfo->fx, p1, p2);
			if (fx_realtime[p1]) {
				DEBUG(0,printk("AWE32: fx_realtime (%d)\n", voice));
				awe_voice_change(voice, fx_realtime[p1]);
			}
		}
		break;

	case _AWE_TERMINATE_ALL:
		DEBUG(0,printk("AWE32: terminate all\n"));
		awe_reset(0);
		break;

	case _AWE_TERMINATE_CHANNEL:
		awe_voice_change(voice, (fx_affect_func)awe_terminate);
		break;

	case _AWE_NOTEOFF_ALL:
		for (i = 0; i < awe_max_voices; i++)
			awe_note_off(i);
		break;

	case _AWE_INITIAL_VOLUME:
		DEBUG(0,printk("AWE32: init attenuation %d\n", p1));
		init_atten = p1;
		for (i = 0; i < awe_max_voices; i++)
			awe_set_voice_vol(i, FALSE);
		break;

	case _AWE_SET_GUS_BANK:
		DEBUG(0,printk("AWE32: set gus bank %d\n", p1));
		awe_gus_bank = p1;
		break;
		
	 /* v0.3 stuffs */
	case _AWE_CHANNEL_MODE:
		DEBUG(0,printk("AWE32: channel mode = %d\n", p1));
		awe_channel_mode = p1;
		awe_reset(0);
		break;

	case _AWE_DRUM_CHANNELS:
		DEBUG(0,printk("AWE32: drum flags = %x\n", p1));
		drum_flags = p1;
		break;

	case _AWE_EXCLUSIVE_SOUND:
		DEBUG(0,printk("AWE32: exclusive mode = %d\n", p1));
		awe_exclusive_sound = p1;
		break;

	case _AWE_GET_CURRENT_MODE:
		{
			awe_mode_rec tmprec;
			tmprec.base_addr = awe_base;
			tmprec.mem_size = awe_mem_size / 2;
			tmprec.max_voices = awe_max_voices;
			tmprec.max_infos = AWE_MAX_INFOS;
			tmprec.max_samples = AWE_MAX_SAMPLES;
			tmprec.current_sf_id = current_sf_id;
			tmprec.free_mem = free_mem_ptr;
			tmprec.free_info = free_info;
			tmprec.free_sample = free_sample;
			tmprec.reverb_mode = reverb_mode;
			tmprec.chorus_mode = chorus_mode;
			tmprec.init_atten = init_atten;
			tmprec.channel_mode = awe_channel_mode;
			tmprec.gus_bank = awe_gus_bank;
			tmprec.exclusive_sound = awe_exclusive_sound;
			tmprec.drum_flags = drum_flags;
			tmprec.debug_mode = debug_mode;
			IOCTL_TO_USER(*(awe_mode_rec**)&event[4], 0, &tmprec, sizeof(tmprec));
			break;
		}
	default:
		DEBUG(0,printk("AWE32: hw control cmd=%d voice=%d\n", cmd, voice));
		break;
	}
}


/*----------------------------------------------------------------
 * load a sound patch:
 *   three types of patches are accepted: AWE, GUS, and SYSEX.
 *----------------------------------------------------------------*/

static int
awe_load_patch(int dev, int format, const char *addr,
	       int offs, int count, int pmgr_flag)
{
	awe_patch_info patch;
	int rc = 0;

#ifdef AWE_HAS_GUS_COMPATIBILITY
	if (format == GUS_PATCH) {
		return awe_load_guspatch(addr, offs, count, pmgr_flag);
	} else
#endif
	if (format == SYSEX_PATCH) {
		/* no system exclusive message supported yet */
		return 0;
	} else if (format != AWE_PATCH) {
		FATALERR(printk("AWE32 Error: Invalid patch format (key) 0x%x\n", format));
		return RET_ERROR(EINVAL);
	}
	
	if (count < AWE_PATCH_INFO_SIZE) {
		FATALERR(printk("AWE32 Error: Patch header too short\n"));
		return RET_ERROR(EINVAL);
	}
	COPY_FROM_USER(((char*)&patch) + offs, addr, offs, 
		       AWE_PATCH_INFO_SIZE - offs);

	count -= AWE_PATCH_INFO_SIZE;
	if (count < patch.len) {
		FATALERR(printk("AWE32 Warning: Patch record too short (%d<%d)\n",
		       count, (int)patch.len));
		patch.len = count;
	}
	
	switch (patch.type) {
	case AWE_LOAD_INFO:
		rc = awe_load_info(&patch, addr);
		break;

	case AWE_LOAD_DATA:
		rc = awe_load_data(&patch, addr);
		/*
		if (!pmgr_flag && rc == 0)
			pmgr_inform(dev, PM_E_PATCH_LOADED, instr, free_sample, 0, 0);
		*/
		break;

	default:
		FATALERR(printk("AWE32 Error: unknown patch format type %d\n",
		       patch.type));
		rc = RET_ERROR(EINVAL);
	}

	return rc;
}


/* load voice information data */
static int
awe_load_info(awe_patch_info *patch, const char *addr)
{
	awe_voice_list *rec, *curp;
	long offset;
	short i, nvoices;
	unsigned char bank, instr;
	int total_size;

	if (patch->len < AWE_VOICE_REC_SIZE) {
		FATALERR(printk("AWE32 Error: invalid patch info length\n"));
		return RET_ERROR(EINVAL);
	}

	offset = AWE_PATCH_INFO_SIZE;
	GET_BYTE_FROM_USER(bank, addr, offset); offset++;
	GET_BYTE_FROM_USER(instr, addr, offset); offset++;
	GET_SHORT_FROM_USER(nvoices, addr, offset); offset+=2;

	if (nvoices <= 0 || nvoices >= 100) {
		FATALERR(printk("AWE32 Error: Illegal voice number %d\n", nvoices));
		return RET_ERROR(EINVAL);
	}
	if (free_info + nvoices > AWE_MAX_INFOS) {
		ERRMSG(printk("AWE32 Error: Too many voice informations\n"));
		return RET_ERROR(ENOSPC);
	}

	total_size = AWE_VOICE_REC_SIZE + AWE_VOICE_INFO_SIZE * nvoices;
	if (patch->len < total_size) {
		ERRMSG(printk("AWE32 Error: patch length(%d) is smaller than nvoices(%d)\n",
		       (int)patch->len, nvoices));
		return RET_ERROR(EINVAL);
	}

	curp = awe_search_instr(bank, instr);
	for (i = 0; i < nvoices; i++) {
		rec = &infos[free_info + i];

		rec->bank = bank;
		rec->instr = instr;
		if (i < nvoices - 1)
			rec->next_instr = rec + 1;
		else
			rec->next_instr = curp;
		rec->next_bank = NULL;

		/* copy awe_voice_info parameters */
		COPY_FROM_USER(&rec->v, addr, offset, AWE_VOICE_INFO_SIZE);
		offset += AWE_VOICE_INFO_SIZE;
		rec->v.sf_id = current_sf_id;
		if (rec->v.mode & AWE_MODE_INIT_PARM)
			awe_init_voice_parm(&rec->v.parm);
		awe_set_sample(&rec->v);
	}

	/* prepend to top of the list */
	infos[free_info].next_bank = preset_table[instr];
	preset_table[instr] = &infos[free_info];
	free_info += nvoices;

	return 0;
}


/* load wave sample data */
static int
awe_load_data(awe_patch_info *patch, const char *addr)
{
	long offset;
	int size;
	int rc;

	if (free_sample >= AWE_MAX_SAMPLES) {
		ERRMSG(printk("AWE32 Error: Sample table full\n"));
		return RET_ERROR(ENOSPC);
	}

	size = (patch->len - AWE_SAMPLE_INFO_SIZE) / 2;
	offset = AWE_PATCH_INFO_SIZE;
	COPY_FROM_USER(&samples[free_sample], addr, offset,
		       AWE_SAMPLE_INFO_SIZE);
	offset += AWE_SAMPLE_INFO_SIZE;
	if (size != samples[free_sample].size) {
		ERRMSG(printk("AWE32 Warning: sample size differed (%d != %d)\n",
		       (int)samples[free_sample].size, (int)size));
		samples[free_sample].size = size;
	}
	if (samples[free_sample].size > 0)
		if ((rc = awe_write_wave_data(addr, offset, size)) != 0)
			return rc;

	awe_check_loaded();
	samples[free_sample].sf_id = current_sf_id;

	free_sample++;
	return 0;
}

/* check the other samples are already loaded */
static void
awe_check_loaded(void)
{
	if (!loaded_once) {
		/* it's the first time */
		if (current_sf_id == 1) {
			last_sample = free_sample;
			last_info = free_info;
		}
		current_sf_id++;
		loaded_once = 1;
	}
}


/*----------------------------------------------------------------*/

static const char *readbuf_addr;
static long readbuf_offs;
static int readbuf_flags;

/* initialize read buffer */
static void
awe_init_readbuf(const char *addr, long offset, int size, int mode_flags)
{
	readbuf_addr = addr;
	readbuf_offs = offset;
	readbuf_flags = mode_flags;
}

/* read directly from user buffer */
static unsigned short
awe_read_word(int pos)
{
	unsigned short c;
	/* read from user buffer */
	if (readbuf_flags & AWE_SAMPLE_8BITS) {
		unsigned char cc;
		GET_BYTE_FROM_USER(cc, readbuf_addr, readbuf_offs + pos);
		c = cc << 8; /* convert 8bit -> 16bit */
	} else {
		GET_SHORT_FROM_USER(c, readbuf_addr, readbuf_offs + pos * 2);
	}
	if (readbuf_flags & AWE_SAMPLE_UNSIGNED)
		c ^= 0x8000; /* unsigned -> signed */
	return c;
}



#define BLANK_LOOP_START	8
#define BLANK_LOOP_END		40
#define BLANK_LOOP_SIZE		48

/* loading onto memory */
static int 
awe_write_wave_data(const char *addr, long offset, int size)
{
	awe_sample_info *sp = &samples[free_sample];
	int i, truesize;
	int rc;
	unsigned long csum1, csum2;

	/* be sure loop points start < end */
	if (sp->loopstart > sp->loopend) {
		long tmp = sp->loopstart;
		sp->loopstart = sp->loopend;
		sp->loopend = tmp;
	}

	/* compute true data size to be loaded */
	truesize = size;
	if (sp->mode_flags & AWE_SAMPLE_BIDIR_LOOP)
		truesize += sp->loopend - sp->loopstart;
	if (sp->mode_flags & AWE_SAMPLE_NO_BLANK)
		truesize += BLANK_LOOP_SIZE;
	if (size > 0 && free_mem_ptr + truesize >= awe_mem_size/2) {
		ERRMSG(printk("AWE32 Error: Sample memory full\n"));
		return RET_ERROR(ENOSPC);
	}

	/* recalculate address offset */
	sp->end -= sp->start;
	sp->loopstart -= sp->start;
	sp->loopend -= sp->start;
	sp->size = truesize;

	sp->start = free_mem_ptr + AWE_DRAM_OFFSET;
	sp->end += free_mem_ptr + AWE_DRAM_OFFSET;
	sp->loopstart += free_mem_ptr + AWE_DRAM_OFFSET;
	sp->loopend += free_mem_ptr + AWE_DRAM_OFFSET;

	if ((rc = awe_open_dram_for_write(free_mem_ptr)) != 0) {
		return rc;
	}

	awe_init_readbuf(addr, offset, size, sp->mode_flags);
	csum1 = 0;
	for (i = 0; i < size; i++) {
		unsigned short c;
		c = awe_read_word(i);
		csum1 += c;
		awe_write_dram(c);
		if (i == sp->loopend &&
		    (sp->mode_flags & (AWE_SAMPLE_BIDIR_LOOP|AWE_SAMPLE_REVERSE_LOOP))) {
			int looplen = sp->loopend - sp->loopstart;
			/* copy reverse loop */
			int k;
			for (k = 0; k < looplen; k++) {
				/* non-buffered data */
				c = awe_read_word(i - k);
				awe_write_dram(c);
			}
			if (sp->mode_flags & AWE_SAMPLE_BIDIR_LOOP) {
				sp->end += looplen;
			} else {
				sp->start += looplen;
				sp->end += looplen;
			}
		}
	}

	/* if no blank loop is attached in the sample, add it */
	if (sp->mode_flags & AWE_SAMPLE_NO_BLANK) {
		for (i = 0; i < BLANK_LOOP_SIZE; i++)
			awe_write_dram(0);
		if (sp->mode_flags & AWE_SAMPLE_SINGLESHOT) {
			sp->loopstart = sp->end + BLANK_LOOP_START;
			sp->loopend = sp->end + BLANK_LOOP_END;
		}
		sp->size += BLANK_LOOP_SIZE;
	}

	awe_close_dram();
	if (sp->checksum_flag) {
#ifdef AWE_CHECKSUM_DATA
		if (sp->checksum_flag != 2 && csum1 != sp->checksum) {
			ERRMSG(printk("AWE32: [%d] checksum mismatch on data %x:%x\n",
			       free_sample, (int)sp->checksum, (int)csum1));
			return RET_ERROR(NO_DATA_ERR);
		}
#endif /* AWE_CHECKSUM_DATA */
#ifdef AWE_CHECKSUM_MEMORY
		if (awe_open_dram_for_read(free_mem_ptr) == 0) {
			csum2 = 0;
			for (i = 0; i < size; i++) {
				unsigned short c;
				c = awe_peek(AWE_SMLD);
				csum2 += c;
			}
			awe_close_dram_for_read();
			if (csum1 != csum2) {
				ERRMSG(printk("AWE32: [%d] checksum mismatch on DRAM %x:%x\n",
					      free_sample, (int)csum1, (int)csum2));
				return RET_ERROR(NO_DATA_ERR);
			}
		}
#endif /* AWE_CHECKSUM_MEMORY */
	}
	free_mem_ptr += sp->size;

	/* re-initialize FM passthrough */
	awe_init_fm();
	awe_tweak();

	return 0;
}


/*----------------------------------------------------------------*/

#ifdef AWE_HAS_GUS_COMPATIBILITY

/* calculate GUS envelope time:
 * is this correct?  i have no idea..
 */
static int
calc_gus_envelope_time(int rate, int start, int end)
{
	int r, p, t;
	r = (3 - ((rate >> 6) & 3)) * 3;
	p = rate & 0x3f;
	t = end - start;
	if (t < 0) t = -t;
	if (13 > r)
		t = t << (13 - r);
	else
		t = t >> (r - 13);
	return (t * 10) / (p * 441);
}

#define calc_gus_sustain(val)  (0x7f - vol_table[(val)/2])
#define calc_gus_attenuation(val)	vol_table[(val)/2]

/* load GUS patch */
static int
awe_load_guspatch(const char *addr, int offs, int size, int pmgr_flag)
{
	struct patch_info patch;
	awe_voice_list *rec, *curp;
	long sizeof_patch;
	int note;
	int rc;

	sizeof_patch = (long)&patch.data[0] - (long)&patch; /* header size */
	if (free_sample >= AWE_MAX_SAMPLES) {
		ERRMSG(printk("AWE32 Error: Sample table full\n"));
		return RET_ERROR(ENOSPC);
	}
	if (free_info >= AWE_MAX_INFOS) {
		ERRMSG(printk("AWE32 Error: Too many voice informations\n"));
		return RET_ERROR(ENOSPC);
	}
	if (size < sizeof_patch) {
		ERRMSG(printk("AWE32 Error: Patch header too short\n"));
		return RET_ERROR(EINVAL);
	}
	COPY_FROM_USER(((char*)&patch) + offs, addr, offs, sizeof_patch - offs);
	size -= sizeof_patch;
	if (size < patch.len) {
		FATALERR(printk("AWE32 Warning: Patch record too short (%d<%d)\n",
		       size, (int)patch.len));
		patch.len = size;
	}

	samples[free_sample].sf_id = 0;
	samples[free_sample].sample = free_sample;
	samples[free_sample].start = 0;
	samples[free_sample].end = patch.len;
	samples[free_sample].loopstart = patch.loop_start;
	samples[free_sample].loopend = patch.loop_end + 1;
	samples[free_sample].size = patch.len;

	/* set up mode flags */
	samples[free_sample].mode_flags = 0;
	if (!(patch.mode & WAVE_16_BITS))
		samples[free_sample].mode_flags |= AWE_SAMPLE_8BITS;
	if (patch.mode & WAVE_UNSIGNED)
		samples[free_sample].mode_flags |= AWE_SAMPLE_UNSIGNED;
	samples[free_sample].mode_flags |= AWE_SAMPLE_NO_BLANK;
	if (!(patch.mode & (WAVE_LOOPING|WAVE_BIDIR_LOOP|WAVE_LOOP_BACK)))
		samples[free_sample].mode_flags |= AWE_SAMPLE_SINGLESHOT;
	if (patch.mode & WAVE_BIDIR_LOOP)
		samples[free_sample].mode_flags |= AWE_SAMPLE_BIDIR_LOOP;
	if (patch.mode & WAVE_LOOP_BACK)
		samples[free_sample].mode_flags |= AWE_SAMPLE_REVERSE_LOOP;

	DEBUG(0,printk("AWE32: [sample %d mode %x]\n", patch.instr_no,
		       samples[free_sample].mode_flags));
	if (patch.mode & WAVE_16_BITS) {
		/* convert to word offsets */
		samples[free_sample].size /= 2;
		samples[free_sample].end /= 2;
		samples[free_sample].loopstart /= 2;
		samples[free_sample].loopend /= 2;
	}
	samples[free_sample].checksum_flag = 0;
	samples[free_sample].checksum = 0;

	if ((rc = awe_write_wave_data(addr, sizeof_patch,
				      samples[free_sample].size)) != 0)
		return rc;

	awe_check_loaded();
	samples[free_sample].sf_id = current_sf_id;
	free_sample++;

	/* set up voice info */
	rec = &infos[free_info];
	awe_init_voice_info(&rec->v);
	rec->v.sf_id = current_sf_id;
	rec->v.sample = free_sample - 1; /* the last sample */
	rec->v.rate_offset = calc_rate_offset(patch.base_freq);
	note = freq_to_note(patch.base_note);
	rec->v.root = note / 100;
	rec->v.tune = -(note % 100);
	rec->v.low = freq_to_note(patch.low_note) / 100;
	rec->v.high = freq_to_note(patch.high_note) / 100;
	DEBUG(1,printk("AWE32: [gus base offset=%d, note=%d, range=%d-%d(%d-%d)]\n",
		       rec->v.rate_offset, note,
		       rec->v.low, rec->v.high,
	      patch.low_note, patch.high_note));
	/* panning position; -128 - 127 => 0-127 */
	rec->v.pan = (patch.panning + 128) / 2;

	/* detuning is ignored */
	/* 6points volume envelope */
	if (patch.mode & WAVE_ENVELOPES) {
		int attack, hold, decay, release;
		attack = calc_gus_envelope_time
			(patch.env_rate[0], 0, patch.env_offset[0]);
		hold = calc_gus_envelope_time
			(patch.env_rate[1], patch.env_offset[0],
			 patch.env_offset[1]);
		decay = calc_gus_envelope_time
			(patch.env_rate[2], patch.env_offset[1],
			 patch.env_offset[2]);
		release = calc_gus_envelope_time
			(patch.env_rate[3], patch.env_offset[1],
			 patch.env_offset[4]);
		release += calc_gus_envelope_time
			(patch.env_rate[4], patch.env_offset[3],
			 patch.env_offset[4]);
		release += calc_gus_envelope_time
			(patch.env_rate[5], patch.env_offset[4],
			 patch.env_offset[5]);
		rec->v.parm.volatkhld = (calc_parm_attack(attack) << 8) |
			calc_parm_hold(hold);
		rec->v.parm.voldcysus = (calc_gus_sustain(patch.env_offset[2]) << 8) |
			calc_parm_decay(decay);
		rec->v.parm.volrelease = 0x8000 | calc_parm_decay(release);
		DEBUG(2,printk("AWE32: [gusenv atk=%d, hld=%d, dcy=%d, rel=%d]\n", attack, hold, decay, release));
		rec->v.attenuation = calc_gus_attenuation(patch.env_offset[0]);
	}

	/* tremolo effect */
	if (patch.mode & WAVE_TREMOLO) {
		int rate = (patch.tremolo_rate * 1000 / 38) / 42;
		rec->v.parm.tremfrq = ((patch.tremolo_depth / 2) << 8) | rate;
		DEBUG(2,printk("AWE32: [gusenv tremolo rate=%d, dep=%d, tremfrq=%x]\n",
			       patch.tremolo_rate, patch.tremolo_depth,
			       rec->v.parm.tremfrq));
	}
	/* vibrato effect */
	if (patch.mode & WAVE_VIBRATO) {
		int rate = (patch.vibrato_rate * 1000 / 38) / 42;
		rec->v.parm.fm2frq2 = ((patch.vibrato_depth / 6) << 8) | rate;
		DEBUG(2,printk("AWE32: [gusenv vibrato rate=%d, dep=%d, tremfrq=%x]\n",
			       patch.tremolo_rate, patch.tremolo_depth,
			       rec->v.parm.tremfrq));
	}
	
	/* scale_freq, scale_factor, volume, and fractions not implemented */

	/* set the voice index */
	awe_set_sample(&rec->v);

	/* prepend to top of the list */
	curp = awe_search_instr(awe_gus_bank, patch.instr_no);
	rec->bank = awe_gus_bank;
	rec->instr = patch.instr_no;
	rec->next_instr = curp;
	rec->next_bank = preset_table[rec->instr];
	preset_table[rec->instr] = rec;
	free_info++;

	return 0;
}

#endif  /* AWE_HAS_GUS_COMPATIBILITY */

/*----------------------------------------------------------------*/

/* remove samples with different sf_id from instrument list */
static awe_voice_list *
awe_get_removed_list(awe_voice_list *curp)
{
	awe_voice_list *lastp, **prevp;
	int maxc;
	lastp = curp;
	prevp = &lastp;
	for (maxc = AWE_MAX_INFOS;
	     curp && maxc; curp = curp->next_instr, maxc--) {
		if (curp->v.sf_id != 1)
			*prevp = curp->next_instr;
		else
			prevp = &curp->next_instr;
	}
	return lastp;
}


/* remove last loaded samples */
static void
awe_remove_samples(void)
{
	awe_voice_list **prevp, *p, *nextp;
	int maxc;
	int i;

	/* no sample is loaded yet */
	if (last_sample == free_sample && last_info == free_info)
		return;

	/* only the primary samples are loaded */
	if (current_sf_id <= 1)
		return;

	/* remove the records from preset table */
	for (i = 0; i < AWE_MAX_PRESETS; i++) {
		prevp = &preset_table[i];
		for (maxc = AWE_MAX_INFOS, p = preset_table[i];
		     p && maxc; p = nextp, maxc--) {
			nextp = p->next_bank;
			p = awe_get_removed_list(p);
			if (p == NULL)
				*prevp = nextp;
			else {
				*prevp = p;
				prevp = &p->next_bank;
			}
		}
	}

	for (i = last_sample; i < free_sample; i++)
		free_mem_ptr -= samples[i].size;

	free_sample = last_sample;
	free_info = last_info;
	current_sf_id = 1;
	loaded_once = 0;
}


/* search the specified sample */
static short
awe_set_sample(awe_voice_info *vp)
{
	int i;
	for (i = 0; i < free_sample; i++) {
		if (samples[i].sf_id == vp->sf_id &&
		    samples[i].sample == vp->sample) {
			/* set the actual sample offsets */
			vp->start += samples[i].start;
			vp->end += samples[i].end;
			vp->loopstart += samples[i].loopstart;
			vp->loopend += samples[i].loopend;
			/* copy mode flags */
			vp->mode = samples[i].mode_flags;
			/* set index */
			vp->index = i;
			return i;
		}
	}
	return -1;
}


/* voice pressure change */
static void
awe_aftertouch(int dev, int voice, int pressure)
{
	DEBUG(2,printk("AWE32: [after(%d) %d]\n", voice, pressure));
	if (awe_channel_mode == 2) {
		int note = (voice_alloc->map[voice] & 0xff) - 1;
		awe_start_note(dev, voice, note + 0x80, pressure);

	} else if (awe_channel_mode == 0) {
		if (voice < 0 || voice >= awe_max_voices)
			return;
		voices[voice].velocity = pressure;
		awe_set_voice_vol(voice, FALSE);
	}
}


/* voice control change */
static void
awe_controller(int dev, int voice, int ctrl_num, int value)
{
	awe_chan_info *cinfo;

	if (awe_channel_mode) {
		if (awe_channel_mode == 2) /* get channel */
			voice = voice_alloc->map[voice] >> 8;
		if (voice < 0 || voice >= AWE_MAX_CHANNELS)
			return;
		cinfo = &channels[voice];
	} else {
		if (voice < 0 || voice >= awe_max_voices)
			return;
		cinfo = voices[voice].cinfo;
	}

	switch (ctrl_num) {
	case CTL_BANK_SELECT: /* SEQ1 control */
		DEBUG(2,printk("AWE32: [bank(%d) %d]\n", voice, value));
		cinfo->bank = value;
		awe_set_instr(dev, voice, cinfo->instr);
		break;

	case CTRL_PITCH_BENDER: /* SEQ1 V2 contorl */
		DEBUG(2,printk("AWE32: [bend(%d) %d]\n", voice, value));
		/* zero centered */
		cinfo->bender = value;
		awe_voice_change(voice, awe_set_voice_pitch);
		break;

	case CTRL_PITCH_BENDER_RANGE: /* SEQ1 V2 control */
		DEBUG(2,printk("AWE32: [range(%d) %d]\n", voice, value));
		/* value = sense x 100 */
		cinfo->bender_range = value;
		/* no audible pitch change yet.. */
		break;

	case CTL_EXPRESSION: /* SEQ1 control */
		if (!awe_channel_mode)
			value /= 128;
	case CTRL_EXPRESSION: /* SEQ1 V2 control */
		DEBUG(2,printk("AWE32: [expr(%d) %d]\n", voice, value));
		/* 0 - 127 */
		cinfo->expression_vol = value;
		awe_voice_change(voice, awe_set_voice_vol);
		break;

	case CTL_PAN:	/* SEQ1 control */
		DEBUG(2,printk("AWE32: [pan(%d) %d]\n", voice, value));
		/* (0-127) -> signed 8bit */
		cinfo->panning = value * 2 - 128;
		awe_voice_change(voice, awe_set_pan);
		break;

	case CTL_MAIN_VOLUME:	/* SEQ1 control */
		if (!awe_channel_mode)
			value = (value * 100) / 16383;
	case CTRL_MAIN_VOLUME:	/* SEQ1 V2 control */
		DEBUG(2,printk("AWE32: [mainvol(%d) %d]\n", voice, value));
		/* 0 - 127 */
		cinfo->main_vol = value;
		awe_voice_change(voice, awe_set_voice_vol);
		break;

	case CTL_EXT_EFF_DEPTH: /* reverb effects: 0-127 */
		DEBUG(2,printk("AWE32: [reverb(%d) %d]\n", voice, value));
		FX_SET(&cinfo->fx, AWE_FX_REVERB, value * 2);
		break;

	case CTL_CHORUS_DEPTH: /* chorus effects: 0-127 */
		DEBUG(2,printk("AWE32: [chorus(%d) %d]\n", voice, value));
		FX_SET(&cinfo->fx, AWE_FX_CHORUS, value * 2);
		break;

#ifdef AWE_ACCEPT_ALL_SOUNDS_CONTROLL
	case 120:  /* all sounds off */
		{int i; for (i = 0; i < AWE_NORMAL_VOICES; i++)
			awe_terminate(i);
		}
		/*awe_reset(0);*/
		break;
	case 123:  /* all notes off */
		{int i;
		for (i = 0; i < awe_max_voices; i++)
			awe_note_off(i);
		}
		break;
#endif

	case CTL_SUSTAIN: /* sustain the channel */
		cinfo->sustained = value;
		if (value == 0)
			awe_voice_change(voice, awe_sustain_off);
		break;

	default:
		DEBUG(0,printk("AWE32: [control(%d) ctrl=%d val=%d]\n",
			   voice, ctrl_num, value));
		break;
	}
}



/* change the voice parameters */
static void awe_voice_change(int voice, fx_affect_func func)
{
	int i; 
	if (! awe_channel_mode) {
		func(voice, FALSE);
		return;
	}

	for (i = 0; i < awe_max_voices; i++)
		if (voices[i].ch == voice)
			func(i, FALSE);
}


/* drop sustain */
static void awe_sustain_off(int voice, int forced)
{
	if (voices[voice].state == AWE_ST_SUSTAINED)
		awe_note_off(voice);
}


/* voice pan change (value = -128 - 127) */
static void
awe_panning(int dev, int voice, int value)
{
	awe_chan_info *cinfo;
	if (awe_channel_mode) {
		if (voice < 0 || voice >= AWE_MAX_CHANNELS)
			return;
		cinfo = &channels[voice];
	} else {
		if (voice < 0 || voice >= awe_max_voices)
			return;
		cinfo = voices[voice].cinfo;
	}

	cinfo->panning = value;
	DEBUG(2,printk("AWE32: [pan(%d) %d]\n", voice, cinfo->panning));
	awe_voice_change(voice, awe_set_pan);
}


/* volume mode change */
static void
awe_volume_method(int dev, int mode)
{
	/* not impremented */
	DEBUG(0,printk("AWE32: [volmethod mode=%d]\n", mode));
}


#ifndef AWE_NO_PATCHMGR
/* patch manager */
static int
awe_patchmgr(int dev, struct patmgr_info *rec)
{
	FATALERR(printk("AWE32 Warning: patch manager control not supported\n"));
	return 0;
}
#endif


/* pitch wheel change: 0-16384 */
static void
awe_bender(int dev, int voice, int value)
{
	awe_chan_info *cinfo;
	if (awe_channel_mode) {
		if (awe_channel_mode == 2)
			voice = voice_alloc->map[voice] >> 8;
		if (voice < 0 || voice >= AWE_MAX_CHANNELS)
			return;
		cinfo = &channels[voice];
	} else {
		if (voice < 0 || voice >= awe_max_voices)
			return;
		cinfo = voices[voice].cinfo;
	}
	/* convert to zero centered value */
	cinfo->bender = value - 8192;
	DEBUG(2,printk("AWE32: [bend(%d) %d]\n", voice, cinfo->bender));
	awe_voice_change(voice, awe_set_voice_pitch);
}

/* calculate the number of voices for this note & velocity */
static int
awe_search_multi_voices(awe_voice_list *rec, int note, int velocity, awe_voice_info **vlist)
{
	int maxc;
	int nvoices;
	unsigned short sf_id;

	sf_id = current_sf_id;
	nvoices = 0;
	for (maxc = AWE_MAX_INFOS;
	     rec && maxc; rec = rec->next_instr, maxc--) {
		if (rec->v.low <= note && note <= rec->v.high &&
		    velocity >= rec->v.vellow && velocity <= rec->v.velhigh) {
			if (nvoices == 0)
				sf_id = rec->v.sf_id;
			else if (rec->v.sf_id != sf_id)
				continue;
			vlist[nvoices] = &rec->v;
			nvoices++;
			if (nvoices >= AWE_MAX_VOICES)
				break;
		}
	}
	return nvoices;	
}

/* allocate voices corresponding note and velocity; supports multiple insts. */
static void
awe_alloc_multi_voices(int ch, int note, int velocity)
{
	int i, v, nvoices;
	awe_voice_info *vlist[AWE_MAX_VOICES];

	if (channels[ch].vrec == NULL && channels[ch].def_vrec == NULL)
		awe_set_instr(0, ch, channels[ch].instr);

	nvoices = awe_search_multi_voices(channels[ch].vrec, note, velocity, vlist);
	if (nvoices == 0)
		nvoices = awe_search_multi_voices(channels[ch].def_vrec, note, velocity, vlist);

	/* allocate the voices */
	current_alloc_time++;
	for (i = 0; i < nvoices; i++) {
		v = awe_clear_voice();
		voices[v].key = AWE_CHAN_KEY(ch, note);
		voices[v].ch = ch;
		voices[v].time = current_alloc_time;
		voices[v].cinfo = &channels[ch];
		voices[v].sample = vlist[i];
		voices[v].state = AWE_ST_MARK;
	}

	/* clear the mark in allocated voices */
	for (i = 0; i < awe_max_voices; i++)
		if (voices[i].state == AWE_ST_MARK)
			voices[i].state = AWE_ST_OFF;
}


/* search an empty voice; used internally */
static int
awe_clear_voice(void)
{
	int i, time, best;

	/* looking for the oldest empty voice */
	best = -1;
	time = 0x7fffffff;
	for (i = 0; i < awe_max_voices; i++) {
		if (voices[i].state == AWE_ST_OFF && voices[i].time < time) {
			best = i;
			time = voices[i].time;
		}
	}
	if (best >= 0)
		return best;

	/* looking for the oldest sustained voice */
	time = 0x7fffffff;
	for (i = 0; i < awe_max_voices; i++) {
		if (voices[i].state == AWE_ST_SUSTAINED &&
		    voices[i].time < time) {
			best = i;
			time = voices[i].time;
		}
	}
	if (best >= 0) {
		awe_note_off(best);
		return best;
	}

	/* looking for the oldest voice not marked */
	time = 0x7fffffff;
	best = 0;
	for (i = 0; i < awe_max_voices; i++) {
		if (voices[i].state != AWE_ST_MARK && voices[i].time < time) {
			best = i;
			time = voices[i].time;
		}
	}
	/*awe_terminate(best);*/
	awe_note_off(best);
	return best;
}


/* allocate a voice corresponding note and velocity; single instrument */
static void
awe_alloc_one_voice(int voice, int note, int velocity)
{
	int nvoices;
	awe_voice_info *vlist[AWE_MAX_VOICES];

	if (voices[voice].cinfo->vrec == NULL && voices[voice].cinfo->def_vrec == NULL)
		awe_set_instr(0, voice, voices[voice].cinfo->instr);

	nvoices = awe_search_multi_voices(voices[voice].cinfo->vrec, note, velocity, vlist);
	if (nvoices == 0)
		nvoices = awe_search_multi_voices(voices[voice].cinfo->def_vrec, note, velocity, vlist);

	if (nvoices > 0) {
		voices[voice].time = ++current_alloc_time;
		voices[voice].sample = vlist[0]; /* use the first one */
	}
}


/* search an empty voice; used by sequencer2 */
static int
awe_alloc(int dev, int chn, int note, struct voice_alloc_info *alloc)
{
	awe_channel_mode = 2;
	return awe_clear_voice();
}


/* set up voice; used by sequencer2 */
static void
awe_setup_voice(int dev, int voice, int chn)
{
	struct channel_info *info;
	if (synth_devs[dev] == NULL ||
	    (info = &synth_devs[dev]->chn_info[chn]) == NULL)
		return;

	if (voice < 0 || voice >= awe_max_voices)
		return;

	DEBUG(2,printk("AWE32: [setup(%d) ch=%d]\n", voice, chn));
	channels[chn].expression_vol = info->controllers[CTL_EXPRESSION];
	channels[chn].main_vol = info->controllers[CTL_MAIN_VOLUME];
	channels[chn].panning =
		info->controllers[CTL_PAN] * 2 - 128; /* signed 8bit */
	channels[chn].bender = info->bender_value; /* zero center */
	channels[chn].bank = info->controllers[CTL_BANK_SELECT];
	channels[chn].sustained = info->controllers[CTL_SUSTAIN];
	if (info->controllers[CTL_EXT_EFF_DEPTH]) {
		FX_SET(&channels[chn].fx, AWE_FX_REVERB,
		       info->controllers[CTL_EXT_EFF_DEPTH] * 2);
	}
	if (info->controllers[CTL_CHORUS_DEPTH]) {
		FX_SET(&channels[chn].fx, AWE_FX_CHORUS,
		       info->controllers[CTL_CHORUS_DEPTH] * 2);
	}
	awe_set_instr(dev, chn, info->pgm_num);
}


/*================================================================
 * initialization of AWE32
 *================================================================*/

/* intiailize audio channels */
static void
awe_init_audio(void)
{
	int ch;

	/* turn off envelope engines */
	for (ch = 0; ch < AWE_MAX_VOICES; ch++) {
		awe_poke(AWE_DCYSUSV(ch), 0x80);
	}
  
	/* reset all other parameters to zero */
	for (ch = 0; ch < AWE_MAX_VOICES; ch++) {
		awe_poke(AWE_ENVVOL(ch), 0);
		awe_poke(AWE_ENVVAL(ch), 0);
		awe_poke(AWE_DCYSUS(ch), 0);
		awe_poke(AWE_ATKHLDV(ch), 0);
		awe_poke(AWE_LFO1VAL(ch), 0);
		awe_poke(AWE_ATKHLD(ch), 0);
		awe_poke(AWE_LFO2VAL(ch), 0);
		awe_poke(AWE_IP(ch), 0);
		awe_poke(AWE_IFATN(ch), 0);
		awe_poke(AWE_PEFE(ch), 0);
		awe_poke(AWE_FMMOD(ch), 0);
		awe_poke(AWE_TREMFRQ(ch), 0);
		awe_poke(AWE_FM2FRQ2(ch), 0);
		awe_poke_dw(AWE_PTRX(ch), 0);
		awe_poke_dw(AWE_VTFT(ch), 0);
		awe_poke_dw(AWE_PSST(ch), 0);
		awe_poke_dw(AWE_CSL(ch), 0);
		awe_poke_dw(AWE_CCCA(ch), 0);
	}

	for (ch = 0; ch < AWE_MAX_VOICES; ch++) {
		awe_poke_dw(AWE_CPF(ch), 0);
		awe_poke_dw(AWE_CVCF(ch), 0);
	}
}


/* initialize DMA address */
static void
awe_init_dma(void)
{
	awe_poke_dw(AWE_SMALR, 0);
	awe_poke_dw(AWE_SMARR, 0);
	awe_poke_dw(AWE_SMALW, 0);
	awe_poke_dw(AWE_SMARW, 0);
}


/* initialization arrays; from ADIP */

static unsigned short init1[128] = {
	0x03ff, 0x0030,  0x07ff, 0x0130, 0x0bff, 0x0230,  0x0fff, 0x0330,
	0x13ff, 0x0430,  0x17ff, 0x0530, 0x1bff, 0x0630,  0x1fff, 0x0730,
	0x23ff, 0x0830,  0x27ff, 0x0930, 0x2bff, 0x0a30,  0x2fff, 0x0b30,
	0x33ff, 0x0c30,  0x37ff, 0x0d30, 0x3bff, 0x0e30,  0x3fff, 0x0f30,

	0x43ff, 0x0030,  0x47ff, 0x0130, 0x4bff, 0x0230,  0x4fff, 0x0330,
	0x53ff, 0x0430,  0x57ff, 0x0530, 0x5bff, 0x0630,  0x5fff, 0x0730,
	0x63ff, 0x0830,  0x67ff, 0x0930, 0x6bff, 0x0a30,  0x6fff, 0x0b30,
	0x73ff, 0x0c30,  0x77ff, 0x0d30, 0x7bff, 0x0e30,  0x7fff, 0x0f30,

	0x83ff, 0x0030,  0x87ff, 0x0130, 0x8bff, 0x0230,  0x8fff, 0x0330,
	0x93ff, 0x0430,  0x97ff, 0x0530, 0x9bff, 0x0630,  0x9fff, 0x0730,
	0xa3ff, 0x0830,  0xa7ff, 0x0930, 0xabff, 0x0a30,  0xafff, 0x0b30,
	0xb3ff, 0x0c30,  0xb7ff, 0x0d30, 0xbbff, 0x0e30,  0xbfff, 0x0f30,

	0xc3ff, 0x0030,  0xc7ff, 0x0130, 0xcbff, 0x0230,  0xcfff, 0x0330,
	0xd3ff, 0x0430,  0xd7ff, 0x0530, 0xdbff, 0x0630,  0xdfff, 0x0730,
	0xe3ff, 0x0830,  0xe7ff, 0x0930, 0xebff, 0x0a30,  0xefff, 0x0b30,
	0xf3ff, 0x0c30,  0xf7ff, 0x0d30, 0xfbff, 0x0e30,  0xffff, 0x0f30,
};

static unsigned short init2[128] = {
	0x03ff, 0x8030, 0x07ff, 0x8130, 0x0bff, 0x8230, 0x0fff, 0x8330,
	0x13ff, 0x8430, 0x17ff, 0x8530, 0x1bff, 0x8630, 0x1fff, 0x8730,
	0x23ff, 0x8830, 0x27ff, 0x8930, 0x2bff, 0x8a30, 0x2fff, 0x8b30,
	0x33ff, 0x8c30, 0x37ff, 0x8d30, 0x3bff, 0x8e30, 0x3fff, 0x8f30,

	0x43ff, 0x8030, 0x47ff, 0x8130, 0x4bff, 0x8230, 0x4fff, 0x8330,
	0x53ff, 0x8430, 0x57ff, 0x8530, 0x5bff, 0x8630, 0x5fff, 0x8730,
	0x63ff, 0x8830, 0x67ff, 0x8930, 0x6bff, 0x8a30, 0x6fff, 0x8b30,
	0x73ff, 0x8c30, 0x77ff, 0x8d30, 0x7bff, 0x8e30, 0x7fff, 0x8f30,

	0x83ff, 0x8030, 0x87ff, 0x8130, 0x8bff, 0x8230, 0x8fff, 0x8330,
	0x93ff, 0x8430, 0x97ff, 0x8530, 0x9bff, 0x8630, 0x9fff, 0x8730,
	0xa3ff, 0x8830, 0xa7ff, 0x8930, 0xabff, 0x8a30, 0xafff, 0x8b30,
	0xb3ff, 0x8c30, 0xb7ff, 0x8d30, 0xbbff, 0x8e30, 0xbfff, 0x8f30,

	0xc3ff, 0x8030, 0xc7ff, 0x8130, 0xcbff, 0x8230, 0xcfff, 0x8330,
	0xd3ff, 0x8430, 0xd7ff, 0x8530, 0xdbff, 0x8630, 0xdfff, 0x8730,
	0xe3ff, 0x8830, 0xe7ff, 0x8930, 0xebff, 0x8a30, 0xefff, 0x8b30,
	0xf3ff, 0x8c30, 0xf7ff, 0x8d30, 0xfbff, 0x8e30, 0xffff, 0x8f30,
};

static unsigned short init3[128] = {
	0x0C10, 0x8470, 0x14FE, 0xB488, 0x167F, 0xA470, 0x18E7, 0x84B5,
	0x1B6E, 0x842A, 0x1F1D, 0x852A, 0x0DA3, 0x8F7C, 0x167E, 0xF254,
	0x0000, 0x842A, 0x0001, 0x852A, 0x18E6, 0x8BAA, 0x1B6D, 0xF234,
	0x229F, 0x8429, 0x2746, 0x8529, 0x1F1C, 0x86E7, 0x229E, 0xF224,

	0x0DA4, 0x8429, 0x2C29, 0x8529, 0x2745, 0x87F6, 0x2C28, 0xF254,
	0x383B, 0x8428, 0x320F, 0x8528, 0x320E, 0x8F02, 0x1341, 0xF264,
	0x3EB6, 0x8428, 0x3EB9, 0x8528, 0x383A, 0x8FA9, 0x3EB5, 0xF294,
	0x3EB7, 0x8474, 0x3EBA, 0x8575, 0x3EB8, 0xC4C3, 0x3EBB, 0xC5C3,

	0x0000, 0xA404, 0x0001, 0xA504, 0x141F, 0x8671, 0x14FD, 0x8287,
	0x3EBC, 0xE610, 0x3EC8, 0x8C7B, 0x031A, 0x87E6, 0x3EC8, 0x86F7,
	0x3EC0, 0x821E, 0x3EBE, 0xD208, 0x3EBD, 0x821F, 0x3ECA, 0x8386,
	0x3EC1, 0x8C03, 0x3EC9, 0x831E, 0x3ECA, 0x8C4C, 0x3EBF, 0x8C55,

	0x3EC9, 0xC208, 0x3EC4, 0xBC84, 0x3EC8, 0x8EAD, 0x3EC8, 0xD308,
	0x3EC2, 0x8F7E, 0x3ECB, 0x8219, 0x3ECB, 0xD26E, 0x3EC5, 0x831F,
	0x3EC6, 0xC308, 0x3EC3, 0xB2FF, 0x3EC9, 0x8265, 0x3EC9, 0x8319,
	0x1342, 0xD36E, 0x3EC7, 0xB3FF, 0x0000, 0x8365, 0x1420, 0x9570,
};

static unsigned short init4[128] = {
	0x0C10, 0x8470, 0x14FE, 0xB488, 0x167F, 0xA470, 0x18E7, 0x84B5,
	0x1B6E, 0x842A, 0x1F1D, 0x852A, 0x0DA3, 0x0F7C, 0x167E, 0x7254,
	0x0000, 0x842A, 0x0001, 0x852A, 0x18E6, 0x0BAA, 0x1B6D, 0x7234,
	0x229F, 0x8429, 0x2746, 0x8529, 0x1F1C, 0x06E7, 0x229E, 0x7224,

	0x0DA4, 0x8429, 0x2C29, 0x8529, 0x2745, 0x07F6, 0x2C28, 0x7254,
	0x383B, 0x8428, 0x320F, 0x8528, 0x320E, 0x0F02, 0x1341, 0x7264,
	0x3EB6, 0x8428, 0x3EB9, 0x8528, 0x383A, 0x0FA9, 0x3EB5, 0x7294,
	0x3EB7, 0x8474, 0x3EBA, 0x8575, 0x3EB8, 0x44C3, 0x3EBB, 0x45C3,

	0x0000, 0xA404, 0x0001, 0xA504, 0x141F, 0x0671, 0x14FD, 0x0287,
	0x3EBC, 0xE610, 0x3EC8, 0x0C7B, 0x031A, 0x07E6, 0x3EC8, 0x86F7,
	0x3EC0, 0x821E, 0x3EBE, 0xD208, 0x3EBD, 0x021F, 0x3ECA, 0x0386,
	0x3EC1, 0x0C03, 0x3EC9, 0x031E, 0x3ECA, 0x8C4C, 0x3EBF, 0x0C55,

	0x3EC9, 0xC208, 0x3EC4, 0xBC84, 0x3EC8, 0x0EAD, 0x3EC8, 0xD308,
	0x3EC2, 0x8F7E, 0x3ECB, 0x0219, 0x3ECB, 0xD26E, 0x3EC5, 0x031F,
	0x3EC6, 0xC308, 0x3EC3, 0x32FF, 0x3EC9, 0x0265, 0x3EC9, 0x8319,
	0x1342, 0xD36E, 0x3EC7, 0x33FF, 0x0000, 0x8365, 0x1420, 0x9570,
};


/* send initialization arrays to start up */
static void
awe_init_array(void)
{
	awe_send_array(init1);
	awe_wait(1024);
	awe_send_array(init2);
	awe_send_array(init3);
	awe_poke_dw(AWE_HWCF4, 0);
	awe_poke_dw(AWE_HWCF5, 0x83);
	awe_poke_dw(AWE_HWCF6, 0x8000);
	awe_send_array(init4);
}

/* send an initialization array */
static void
awe_send_array(unsigned short *data)
{
	int i;
	unsigned short *p;

	p = data;
	for (i = 0; i < AWE_MAX_VOICES; i++, p++)
		awe_poke(AWE_INIT1(i), *p);
	for (i = 0; i < AWE_MAX_VOICES; i++, p++)
		awe_poke(AWE_INIT2(i), *p);
	for (i = 0; i < AWE_MAX_VOICES; i++, p++)
		awe_poke(AWE_INIT3(i), *p);
	for (i = 0; i < AWE_MAX_VOICES; i++, p++)
		awe_poke(AWE_INIT4(i), *p);
}


/*
 * set up awe32 channels to some known state.
 */

/* set the envelope & LFO parameters to the default values; see ADIP */
static void
awe_tweak_voice(int i)
{
	/* set all mod/vol envelope shape to minimum */
	awe_poke(AWE_ENVVOL(i), 0x8000);
	awe_poke(AWE_ENVVAL(i), 0x8000);
	awe_poke(AWE_DCYSUS(i), 0x7F7F);
	awe_poke(AWE_ATKHLDV(i), 0x7F7F);
	awe_poke(AWE_ATKHLD(i), 0x7F7F);
	awe_poke(AWE_PEFE(i), 0);  /* mod envelope height to zero */
	awe_poke(AWE_LFO1VAL(i), 0x8000); /* no delay for LFO1 */
	awe_poke(AWE_LFO2VAL(i), 0x8000);
	awe_poke(AWE_IP(i), 0xE000);	/* no pitch shift */
	awe_poke(AWE_IFATN(i), 0xFF00);	/* volume to minimum */
	awe_poke(AWE_FMMOD(i), 0);
	awe_poke(AWE_TREMFRQ(i), 0);
	awe_poke(AWE_FM2FRQ2(i), 0);
}

static void
awe_tweak(void)
{
	int i;
	/* reset all channels */
	for (i = 0; i < awe_max_voices; i++)
		awe_tweak_voice(i);
}


/*
 *  initializes the FM section of AWE32;
 *   see Vince Vu's unofficial AWE32 programming guide
 */

static void
awe_init_fm(void)
{
#ifndef AWE_ALWAYS_INIT_FM
	/* if no extended memory is on board.. */
	if (awe_mem_size <= 0)
		return;
#endif
	DEBUG(3,printk("AWE32: initializing FM\n"));

	/* Initialize the last two channels for DRAM refresh and producing
	   the reverb and chorus effects for Yamaha OPL-3 synthesizer */

	/* 31: FM left channel, 0xffffe0-0xffffe8 */
	awe_poke(AWE_DCYSUSV(30), 0x80);
	awe_poke_dw(AWE_PSST(30), 0xFFFFFFE0); /* full left */
	awe_poke_dw(AWE_CSL(30), 0x00FFFFE8 |
		    (DEF_FM_CHORUS_DEPTH << 24));
	awe_poke_dw(AWE_PTRX(30), (DEF_FM_REVERB_DEPTH << 8));
	awe_poke_dw(AWE_CPF(30), 0);
	awe_poke_dw(AWE_CCCA(30), 0x00FFFFE3);

	/* 32: FM right channel, 0xfffff0-0xfffff8 */
	awe_poke(AWE_DCYSUSV(31), 0x80);
	awe_poke_dw(AWE_PSST(31), 0x00FFFFF0); /* full right */
	awe_poke_dw(AWE_CSL(31), 0x00FFFFF8 |
		    (DEF_FM_CHORUS_DEPTH << 24));
	awe_poke_dw(AWE_PTRX(31), (DEF_FM_REVERB_DEPTH << 8));
	awe_poke_dw(AWE_CPF(31), 0);
	awe_poke_dw(AWE_CCCA(31), 0x00FFFFF3);

	/* skew volume & cutoff */
	awe_poke_dw(AWE_VTFT(30), 0x8000FFFF);
	awe_poke_dw(AWE_VTFT(31), 0x8000FFFF);

	/* change maximum channels to 30 */
	awe_max_voices = AWE_NORMAL_VOICES;
	awe_info.nr_voices = awe_max_voices;
	voice_alloc->max_voice = awe_max_voices;
}

/*
 *  AWE32 DRAM access routines
 */

/* open DRAM write accessing mode */
static int
awe_open_dram_for_write(int offset)
{
	int i;

	/* use all channels for DMA transfer */
	for (i = 0; i < AWE_NORMAL_VOICES; i++) {
		awe_poke(AWE_DCYSUSV(i), 0x80);
		awe_poke_dw(AWE_VTFT(i), 0);
		awe_poke_dw(AWE_CVCF(i), 0);
		awe_poke_dw(AWE_PTRX(i), 0x40000000);
		awe_poke_dw(AWE_CPF(i), 0x40000000);
		awe_poke_dw(AWE_PSST(i), 0);
		awe_poke_dw(AWE_CSL(i), 0);
		awe_poke_dw(AWE_CCCA(i), 0x06000000);
	}
	/* point channels 31 & 32 to ROM samples for DRAM refresh */
	awe_poke_dw(AWE_VTFT(30), 0);
	awe_poke_dw(AWE_PSST(30), 0x1d8);
	awe_poke_dw(AWE_CSL(30), 0x1e0);
	awe_poke_dw(AWE_CCCA(30), 0x1d8);
	awe_poke_dw(AWE_VTFT(31), 0);
	awe_poke_dw(AWE_PSST(31), 0x1d8);
	awe_poke_dw(AWE_CSL(31), 0x1e0);
	awe_poke_dw(AWE_CCCA(31), 0x1d8);

	/* if full bit is on, not ready to write on */
	if (awe_peek_dw(AWE_SMALW) & 0x80000000) {
		for (i = 0; i < AWE_NORMAL_VOICES; i++)
			awe_poke_dw(AWE_CCCA(i), 0);
		return RET_ERROR(ENOSPC);
	}

	/* set address to write */
	awe_poke_dw(AWE_SMALW, offset + AWE_DRAM_OFFSET);

	return 0;
}

/* open DRAM for RAM size detection */
static void
awe_open_dram_for_check(void)
{
	int i;
	for (i = 0; i < AWE_NORMAL_VOICES; i++) {
		awe_poke(AWE_DCYSUSV(i), 0x80);
		awe_poke_dw(AWE_VTFT(i), 0);
		awe_poke_dw(AWE_CVCF(i), 0);
		awe_poke_dw(AWE_PTRX(i), 0x40000000);
		awe_poke_dw(AWE_CPF(i), 0x40000000);
		awe_poke_dw(AWE_PSST(i), 0);
		awe_poke_dw(AWE_CSL(i), 0);
		if (i & 1) /* DMA write */
			awe_poke_dw(AWE_CCCA(i), 0x06000000);
		else	   /* DMA read */
			awe_poke_dw(AWE_CCCA(i), 0x04000000);
	}
}


/* close dram access */
static void
awe_close_dram(void)
{
	int i;
	/* wait until FULL bit in SMAxW register be false */
	for (i = 0; i < 10000; i++) {
		if (!(awe_peek_dw(AWE_SMALW) & 0x80000000))
			break;
		awe_wait(10);
	}

	for (i = 0; i < AWE_NORMAL_VOICES; i++) {
		awe_poke_dw(AWE_CCCA(i), 0);
		awe_poke(AWE_DCYSUSV(i), 0x807F);
	}
}


#ifdef AWE_CHECKSUM_MEMORY
/* open DRAM read accessing mode */
static int
awe_open_dram_for_read(int offset)
{
	int i;

	/* use all channels for DMA transfer */
	for (i = 0; i < AWE_NORMAL_VOICES; i++) {
		awe_poke(AWE_DCYSUSV(i), 0x80);
		awe_poke_dw(AWE_VTFT(i), 0);
		awe_poke_dw(AWE_CVCF(i), 0);
		awe_poke_dw(AWE_PTRX(i), 0x40000000);
		awe_poke_dw(AWE_CPF(i), 0x40000000);
		awe_poke_dw(AWE_PSST(i), 0);
		awe_poke_dw(AWE_CSL(i), 0);
		awe_poke_dw(AWE_CCCA(i), 0x04000000);
	}
	/* point channels 31 & 32 to ROM samples for DRAM refresh */
	awe_poke_dw(AWE_VTFT(30), 0);
	awe_poke_dw(AWE_PSST(30), 0x1d8);
	awe_poke_dw(AWE_CSL(30), 0x1e0);
	awe_poke_dw(AWE_CCCA(30), 0x1d8);
	awe_poke_dw(AWE_VTFT(31), 0);
	awe_poke_dw(AWE_PSST(31), 0x1d8);
	awe_poke_dw(AWE_CSL(31), 0x1e0);
	awe_poke_dw(AWE_CCCA(31), 0x1d8);

	/* if empty flag is on, not ready to read */
	if (awe_peek_dw(AWE_SMALR) & 0x80000000) {
		for (i = 0; i < AWE_NORMAL_VOICES; i++)
			awe_poke_dw(AWE_CCCA(i), 0);
		return RET_ERROR(ENOSPC);
	}

	/* set address to read */
	awe_poke_dw(AWE_SMALR, offset + AWE_DRAM_OFFSET);
	/* drop stale data */
	awe_peek(AWE_SMLD);
	return 0;
}

/* close dram access for read */
static void
awe_close_dram_for_read(void)
{
	int i;
	/* wait until FULL bit in SMAxW register be false */
	for (i = 0; i < 10000; i++) {
		if (!(awe_peek_dw(AWE_SMALR) & 0x80000000))
			break;
		awe_wait(10);
	}
	for (i = 0; i < AWE_NORMAL_VOICES; i++) {
		awe_poke_dw(AWE_CCCA(i), 0);
		awe_poke(AWE_DCYSUSV(i), 0x807F);
	}
}
#endif /* AWE_CHECKSUM_MEMORY */


/* write a word data */
static void
awe_write_dram(unsigned short c)
{
	awe_poke(AWE_SMLD, c);
}

/*================================================================
 * detect presence of AWE32 and check memory size
 *================================================================*/

/* detect emu8000 chip on the specified address; from VV's guide */

static int
awe_detect_base(int addr)
{
	awe_base = addr;
	if ((awe_peek(AWE_U1) & 0x000F) != 0x000C)
		return 0;
	if ((awe_peek(AWE_HWCF1) & 0x007E) != 0x0058)
		return 0;
	if ((awe_peek(AWE_HWCF2) & 0x0003) != 0x0003)
		return 0;
        DEBUG(0,printk("AWE32 found at %x\n", awe_base));
	return 1;
}
	
static int
awe_detect(void)
{
	int base;
#ifdef AWE_DEFAULT_BASE_ADDR
	if (awe_detect_base(AWE_DEFAULT_BASE_ADDR))
		return 1;
#endif
	if (awe_base == 0) {
		for (base = 0x620; base <= 0x680; base += 0x20)
			if (awe_detect_base(base))
				return 1;
	}
	FATALERR(printk("AWE32 not found\n"));
	awe_base = 0;
	return 0;
}


/*================================================================
 * check dram size on AWE board
 *================================================================*/

/* any three numbers you like */
#define UNIQUE_ID1	0x1234
#define UNIQUE_ID2	0x4321
#define UNIQUE_ID3	0xFFFF

static int
awe_check_dram(void)
{
	awe_open_dram_for_check();

	/* set up unique two id numbers */
	awe_poke_dw(AWE_SMALW, AWE_DRAM_OFFSET);
	awe_poke(AWE_SMLD, UNIQUE_ID1);
	awe_poke(AWE_SMLD, UNIQUE_ID2);

	awe_mem_size = 0;
	while (awe_mem_size < AWE_MAX_DRAM_SIZE) {
		awe_wait(2);
		/* read a data on the DRAM start address */
		awe_poke_dw(AWE_SMALR, AWE_DRAM_OFFSET);
		awe_peek(AWE_SMLD); /* discard stale data  */
		if (awe_peek(AWE_SMLD) != UNIQUE_ID1)
			break;
		if (awe_peek(AWE_SMLD) != UNIQUE_ID2)
			break;
		awe_mem_size += 32;  /* increment 32 Kbytes */
		/* Write a unique data on the test address;
		 * if the address is out of range, the data is written on
		 * 0x200000(=AWE_DRAM_OFFSET).  Then the two id words are
		 * broken by this data.
		 */
		awe_poke_dw(AWE_SMALW, AWE_DRAM_OFFSET + awe_mem_size*512L);
		awe_poke(AWE_SMLD, UNIQUE_ID3);
	}
	awe_close_dram();

	DEBUG(0,printk("AWE32: %d Kbytes memory detected\n", (int)awe_mem_size));
#ifdef AWE_DEFAULT_MEM_SIZE
	if (awe_mem_size == 0)
		awe_mem_size = AWE_DEFAULT_MEM_SIZE;
#endif
	/* convert to Kbytes */
	awe_mem_size *= 1024;
	return awe_mem_size;
}


/*================================================================
 * chorus and reverb controls; from VV's guide
 *================================================================*/

/* 5 parameters for each chorus mode; 3 x 16bit, 2 x 32bit */
static unsigned short chorus_parm[8][5] = {
	{0xE600, 0x03F6, 0xBC2C ,0x0000, 0x006D}, /* chorus 1 */
	{0xE608, 0x031A, 0xBC6E, 0x0000, 0x017C}, /* chorus 2 */
	{0xE610, 0x031A, 0xBC84, 0x0000, 0x0083}, /* chorus 3 */
	{0xE620, 0x0269, 0xBC6E, 0x0000, 0x017C}, /* chorus 4 */
	{0xE680, 0x04D3, 0xBCA6, 0x0000, 0x005B}, /* feedback */
	{0xE6E0, 0x044E, 0xBC37, 0x0000, 0x0026}, /* flanger */
	{0xE600, 0x0B06, 0xBC00, 0xE000, 0x0083}, /* short delay */
	{0xE6C0, 0x0B06, 0xBC00, 0xE000, 0x0083}, /* short delay + feedback */
};

static void awe_set_chorus_mode(int effect)
{
	awe_poke(AWE_INIT3(9), chorus_parm[effect][0]);
	awe_poke(AWE_INIT3(12), chorus_parm[effect][1]);
	awe_poke(AWE_INIT4(3), chorus_parm[effect][2]);
	awe_poke_dw(AWE_HWCF4, (unsigned long)chorus_parm[effect][3]);
	awe_poke_dw(AWE_HWCF5, (unsigned long)chorus_parm[effect][4]);
	awe_poke_dw(AWE_HWCF6, 0x8000);
	awe_poke_dw(AWE_HWCF7, 0x0000);
}

/*----------------------------------------------------------------*/

/* reverb mode settings; write the following 28 data of 16 bit length
 *   on the corresponding ports in the reverb_cmds array
 */
static unsigned short reverb_parm[8][28] = {
{  /* room 1 */
	0xB488, 0xA450, 0x9550, 0x84B5, 0x383A, 0x3EB5, 0x72F4,
	0x72A4, 0x7254, 0x7204, 0x7204, 0x7204, 0x4416, 0x4516,
	0xA490, 0xA590, 0x842A, 0x852A, 0x842A, 0x852A, 0x8429,
	0x8529, 0x8429, 0x8529, 0x8428, 0x8528, 0x8428, 0x8528,
},
{  /* room 2 */
	0xB488, 0xA458, 0x9558, 0x84B5, 0x383A, 0x3EB5, 0x7284,
	0x7254, 0x7224, 0x7224, 0x7254, 0x7284, 0x4448, 0x4548,
	0xA440, 0xA540, 0x842A, 0x852A, 0x842A, 0x852A, 0x8429,
	0x8529, 0x8429, 0x8529, 0x8428, 0x8528, 0x8428, 0x8528,
},
{  /* room 3 */
	0xB488, 0xA460, 0x9560, 0x84B5, 0x383A, 0x3EB5, 0x7284,
	0x7254, 0x7224, 0x7224, 0x7254, 0x7284, 0x4416, 0x4516,
	0xA490, 0xA590, 0x842C, 0x852C, 0x842C, 0x852C, 0x842B,
	0x852B, 0x842B, 0x852B, 0x842A, 0x852A, 0x842A, 0x852A,
},
{  /* hall 1 */
	0xB488, 0xA470, 0x9570, 0x84B5, 0x383A, 0x3EB5, 0x7284,
	0x7254, 0x7224, 0x7224, 0x7254, 0x7284, 0x4448, 0x4548,
	0xA440, 0xA540, 0x842B, 0x852B, 0x842B, 0x852B, 0x842A,
	0x852A, 0x842A, 0x852A, 0x8429, 0x8529, 0x8429, 0x8529,
},
{  /* hall 2 */
	0xB488, 0xA470, 0x9570, 0x84B5, 0x383A, 0x3EB5, 0x7254,
	0x7234, 0x7224, 0x7254, 0x7264, 0x7294, 0x44C3, 0x45C3,
	0xA404, 0xA504, 0x842A, 0x852A, 0x842A, 0x852A, 0x8429,
	0x8529, 0x8429, 0x8529, 0x8428, 0x8528, 0x8428, 0x8528,
},
{  /* plate */
	0xB4FF, 0xA470, 0x9570, 0x84B5, 0x383A, 0x3EB5, 0x7234,
	0x7234, 0x7234, 0x7234, 0x7234, 0x7234, 0x4448, 0x4548,
	0xA440, 0xA540, 0x842A, 0x852A, 0x842A, 0x852A, 0x8429,
	0x8529, 0x8429, 0x8529, 0x8428, 0x8528, 0x8428, 0x8528,
},
{  /* delay */
	0xB4FF, 0xA470, 0x9500, 0x84B5, 0x333A, 0x39B5, 0x7204,
	0x7204, 0x7204, 0x7204, 0x7204, 0x72F4, 0x4400, 0x4500,
	0xA4FF, 0xA5FF, 0x8420, 0x8520, 0x8420, 0x8520, 0x8420,
	0x8520, 0x8420, 0x8520, 0x8420, 0x8520, 0x8420, 0x8520,
},
{  /* panning delay */
	0xB4FF, 0xA490, 0x9590, 0x8474, 0x333A, 0x39B5, 0x7204,
	0x7204, 0x7204, 0x7204, 0x7204, 0x72F4, 0x4400, 0x4500,
	0xA4FF, 0xA5FF, 0x8420, 0x8520, 0x8420, 0x8520, 0x8420,
	0x8520, 0x8420, 0x8520, 0x8420, 0x8520, 0x8420, 0x8520,
},
};

static struct ReverbCmdPair {
	unsigned short cmd, port;
} reverb_cmds[28] = {
  {AWE_INIT1(0x03)}, {AWE_INIT1(0x05)}, {AWE_INIT4(0x1F)}, {AWE_INIT1(0x07)},
  {AWE_INIT2(0x14)}, {AWE_INIT2(0x16)}, {AWE_INIT1(0x0F)}, {AWE_INIT1(0x17)},
  {AWE_INIT1(0x1F)}, {AWE_INIT2(0x07)}, {AWE_INIT2(0x0F)}, {AWE_INIT2(0x17)},
  {AWE_INIT2(0x1D)}, {AWE_INIT2(0x1F)}, {AWE_INIT3(0x01)}, {AWE_INIT3(0x03)},
  {AWE_INIT1(0x09)}, {AWE_INIT1(0x0B)}, {AWE_INIT1(0x11)}, {AWE_INIT1(0x13)},
  {AWE_INIT1(0x19)}, {AWE_INIT1(0x1B)}, {AWE_INIT2(0x01)}, {AWE_INIT2(0x03)},
  {AWE_INIT2(0x09)}, {AWE_INIT2(0x0B)}, {AWE_INIT2(0x11)}, {AWE_INIT2(0x13)},
};


static void awe_set_reverb_mode(int effect)
{
	int i;
	for (i = 0; i < 28; i++)
		awe_poke(reverb_cmds[i].cmd, reverb_cmds[i].port,
			 reverb_parm[effect][i]);
}

#endif /* CONFIG_AWE32_SYNTH */
