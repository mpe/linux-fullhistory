/*
 *	dev_table.h
 *
 *	Global definitions for device call tables
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */


#ifndef _DEV_TABLE_H_
#define _DEV_TABLE_H_

#include <linux/config.h>

/*
 * Sound card numbers 27 to 999. (1 to 26 are defined in soundcard.h)
 * Numbers 1000 to N are reserved for driver's internal use.
 */

#define SNDCARD_DESKPROXL		27	/* Compaq Deskpro XL */
#define SNDCARD_VIDC			28	/* ARMs VIDC */
#define SNDCARD_SBPNP			29
#define SNDCARD_OPL3SA1			38
#define SNDCARD_OPL3SA1_SB		39
#define SNDCARD_OPL3SA1_MPU		40
#define SNDCARD_SOFTOSS			36
#define SNDCARD_VMIDI			37
#define SNDCARD_WAVEFRONT               41
#define SNDCARD_OPL3SA2                 42
#define SNDCARD_OPL3SA2_MPU             43
#define SNDCARD_WAVEARTIST		44
#define SNDCARD_AD1816                  88

void attach_opl3sa_wss (struct address_info *hw_config);
int probe_opl3sa_wss (struct address_info *hw_config);
void attach_opl3sa_sb (struct address_info *hw_config);
int probe_opl3sa_sb (struct address_info *hw_config);
void attach_opl3sa_mpu (struct address_info *hw_config);
int probe_opl3sa_mpu (struct address_info *hw_config);
void unload_opl3sa_wss(struct address_info *hw_info);
void unload_opl3sa_sb(struct address_info *hw_info);
void unload_opl3sa_mpu(struct address_info *hw_info);
void attach_softsyn_card (struct address_info *hw_config);
int probe_softsyn (struct address_info *hw_config);
void unload_softsyn (struct address_info *hw_config);

/*
 *	NOTE! 	NOTE!	NOTE!	NOTE!
 *
 *	If you modify this file, please check the dev_table.c also.
 *
 *	NOTE! 	NOTE!	NOTE!	NOTE!
 */

extern int sound_started;

struct driver_info 
{
	char *driver_id;
	int card_subtype;	/* Driver specific. Usually 0 */
	int card_type;		/*	From soundcard.h	*/
	char *name;
	void (*attach) (struct address_info *hw_config);
	int (*probe) (struct address_info *hw_config);
	void (*unload) (struct address_info *hw_config);
};

struct card_info 
{
	int card_type;	/* Link (search key) to the driver list */
	struct address_info config;
	int enabled;
	void *for_driver_use;
};


/*
 * Device specific parameters (used only by dmabuf.c)
 */
#define MAX_SUB_BUFFERS		(32*MAX_REALTIME_FACTOR)

#define DMODE_NONE		0
#define DMODE_OUTPUT		PCM_ENABLE_OUTPUT
#define DMODE_INPUT		PCM_ENABLE_INPUT

struct dma_buffparms 
{
	int      dma_mode;	/* DMODE_INPUT, DMODE_OUTPUT or DMODE_NONE */
	int	 closing;

	/*
 	 * Pointers to raw buffers
 	 */

  	char     *raw_buf;
    	unsigned long   raw_buf_phys;
	int buffsize;

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
#define DMA_DIRTY	0x00000080
#define DMA_POST	0x00000100
#define DMA_NODMA	0x00000200
#define DMA_NOTIMEOUT	0x00000400

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
        int	 needs_reorg;
	int	 max_fragments;

	int	 bytes_in_use;

	int	 underrun_count;
	unsigned long	 byte_counter;
	unsigned long	 user_counter;
	unsigned long	 max_byte_counter;
	int	 data_rate; /* Bytes/second */

	int	 mapping_flags;
#define			DMA_MAP_MAPPED		0x00000001
	char	neutral_byte;
	int	dma;		/* DMA channel */

#ifdef OS_DMA_PARMS
	OS_DMA_PARMS
#endif
	int     applic_profile;	/* Application profile (APF_*) */
	/* Interrupt callback stuff */
	void (*audio_callback) (int dev, int parm);
	int callback_parm;

