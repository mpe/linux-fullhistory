/* 
 * sound/softoss.c
 *
 * Software based MIDI synthsesizer driver.
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 *
 * Thomas Sailer   : ioctl code reworked (vmalloc/vfree removed)
 */
#include <linux/config.h>
#include <linux/module.h>

/*
 * When POLLED_MODE is defined, the resampling loop is run using a timer
 * callback routine. Normally the resampling loop is executed inside
 * audio buffer interrupt handler which doesn't work with single mode DMA.
 */
#define SOFTSYN_MAIN
#undef  POLLED_MODE
#define HANDLE_LFO

#define ENVELOPE_SCALE		8
#define NO_SAMPLE		0xffff

#include "sound_config.h"
#include "soundmodule.h"

#ifdef CONFIG_SOFTOSS
#include "softoss.h"
#include <linux/ultrasound.h>

int softsynth_disabled = 0;

static volatile int intr_pending = 0;

#ifdef POLLED_MODE

static struct timer_list poll_timer = {
	NULL, NULL, 0, 0, softsyn_poll
};

#else
#endif

#ifdef HANDLE_LFO
/*
 * LFO table. Playback at 128 Hz gives 1 Hz LFO frequency.
 */
static int      tremolo_table[128] =
{
	0, 39, 158, 355, 630, 982, 1411, 1915,
	2494, 3146, 3869, 4662, 5522, 6448, 7438, 8489,
	9598, 10762, 11980, 13248, 14563, 15922, 17321, 18758,
	20228, 21729, 23256, 24806, 26375, 27960, 29556, 31160,
	32768, 34376, 35980, 37576, 39161, 40730, 42280, 43807,
	45308, 46778, 48215, 49614, 50973, 52288, 53556, 54774,
	55938, 57047, 58098, 59088, 60014, 60874, 61667, 62390,
	63042, 63621, 64125, 64554, 64906, 65181, 65378, 65497,
	65536, 65497, 65378, 65181, 64906, 64554, 64125, 63621,
	63042, 62390, 61667, 60874, 60014, 59087, 58098, 57047,
	55938, 54774, 53556, 52288, 50973, 49614, 48215, 46778,
	45308, 43807, 42280, 40730, 39161, 37576, 35980, 34376,
	32768, 31160, 29556, 27960, 26375, 24806, 23256, 21729,
	20228, 18758, 17321, 15922, 14563, 13248, 11980, 10762,
	9598, 8489, 7438, 6448, 5522, 4662, 3869, 3146,
	2494, 1915, 1411, 982, 630, 355, 158, 39
};

static int      vibrato_table[128] =
{
	0, 1608, 3212, 4808, 6393, 7962, 9512, 11039,
	12540, 14010, 15447, 16846, 18205, 19520, 20788, 22006,
	23170, 24279, 25330, 26320, 27246, 28106, 28899, 29622,
	30274, 30853, 31357, 31786, 32138, 32413, 32610, 32729,
	32768, 32729, 32610, 32413, 32138, 31786, 31357, 30853,
	30274, 29622, 28899, 28106, 27246, 26320, 25330, 24279,
	23170, 22006, 20788, 19520, 18205, 16846, 15447, 14010,
	12540, 11039, 9512, 7962, 6393, 4808, 3212, 1608,
	0, -1608, -3212, -4808, -6393, -7962, -9512, -11039,
	-12540, -14010, -15447, -16846, -18205, -19520, -20788, -22006,
	-23170, -24279, -25330, -26320, -27246, -28106, -28899, -29622,
	-30274, -30853, -31357, -31786, -32138, -32413, -32610, -32729,
	-32768, -32729, -32610, -32413, -32138, -31786, -31357, -30853,
	-30274, -29622, -28899, -28106, -27246, -26320, -25330, -24279,
	-23170, -22006, -20788, -19520, -18205, -16846, -15447, -14010,
	-12540, -11039, -9512, -7962, -6393, -4808, -3212, -1608
};

#endif

static unsigned long last_resample_jiffies;
static unsigned long resample_counter;

extern int     *sound_osp;

static volatile int is_running = 0;
static int      softsynth_loaded = 0;

static struct synth_info softsyn_info = {
	"SoftOSS", 0, SYNTH_TYPE_SAMPLE, SAMPLE_TYPE_GUS, 0, 16, 0, MAX_PATCH
};

static struct softsyn_devc sdev_info = {
	0
};

softsyn_devc   *devc = &sdev_info;	/* used in softoss_rs.c */

static struct voice_alloc_info *voice_alloc;

static int      softsyn_open(int synthdev, int mode);
static void     init_voice(softsyn_devc * devc, int voice);
static void     compute_step(int voice);

static volatile int tmr_running = 0;
static int      voice_limit = 24;


static void set_max_voices(int nr)
{
	int i;

	if (nr < 4)
		nr = 4;

	if (nr > voice_limit)
		nr = voice_limit;

	voice_alloc->max_voice = devc->maxvoice = nr;
	devc->afterscale = 5;

	for (i = 31; i > 0; i--)
		if (nr & (1 << i))
		{
			devc->afterscale = i + 1;
			return;
		}
}

static void update_vibrato(int voice)
{
	voice_info *v = &softoss_voices[voice];

#ifdef HANDLE_LFO
	int x;

	x = vibrato_table[v->vibrato_phase >> 8];
	v->vibrato_phase = (v->vibrato_phase + v->vibrato_step) & 0x7fff;

	x = (x * v->vibrato_depth) >> 15;
	v->vibrato_level = (x * 600) >> 8;

	compute_step(voice);
#else
	v->vibrato_level = 0;
#endif
}

