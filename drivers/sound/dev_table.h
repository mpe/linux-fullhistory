/*
 *	dev_table.h
 *
 *	Global definitions for device call tables
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1996
 *
 * USS/Lite for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */


#ifndef _DEV_TABLE_H_
#define _DEV_TABLE_H_


/*
 * Sound card numbers 27 to 999. (1 to 26 are defined in soundcard.h)
 * Numbers 1000 to N are reserved for driver's internal use.
 */
#define SNDCARD_DESKPROXL		27	/* Compaq Deskpro XL */

/*
 *	NOTE! 	NOTE!	NOTE!	NOTE!
 *
 *	If you modify this file, please check the dev_table.c also.
 *
 *	NOTE! 	NOTE!	NOTE!	NOTE!
 */

extern int sound_started;

struct driver_info {
	char *driver_id;
	int card_subtype;	/* Driver spesific. Usually 0 */
	int card_type;		/*	From soundcard.h	*/
	char *name;
	void (*attach) (struct address_info *hw_config);
	int (*probe) (struct address_info *hw_config);
	void (*unload) (struct address_info *hw_config);
};

struct card_info {
	int card_type;	/* Link (search key) to the driver list */
	struct address_info config;
	int enabled;
	void *for_driver_use;
};

typedef struct pnp_sounddev
{
	int id;
	void (*setup)(void *dev);
	char *driver_name;
}pnp_sounddev;

/*
 * Device specific parameters (used only by dmabuf.c)
 */
#define MAX_SUB_BUFFERS		(32*MAX_REALTIME_FACTOR)

#define DMODE_NONE		0
#define DMODE_OUTPUT		PCM_ENABLE_OUTPUT
#define DMODE_INPUT		PCM_ENABLE_INPUT

struct dma_buffparms {
	int      dma_mode;	/* DMODE_INPUT, DMODE_OUTPUT or DMODE_NONE */
	int	 closing;

	/*
 	 * Pointers to raw buffers
 	 */

  	char     *raw_buf;
    	unsigned long   raw_buf_phys;

     	/*
         * Device state tables
         */

	unsigned long flags;
#define DMA_BUSY	0x00000001
#define DMA_RESTART	0x00000002
#define DMA_ACTIVE	0x00000004
#define DMA_STARTED	0x00000008
#define DMA_EMPTY	0x00000010	
#define DMA_ALLOC_DONE	0x00000020
#define DMA_SYNCING	0x00000040
#define DMA_CLEAN	0x00000080

	int      open_mode;

	/*
	 * Queue parameters.
	 */
       	int      qlen;
       	int      qhead;
       	int      qtail;
	int	 cfrag;	/* Current incomplete fragment (write) */

	int      nbufs;
	int      counts[MAX_SUB_BUFFERS];
	int      subdivision;

	int      fragment_size;
	int	 max_fragments;

	int	 bytes_in_use;

	int	 underrun_count;
	int	 byte_counter;

	int	 mapping_flags;
#define			DMA_MAP_MAPPED		0x00000001
	char	neutral_byte;
#ifdef OS_DMA_PARMS
	OS_DMA_PARMS
#endif
};

/*
 * Structure for use with various microcontrollers and DSP processors 
 * in the recent soundcards.
 */
typedef struct coproc_operations {
		char name[32];
		int (*open) (void *devc, int sub_device);
		void (*close) (void *devc, int sub_device);
		int (*ioctl) (void *devc, unsigned int cmd, caddr_t arg, int local);
		void (*reset) (void *devc);

		void *devc;		/* Driver specific info */
	} coproc_operations;

struct audio_driver {
	int (*open) (int dev, int mode);
	void (*close) (int dev);
	void (*output_block) (int dev, unsigned long buf, 
			      int count, int intrflag, int dma_restart);
	void (*start_input) (int dev, unsigned long buf, 
			     int count, int intrflag, int dma_restart);
	int (*ioctl) (int dev, unsigned int cmd, caddr_t arg, int local);
	int (*prepare_for_input) (int dev, int bufsize, int nbufs);
	int (*prepare_for_output) (int dev, int bufsize, int nbufs);
	void (*reset) (int dev);
	void (*halt_xfer) (int dev);
	int (*local_qlen)(int dev);
        void (*copy_from_user)(int dev, char *localbuf, int localoffs,
                               const char *userbuf, int useroffs, int len);
	void (*halt_input) (int dev);
	void (*halt_output) (int dev);
	void (*trigger) (int dev, int bits);
	int (*set_speed)(int dev, int speed);
	unsigned int (*set_bits)(int dev, unsigned int bits);
	short (*set_channels)(int dev, short channels);
};

