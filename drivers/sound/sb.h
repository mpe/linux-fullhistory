#define DSP_RESET	(devc->base + 0x6)
#define DSP_READ	(devc->base + 0xA)
#define DSP_WRITE	(devc->base + 0xC)
#define DSP_COMMAND	(devc->base + 0xC)
#define DSP_STATUS	(devc->base + 0xC)
#define DSP_DATA_AVAIL	(devc->base + 0xE)
#define DSP_DATA_AVL16	(devc->base + 0xF)
#define MIXER_ADDR	(devc->base + 0x4)
#define MIXER_DATA	(devc->base + 0x5)
#define OPL3_LEFT	(devc->base + 0x0)
#define OPL3_RIGHT	(devc->base + 0x2)
#define OPL3_BOTH	(devc->base + 0x8)
/* DSP Commands */

#define DSP_CMD_SPKON		0xD1
#define DSP_CMD_SPKOFF		0xD3
#define DSP_CMD_DMAON		0xD0
#define DSP_CMD_DMAOFF		0xD4

#define IMODE_NONE		0
#define IMODE_OUTPUT		PCM_ENABLE_OUTPUT
#define IMODE_INPUT		PCM_ENABLE_INPUT
#define IMODE_INIT		3
#define IMODE_MIDI		4

#define NORMAL_MIDI	0
#define UART_MIDI	1


/*
 * Device models
 */
#define MDL_NONE	0
#define MDL_SB1		1	/* SB1.0 or 1.5 */
#define MDL_SB2		2	/* SB2.0 */
#define MDL_SB201	3	/* SB2.01 */
#define MDL_SBPRO	4	/* SB Pro */
#define MDL_SB16	5	/* SB16/32/AWE */
#define MDL_JAZZ	10	/* Media Vision Jazz16 */
#define MDL_SMW		11	/* Logitech Soundman Wave (Jazz16) */
#define MDL_ESS		12	/* ESS ES688 and ES1688 */
#define MDL_AZTECH	13	/* Aztech Sound Galaxy family */

/*
 * Config flags
 */
#define SB_NO_MIDI	0x00000001
#define SB_NO_MIXER	0x00000002
#define SB_NO_AUDIO	0x00000004
#define SB_NO_RECORDING	0x00000008 /* No audio recording */
#define SB_MIDI_ONLY	(SB_NO_AUDIO|SB_NO_MIXER)

struct mixer_def {
	unsigned int regno: 8;
	unsigned int bitoffs:4;
	unsigned int nbits:4;
};

typedef struct mixer_def mixer_tab[32][2];
typedef struct mixer_def mixer_ent;

typedef struct sb_devc {
	   int dev;

	/* Hardware parameters */
	   int *osp;
	   int minor, major;
	   int type;
	   int model, submodel;
	   int caps;
#	define SBCAP_STEREO	0x00000001
#	define SBCAP_16BITS	0x00000002

	/* Hardware resources */
	   int base;
	   int irq;
	   int dma8, dma16;

	/* State variables */
 	   int opened;
	   int speed, bits, channels;
	   volatile int irq_ok;
	   volatile int intr_active, irq_mode;

	/* Mixer fields */
	   int levels[SOUND_MIXER_NRDEVICES];
	   mixer_tab *iomap;
	   int mixer_caps, recmask, supported_devices;
	   int supported_rec_devices;
	   int my_mixerdev;

	/* Audio fields */
	   unsigned long trg_buf;
	   int      trigger_bits;
	   int      trg_bytes;
	   int      trg_intrflag;
	   int      trg_restart;
	   unsigned char tconst;
	   int my_dev;
	
	/* MIDI fields */
	   int my_mididev;
	   int input_opened;
	   void (*midi_input_intr) (int dev, unsigned char data);
	} sb_devc;

int sb_dsp_command (sb_devc *devc, unsigned char val);
int sb_dsp_get_byte (sb_devc *devc);
int sb_dsp_reset (sb_devc *devc);
void sb_setmixer (sb_devc *devc, unsigned int port, unsigned int value);
unsigned int sb_getmixer (sb_devc *devc, unsigned int port);
int sb_dsp_detect (struct address_info *hw_config);
void sb_dsp_init (struct address_info *hw_config);
void sb_dsp_unload(struct address_info *hw_config);
int sb_mixer_init(sb_devc *devc);
void smw_mixer_init(sb_devc *devc);
void sb_dsp_midi_init (sb_devc *devc);
void sb_audio_init (sb_devc *devc, char *name);
void sb_midi_interrupt (sb_devc *devc);
int ess_write (sb_devc *devc, unsigned char reg, unsigned char data);
int ess_read (sb_devc *devc, unsigned char reg);