#ifdef HANDLE_LFO
static void update_tremolo(int voice)
{
	voice_info *v = &softoss_voices[voice];
	int x;

	x = tremolo_table[v->tremolo_phase >> 8];
	v->tremolo_phase = (v->tremolo_phase + v->tremolo_step) & 0x7fff;

	v->tremolo_level = (x * v->tremolo_depth) >> 20;
}
#endif

static void start_vibrato(int voice)
{
	voice_info *v = &softoss_voices[voice];
	int rate;

	if (!v->vibrato_depth)
		return;

	rate = v->vibrato_rate * 6 * 128;
	v->vibrato_step = (rate * devc->control_rate) / devc->speed;

	devc->vibratomap |= (1 << voice);	/* Enable vibrato */
}

static void start_tremolo(int voice)
{
	voice_info *v = &softoss_voices[voice];
	int rate;

	if (!v->tremolo_depth)
		return;

	rate = v->tremolo_rate * 6 * 128;
	v->tremolo_step = (rate * devc->control_rate) / devc->speed;

	devc->tremolomap |= (1 << voice);	/* Enable tremolo */
}

static void update_volume(int voice)
{
	voice_info *v = &softoss_voices[voice];
	unsigned int vol;

	/*
	 *	Compute plain volume
	 */

	vol = (v->velocity * v->expression_vol * v->main_vol) >> 12;

#ifdef HANDLE_LFO
	/*
	 *	Handle LFO
	 */

	if (devc->tremolomap & (1 << voice))
	{
		int t;

		t = 32768 - v->tremolo_level;
		vol = (vol * t) >> 15;
		update_tremolo(voice);
	}
#endif
	/*
	 *	Envelope
	 */
	 
	if (v->mode & WAVE_ENVELOPES && !v->percussive_voice)
		vol = (vol * (v->envelope_vol >> 16)) >> 19;
	else
		vol >>= 4;

	/*
	 * Handle panning
	 */

	if (v->panning < 0)	/* Pan left */
		v->rightvol = (vol * (128 + v->panning)) / 128;
	else
		v->rightvol = vol;

	if (v->panning > 0)	/* Pan right */
		v->leftvol = (vol * (128 - v->panning)) / 128;
	else
		v->leftvol = vol;
}

static void step_envelope(int voice, int do_release, int velocity)
{
	voice_info *v = &softoss_voices[voice];
	int r, rate, time, dif;
	unsigned int vol;
	unsigned long flags;

	save_flags(flags);
	cli();

	if (!voice_active[voice] || v->sample == NULL)
	{
		restore_flags(flags);
		return;
	}
	if (!do_release)
	{
		if (v->mode & WAVE_SUSTAIN_ON && v->envelope_phase == 2)
		{	
			/* Stop envelope until note off */
			v->envelope_volstep = 0;
			v->envelope_time = 0x7fffffff;
			if (v->mode & WAVE_VIBRATO)
				start_vibrato(voice);
			if (v->mode & WAVE_TREMOLO)
				start_tremolo(voice);
			restore_flags(flags);
			return;
		}
	}
	if (do_release)
		v->envelope_phase = 3;
	else
		v->envelope_phase++;

	if (v->envelope_phase >= 5)	/* Finished */
	{
		init_voice(devc, voice);
		restore_flags(flags);
		return;
	}
	vol = v->envelope_target = v->sample->env_offset[v->envelope_phase] << 22;


	rate = v->sample->env_rate[v->envelope_phase];
	r = 3 - ((rate >> 6) & 0x3);
	r *= 3;
	r = (int) (rate & 0x3f) << r;
	rate = (((r * 44100) / devc->speed) * devc->control_rate) << 8;

	if (rate < (1 << 20))	/* Avoid infinitely "releasing" voices */
		rate = 1 << 20;

	dif = (v->envelope_vol - vol);
	if (dif < 0)
		dif *= -1;
	if (dif < rate * 2)	/* Too close */
	{
		step_envelope(voice, 0, 60);
		restore_flags(flags);
		return;
	}

	if (vol > v->envelope_vol)
	{
		v->envelope_volstep = rate;
		time = (vol - v->envelope_vol) / rate;
	}
	else
	{
		v->envelope_volstep = -rate;
		time = (v->envelope_vol - vol) / rate;
	}

	time--;
	if (time <= 0)
		time = 1;
	v->envelope_time = time;
	restore_flags(flags);
}

static void step_envelope_lfo(int voice)
{
	voice_info *v = &softoss_voices[voice];

	/*
	 * Update pitch (vibrato) LFO 
	 */

	if (devc->vibratomap & (1 << voice))
		update_vibrato(voice);

	/* 
	 * Update envelope
	 */

	if (v->mode & WAVE_ENVELOPES)
	{
		v->envelope_vol += v->envelope_volstep;
		/* Overshoot protection */
		if (v->envelope_vol < 0)
		{
			v->envelope_vol = v->envelope_target;
			v->envelope_volstep = 0;
		}
		if (v->envelope_time-- <= 0)
		{
			v->envelope_vol = v->envelope_target;
			step_envelope(voice, 0, 60);
		}
	}
}

static void compute_step(int voice)
{
	voice_info *v = &softoss_voices[voice];

	/*
	 *	Since the pitch bender may have been set before playing the note, we
	 *	have to calculate the bending now.
	 */

	v->current_freq = compute_finetune(v->orig_freq,
					   v->bender,
					   v->bender_range,
					   v->vibrato_level);
	v->step = (((v->current_freq << 9) + (devc->speed >> 1)) / devc->speed);

	if (v->mode & WAVE_LOOP_BACK)
		v->step *= -1;	/* Reversed playback */
}