struct audio_operations {
        char name[32];
	int flags;
#define NOTHING_SPECIAL 	0x00
#define NEEDS_RESTART		0x01
#define DMA_AUTOMODE		0x02
#define DMA_DUPLEX		0x04
#define DMA_PSEUDO_AUTOMODE	0x08
#define DMA_HARDSTOP		0x10
	int  format_mask;	/* Bitmask for supported audio formats */
	void *devc;		/* Driver specific info */
	struct audio_driver *d;
	long buffsize;
	int dmachan1, dmachan2;
	struct dma_buffparms *dmap_in, *dmap_out;
	struct coproc_operations *coproc;
	int mixer_dev;
	int enable_bits;
 	int open_mode;
	int go;
	int min_fragment;	/* 0 == unlimited */
};

struct mixer_operations {
	char id[16];
	char name[32];
	int (*ioctl) (int dev, unsigned int cmd, caddr_t arg);
	
	void *devc;
};

struct synth_operations {
	struct synth_info *info;
	int midi_dev;
	int synth_type;
	int synth_subtype;

	int (*open) (int dev, int mode);
	void (*close) (int dev);
	int (*ioctl) (int dev, unsigned int cmd, caddr_t arg);
	int (*kill_note) (int dev, int voice, int note, int velocity);
	int (*start_note) (int dev, int voice, int note, int velocity);
	int (*set_instr) (int dev, int voice, int instr);
	void (*reset) (int dev);
	void (*hw_control) (int dev, unsigned char *event);
	int (*load_patch) (int dev, int format, const char *addr,
	     int offs, int count, int pmgr_flag);
	void (*aftertouch) (int dev, int voice, int pressure);
	void (*controller) (int dev, int voice, int ctrl_num, int value);
	void (*panning) (int dev, int voice, int value);
	void (*volume_method) (int dev, int mode);
	int (*pmgr_interface) (int dev, struct patmgr_info *info);
	void (*bender) (int dev, int chn, int value);
	int (*alloc_voice) (int dev, int chn, int note, struct voice_alloc_info *alloc);
	void (*setup_voice) (int dev, int voice, int chn);
	int (*send_sysex)(int dev, unsigned char *bytes, int len);

 	struct voice_alloc_info alloc;
 	struct channel_info chn_info[16];
};

struct midi_input_info { /* MIDI input scanner variables */
#define MI_MAX	10
    		int             m_busy;
    		unsigned char   m_buf[MI_MAX];
		unsigned char	m_prev_status;	/* For running status */
    		int             m_ptr;
#define MST_INIT			0
#define MST_DATA			1
#define MST_SYSEX			2
    		int             m_state;
    		int             m_left;
	};

struct midi_operations {
	struct midi_info info;
	struct synth_operations *converter;
	struct midi_input_info in_info;
	int (*open) (int dev, int mode,
		void (*inputintr)(int dev, unsigned char data),
		void (*outputintr)(int dev)
		);
	void (*close) (int dev);
	int (*ioctl) (int dev, unsigned int cmd, caddr_t arg);
	int (*putc) (int dev, unsigned char data);
	int (*start_read) (int dev);
	int (*end_read) (int dev);
	void (*kick)(int dev);
	int (*command) (int dev, unsigned char *data);
	int (*buffer_status) (int dev);
	int (*prefix_cmd) (int dev, unsigned char status);
	struct coproc_operations *coproc;
	void *devc;
};

struct sound_lowlev_timer {
		int dev;
		unsigned int (*tmr_start)(int dev, unsigned int usecs);
		void (*tmr_disable)(int dev);
		void (*tmr_restart)(int dev);
	};