	int	 buf_flags[MAX_SUB_BUFFERS];
#define		 BUFF_EOF		0x00000001 /* Increment eof count */
#define		 BUFF_DIRTY		0x00000002 /* Buffer written */
};

/*
 * Structure for use with various microcontrollers and DSP processors 
 * in the recent sound cards.
 */
typedef struct coproc_operations 
{
	char name[64];
	int (*open) (void *devc, int sub_device);
	void (*close) (void *devc, int sub_device);
	int (*ioctl) (void *devc, unsigned int cmd, caddr_t arg, int local);
	void (*reset) (void *devc);

	void *devc;		/* Driver specific info */
} coproc_operations;

struct audio_driver 
{
	int (*open) (int dev, int mode);
	void (*close) (int dev);
	void (*output_block) (int dev, unsigned long buf, 
			      int count, int intrflag);
	void (*start_input) (int dev, unsigned long buf, 
			     int count, int intrflag);
	int (*ioctl) (int dev, unsigned int cmd, caddr_t arg);
	int (*prepare_for_input) (int dev, int bufsize, int nbufs);
	int (*prepare_for_output) (int dev, int bufsize, int nbufs);
	void (*halt_io) (int dev);
	int (*local_qlen)(int dev);
	void (*copy_user) (int dev,
			char *localbuf, int localoffs,
                        const char *userbuf, int useroffs,
                        int max_in, int max_out,
                        int *used, int *returned,
                        int len);
	void (*halt_input) (int dev);
	void (*halt_output) (int dev);
	void (*trigger) (int dev, int bits);
	int (*set_speed)(int dev, int speed);
	unsigned int (*set_bits)(int dev, unsigned int bits);
	short (*set_channels)(int dev, short channels);
	void (*postprocess_write)(int dev); 	/* Device spesific postprocessing for written data */
	void (*preprocess_read)(int dev); 	/* Device spesific preprocessing for read data */
	void (*mmap)(int dev);
};

struct audio_operations 
{
        char name[128];
	int flags;
#define NOTHING_SPECIAL 	0x00
#define NEEDS_RESTART		0x01
#define DMA_AUTOMODE		0x02
#define DMA_DUPLEX		0x04
#define DMA_PSEUDO_AUTOMODE	0x08
#define DMA_HARDSTOP		0x10
#define DMA_EXACT		0x40
#define DMA_NORESET		0x80
	int  format_mask;	/* Bitmask for supported audio formats */
	void *devc;		/* Driver specific info */
	struct audio_driver *d;
	void *portc;		/* Driver spesific info */
	struct dma_buffparms *dmap_in, *dmap_out;
	struct coproc_operations *coproc;
	int mixer_dev;
	int enable_bits;
 	int open_mode;
	int go;
	int min_fragment;	/* 0 == unlimited */
	int max_fragment;	/* 0 == unlimited */
	int parent_dev;		/* 0 -> no parent, 1 to n -> parent=parent_dev+1 */

	/* fields formerly in dmabuf.c */
	struct wait_queue *in_sleeper;
	struct wait_queue *out_sleeper;

	/* fields formerly in audio.c */
	int audio_mode;

#define		AM_NONE		0
#define		AM_WRITE	OPEN_WRITE
#define 	AM_READ		OPEN_READ

	int local_format;
	int audio_format;
	int local_conversion;
#define CNV_MU_LAW	0x00000001

	/* large structures at the end to keep offsets small */
	struct dma_buffparms dmaps[2];
};

int *load_mixer_volumes(char *name, int *levels, int present);

struct mixer_operations 
{
	char id[16];
	char name[64];
	int (*ioctl) (int dev, unsigned int cmd, caddr_t arg);
	
	void *devc;
	int modify_counter;
};

struct synth_operations 
{
	char *id;	/* Unique identifier (ASCII) max 29 char */
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
	void (*bender) (int dev, int chn, int value);
	int (*alloc_voice) (int dev, int chn, int note, struct voice_alloc_info *alloc);
	void (*setup_voice) (int dev, int voice, int chn);
	int (*send_sysex)(int dev, unsigned char *bytes, int len);