static void init_voice(softsyn_devc * devc, int voice)
{
	voice_info *v = &softoss_voices[voice];
	unsigned long flags;

	save_flags(flags);
	cli();
	voice_active[voice] = 0;
	devc->vibratomap &= ~(1 << voice);
	devc->tremolomap &= ~(1 << voice);
	v->mode = 0;
	v->wave = NULL;
	v->sample = NULL;
	v->ptr = 0;
	v->step = 0;
	v->startloop = 0;
	v->startbackloop = 0;
	v->endloop = 0;
	v->looplen = 0;
	v->bender = 0;
	v->bender_range = 200;
	v->panning = 0;
	v->main_vol = 127;
	v->expression_vol = 127;
	v->patch_vol = 127;
	v->percussive_voice = 0;
	v->sustain_mode = 0;
	v->envelope_phase = 1;
	v->envelope_vol = 1 << 24;
	v->envelope_volstep = 256;
	v->envelope_time = 0;
	v->vibrato_phase = 0;
	v->vibrato_step = 0;
	v->vibrato_level = 0;
	v->vibrato_rate = 0;
	v->vibrato_depth = 0;
	v->tremolo_phase = 0;
	v->tremolo_step = 0;
	v->tremolo_level = 0;
	v->tremolo_rate = 0;
	v->tremolo_depth = 0;
	voice_alloc->map[voice] = 0;
	voice_alloc->alloc_times[voice] = 0;
	restore_flags(flags);
}

static void reset_samples(softsyn_devc * devc)
{
	int i;

	for (i = 0; i < MAX_VOICE; i++)
		voice_active[i] = 0;
	for (i = 0; i < devc->maxvoice; i++)
	{
		init_voice(devc, i);
		softoss_voices[i].instr = 0;
	}

	devc->ram_used = 0;

	for (i = 0; i < MAX_PATCH; i++)
		devc->programs[i] = NO_SAMPLE;

	for (i = 0; i < devc->nrsamples; i++)
	{
		vfree(devc->samples[i]);
		vfree(devc->wave[i]);
		devc->samples[i] = NULL;
		devc->wave[i] = NULL;
	}
	devc->nrsamples = 0;
}

static void init_engine(softsyn_devc * devc)
{
	int i, fz, srate, sz = devc->channels;

	set_max_voices(devc->default_max_voices);
	voice_alloc->timestamp = 0;

	if (devc->bits == 16)
		sz *= 2;

	fz = devc->fragsize / sz;	/* Samples per fragment */
	devc->samples_per_fragment = fz;

	devc->usecs = 0;
	devc->usecs_per_frag = (1000000 * fz) / devc->speed;

	for (i = 0; i < devc->maxvoice; i++)
	{
		init_voice(devc, i);
		softoss_voices[i].instr = 0;
	}
	devc->engine_state = ES_STOPPED;

	/*
	 *    Initialize delay
	 */

	for (i = 0; i < DELAY_SIZE; i++)
		left_delay[i] = right_delay[i] = 0;
	delayp = 0;
	srate = (devc->speed / 10000);	/* 1 to 4 */
	if (srate <= 0)
		srate = 1;
	devc->delay_size = (DELAY_SIZE * srate) / 4;
	if (devc->delay_size == 0 || devc->delay_size > DELAY_SIZE)
		devc->delay_size = DELAY_SIZE;
}

void softsyn_control_loop(void)
{
	int voice;

	/*
	 *    Recompute envlope, LFO, etc.
	 */
	for (voice = 0; voice < devc->maxvoice; voice++)
	{
		if (voice_active[voice])
		{
			update_volume(voice);
			step_envelope_lfo(voice);
		}
		else
			voice_alloc->map[voice] = 0;
	}
}

static void start_engine(softsyn_devc * devc);

static void do_resample(int dummy)
{
	struct dma_buffparms *dmap = audio_devs[devc->audiodev]->dmap_out;
	struct voice_info *vinfo;
	unsigned long   flags, jif;

	int voice, loops;
	short *buf;

	if (softsynth_disabled)
		return;

	save_flags(flags);
	cli();

	if (is_running)
	{
		printk(KERN_WARNING "SoftOSS: Playback overrun\n");
		restore_flags(flags);
		return;
	}
	jif = jiffies;
	if (jif == last_resample_jiffies)
	{
		if (resample_counter++ > 50)
		{
			for (voice = 0; voice < devc->maxvoice; voice++)
				init_voice(devc, voice);
			voice_limit--;
			resample_counter = 0;
			printk(KERN_WARNING "SoftOSS: CPU overload. Limiting # of voices to %d\n", voice_limit);

			if (voice_limit < 10)
			{
				voice_limit = 10;
				devc->speed = (devc->speed * 2) / 3;

				printk(KERN_WARNING "SoftOSS: Dropping sampling rate and stopping the device.\n");
				softsynth_disabled = 1;
			}
		}
	}
	else
	{
		last_resample_jiffies = jif;
		resample_counter = 0;
	}

	/* is_running = 1; */

	if (dmap->qlen > devc->max_playahead)
	{
		printk(KERN_WARNING "SoftOSS: audio buffers full\n");
		is_running = 0;
		restore_flags(flags);
		return;
	}
	/*
	 * First verify that all active voices are valid (do this just once per block).
	 */

	for (voice = 0; voice < devc->maxvoice; voice++)
	{
		if (voice_active[voice])
		{
			int ptr;

			vinfo = &softoss_voices[voice];
			ptr = vinfo->ptr >> 9;

			if (vinfo->wave == NULL || ptr < 0 || ptr > vinfo->sample->len)
				init_voice(devc, voice);
			else if (!(vinfo->mode & WAVE_LOOPING) && (vinfo->ptr + vinfo->step) > vinfo->endloop)
				  voice_active[voice] = 0;
		}
	}
	
	/*
	 *    Start the resampling process
	 */

	loops = devc->samples_per_fragment;
	buf = (short *) (dmap->raw_buf + (dmap->qtail * dmap->fragment_size));

	softsynth_resample_loop(buf, loops);	/* In Xsoftsynth_rs.c */

	dmap->qtail = (dmap->qtail + 1) % dmap->nbufs;
	dmap->qlen++;
	dmap->user_counter += dmap->fragment_size;

	devc->usecs += devc->usecs_per_frag;

	if (tmr_running)
		sound_timer_interrupt();
	/*
	 *    Execute timer
	 */

	if (!tmr_running)
	{
		if (devc->usecs >= devc->next_event_usecs)
		{
			devc->next_event_usecs = ~0;
			sequencer_timer(0);
		}
	}
	
	is_running = 0;
	restore_flags(flags);
}