struct sound_timer_operations {
	struct sound_timer_info info;
	int priority;
	int devlink;
	int (*open)(int dev, int mode);
	void (*close)(int dev);
	int (*event)(int dev, unsigned char *ev);
	unsigned long (*get_time)(int dev);
	int (*ioctl) (int dev, unsigned int cmd, caddr_t arg);
	void (*arm_timer)(int dev, long time);
};

#ifdef _DEV_TABLE_C_   
	struct audio_operations *audio_devs[MAX_AUDIO_DEV] = {NULL}; int num_audiodevs = 0;
	struct mixer_operations *mixer_devs[MAX_MIXER_DEV] = {NULL}; int num_mixers = 0;
	struct synth_operations *synth_devs[MAX_SYNTH_DEV+MAX_MIDI_DEV] = {NULL}; int num_synths = 0;
	struct midi_operations *midi_devs[MAX_MIDI_DEV] = {NULL}; int num_midis = 0;

#ifdef CONFIG_SEQUENCER
	extern struct sound_timer_operations default_sound_timer;
	struct sound_timer_operations *sound_timer_devs[MAX_TIMER_DEV] = 
		{&default_sound_timer, NULL}; 
	int num_sound_timers = 1;
#else
	struct sound_timer_operations *sound_timer_devs[MAX_TIMER_DEV] = 
		{NULL}; 
	int num_sound_timers = 0;
#endif

/*
 * List of low level drivers compiled into the kernel.
 */

	struct driver_info sound_drivers[] = {
#ifdef CONFIG_PSS
	  {"PSS", 0, SNDCARD_PSS, "Echo Personal Sound System PSS (ESC614)", attach_pss, probe_pss, unload_pss},
	  {"PSSMPU", 0, SNDCARD_PSS_MPU, "PSS-MPU", attach_pss_mpu, probe_pss_mpu, unload_pss_mpu},
	  {"PSSMSS", 0, SNDCARD_PSS_MSS, "PSS-MSS", attach_pss_mss, probe_pss_mss, unload_pss_mss},
#endif
#ifdef CONFIG_MSS
		{"MSS", 0, SNDCARD_MSS,	"MS Sound System",	attach_ms_sound, probe_ms_sound, unload_ms_sound},
	/* MSS without IRQ/DMA config registers (for DEC Alphas) */
		{"PCXBJ", 1, SNDCARD_PSEUDO_MSS,	"MS Sound System (AXP)",	attach_ms_sound, probe_ms_sound, unload_ms_sound},
	/* Compaq Deskpro XL */
		{"DESKPROXL", 2, SNDCARD_DESKPROXL,	"Compaq Deskpro XL",	attach_ms_sound, probe_ms_sound, unload_ms_sound},
#endif
#ifdef CONFIG_MAD16
		{"MAD16", 0, SNDCARD_MAD16,	"MAD16/Mozart (MSS)",		attach_mad16, probe_mad16, unload_mad16},
		{"MAD16MPU", 0, SNDCARD_MAD16_MPU,	"MAD16/Mozart (MPU)",		attach_mad16_mpu, probe_mad16_mpu, unload_mad16_mpu},
#endif
#ifdef CONFIG_CS4232
		{"CS4232", 0, SNDCARD_CS4232,	"CS4232",		attach_cs4232, probe_cs4232, unload_cs4232},
		{"CS4232MPU", 0, SNDCARD_CS4232_MPU,	"CS4232 MIDI",		attach_cs4232_mpu, probe_cs4232_mpu, unload_cs4232_mpu},
#endif
#ifdef CONFIG_YM3812
		{"OPL3", 0, SNDCARD_ADLIB,	"OPL-2/OPL-3 FM",		attach_adlib_card, probe_adlib, unload_adlib},
#endif
#ifdef CONFIG_PAS
		{"PAS16", 0, SNDCARD_PAS,	"ProAudioSpectrum",	attach_pas_card, probe_pas, unload_pas},
#endif
#if defined(CONFIG_MPU401) && defined(CONFIG_MIDI)
		{"MPU401", 0, SNDCARD_MPU401,"Roland MPU-401",	attach_mpu401, probe_mpu401, unload_mpu401},
#endif
#if defined(CONFIG_MAUI)
		{"MAUI", 0, SNDCARD_MAUI,"TB Maui",	attach_maui, probe_maui, unload_maui},
#endif
#if defined(CONFIG_UART6850) && defined(CONFIG_MIDI)
		{"MIDI6850", 0, SNDCARD_UART6850,"6860 UART Midi",	attach_uart6850, probe_uart6850, unload_uart6850},
#endif
#ifdef CONFIG_SB
		{"SBLAST", 0, SNDCARD_SB,	"SoundBlaster",		attach_sb_card, probe_sb, unload_sb},
#ifdef CONFIG_MIDI
		{"UART401", 0, SNDCARD_UART401,"MPU-401 UART",	attach_uart401, probe_uart401, unload_uart401},
#endif
#endif
#ifdef CONFIG_GUS16
		{"GUS16", 0, SNDCARD_GUS16,	"Ultrasound 16-bit opt.",	attach_gus_db16, probe_gus_db16, unload_gus_db16},
#endif
#ifdef CONFIG_GUS
		{"GUS", 0, SNDCARD_GUS,	"Gravis Ultrasound",	attach_gus_card, probe_gus, unload_gus},
		{"GUSPNP", 1, SNDCARD_GUSPNP,	"GUS PnP",	attach_gus_card, probe_gus, unload_gus},
#endif
#ifdef CONFIG_SSCAPE
		{"SSCAPE", 0, SNDCARD_SSCAPE, "Ensoniq Soundscape",	attach_sscape, probe_sscape, unload_sscape},
		{"SSCAPEMSS", 0, SNDCARD_SSCAPE_MSS,	"MS Sound System (SoundScape)",	attach_ss_ms_sound, probe_ss_ms_sound, unload_ss_ms_sound},
#endif
#ifdef CONFIG_TRIX
		{"TRXPRO", 0, SNDCARD_TRXPRO, "MediaTriX AudioTriX Pro",	attach_trix_wss, probe_trix_wss, unload_trix_wss},
		{"TRXPROSB", 0, SNDCARD_TRXPRO_SB, "AudioTriX (SB mode)",	attach_trix_sb, probe_trix_sb, unload_trix_sb},
		{"TRXPROMPU", 0, SNDCARD_TRXPRO_MPU, "AudioTriX MIDI",	attach_trix_mpu, probe_trix_mpu, unload_trix_mpu},
#endif
		{NULL, 0, 0,		"*?*",			NULL, NULL, NULL}
	};

	int num_sound_drivers =
	    sizeof(sound_drivers) / sizeof (struct driver_info);
	int max_sound_drivers =
	    sizeof(sound_drivers) / sizeof (struct driver_info);