 	struct voice_alloc_info alloc;
 	struct channel_info chn_info[16];
	int emulation;
#define	EMU_GM			1	/* General MIDI */
#define	EMU_XG			2	/* Yamaha XG */
#define MAX_SYSEX_BUF	64
	unsigned char sysex_buf[MAX_SYSEX_BUF];
	int sysex_ptr;
};

struct midi_input_info 
{
	/* MIDI input scanner variables */
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

struct midi_operations 
{
	struct midi_info info;
	struct synth_operations *converter;
	struct midi_input_info in_info;
	int (*open) (int dev, int mode,
		void (*inputintr)(int dev, unsigned char data),
		void (*outputintr)(int dev)
		);
	void (*close) (int dev);
	int (*ioctl) (int dev, unsigned int cmd, caddr_t arg);
	int (*outputc) (int dev, unsigned char data);
	int (*start_read) (int dev);
	int (*end_read) (int dev);
	void (*kick)(int dev);
	int (*command) (int dev, unsigned char *data);
	int (*buffer_status) (int dev);
	int (*prefix_cmd) (int dev, unsigned char status);
	struct coproc_operations *coproc;
	void *devc;
};

struct sound_lowlev_timer 
{
	int dev;
	int priority;
	unsigned int (*tmr_start)(int dev, unsigned int usecs);
	void (*tmr_disable)(int dev);
	void (*tmr_restart)(int dev);
};

struct sound_timer_operations 
{
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

#if defined(CONFIG_SEQUENCER) && !defined(EXCLUDE_TIMERS) && !defined(VMIDI)
extern struct sound_timer_operations default_sound_timer;
struct sound_timer_operations *sound_timer_devs[MAX_TIMER_DEV] = {
	&default_sound_timer, NULL
}; 
int num_sound_timers = 1;
#else
struct sound_timer_operations *sound_timer_devs[MAX_TIMER_DEV] = {
	NULL
};
int num_sound_timers = 0;
#endif

/*
 * List of low level drivers compiled into the kernel.
 */

struct driver_info sound_drivers[] = 
{
#ifdef CONFIG_SOUND_PSS
	{"PSS", 0, SNDCARD_PSS, "Echo Personal Sound System PSS (ESC614)", attach_pss, probe_pss, unload_pss},
	{"PSSMPU", 0, SNDCARD_PSS_MPU, "PSS-MPU", attach_pss_mpu, probe_pss_mpu, unload_pss_mpu},
	{"PSSMSS", 0, SNDCARD_PSS_MSS, "PSS-MSS", attach_pss_mss, probe_pss_mss, unload_pss_mss},
#endif

#ifdef CONFIG_SOUND_GUS
#ifdef CONFIG_GUS16
	{"GUS16", 0, SNDCARD_GUS16,	"Ultrasound 16-bit opt.",	attach_gus_db16, probe_gus_db16, unload_gus_db16},
#endif
#ifdef CONFIG_GUS
	{"GUS", 0, SNDCARD_GUS,	"Gravis Ultrasound",	attach_gus_card, probe_gus, unload_gus},
	{"GUSPNP", 1, SNDCARD_GUSPNP,	"GUS PnP",	attach_gus_card, probe_gus, unload_gus},
#endif
#endif

#ifdef CONFIG_SOUND_MSS
	{"MSS", 0, SNDCARD_MSS,	"MS Sound System",	attach_ms_sound, probe_ms_sound, unload_ms_sound},
	/* Compaq Deskpro XL */
	{"DESKPROXL", 2, SNDCARD_DESKPROXL,	"Compaq Deskpro XL",	attach_ms_sound, probe_ms_sound, unload_ms_sound},
#endif

#ifdef CONFIG_SOUND_MAD16
	{"MAD16", 0, SNDCARD_MAD16,	"MAD16/Mozart (MSS)",		attach_mad16, probe_mad16, unload_mad16},
	{"MAD16MPU", 0, SNDCARD_MAD16_MPU,	"MAD16/Mozart (MPU)",		attach_mad16_mpu, probe_mad16_mpu, unload_mad16_mpu},
#endif

#ifdef CONFIG_SOUND_CS4232
	{"CS4232", 0, SNDCARD_CS4232,	"CS4232",		attach_cs4232, probe_cs4232, unload_cs4232},
#endif
#ifdef CONFIG_CS4232_MPU_BASE
	{"CS4232MPU", 0, SNDCARD_CS4232_MPU,	"CS4232 MIDI",		attach_cs4232_mpu, probe_cs4232_mpu, unload_cs4232_mpu},
#endif

#ifdef CONFIG_SOUND_OPL3SA2
	{"OPL3SA2", 0, SNDCARD_OPL3SA2,	"OPL3SA2",		attach_opl3sa2, probe_opl3sa2, unload_opl3sa2},
	{"OPL3SA2MPU", 0, SNDCARD_OPL3SA2_MPU,	"OPL3SA2 MIDI",		attach_opl3sa2_mpu, probe_opl3sa2_mpu, unload_opl3sa2_mpu},
#endif

#ifdef CONFIG_SGALAXY
	{"SGALAXY", 0, SNDCARD_SGALAXY,	"Sound Galaxy WSS",		attach_sgalaxy, probe_sgalaxy, unload_sgalaxy},
#endif

#ifdef CONFIG_SOUND_AD1816
        {"AD1816", 0, SNDCARD_AD1816,   "AD1816",               attach_ad1816, 
probe_ad1816, unload_ad1816},
#endif

#ifdef CONFIG_SOUND_YM3812
	{"OPL3", 0, SNDCARD_ADLIB,	"OPL-2/OPL-3 FM",		attach_adlib_card, probe_adlib, unload_adlib},
#endif

#ifdef CONFIG_SOUND_PAS
	{"PAS16", 0, SNDCARD_PAS,	"ProAudioSpectrum",	attach_pas_card, probe_pas, unload_pas},
#endif

#if (defined(CONFIG_SOUND_MPU401) || defined(CONFIG_SOUND_MPU_EMU)) && defined(CONFIG_MIDI)
	{"MPU401", 0, SNDCARD_MPU401,"Roland MPU-401",	attach_mpu401, probe_mpu401, unload_mpu401},
#endif

#if defined(CONFIG_SOUND_UART401) && defined(CONFIG_MIDI)
	{"UART401", 0, SNDCARD_UART401,"MPU-401 (UART)", 
		attach_uart401, probe_uart401, unload_uart401},
#endif

#if defined(CONFIG_SOUND_WAVEFRONT)
	{"WAVEFRONT", 0, SNDCARD_WAVEFRONT,"TB WaveFront", attach_wavefront, probe_wavefront, unload_wavefront},
#endif

#if defined(CONFIG_SOUND_MAUI)
	{"MAUI", 0, SNDCARD_MAUI,"TB Maui",	attach_maui, probe_maui, unload_maui},
#endif

#if defined(CONFIG_SOUND_UART6850) && defined(CONFIG_MIDI)
	{"MIDI6850", 0, SNDCARD_UART6850,"6860 UART Midi",	attach_uart6850, probe_uart6850, unload_uart6850},
#endif




#ifdef CONFIG_SOUND_SBDSP
	{"SBLAST", 0, SNDCARD_SB,	"Sound Blaster",		attach_sb_card, probe_sb, unload_sb},
	{"SBPNP", 6, SNDCARD_SBPNP,	"Sound Blaster PnP",		attach_sb_card, probe_sb, unload_sb},

#ifdef CONFIG_MIDI
	{"SBMPU", 0, SNDCARD_SB16MIDI,"SB MPU-401",	attach_sbmpu, probe_sbmpu, unload_sbmpu},
#endif
#endif

#ifdef CONFIG_SOUND_SSCAPE
	{"SSCAPE", 0, SNDCARD_SSCAPE, "Ensoniq SoundScape",	attach_sscape, probe_sscape, unload_sscape},
	{"SSCAPEMSS", 0, SNDCARD_SSCAPE_MSS,	"MS Sound System (SoundScape)",	attach_ss_ms_sound, probe_ss_ms_sound, unload_ss_ms_sound},
#endif

#ifdef CONFIG_SOUND_OPL3SA1
	{"OPL3SA", 0, SNDCARD_OPL3SA1, "Yamaha OPL3-SA",	attach_opl3sa_wss, probe_opl3sa_wss, unload_opl3sa_wss}, 
/*	{"OPL3SASB", 0, SNDCARD_OPL3SA1_SB, "OPL3-SA (SB mode)",	attach_opl3sa_sb, probe_opl3sa_sb, unload_opl3sa_sb}, */
	{"OPL3SAMPU", 0, SNDCARD_OPL3SA1_MPU, "OPL3-SA MIDI",	attach_opl3sa_mpu, probe_opl3sa_mpu, unload_opl3sa_mpu},
#endif

#ifdef CONFIG_SOUND_TRIX
	{"TRXPRO", 0, SNDCARD_TRXPRO, "MediaTrix AudioTrix Pro",	attach_trix_wss, probe_trix_wss, unload_trix_wss},
	{"TRXPROSB", 0, SNDCARD_TRXPRO_SB, "AudioTrix (SB mode)",	attach_trix_sb, probe_trix_sb, unload_trix_sb},
	{"TRXPROMPU", 0, SNDCARD_TRXPRO_MPU, "AudioTrix MIDI",	attach_trix_mpu, probe_trix_mpu, unload_trix_mpu},
#endif

#ifdef CONFIG_SOUND_SOFTOSS
	{"SOFTSYN", 0, SNDCARD_SOFTOSS,	"SoftOSS Virtual Wave Table", 
		attach_softsyn_card, probe_softsyn, unload_softsyn},
#endif

#if defined(CONFIG_SOUND_VMIDI) && defined(CONFIG_MIDI)
	{"VMIDI", 0, SNDCARD_VMIDI,"Loopback MIDI Device",      attach_v_midi, probe_v_midi, unload_v_midi},
#endif
#ifdef CONFIG_SOUND_VIDC
	{"VIDC", 0, SNDCARD_VIDC, "ARM VIDC 16-bit D/A", attach_vidc, probe_vidc, unload_vidc },
#endif
#ifdef CONFIG_SOUND_WAVEARTIST
	{"WaveArtist", 0, SNDCARD_WAVEARTIST, "NetWinder WaveArtist", attach_waveartist, probe_waveartist, unload_waveartist },
#endif
	{NULL, 0, 0,		"*?*",			NULL, NULL, NULL}
};

int num_sound_drivers = sizeof(sound_drivers) / sizeof (struct driver_info);


#ifndef FULL_SOUND

/*
 *	List of devices actually configured in the system.
 *
 *	Note! The detection order is significant. Don't change it.
 */

struct card_info snd_installed_cards[] = 
{
#ifdef CONFIG_SOUND_PSS
	{SNDCARD_PSS, {CONFIG_PSS_BASE, 0, -1, -1}, SND_DEFAULT_ENABLE},
#ifdef CONFIG_PSS_MPU_BASE
	{SNDCARD_PSS_MPU, {CONFIG_PSS_MPU_BASE, CONFIG_PSS_MPU_IRQ, 0, -1}, SND_DEFAULT_ENABLE},
#endif
#ifdef CONFIG_PSS_MSS_BASE
	{SNDCARD_PSS_MSS, {CONFIG_PSS_MSS_BASE, CONFIG_PSS_MSS_IRQ, CONFIG_PSS_MSS_DMA, -1}, SND_DEFAULT_ENABLE},
#endif
#endif

#ifdef CONFIG_SOUND_TRIX
#ifndef CONFIG_TRIX_DMA2
#define CONFIG_TRIX_DMA2 CONFIG_TRIX_DMA
#endif
	{SNDCARD_TRXPRO, {CONFIG_TRIX_BASE, CONFIG_TRIX_IRQ, CONFIG_TRIX_DMA, CONFIG_TRIX_DMA2}, SND_DEFAULT_ENABLE},
#ifdef CONFIG_TRIX_SB_BASE
	{SNDCARD_TRXPRO_SB, {CONFIG_TRIX_SB_BASE, CONFIG_TRIX_SB_IRQ, CONFIG_TRIX_SB_DMA, -1}, SND_DEFAULT_ENABLE},
#endif
#ifdef CONFIG_TRIX_MPU_BASE
	{SNDCARD_TRXPRO_MPU, {CONFIG_TRIX_MPU_BASE, CONFIG_TRIX_MPU_IRQ, 0, -1}, SND_DEFAULT_ENABLE},
#endif
#endif

#ifdef CONFIG_SOUND_OPL3SA1
	{SNDCARD_OPL3SA1, {CONFIG_OPL3SA1_BASE, CONFIG_OPL3SA1_IRQ, CONFIG_OPL3SA1_DMA, CONFIG_OPL3SA1_DMA2}, SND_DEFAULT_ENABLE},
#ifdef CONFIG_OPL3SA1_MPU_BASE
	{SNDCARD_OPL3SA1_MPU, {CONFIG_OPL3SA1_MPU_BASE, CONFIG_OPL3SA1_MPU_IRQ, 0, -1}, SND_DEFAULT_ENABLE},
#endif
#endif

#ifdef CONFIG_SOUND_SOFTOSS
	{SNDCARD_SOFTOSS, {0, 0, -1, -1}, SND_DEFAULT_ENABLE},
#endif

#ifdef CONFIG_SOUND_SSCAPE
	{SNDCARD_SSCAPE, {CONFIG_SSCAPE_BASE, CONFIG_SSCAPE_IRQ, CONFIG_SSCAPE_DMA, -1}, SND_DEFAULT_ENABLE},
	{SNDCARD_SSCAPE_MSS, {CONFIG_SSCAPE_MSS_BASE, CONFIG_SSCAPE_MSS_IRQ, CONFIG_SSCAPE_DMA, -1}, SND_DEFAULT_ENABLE},
#endif

#ifdef CONFIG_SOUND_MAD16
#ifndef CONFIG_MAD16_DMA2
#define CONFIG_MAD16_DMA2 CONFIG_MAD16_DMA
#endif
	{SNDCARD_MAD16, {CONFIG_MAD16_BASE, CONFIG_MAD16_IRQ, CONFIG_MAD16_DMA, CONFIG_MAD16_DMA2}, SND_DEFAULT_ENABLE},
#ifdef CONFIG_MAD16_MPU_BASE
	{SNDCARD_MAD16_MPU, {CONFIG_MAD16_MPU_BASE, CONFIG_MAD16_MPU_IRQ, 0, -1}, SND_DEFAULT_ENABLE},
#endif
#endif

#ifdef CONFIG_SOUND_CS4232
#ifndef CONFIG_CS4232_DMA2
#define CONFIG_CS4232_DMA2 CONFIG_CS4232_DMA
#endif
#ifdef CONFIG_CS4232_MPU_BASE
	{SNDCARD_CS4232_MPU, {CONFIG_CS4232_MPU_BASE, CONFIG_CS4232_MPU_IRQ, 0, -1}, SND_DEFAULT_ENABLE},
#endif
	{SNDCARD_CS4232, {CONFIG_CS4232_BASE, CONFIG_CS4232_IRQ, CONFIG_CS4232_DMA, CONFIG_CS4232_DMA2}, SND_DEFAULT_ENABLE},
#endif

#ifdef CONFIG_SOUND_OPL3SA2
#ifndef CONFIG_OPL3SA2_DMA2
#define CONFIG_OPL3SA2_DMA2 CONFIG_OPL3SA2_DMA
#endif
#ifdef CONFIG_OPL3SA2_MPU_BASE
	{SNDCARD_OPL3SA2_MPU, {CONFIG_OPL3SA2_MPU_BASE, CONFIG_OPL3SA2_MPU_IRQ, 0, -1}, SND_DEFAULT_ENABLE},
#endif
	{SNDCARD_OPL3SA2, {CONFIG_OPL3SA2_BASE, CONFIG_OPL3SA2_IRQ, CONFIG_OPL3SA2_DMA, CONFIG_OPL3SA2_DMA2}, SND_DEFAULT_ENABLE},
#endif

#ifdef CONFIG_SGALAXY
#ifndef CONFIG_SGALAXY_DMA2
#define CONFIG_SGALAXY_DMA2 CONFIG_SGALAXY_DMA
#endif
	{SNDCARD_SGALAXY, {CONFIG_SGALAXY_BASE, CONFIG_SGALAXY_IRQ, CONFIG_SGALAXY_DMA, CONFIG_SGALAXY_DMA2, 0, NULL, CONFIG_SGALAXY_SGBASE}, SND_DEFAULT_ENABLE},
#endif


#ifdef CONFIG_SOUND_MSS
#ifndef CONFIG_MSS_DMA2
#define CONFIG_MSS_DMA2 -1
#endif

#ifdef DESKPROXL
	{SNDCARD_DESKPROXL, {CONFIG_MSS_BASE, CONFIG_MSS_IRQ, CONFIG_MSS_DMA, CONFIG_MSS_DMA2}, SND_DEFAULT_ENABLE},
#else
	{SNDCARD_MSS, {CONFIG_MSS_BASE, CONFIG_MSS_IRQ, CONFIG_MSS_DMA, CONFIG_MSS_DMA2}, SND_DEFAULT_ENABLE},
#endif

#ifdef MSS2_BASE
	{SNDCARD_MSS, {MSS2_BASE, MSS2_IRQ, MSS2_DMA, MSS2_DMA2}, SND_DEFAULT_ENABLE},
#endif
#endif

#ifdef CONFIG_SOUND_PAS
	{SNDCARD_PAS, {CONFIG_PAS_BASE, CONFIG_PAS_IRQ, CONFIG_PAS_DMA, -1}, SND_DEFAULT_ENABLE},
#endif

#ifdef CONFIG_SOUND_SB
#ifndef CONFIG_SB_DMA
#define CONFIG_SB_DMA		1
#endif
#ifndef CONFIG_SB_DMA2
#define CONFIG_SB_DMA2		-1
#endif
	{SNDCARD_SB, {CONFIG_SB_BASE, CONFIG_SB_IRQ, CONFIG_SB_DMA, CONFIG_SB_DMA2}, SND_DEFAULT_ENABLE},
#ifdef SB2_BASE
	{SNDCARD_SB, {SB2_BASE, SB2_IRQ, SB2_DMA, SB2_DMA2}, SND_DEFAULT_ENABLE},
#endif
#endif

#if defined(CONFIG_WAVEFRONT) 
	{SNDCARD_WAVEFRONT, {WAVEFRONT_BASE, WAVEFRONT_IRQ, 0, -1}, SND_DEFAULT_ENABLE},
#endif

#ifdef CONFIG_SOUND_MAUI
	{SNDCARD_MAUI, {CONFIG_MAUI_BASE, CONFIG_MAUI_IRQ, 0, -1}, SND_DEFAULT_ENABLE},
#endif

#if defined(CONFIG_SOUND_MPU401) && defined(CONFIG_MIDI)
	{SNDCARD_MPU401, {CONFIG_MPU_BASE, CONFIG_MPU_IRQ, 0, -1}, SND_DEFAULT_ENABLE},
#ifdef MPU2_BASE
	{SNDCARD_MPU401, {MPU2_BASE, MPU2_IRQ, 0, -1}, SND_DEFAULT_ENABLE},
#endif
#ifdef MPU3_BASE
	{SNDCARD_MPU401, {MPU3_BASE, MPU3_IRQ, 0, -1}, SND_DEFAULT_ENABLE},
#endif
#endif

#if defined(CONFIG_SOUND_UART6850) && defined(CONFIG_MIDI)
	{SNDCARD_UART6850, {CONFIG_U6850_BASE, CONFIG_U6850_IRQ, 0, -1}, SND_DEFAULT_ENABLE},
#endif

#ifdef CONFIG_SOUND_SB
#if defined(CONFIG_MIDI) && defined(CONFIG_SB_MPU_BASE)
	{SNDCARD_SB16MIDI,{CONFIG_SB_MPU_BASE, CONFIG_SB_MPU_IRQ, 0, -1}, SND_DEFAULT_ENABLE},
#endif
#endif

#ifdef CONFIG_SOUND_GUS
#ifndef CONFIG_GUS_DMA2
#define CONFIG_GUS_DMA2 CONFIG_GUS_DMA
#endif
#ifdef CONFIG_GUS16
	{SNDCARD_GUS16, {CONFIG_GUS16_BASE, CONFIG_GUS16_IRQ, CONFIG_GUS16_DMA, -1}, SND_DEFAULT_ENABLE},
#endif
	{SNDCARD_GUS, {CONFIG_GUS_BASE, CONFIG_GUS_IRQ, CONFIG_GUS_DMA, CONFIG_GUS_DMA2}, SND_DEFAULT_ENABLE},
#endif

#ifdef CONFIG_SOUND_YM3812
	{SNDCARD_ADLIB, {FM_MONO, 0, 0, -1}, SND_DEFAULT_ENABLE},
#endif

#if defined(CONFIG_SOUND_VMIDI) && defined(CONFIG_MIDI)
	{SNDCARD_VMIDI, {0, 0, 0, -1}, SND_DEFAULT_ENABLE},
#endif

#ifdef CONFIG_SOUND_VIDC
	{ SNDCARD_VIDC, {0, 0, 0, 0}, SND_DEFAULT_ENABLE },
#endif

#ifdef CONFIG_SOUND_WAVEARTIST
	{ SNDCARD_WAVEARTIST, { CONFIG_WAVEARTIST_BASE, CONFIG_WAVEARTIST_IRQ, CONFIG_WAVEARTIST_DMA, CONFIG_WAVEARTIST_DMA2 }, SND_DEFAULT_ENABLE },
#endif
	{0, {0}, 0}
};

int num_sound_cards = sizeof(snd_installed_cards) / sizeof (struct card_info);
static int max_sound_cards =  sizeof(snd_installed_cards) / sizeof (struct card_info);

#else
int num_sound_cards = 0;
struct card_info snd_installed_cards[20] = {{0}};
static int max_sound_cards = 20;
#endif

#if defined(MODULE) || (!defined(linux) && !defined(_AIX))
int trace_init = 0;
#else
int trace_init = 1;
#endif

#else
extern struct audio_operations * audio_devs[MAX_AUDIO_DEV]; extern int num_audiodevs;
extern struct mixer_operations * mixer_devs[MAX_MIXER_DEV]; extern int num_mixers;
extern struct synth_operations * synth_devs[MAX_SYNTH_DEV+MAX_MIDI_DEV]; extern int num_synths;
extern struct midi_operations * midi_devs[MAX_MIDI_DEV]; extern int num_midis;
extern struct sound_timer_operations * sound_timer_devs[MAX_TIMER_DEV]; extern int num_sound_timers;

extern struct driver_info sound_drivers[];
extern int num_sound_drivers;
extern struct card_info snd_installed_cards[];
extern int num_sound_cards;

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

extern int sound_map_buffer (int dev, struct dma_buffparms *dmap, buffmem_desc *info);
int sndtable_probe (int unit, struct address_info *hw_config);
int sndtable_init_card (int unit, struct address_info *hw_config);
int sndtable_start_card (int unit, struct address_info *hw_config);
void sound_timer_init (struct sound_lowlev_timer *t, char *name);
void sound_dma_intr (int dev, struct dma_buffparms *dmap, int chan);

#define AUDIO_DRIVER_VERSION	2
#define MIXER_DRIVER_VERSION	2
int sound_install_audiodrv(int vers, char *name, struct audio_driver *driver,
			int driver_size, int flags, unsigned int format_mask,
			void *devc, int dma1, int dma2);
int sound_install_mixer(int vers, char *name, struct mixer_operations *driver,
			int driver_size, void *devc);

void sound_unload_audiodev(int dev);
void sound_unload_mixerdev(int dev);
void sound_unload_mididev(int dev);
void sound_unload_synthdev(int dev);
void sound_unload_timerdev(int dev);
int sound_alloc_audiodev(void);
int sound_alloc_mixerdev(void);
int sound_alloc_timerdev(void);
int sound_alloc_synthdev(void);
int sound_alloc_mididev(void);
#endif	/* _DEV_TABLE_H_ */