static void delayed_resample(int dummy)
{
	struct dma_buffparms *dmap = audio_devs[devc->audiodev]->dmap_out;
	int n = 0;

	if (is_running)
		return;

	while (devc->engine_state != ES_STOPPED && dmap->qlen < devc->max_playahead && n++ < 2)
		do_resample(0);
	intr_pending = 0;
}

#ifdef POLLED_MODE
static void softsyn_poll(unsigned long dummy)
{
	delayed_resample(0);

	if (devc->engine_state != ES_STOPPED)
	{
		  poll_timer.expires = jiffies+1;
		  add_timer(&poll_timer);
	}
}

#else
static void softsyn_callback(int dev, int parm)
{
	delayed_resample(0);
}
#endif

static void start_engine(softsyn_devc * devc)
{
	struct dma_buffparms *dmap;
	int trig, n;
	mm_segment_t fs;

	if (!devc->audio_opened)
		if (softsyn_open(devc->synthdev, 0) < 0)
			return;

	if (devc->audiodev >= num_audiodevs)
		return;
	
	dmap = audio_devs[devc->audiodev]->dmap_out;
	
	devc->usecs = 0;
	devc->next_event_usecs = ~0;
	devc->control_rate = 64;
	devc->control_counter = 0;

	if (devc->engine_state == ES_STOPPED) 
	{
		n = trig = 0;
		fs = get_fs();
		set_fs(get_ds());
		dma_ioctl(devc->audiodev, SNDCTL_DSP_SETTRIGGER, (caddr_t)&trig);
#ifdef POLLED_MODE
		poll_timer.expires = jiffies+1;
		add_timer(&poll_timer);
		/* Start polling */
#else
		dmap->audio_callback = softsyn_callback;
		dmap->qhead = dmap->qtail = dmap->qlen = 0;
#endif
		while (dmap->qlen < devc->max_playahead && n++ < 2)
			do_resample(0);
		devc->engine_state = ES_STARTED;
		last_resample_jiffies = jiffies;
		resample_counter = 0;
		trig = PCM_ENABLE_OUTPUT;
		if (dma_ioctl(devc->audiodev, SNDCTL_DSP_SETTRIGGER, (caddr_t)&trig) < 0)
			printk(KERN_ERR "SoftOSS: Trigger failed\n");
		set_fs(fs);
	}
}

static void stop_engine(softsyn_devc * devc)
{
}

static void request_engine(softsyn_devc * devc, int ticks)
{
	if (ticks < 0)		/* Relative time */
		devc->next_event_usecs = devc->usecs - ticks * (1000000 / HZ);
	else
		devc->next_event_usecs = ticks * (1000000 / HZ);
}

/*
 * Softsync hook serves mode1 (timing) calls made by sequencer.c
 */
 
static int softsynth_hook(int cmd, int parm1, int parm2, unsigned long parm3)
{
	switch (cmd)
	{
		case SSYN_START:
			start_engine(devc);
			break;

		case SSYN_STOP:
			stop_engine(devc);
			break;

		case SSYN_REQUEST:
			request_engine(devc, parm1);
			break;

		case SSYN_GETTIME:
			return devc->usecs / (1000000 / HZ);
			break;

		default:
			printk(KERN_WARNING "SoftOSS: Unknown request %d\n", cmd);
	}
	return 0;
}

static int softsyn_ioctl(int dev, unsigned int cmd, caddr_t arg)
{
	switch (cmd) 
	{
		case SNDCTL_SYNTH_INFO:
			softsyn_info.nr_voices = devc->maxvoice;
			if (copy_to_user(arg, &softsyn_info, sizeof(softsyn_info)))
				return -EFAULT;
			return 0;

		case SNDCTL_SEQ_RESETSAMPLES:
			stop_engine(devc);
			reset_samples(devc);
			return 0;
		  
		case SNDCTL_SYNTH_MEMAVL:
			return devc->ram_size - devc->ram_used;

		default:
			return -EINVAL;
	}
}