#ifndef FULL_SOUND
/*
 *	List of devices actually configured in the system.
 *
 *	Note! The detection order is significant. Don't change it.
 */

	struct card_info snd_installed_cards[] = {
#ifdef CONFIG_PSS
	     {SNDCARD_PSS, {PSS_BASE, 0, -1, -1}, SND_DEFAULT_ENABLE},
#	ifdef PSS_MPU_BASE
	     {SNDCARD_PSS_MPU, {PSS_MPU_BASE, PSS_MPU_IRQ, 0, -1}, SND_DEFAULT_ENABLE},
#	endif
#	ifdef PSS_MSS_BASE
	     {SNDCARD_PSS_MSS, {PSS_MSS_BASE, PSS_MSS_IRQ, PSS_MSS_DMA, -1}, SND_DEFAULT_ENABLE},
#	endif
#endif
#ifdef CONFIG_TRIX
#ifndef TRIX_DMA2
#define TRIX_DMA2 TRIX_DMA
#endif
	     {SNDCARD_TRXPRO, {TRIX_BASE, TRIX_IRQ, TRIX_DMA, TRIX_DMA2}, SND_DEFAULT_ENABLE},
#	ifdef TRIX_SB_BASE
	     {SNDCARD_TRXPRO_SB, {TRIX_SB_BASE, TRIX_SB_IRQ, TRIX_SB_DMA, -1}, SND_DEFAULT_ENABLE},
#	endif
#	ifdef TRIX_MPU_BASE
	     {SNDCARD_TRXPRO_MPU, {TRIX_MPU_BASE, TRIX_MPU_IRQ, 0, -1}, SND_DEFAULT_ENABLE},
#	endif
#endif
#ifdef CONFIG_SSCAPE
	     {SNDCARD_SSCAPE, {SSCAPE_BASE, SSCAPE_IRQ, SSCAPE_DMA, -1}, SND_DEFAULT_ENABLE},
	     {SNDCARD_SSCAPE_MSS, {SSCAPE_MSS_BASE, SSCAPE_MSS_IRQ, SSCAPE_DMA, -1}, SND_DEFAULT_ENABLE},
#endif
#ifdef CONFIG_MAD16
#ifndef MAD16_DMA2
#define MAD16_DMA2 MAD16_DMA
#endif
	     {SNDCARD_MAD16, {MAD16_BASE, MAD16_IRQ, MAD16_DMA, MAD16_DMA2}, SND_DEFAULT_ENABLE},
#	ifdef MAD16_MPU_BASE
	     {SNDCARD_MAD16_MPU, {MAD16_MPU_BASE, MAD16_MPU_IRQ, 0, -1}, SND_DEFAULT_ENABLE},
#	endif
#endif

#ifdef CONFIG_CS4232
#ifndef CS4232_DMA2
#define CS4232_DMA2 CS4232_DMA
#endif
#	ifdef CS4232_MPU_BASE
	     {SNDCARD_CS4232_MPU, {CS4232_MPU_BASE, CS4232_MPU_IRQ, 0, -1}, SND_DEFAULT_ENABLE},
#	endif
	     {SNDCARD_CS4232, {CS4232_BASE, CS4232_IRQ, CS4232_DMA, CS4232_DMA2}, SND_DEFAULT_ENABLE},
#endif


#ifdef CONFIG_MSS
#	ifdef DESKPROXL
		{SNDCARD_DESKPROXL, {MSS_BASE, MSS_IRQ, MSS_DMA, -1}, SND_DEFAULT_ENABLE},
#	else
		{SNDCARD_MSS, {MSS_BASE, MSS_IRQ, MSS_DMA, -1}, SND_DEFAULT_ENABLE},
#	endif
#	ifdef MSS2_BASE
		{SNDCARD_MSS, {MSS2_BASE, MSS2_IRQ, MSS2_DMA, -1}, SND_DEFAULT_ENABLE},
#	endif
#endif


#ifdef CONFIG_PAS
		{SNDCARD_PAS, {PAS_BASE, PAS_IRQ, PAS_DMA, -1}, SND_DEFAULT_ENABLE},
#endif

#ifdef CONFIG_SB
#	ifndef SBC_DMA
#		define SBC_DMA		1
#	endif
#	ifndef SB_DMA2
#		define SB_DMA2		-1
#	endif
		{SNDCARD_SB, {SBC_BASE, SBC_IRQ, SBC_DMA, SB_DMA2}, SND_DEFAULT_ENABLE},
# 	ifdef SB2_BASE
		{SNDCARD_SB, {SB2_BASE, SB2_IRQ, SB2_DMA, SB2_DMA2}, SND_DEFAULT_ENABLE},
#	endif
#endif
#if defined(CONFIG_MAUI) 
		{SNDCARD_MAUI, {MAUI_BASE, MAUI_IRQ, 0, -1}, SND_DEFAULT_ENABLE},
#endif

#if defined(CONFIG_MPU401) && defined(CONFIG_MIDI)
		{SNDCARD_MPU401, {MPU_BASE, MPU_IRQ, 0, -1}, SND_DEFAULT_ENABLE},
#ifdef MPU2_BASE
		{SNDCARD_MPU401, {MPU2_BASE, MPU2_IRQ, 0, -1}, SND_DEFAULT_ENABLE},
#endif
#ifdef MPU3_BASE
		{SNDCARD_MPU401, {MPU3_BASE, MPU2_IRQ, 0, -1}, SND_DEFAULT_ENABLE},
#endif
#endif

#if defined(CONFIG_UART6850) && defined(CONFIG_MIDI)
		{SNDCARD_UART6850, {U6850_BASE, U6850_IRQ, 0, -1}, SND_DEFAULT_ENABLE},
#endif

#if defined(CONFIG_SB) 
#if defined(CONFIG_MIDI) && defined(SB_MPU_BASE)
		{SNDCARD_SB16MIDI,{SB_MPU_BASE, SB_MPU_IRQ, 0, -1}, SND_DEFAULT_ENABLE},
#endif
#endif

#ifdef CONFIG_GUS
#ifndef GUS_DMA2
#define GUS_DMA2 GUS_DMA
#endif
#ifdef CONFIG_GUS16
		{SNDCARD_GUS16, {GUS16_BASE, GUS16_IRQ, GUS16_DMA, -1}, SND_DEFAULT_ENABLE},
#endif
		{SNDCARD_GUS, {GUS_BASE, GUS_IRQ, GUS_DMA, GUS_DMA2}, SND_DEFAULT_ENABLE},
#endif

#ifdef CONFIG_YM3812
		{SNDCARD_ADLIB, {FM_MONO, 0, 0, -1}, SND_DEFAULT_ENABLE},
#endif
/* Define some expansion space */
		{0, {0}, 0},
		{0, {0}, 0},
		{0, {0}, 0},
		{0, {0}, 0},
		{0, {0}, 0}
	};

	int num_sound_cards =
	    sizeof(snd_installed_cards) / sizeof (struct card_info);
	int max_sound_cards =
	    sizeof(snd_installed_cards) / sizeof (struct card_info);

