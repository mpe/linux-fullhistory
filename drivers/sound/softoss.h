/*
 * softoss.h	- Definitions for Software MIDI Synthesizer.
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */


/*
 * Sequencer mode1 timer calls made by sequencer.c
 */
extern int (*softsynthp) (int cmd, int parm1, int parm2, unsigned long parm3);

#define SSYN_START	1
#define SSYN_REQUEST	2	/* parm1 = time */
#define SSYN_STOP	3
#define SSYN_GETTIME	4	/* Returns number of ticks since reset */

#define MAX_PATCH 256
#define MAX_SAMPLE 512
#define MAX_VOICE 32
#define DEFAULT_VOICES 16

typedef struct voice_info
{
/*
 * Don't change anything in the beginning of this struct. These fields are used
 * by the resampling loop which may have been written in assembly for some
 * architectures. Any change may make the resampling code incompatible
 */
  int instr;
  short *wave;
  struct patch_info *sample;

  unsigned int ptr; int step; /* Pointer to the wave data and pointer increment */

  int mode;
  int startloop, startbackloop, endloop, looplen;

  unsigned int leftvol, rightvol;
/***** Don't change anything above this */

  volatile unsigned long orig_freq, current_freq;
  volatile int bender, bender_range, panning;
  volatile int main_vol, expression_vol, patch_vol, velocity;

/* Envelope parameters */

  int envelope_phase;
  volatile int envelope_vol;
  volatile int envelope_volstep;
  int envelope_time; /* Number of remaining envelope steps */
  unsigned int envelope_target;
  int percussive_voice;
  int sustain_mode; /* 0=off, 1=sustain on, 2=sustain on+key released */

/*	Vibrato	*/
  int vibrato_rate;
  int vibrato_depth;
  int vibrato_phase;
  int vibrato_step;
  int vibrato_level;

/*	Tremolo	*/
  int tremolo_rate;
  int tremolo_depth;
  int tremolo_phase;
  int tremolo_step;
  int tremolo_level;
} voice_info;

extern voice_info softoss_voices[MAX_VOICE]; /* Voice spesific info */

typedef struct softsyn_devc
{
/*
 * Don't change anything in the beginning of this struct. These fields are used
 * by the resampling loop which may have been written in assembly for some
 * architectures. Any change may make the resampling code incompatible
 */
	int maxvoice;		/* # of voices to be processed */
  	int afterscale;
	int delay_size;
  	int control_rate, control_counter;
/***** Don't change anything above this */

	int ram_size;
	int ram_used;

	int synthdev;
	int timerdev;
	int sequencer_mode;
/*
 *	Audio parameters
 */

	int audiodev;
	int audio_opened;
	int speed;
	int channels;
	int bits;
	int default_max_voices;
	int max_playahead;
	struct file finfo;
	int fragsize;
	int samples_per_fragment;
	
/*
 * 	Sample storage
 */
	int nrsamples;
	struct patch_info *samples[MAX_SAMPLE];
	short *wave[MAX_SAMPLE];

/*
 * 	Programs
 */
	int programs[MAX_PATCH];

/*
 *	Timer parameters
 */
	volatile unsigned long usecs;
	volatile unsigned long usecs_per_frag;
	volatile unsigned long next_event_usecs;

/*
 * 	Engine state
 */

	volatile int engine_state;
#define ES_STOPPED			0
#define ES_STARTED			1

	/* Voice spesific bitmaps */
	volatile int tremolomap; 
	volatile int vibratomap;

} softsyn_devc;

void softsynth_resample_loop(short *buf, int loops);
extern void softsyn_control_loop(void);

#define DELAY_SIZE	4096

#ifdef SOFTSYN_MAIN
  short voice_active[MAX_VOICE] = {0};
  voice_info softoss_voices[MAX_VOICE] = {{0}}; /* Voice spesific info */
  int left_delay[DELAY_SIZE]={0}, right_delay[DELAY_SIZE]={0};
  int delayp=0;
#else
  extern softsyn_devc *devc;

  extern int left_delay[DELAY_SIZE], right_delay[DELAY_SIZE];
  extern int delayp;
  extern short voice_active[MAX_VOICE];
#endif