static int softsyn_kill_note(int devno, int voice, int note, int velocity)
{
	if (voice < 0 || voice > devc->maxvoice)
		return 0;
	voice_alloc->map[voice] = 0xffff;	/* Releasing */

	if (softoss_voices[voice].sustain_mode & 1)	/* Sustain controller on */
	{
		softoss_voices[voice].sustain_mode = 3;	/* Note off pending */
		return 0;
	}
	if (velocity > 127 || softoss_voices[voice].mode & WAVE_FAST_RELEASE)
	{
		init_voice(devc, voice);	/* Mark it inactive */
		return 0;
	}
	if (softoss_voices[voice].mode & WAVE_ENVELOPES)
		step_envelope(voice, 1, velocity);	/* Enter sustain phase */
	else
		init_voice(devc, voice);	/* Mark it inactive */
	return 0;
}

static int softsyn_set_instr(int dev, int voice, int instr)
{
	if (voice < 0 || voice > devc->maxvoice)
		return 0;

	if (instr < 0 || instr > MAX_PATCH)
	{
		printk(KERN_ERR "SoftOSS: Invalid instrument number %d\n", instr);
		return 0;
	}
	softoss_voices[voice].instr = instr;
	return 0;
}

static int softsyn_start_note(int dev, int voice, int note, int volume)
{
	int instr = 0;
	int best_sample, best_delta, delta_freq, selected;
	unsigned long note_freq, freq, base_note, flags;
	voice_info *v = &softoss_voices[voice];

	struct patch_info *sample;

	if (voice < 0 || voice > devc->maxvoice)
		return 0;

	if (volume == 0)	/* Actually note off */
		softsyn_kill_note(dev, voice, note, volume);

	save_flags(flags);
	cli();

	if (note == 255)
	{			/* Just volume update */
		v->velocity = volume;
		if (voice_active[voice])
			update_volume(voice);
		restore_flags(flags);
		return 0;
	}
	voice_active[voice] = 0;	/* Stop the voice for a while */
	devc->vibratomap &= ~(1 << voice);
	devc->tremolomap &= ~(1 << voice);

	instr = v->instr;
	if (instr < 0 || instr > MAX_PATCH || devc->programs[instr] == NO_SAMPLE)
	{
		printk(KERN_WARNING "SoftOSS: Undefined MIDI instrument %d\n", instr);
		restore_flags(flags);
		return 0;
	}
	instr = devc->programs[instr];

	if (instr < 0 || instr >= devc->nrsamples)
	{
		printk(KERN_WARNING "SoftOSS: Corrupted MIDI instrument %d (%d)\n", v->instr, instr);
		restore_flags(flags);
		return 0;
	}
	note_freq = note_to_freq(note);

	selected = -1;

	best_sample = instr;
	best_delta = 1000000;

	while (instr != NO_SAMPLE && instr >= 0 && selected == -1)
	{
		delta_freq = note_freq - devc->samples[instr]->base_note;

		if (delta_freq < 0)
			delta_freq = -delta_freq;
		if (delta_freq < best_delta)
		{
			best_sample = instr;
			best_delta = delta_freq;
		}
		if (devc->samples[instr]->low_note <= note_freq &&
			note_freq <= devc->samples[instr]->high_note)
		{
			selected = instr;
		}
		else instr = devc->samples[instr]->key;	/* Link to next sample */

		if (instr < 0 || instr >= devc->nrsamples)
			instr = NO_SAMPLE;
	}

	if (selected == -1)
		instr = best_sample;
	else
		instr = selected;

	if (instr < 0 || instr == NO_SAMPLE || instr > devc->nrsamples)
	{
		printk(KERN_WARNING "SoftOSS: Unresolved MIDI instrument %d\n", v->instr);
		restore_flags(flags);
		return 0;
	}
	sample = devc->samples[instr];
	v->sample = sample;

	if (v->percussive_voice)	/* No key tracking */
		  v->orig_freq = sample->base_freq;	/* Fixed pitch */
	else
	{
		base_note = sample->base_note / 100;
		note_freq /= 100;

		freq = sample->base_freq * note_freq / base_note;
		v->orig_freq = freq;
	}

	if (!(sample->mode & WAVE_LOOPING))
		sample->loop_end = sample->len;

	v->wave = devc->wave[instr];
	if (volume < 0)
		volume = 0;
	else if (volume > 127)
		volume = 127;
	v->ptr = 0;
	v->startloop = sample->loop_start * 512;
	v->startbackloop = 0;
	v->endloop = sample->loop_end * 512;
	v->looplen = (sample->loop_end - sample->loop_start) * 512;
	v->leftvol = 64;
	v->rightvol = 64;
	v->patch_vol = sample->volume;
	v->velocity = volume;
	v->mode = sample->mode;
	v->vibrato_phase = 0;
	v->vibrato_step = 0;
	v->vibrato_level = 0;
	v->vibrato_rate = 0;
	v->vibrato_depth = 0;
	v->tremolo_phase = 0;
	v->tremolo_step = 0;
	v->tremolo_level = 0;
	v->tremolo_rate = 0;
	v->tremolo_depth = 0;

	if (!(v->mode & WAVE_LOOPING))
		v->mode &= ~(WAVE_BIDIR_LOOP | WAVE_LOOP_BACK);
	else if (v->mode & WAVE_LOOP_BACK)
	{
		v->ptr = sample->len;
		v->startbackloop = v->startloop;
	}
	if (v->mode & WAVE_VIBRATO)
	{
		v->vibrato_rate = sample->vibrato_rate;
		v->vibrato_depth = sample->vibrato_depth;
	}
	if (v->mode & WAVE_TREMOLO)
	{
		v->tremolo_rate = sample->tremolo_rate;
		v->tremolo_depth = sample->tremolo_depth;
	}
	if (v->mode & WAVE_ENVELOPES)
	{
		v->envelope_phase = -1;
		v->envelope_vol = 0;
		step_envelope(voice, 0, 60);
	}
	update_volume(voice);
	compute_step(voice);

	voice_active[voice] = 1;	/* Mark it active */
	restore_flags(flags);
	return 0;
}