#else
	int num_sound_cards = 0;
	struct card_info snd_installed_cards[20] = {{0}};
	int max_sound_cards = 20;
#endif

#   ifdef MODULE
	int trace_init = 0;
#   else
	int trace_init = 1;
#   endif
#else
	extern struct audio_operations * audio_devs[MAX_AUDIO_DEV]; extern int num_audiodevs;
	extern struct mixer_operations * mixer_devs[MAX_MIXER_DEV]; extern int num_mixers;
	extern struct synth_operations * synth_devs[MAX_SYNTH_DEV+MAX_MIDI_DEV]; extern int num_synths;
	extern struct midi_operations * midi_devs[MAX_MIDI_DEV]; extern int num_midis;
	extern struct sound_timer_operations * sound_timer_devs[MAX_TIMER_DEV]; extern int num_sound_timers;

	extern struct driver_info sound_drivers[];
	extern int num_sound_drivers;
	extern int max_sound_drivers;
	extern struct card_info snd_installed_cards[];
	extern int num_sound_cards;
	extern int max_sound_cards;

	extern int trace_init;
#endif	/* _DEV_TABLE_C_ */

void sndtable_init(void);
int sndtable_get_cardcount (void);
struct address_info *sound_getconf(int card_type);
void sound_chconf(int card_type, int ioaddr, int irq, int dma);
int snd_find_driver(int type);
void sound_unload_drivers(void);
void sound_unload_driver(int type);
int sndtable_identify_card(char *name);
void sound_setup (char *str, int *ints);