static int softsyn_open(int synthdev, int mode)
{
	int err;
	extern int softoss_dev;
	int frags = 0x7fff0007;	/* fragment size of 128 bytes */
	mm_segment_t fs;

	if (devc->audio_opened)	/* Already opened */
		return 0;

	softsynth_disabled = 0;
	devc->finfo.f_mode = FMODE_WRITE;
	devc->finfo.f_flags = 0;

	if (softoss_dev >= num_audiodevs)
		softoss_dev = num_audiodevs - 1;

	if (softoss_dev < 0)
		softoss_dev = 0;
	if (softoss_dev >= num_audiodevs)
		return -ENXIO;
	devc->audiodev = softoss_dev;

	if (!(audio_devs[devc->audiodev]->format_mask & AFMT_S16_LE))
	{
/*		printk(KERN_ERR "SoftOSS: The audio device doesn't support 16 bits\n"); */
		return -ENXIO;
	}
	if ((err = audio_open((devc->audiodev << 4) | SND_DEV_DSP16, &devc->finfo)) < 0)
		return err;

	devc->speed = audio_devs[devc->audiodev]->d->set_speed(
					    devc->audiodev, devc->speed);
	devc->channels = audio_devs[devc->audiodev]->d->set_channels(
					 devc->audiodev, devc->channels);
	devc->bits = audio_devs[devc->audiodev]->d->set_bits(
					     devc->audiodev, devc->bits);


	DDB(printk("SoftOSS: Using audio dev %d, speed %d, bits %d, channels %d\n", devc->audiodev, devc->speed, devc->bits, devc->channels));
	fs = get_fs();
	set_fs(get_ds());
	dma_ioctl(devc->audiodev, SNDCTL_DSP_SETFRAGMENT, (caddr_t) & frags);
	dma_ioctl(devc->audiodev, SNDCTL_DSP_GETBLKSIZE, (caddr_t) & devc->fragsize);
	set_fs(fs);

	if (devc->bits != 16 || devc->channels != 2)
	{
		audio_release((devc->audiodev << 4) | SND_DEV_DSP16, &devc->finfo);
/*		printk("SoftOSS: A 16 bit stereo sound card is required\n");*/
		return -EINVAL;
	}
	if (devc->max_playahead >= audio_devs[devc->audiodev]->dmap_out->nbufs)
		devc->max_playahead = audio_devs[devc->audiodev]->dmap_out->nbufs;

	DDB(printk("SoftOSS: Using %d fragments of %d bytes\n", devc->max_playahead, devc->fragsize));

	init_engine(devc);
	devc->audio_opened = 1;
	devc->sequencer_mode = mode;
	return 0;
}

static void softsyn_close(int synthdev)
{
	mm_segment_t fs;

	devc->engine_state = ES_STOPPED;
#ifdef POLLED_MODE
	del_timer(&poll_timer);
#endif
	fs = get_fs();
	set_fs(get_ds());
	dma_ioctl(devc->audiodev, SNDCTL_DSP_RESET, 0);
	set_fs(fs);
	if (devc->audio_opened)
		audio_release((devc->audiodev << 4) | SND_DEV_DSP16, &devc->finfo);
	devc->audio_opened = 0;
}

static void softsyn_hw_control(int dev, unsigned char *event_rec)
{
	int voice, cmd;
	unsigned short p1, p2;
	unsigned int plong;

	cmd = event_rec[2];
	voice = event_rec[3];
	p1 = *(unsigned short *) &event_rec[4];
	p2 = *(unsigned short *) &event_rec[6];
	plong = *(unsigned int *) &event_rec[4];

	switch (cmd)
	{

		case _GUS_NUMVOICES:
			set_max_voices(p1);
			break;

		default:;
	}
}

static int softsyn_load_patch(int dev, int format, const char *addr,
		   int offs, int count, int pmgr_flag)
{
	struct patch_info *patch = NULL;

	int i, p, instr;
	long sizeof_patch;
	int memlen, adj;
	unsigned short  data;
	short *wave = NULL;

	sizeof_patch = (long) &patch->data[0] - (long) patch;	/* Header size */

	if (format != GUS_PATCH)
	{
/*		printk(KERN_ERR "SoftOSS: Invalid patch format (key) 0x%x\n", format);*/
		return -EINVAL;
	}
	if (count < sizeof_patch)
	{
/*		printk(KERN_ERR "SoftOSS: Patch header too short\n");*/
		return -EINVAL;
	}
	count -= sizeof_patch;

	if (devc->nrsamples >= MAX_SAMPLE)
	{
/*		  printk(KERN_ERR "SoftOSS: Sample table full\n");*/
		  return -ENOBUFS;
	}
	
	/*
	 * Copy the header from user space but ignore the first bytes which have
	 * been transferred already.
	 */

	patch = vmalloc(sizeof(*patch));

	if (patch == NULL)
	{
/*		printk(KERN_ERR "SoftOSS: Out of memory\n");*/
		return -ENOMEM;
	}
	if(copy_from_user(&((char *) patch)[offs], &(addr)[offs], sizeof_patch - offs))
		return -EFAULT;

	if (patch->mode & WAVE_ROM)
	{
		vfree(patch);
		return -EINVAL;
	}
	instr = patch->instr_no;

	if (instr < 0 || instr > MAX_PATCH)
	{
/*		printk(KERN_ERR "SoftOSS: Invalid patch number %d\n", instr);*/
		vfree(patch);
		return -EINVAL;
	}
	if (count < patch->len)
	{
/*		printk(KERN_ERR "SoftOSS: Patch record too short (%d<%d)\n", count, (int) patch->len);*/
		patch->len = count;
	}
	if (patch->len <= 0 || patch->len > (devc->ram_size - devc->ram_used))
	{
/*		printk(KERN_ERR "SoftOSS: Invalid sample length %d\n", (int) patch->len); */
		vfree(patch);
		return -EINVAL;
	}
	if (patch->mode & WAVE_LOOPING)
	{
		if (patch->loop_start < 0 || patch->loop_start >= patch->len)
		{
/*			printk(KERN_ERR "SoftOSS: Invalid loop start %d\n", patch->loop_start);*/
			vfree(patch);
			return -EINVAL;
		}
		if (patch->loop_end < patch->loop_start || patch->loop_end > patch->len)
		{
/*			printk(KERN_ERR "SoftOSS: Invalid loop start or end point (%d, %d)\n", patch->loop_start, patch->loop_end);*/
			vfree(patch);
			return -EINVAL;
		}
	}
	/* 
	 *	Next load the wave data to memory
	 */

	memlen = patch->len;
	adj = 1;

	if (!(patch->mode & WAVE_16_BITS))
		memlen *= 2;
	else
		adj = 2;

	wave = vmalloc(memlen);

	if (wave == NULL)
	{
/*		printk(KERN_ERR "SoftOSS: Can't allocate %d bytes of mem for a sample\n", memlen);*/
		vfree(patch);
		return -ENOMEM;
	}
	p = 0;
	for (i = 0; i < memlen / 2; i++)	/* Handle words */
	{
		unsigned char tmp;
		data = 0;
		if (patch->mode & WAVE_16_BITS)
		{
			get_user(*(unsigned char *) &tmp, (unsigned char *) &((addr)[sizeof_patch + p++]));		/* Get lsb */
			data = tmp;
			get_user(*(unsigned char *) &tmp, (unsigned char *) &((addr)[sizeof_patch + p++]));		/* Get msb */
			if (patch->mode & WAVE_UNSIGNED)
				tmp ^= 0x80;	/* Convert to signed */
			data |= (tmp << 8);
		}
		else
		{
			get_user(*(unsigned char *) &tmp, (unsigned char *) &((addr)[sizeof_patch + p++]));
			if (patch->mode & WAVE_UNSIGNED)
				tmp ^= 0x80;	/* Convert to signed */
			    data = (tmp << 8);	/* Convert to 16 bits */
		}
		wave[i] = (short) data;
	}

	devc->ram_used += patch->len;

	/*
	 * Convert pointers to 16 bit indexes
	 */
	patch->len /= adj;
	patch->loop_start /= adj;
	patch->loop_end /= adj;

	/*
	 * Finally link the loaded patch to the chain
	 */

	patch->key = devc->programs[instr];
	devc->programs[instr] = devc->nrsamples;
	devc->wave[devc->nrsamples] = (short *) wave;
	devc->samples[devc->nrsamples++] = patch;

	return 0;
}

static void softsyn_panning(int dev, int voice, int pan)
{
	if (voice < 0 || voice > devc->maxvoice)
		return;

	if (pan < -128)
		pan = -128;
	if (pan > 127)
		pan = 127;

	softoss_voices[voice].panning = pan;
	if (voice_active[voice])
		update_volume(voice);
}

static void softsyn_volume_method(int dev, int mode)
{
}

static void softsyn_aftertouch(int dev, int voice, int pressure)
{
	if (voice < 0 || voice > devc->maxvoice)
		return;

	if (voice_active[voice])
		update_volume(voice);
}

static void softsyn_controller(int dev, int voice, int ctrl_num, int value)
{
	unsigned long flags;

	if (voice < 0 || voice > devc->maxvoice)
		return;
	save_flags(flags);
	cli();

	switch (ctrl_num)
	{
		case CTRL_PITCH_BENDER:
			softoss_voices[voice].bender = value;
			if (voice_active[voice])
				compute_step(voice);	/* Update pitch */
			break;


		case CTRL_PITCH_BENDER_RANGE:
			softoss_voices[voice].bender_range = value;
			break;
			
		case CTL_EXPRESSION:
			value /= 128;
		case CTRL_EXPRESSION:
			softoss_voices[voice].expression_vol = value;
			if (voice_active[voice])
				update_volume(voice);
			break;

		case CTL_PAN:
			softsyn_panning(dev, voice, (value * 2) - 128);
			break;

		case CTL_MAIN_VOLUME:
			value = (value * 100) / 16383;

		case CTRL_MAIN_VOLUME:
			softoss_voices[voice].main_vol = value;
			if (voice_active[voice])
				update_volume(voice);
			break;

		default:
			break;
	}
	restore_flags(flags);
}

static void softsyn_bender(int dev, int voice, int value)
{
	if (voice < 0 || voice > devc->maxvoice)
		return;

	softoss_voices[voice].bender = value - 8192;
	if (voice_active[voice])
		compute_step(voice);	/* Update pitch */
}