int sound_alloc_dmap (int dev, struct dma_buffparms *dmap, int chan);
void sound_free_dmap (int dev, struct dma_buffparms *dmap);
extern int soud_map_buffer (int dev, struct dma_buffparms *dmap, buffmem_desc *info);
void install_pnp_sounddrv(struct pnp_sounddev *drv);
int sndtable_probe (int unit, struct address_info *hw_config);
int sndtable_init_card (int unit, struct address_info *hw_config);
int sndtable_start_card (int unit, struct address_info *hw_config);
void sound_timer_init (struct sound_lowlev_timer *t, char *name);
int sound_start_dma (	int dev, struct dma_buffparms *dmap, int chan,
			unsigned long physaddr,
			int count, int dma_mode, int autoinit);
void sound_dma_intr (int dev, struct dma_buffparms *dmap, int chan);

#define AUDIO_DRIVER_VERSION	1
#define MIXER_DRIVER_VERSION	1
int sound_install_audiodrv(int vers,
			   char *name,
			   struct audio_driver *driver,
			   int driver_size,
			   int flags,
      			   unsigned int format_mask,
			   void *devc,
			   int dma1, 
			   int dma2);
int sound_install_mixer(int vers, 
			char *name,
			struct mixer_operations *driver,
			int driver_size,
			void *devc);

#endif	/* _DEV_TABLE_H_ */