static int softsyn_alloc_voice(int dev, int chn, int note, struct voice_alloc_info *alloc)
{
	int i, p, best = -1, best_time = 0x7fffffff;

	p = alloc->ptr;

	/*
	 * First look for a completely stopped voice
	 */

	for (i = 0; i < alloc->max_voice; i++)
	{
		if (alloc->map[p] == 0)
		{
			alloc->ptr = p;
			voice_active[p] = 0;
			return p;
		}
		if (alloc->alloc_times[p] < best_time)
		{
			best = p;
			best_time = alloc->alloc_times[p];
		}
		p = (p + 1) % alloc->max_voice;
	}

	/*
	 * Then look for a releasing voice
	 */

	for (i = 0; i < alloc->max_voice; i++)
	{
		if (alloc->map[p] == 0xffff)
		{
			alloc->ptr = p;
			voice_active[p] = 0;
			return p;
		}
		p = (p + 1) % alloc->max_voice;
	}

	if (best >= 0)
		p = best;

	alloc->ptr = p;
	voice_active[p] = 0;
	return p;
}

static void softsyn_setup_voice(int dev, int voice, int chn)
{
	unsigned long flags;

	struct channel_info *info = &synth_devs[dev]->chn_info[chn];

	save_flags(flags);
	cli();

	/* init_voice(devc, voice); */
	softsyn_set_instr(dev, voice, info->pgm_num);

	softoss_voices[voice].expression_vol = info->controllers[CTL_EXPRESSION];	/* Just MSB */
	softoss_voices[voice].main_vol = (info->controllers[CTL_MAIN_VOLUME] * 100) / (unsigned) 128;
	softsyn_panning(dev, voice, (info->controllers[CTL_PAN] * 2) - 128);
	softoss_voices[voice].bender = 0;	/* info->bender_value; */
	softoss_voices[voice].bender_range = info->bender_range;

	if (chn == 9)
		softoss_voices[voice].percussive_voice = 1;
	restore_flags(flags);
}

static void softsyn_reset(int devno)
{
	int i;
	unsigned long flags;

	save_flags(flags);
	cli();

	for (i = 0; i < devc->maxvoice; i++)
		init_voice(devc, i);
	restore_flags(flags);
}

static struct synth_operations softsyn_operations =
{
	"SoftOSS",
	&softsyn_info,
	0,
	SYNTH_TYPE_SAMPLE,
	0,
	softsyn_open,
	softsyn_close,
	softsyn_ioctl,
	softsyn_kill_note,
	softsyn_start_note,
	softsyn_set_instr,
	softsyn_reset,
	softsyn_hw_control,
	softsyn_load_patch,
	softsyn_aftertouch,
	softsyn_controller,
	softsyn_panning,
	softsyn_volume_method,
	softsyn_bender,
	softsyn_alloc_voice,
	softsyn_setup_voice
};

/*
 * Timer stuff (for /dev/music).
 */

static unsigned int soft_tmr_start(int dev, unsigned int usecs)
{
	tmr_running = 1;
	start_engine(devc);
	return devc->usecs_per_frag;
}

static void soft_tmr_disable(int dev)
{
	stop_engine(devc);
	tmr_running = 0;
}

static void soft_tmr_restart(int dev)
{
	tmr_running = 1;
}

static struct sound_lowlev_timer soft_tmr =
{
	0,
	9999,
	soft_tmr_start,
	soft_tmr_disable,
	soft_tmr_restart
};

int probe_softsyn(struct address_info *hw_config)
{
	int i;

	if (softsynth_loaded)
		return 0;

	devc->ram_size = 8 * 1024 * 1024;
	devc->ram_used = 0;
	devc->nrsamples = 0;
	for (i = 0; i < MAX_PATCH; i++)
	{
		devc->programs[i] = NO_SAMPLE;
		devc->wave[i] = NULL;
	}

	devc->maxvoice = DEFAULT_VOICES;

	devc->audiodev = 0;
	devc->audio_opened = 0;
	devc->channels = 2;
	devc->bits = 16;
	devc->max_playahead = 32;

#ifdef CONFIG_SOFTOSS_RATE
	devc->speed = CONFIG_SOFTOSS_RATE;
#else
	devc->speed = 32000;
#endif

#ifdef CONFIG_SOFTOSS_VOICES
	devc->default_max_voices = CONFIG_SOFTOSS_VOICES;
#else
	devc->default_max_voices = 32;
#endif
	softsynth_loaded = 1;
	return 1;
}

void attach_softsyn_card(struct address_info *hw_config)
{
	voice_alloc = &softsyn_operations.alloc;
	synth_devs[devc->synthdev = num_synths++] = &softsyn_operations;
	sequencer_init();
	sound_timer_init(&soft_tmr, "SoftOSS");
	devc->timerdev = num_sound_timers;
	softsynthp = softsynth_hook;

#ifndef POLLED_MODE
#endif
}

void unload_softsyn(struct address_info *hw_config)
{
	if (!softsynth_loaded)
		return;
	softsynthp = NULL;
	softsynth_loaded = 0;
	reset_samples(devc);
}

#ifdef MODULE

static struct address_info config;

int init_module(void)
{
	printk(KERN_INFO "SoftOSS driver Copyright (C) by Hannu Savolainen 1993-1997\n");
	if (!probe_softsyn(&config))
		return -ENODEV;
	attach_softsyn_card(&config);
	SOUND_LOCK;
	return 0;
}

void cleanup_module(void)
{
	unload_softsyn(&config);
	sound_unload_synthdev(devc->synthdev);
	sound_unload_timerdev(devc->timerdev);
	SOUND_LOCK_END;
}
#endif
#endif
