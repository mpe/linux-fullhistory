/*****************************************************************************
 *
 *      Trident 4D-Wave OSS driver for Linux 2.2.x
 *
 *	Driver: Alan Cox <alan@redhat.com>
 *
 *  Built from:
 *  	Low level code: <audio@tridentmicro.com> from ALSA
 *  	Framework: Thomas Sailer <sailer@ife.ee.ethz.ch>
 *  	Extended by: Zach Brown <zab@redhat.com>  
 *
 *	Hacked up by:
 *	  Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/*****************************************************************************/

      
#include <linux/module.h>
#include <linux/version.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/sound.h>
#include <linux/malloc.h>
#include <linux/soundcard.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include <asm/hardirq.h>

#ifdef CONFIG_APM
#include <linux/apm_bios.h>
#endif

#include "trident.h"
#include "ac97.h"

/* --------------------------------------------------------------------- */

#define M_DEBUG 1

#ifdef M_DEBUG
static int debug=0;
#define M_printk(args...) {if (debug) printk(args);}
#else
#define M_printk(x)
#endif

/* --------------------------------------------------------------------- */
#define DRIVER_VERSION "0.01"

#define TRIDENT_FMT_STEREO	0x01
#define TRIDENT_FMT_16BIT	0x02
#define TRIDENT_FMT_MASK	0x03
#define TRIDENT_DAC_SHIFT	0   
#define TRIDENT_ADC_SHIFT	4

#define TRIDENT_ENABLE_PE	1
#define TRIDENT_ENABLE_RE	2


#define TRIDENT_CARD_MAGIC	0x5072696E
#define TRIDENT_STATE_MAGIC	0x63657373

#define DAC_RUNNING		1
#define ADC_RUNNING		2

#define NR_DSPS		8

#define SND_DEV_DSP16   5 

static const unsigned sample_size[] = { 1, 2, 2, 4 };
static const unsigned sample_shift[] = { 0, 1, 1, 2 };

enum card_types_t {
	TYPE_4DWAVE_DX,
	TYPE_4DWAVE_NX
};

static const char *card_names[]={
	[TYPE_4DWAVE_DX] = "Trident 4DWave DX",
	[TYPE_4DWAVE_NX] = "Trident 4DWave NX",
};


typedef struct tChannelControl
{
    // register data
    unsigned int *  lpChStart;
    unsigned int *  lpChStop;
    unsigned int *  lpChAint;
    unsigned int *  lpChAinten;

    // register addresses
    unsigned int *  lpAChStart;
    unsigned int *  lpAChStop;
    unsigned int *  lpAChAint;
    unsigned int *  lpAChAinten;
    
    unsigned int data[16];

} CHANNELCONTROL;

/* --------------------------------------------------------------------- */

struct trident_state {
	unsigned int magic;
	int channel;
	struct trident_card *card;	/* Card info */
	/* wave stuff */
	unsigned int rateadc, ratedac;
	unsigned char fmt, enable;

	struct semaphore open_sem;
	mode_t open_mode;
	wait_queue_head_t open_wait;

	/* soundcore stuff */
	int dev_audio;

	struct dmabuf {
		void *rawbuf;
		unsigned buforder;
		unsigned numfrag;
		unsigned fragshift;
		int chan[2];	/* Hardware channel */
		/* XXX zab - swptr only in here so that it can be referenced by
			clear_advance, as far as I can tell :( */
		unsigned hwptr, swptr;
		unsigned total_bytes;
		int count;
		unsigned error; /* over/underrun */
		wait_queue_head_t wait;
		/* redundant, but makes calculations easier */
		unsigned fragsize;
		unsigned dmasize;
		unsigned fragsamples;
		/* OSS stuff */
		unsigned mapped:1;
		unsigned ready:1;
		unsigned endcleared:1;
		unsigned ossfragshift;
		int ossmaxfrags;
		unsigned subdivision;
		u16 base;		/* Offset for ptr */
	} dma_dac, dma_adc;
	
	u8 bDMAStart;

};
	
struct trident_card {
	unsigned int magic;

	/* We keep trident cards in a linked list */
	struct trident_card *next;

	/* The trident has a certain amount of cross channel interaction
	   so we use a single per card lock */
	   
	spinlock_t lock;

	int dev_mixer;

	int card_type;

	/* as most of this is static,
		perhaps it should be a pointer to a global struct */
	struct mixer_goo {
		int modcnt;
		int supported_mixers;
		int stereo_mixers;
		int record_sources;
		/* the caller must guarantee arg sanity before calling these */
/*		int (*read_mixer)(struct trident_card *card, int index);*/
		void (*write_mixer)(struct trident_card *card,int mixer, unsigned int left,unsigned int right);
		int (*recmask_io)(struct trident_card *card,int rw,int mask);
		unsigned int mixer_state[SOUND_MIXER_NRDEVICES];
	} mix;
	
	struct trident_state channels[NR_DSPS];

	/* hardware resources */
	unsigned long iobase;
	u32 irq;

	u32 ChanMap[2];
	int ChanPCMcnt;
	int ChanPCM;
	CHANNELCONTROL ChRegs;
	int ChanDwordCount;
};

static struct timer_list debug_timer;

#define IWriteAinten( x ) \
        {int i; \
         for( i= 0; i < ChanDwordCount; i++) \
		outl((x)->lpChAinten[i], TRID_REG(trident, (x)->lpAChAinten[i]));}

#define IReadAinten( x ) \
        {int i; \
         for( i= 0; i < ChanDwordCount; i++) \
         (x)->lpChAinten[i] = inl(TRID_REG(trident, (x)->lpAChAinten[i]));}

#define ReadAint( x ) \
        IReadAint( x ) 

#define WriteAint( x ) \
        IWriteAint( x ) 

#define IWriteAint( x ) \
        {int i; \
         for( i= 0; i < ChanDwordCount; i++) \
         outl((x)->lpChAint[i], TRID_REG(trident, (x)->lpAChAint[i]));}

#define IReadAint( x ) \
        {int i; \
         for( i= 0; i < ChanDwordCount; i++) \
         (x)->lpChAint[i] = inl(TRID_REG(trident, (x)->lpAChAint[i]));}


/*
 *	Trident support library routines
 */
 
/*---------------------------------------------------------------------------
   void ResetAinten( struct trident_state *trident, int ChannelNum)
  
   Description: This routine will disable interrupts and ack any 
                existing interrupts for specified channel.
  
   Parameters:  trident - pointer to target device class for 4DWave.
                ChannelNum - channel number 
  
   returns:     TRUE if everything went ok, else FALSE.
  
  ---------------------------------------------------------------------------*/

static void ResetAinten(struct trident_card * trident, int ChannelNum)
{
	unsigned int dwMask;
	unsigned int x = ChannelNum >> 5;
	unsigned int ChanDwordCount = trident->ChanDwordCount;

	IReadAinten(&trident->ChRegs);
	dwMask = 1 << (ChannelNum & 0x1f);
	trident->ChRegs.lpChAinten[x] &= ~dwMask;
	IWriteAinten(&trident->ChRegs);
	// Ack the channel in case the interrupt was set before we disable it.
	outl(dwMask, TRID_REG(trident, trident->ChRegs.lpAChAint[x]));
}

/*---------------------------------------------------------------------------
   void EnableEndInterrupts( struct trident_card *trident)
  
   Description: This routine will enable end of loop interrupts.
                End of loop interrupts will occur when a running
                channel reaches ESO.
  
   Parameters:  trident - pointer to target device class for 4DWave.
  
   returns:     TRUE if everything went ok, else FALSE.
  
  ---------------------------------------------------------------------------*/

static int EnableEndInterrupts(struct trident_card * trident)
{
	unsigned int GlobalControl;

	GlobalControl = inb(TRID_REG(trident, T4D_LFO_GC_CIR + 1));
	GlobalControl |= 0x10;
	outb(GlobalControl, TRID_REG(trident, T4D_LFO_GC_CIR + 1));

	M_printk("(trident) globctl=%02X\n", GlobalControl);
	M_printk("(trident) enabled end interrupts\n");
	return (TRUE);
}

/*---------------------------------------------------------------------------
e   void DisableEndInterrupts( struct trident_card *trident)
  
   Description: This routine will disable end of loop interrupts.
                End of loop interrupts will occur when a running
                channel reaches ESO.
  
   Parameters:  
                trident - pointer to target device class for 4DWave.
  
   returns:     TRUE if everything went ok, else FALSE.
  
  ---------------------------------------------------------------------------*/

static int DisableEndInterrupts(struct trident_card * trident)
{
	unsigned int GlobalControl;

	GlobalControl = inb(TRID_REG(trident, T4D_LFO_GC_CIR + 1));
	GlobalControl &= ~0x10;
	outb(GlobalControl, TRID_REG(trident, T4D_LFO_GC_CIR + 1));

	M_printk("(trident) disabled end interrupts\n");
	M_printk("(trident) globctl=%02X\n", GlobalControl);
	return (TRUE);
}

/*---------------------------------------------------------------------------
   void trident_enable_voice_irq( unsigned int HwChannel )
  
    Description: Enable an interrupt channel, any channel 0 thru n.
                 This routine automatically handles the fact that there are
                 more than 32 channels available.
  
    Parameters : HwChannel - Channel number 0 thru n.
                 trident - pointer to target device class for 4DWave.
  
    Return Value: None.
  
  ---------------------------------------------------------------------------*/
void trident_enable_voice_irq(struct trident_card * trident, unsigned int HwChannel)
{
	unsigned int x, Data, ChanDwordCount;

	x = HwChannel >> 5;
	Data = 1 << (HwChannel & 0x1f);
	ChanDwordCount = trident->ChanDwordCount;
	IReadAinten(&trident->ChRegs);
	trident->ChRegs.lpChAinten[x] |= Data;
	IWriteAinten(&trident->ChRegs);
	M_printk("(trident) enabled voice IRQ %d\n", HwChannel);
}

/*---------------------------------------------------------------------------
   void trident_disable_voice_irq( unsigned int HwChannel )
  
    Description: Disable an interrupt channel, any channel 0 thru n.
                 This routine automatically handles the fact that there are
                 more than 32 channels available.
  
    Parameters : HwChannel - Channel number 0 thru n.
                 trident - pointer to target device class for 4DWave.
  
    Return Value: None.
  
  ---------------------------------------------------------------------------*/
void trident_disable_voice_irq(struct trident_card * trident, unsigned int HwChannel)
{
	unsigned int x, Data, ChanDwordCount;

	x = HwChannel >> 5;
	Data = 1 << (HwChannel & 0x1f);
	ChanDwordCount = trident->ChanDwordCount;
	IReadAinten(&trident->ChRegs);
	trident->ChRegs.lpChAinten[x] &= ~Data;
	IWriteAinten(&trident->ChRegs);
	M_printk("(trident) disabled voice IRQ %d\n", HwChannel);
}

/*---------------------------------------------------------------------------
   unsigned int AllocateChannelPCM( void )
  
    Description: Allocate hardware channel by reverse order (63-0).
  
    Parameters :  trident - pointer to target device class for 4DWave.
  
    Return Value: hardware channel - 0-63 or -1 when no channel is available
  
  ---------------------------------------------------------------------------*/

static int AllocateChannelPCM(struct trident_card *trident)
{
	int idx;

	if (trident->ChanPCMcnt >= trident->ChanPCM)
	{
		M_printk(KERN_DEBUG "(trident) no channels available.\n");
		return -1;
	}
	for (idx = 31; idx >= 0; idx--) {
		if (!(trident->ChanMap[1] & (1 << idx))) {
			trident->ChanMap[1] |= 1 << idx;
			trident->ChanPCMcnt++;
			return idx + 32;
		}
	}
	for (idx = 31; idx >= 0; idx--) {
		if (!(trident->ChanMap[0] & (1 << idx))) {
			trident->ChanMap[0] |= 1 << idx;
			trident->ChanPCMcnt++;
			return idx;
		}
	}
	return -1;
}

/*---------------------------------------------------------------------------
   void FreeChannelPCM( int channel )
  
    Description: Free hardware channel.
  
    Parameters :  trident - pointer to target device class for 4DWave.
	          channel - hardware channel number 0-63
  
    Return Value: none
  
  ---------------------------------------------------------------------------*/

static void FreeChannelPCM(struct trident_card *trident, int channel)
{
	if (channel < 0 || channel > 63)
		return;
	if (trident->ChanMap[channel>>5] & (1 << (channel & 0x1f))) {
		trident->ChanMap[channel>>5] &= ~(1 << (channel & 0x1f));
		trident->ChanPCMcnt--;
	}
}

/*---------------------------------------------------------------------------
   void trident_start_voice( ULONG HwChannel )
  
    Description: Start a channel, any channel 0 thru n.
                 This routine automatically handles the fact that there are
                 more than 32 channels available.
  
    Parameters : HwChannel - Channel number 0 thru n.
                 trident - pointer to target device class for 4DWave.
  
    Return Value: None.
  
  ---------------------------------------------------------------------------*/
void trident_start_voice(struct trident_card * trident, unsigned int HwChannel)
{
	unsigned int x = HwChannel >> 5;
	unsigned int Data = 1 << (HwChannel & 0x1f);

	outl(Data, TRID_REG(trident, trident->ChRegs.lpAChStart[x]));
	M_printk("(trident) start voice %d\n", HwChannel);
}

/*---------------------------------------------------------------------------
   void trident_stop_voice( ULONG HwChannel )
  
    Description: Stop a channel, any channel 0 thru n.
                 This routine automatically handles the fact that there are
                 more than 32 channels available.
  
    Parameters : HwChannel - Channel number 0 thru n.
                 trident - pointer to target device class for 4DWave.
  
    Return Value: None.
  
  ---------------------------------------------------------------------------*/
void trident_stop_voice(struct trident_card * trident, unsigned int HwChannel)
{
	unsigned int x = HwChannel >> 5;
	unsigned int Data = 1 << (HwChannel & 0x1f);

	outl(Data, TRID_REG(trident, trident->ChRegs.lpAChStop[x]));
  M_printk("(trident) stop voice %d\n", HwChannel);
}

/*---------------------------------------------------------------------------
   int DidChannelInterrupt( )
  
   Description:  Check if interrupt channel occurred.
  
   Parameters :  trident - pointer to target device class for 4DWave.
  
   Return Value: TRUE if interrupt occurred, else FALSE.
  
  ---------------------------------------------------------------------------*/
static int DidChannelInterrupt(struct trident_card * trident, int channel)
{
	unsigned int ChanDwordCount = trident->ChanDwordCount;
	unsigned int x = channel >> 5;
	unsigned int dwMask = 1 << (channel & 0x1f);

	ReadAint(&trident->ChRegs);
	return (trident->ChRegs.lpChAint[x] & dwMask) ? TRUE : FALSE;
}

/*---------------------------------------------------------------------------
   void AckChannelInterrupt( )
  
   Description:  Acknowledge the interrupt bit for channel intrs.
  
   Parameters :  trident - pointer to target device class for 4DWave.
  
   Return Value: None
  
  ---------------------------------------------------------------------------*/
static void AckChannelInterrupt(struct trident_card * trident, int channel)
{
	unsigned int ChanDwordCount = trident->ChanDwordCount;
	unsigned int x = channel >> 5;
	unsigned int dwMask = 1 << (channel & 0x1f);

	ReadAint(&trident->ChRegs);
	trident->ChRegs.lpChAint[x] &= dwMask;
	IWriteAint(&trident->ChRegs);
}

/*---------------------------------------------------------------------------
   int LoadDeltaHw( unsigned int HwChannel, unsigned int Delta )
  
   Description: This routine writes Delta to the hardware.
  
   Parameters:  Delta - data to write (2 Bytes only)
                HwChannel - Hardware channel to write to.
   		trident - pointer to target device class for 4DWave.
  
   Returns:     TRUE if all goes well, else FALSE. 
  
  ---------------------------------------------------------------------------*/
static int LoadDeltaHw(struct trident_card * trident, unsigned int HwChannel, unsigned int Delta)
{

	outb(HwChannel, TRID_REG(trident, T4D_LFO_GC_CIR));

	if (trident->card_type != TYPE_4DWAVE_NX) {
		outw((unsigned short) Delta, TRID_REG(trident, CH_DX_ESO_DELTA));
	} 
	else			// ID_4DWAVE_NX
	{
		outb((unsigned char) Delta, TRID_REG(trident, CH_NX_DELTA_CSO + 3));
		outb((unsigned char) (Delta >> 8), TRID_REG(trident, CH_NX_DELTA_ESO + 3));
	}

	return TRUE;
}

/*---------------------------------------------------------------------------
   int LoadVirtualChannel( ULONG *Data, ULONG HwChannel)
  
   Description: This routine writes all required channel registers to hardware.
  
   Parameters:  *Data - a pointer to the data to write (5 ULONGS always).
                HwChannel - Hardware channel to write to.
   		trident - pointer to target device class for 4DWave.
  
   Returns:     TRUE if all goes well, else FALSE. 
  
  ---------------------------------------------------------------------------*/
static int LoadVirtualChannel(struct trident_card * trident, unsigned int *Data, unsigned int HwChannel)
{
	unsigned int ChanData[CHANNEL_REGS];
	unsigned int ULONGSToDo = CHANNEL_REGS;
	unsigned int i;
	unsigned int Address = CHANNEL_START;

	/* Copy the data first... Hack... Before mucking with Volume! */
	memcpy((unsigned char *) ChanData, (unsigned char *) Data, ULONGSToDo * 4);

	outb((unsigned char) HwChannel, TRID_REG(trident, T4D_LFO_GC_CIR));

	for (i = 0; i < ULONGSToDo; i++, Address += 4)
		outl(ChanData[i], TRID_REG(trident, Address));

	M_printk("(trident) load virtual channel %d\n", HwChannel);
	return TRUE;
}

/*---------------------------------------------------------------------------
   trident_write_voice_regs
  
   Description: This routine will write the 5 hardware channel registers
                to hardware.
  
   Paramters:   trident - pointer to target device class for 4DWave.
                Channel - Real or Virtual channel number.
                Each register field.
  
   Returns:     TRUE if all goes well, else FALSE. 
  
  ---------------------------------------------------------------------------*/
int trident_write_voice_regs(struct trident_card * trident,
			 unsigned int Channel,
			 unsigned int LBA,
			 unsigned int CSO,
			 unsigned int ESO,
			 unsigned int DELTA,
			 unsigned int ALPHA_FMS,
			 unsigned int FMC_RVOL_CVOL,
			 unsigned int GVSEL,
			 unsigned int PAN,
			 unsigned int VOL,
			 unsigned int CTRL,
			 unsigned int EC)
{
	unsigned int ChanData[CHANNEL_REGS + 1], FmcRvolCvol;

	ChanData[1] = LBA;
	ChanData[4] = (GVSEL << 31) |
	    ((PAN & 0x0000007f) << 24) |
	    ((VOL & 0x000000ff) << 16) |
	    ((CTRL & 0x0000000f) << 12) |
	    (EC & 0x00000fff);

	FmcRvolCvol = FMC_RVOL_CVOL & 0x0000ffff;

	if (trident->card_type != TYPE_4DWAVE_NX) 
	{
		ChanData[0] = (CSO << 16) | (ALPHA_FMS & 0x0000ffff);
		ChanData[2] = (ESO << 16) | (DELTA & 0x0ffff);
		ChanData[3] = FmcRvolCvol;
	}
	else			// ID_4DWAVE_NX
	{
		ChanData[0] = (DELTA << 24) | (CSO & 0x00ffffff);
		ChanData[2] = ((DELTA << 16) & 0xff000000) | (ESO & 0x00ffffff);
		ChanData[3] = (ALPHA_FMS << 16) | FmcRvolCvol;
	}

	LoadVirtualChannel(trident, ChanData, Channel);

	return TRUE;
}

static int compute_rate(u32 rate)
{
	int delta;
	// We special case 44100 and 8000 since rounding with the equation
	// does not give us an accurate enough value. For 11025 and 22050
	// the equation gives us the best answer. All other frequencies will
	// also use the equation. JDW
	if (rate == 44100)
		delta = 0xeb3;
	else if (rate == 8000)
		delta = 0x2ab;
	else if (rate == 48000)
		delta = 0x1000;
	else
		delta = (((rate << 12) + rate) / 48000) & 0x0000ffff;
	return delta;
}

/*---------------------------------------------------------------------------
   trident_set_dac_rate
  
   Description: This routine will set the sample rate for playback.
  
   Paramters:   trident - pointer to target device class for 4DWave.
                rate    - desired sample rate
                set     - actually write hardware if set is true.
  
   Returns:     The rate allowed by device.
  
  ---------------------------------------------------------------------------*/

static unsigned int trident_set_dac_rate(struct trident_state * trident,
					     unsigned int rate, int set)
{
	unsigned int delta;


	if (rate > 48000)
		rate = 48000;
	if (rate < 4000)
		rate = 4000;

	delta = compute_rate(rate);

	if(set)
	{
		//Select channel window
		outb(trident->dma_dac.chan[1], TRID_REG(trident->card, T4D_LFO_GC_CIR));

		if (trident->card->card_type != TYPE_4DWAVE_NX) 
			outw(delta,TRID_REG(trident->card,CH_DX_ESO_DELTA+2));
		else			// ID_4DWAVE_NX
		{
			outb( delta     & 0xff,TRID_REG(trident->card,CH_NX_DELTA_CSO));
			outb((delta>>8) & 0xff,TRID_REG(trident->card,CH_NX_DELTA_ESO));
		}
	}
	 
	M_printk("(trident) called trident_set_dac_rate : rate = %d, set = %d delta = %4x\n", 
			rate, set,delta);

	trident->ratedac = rate;
	return rate;
}

/*---------------------------------------------------------------------------
   trident_set_adc_rate
  
   Description: This routine will set the sample rate for capture.
  
   Paramters:   trident - pointer to target device class for 4DWave.
                rate    - desired sample rate
                set     - actually write hardware if set is true.
  
   Returns:     The rate allowed by device.
  
  ---------------------------------------------------------------------------*/

static unsigned int trident_set_adc_rate(struct trident_state * trident,
					     unsigned int rate,
					     int set)
{
	//snd_printk("trid: called trident_set_adc_rate\n");
	if (rate > 48000)
		rate = 48000;
	if (rate < 4000)
		rate = 4000;

	trident->rateadc = rate;
	/*
	 *	FIXME: hit the hardware
	 */

	return rate;
}
 
extern __inline__ unsigned ld2(unsigned int x)
{
	unsigned r = 0;
	
	if (x >= 0x10000) {
		x >>= 16;
		r += 16;
	}
	if (x >= 0x100) {
		x >>= 8;
		r += 8;
	}
	if (x >= 0x10) {
		x >>= 4;
		r += 4;
	}
	if (x >= 4) {
		x >>= 2;
		r += 2;
	}
	if (x >= 2)
		r++;
	return r;
}


/* --------------------------------------------------------------------- */

static struct trident_card *devs = NULL;

/* --------------------------------------------------------------------- */


/*
 *	Trident AC97 codec programming interface.
 */
	 
static void trident_ac97_set(struct trident_card *trident, u8 cmd, u16 val)
{
	unsigned int    address, data;
	unsigned short  count  = 0xffff;

	data = ((unsigned long)val) << 16;

	if (trident->card_type != TYPE_4DWAVE_NX)
	{
		address = DX_ACR0_AC97_W;
		/* read AC-97 write register status */
		do
		{
			if ((inw(TRID_REG(trident,address))&0x8000) == 0)
			break;
		}
		while(count--);

		data |= (0x8000 | (cmd & 0x000000ff));
	}
	else  // ID_4DWAVE_NX
	{
		address = NX_ACR1_AC97_W;
		/* read AC-97 write register status */
		do
		{
			if ((inw(TRID_REG(trident, address )) & 0x0800) == 0)
				break;
		}
		while (count--);
		data |= (0x0800 | (cmd & 0x000000ff));
	}
	if (count == 0) 
	{
		printk(KERN_ERR "trident: AC97 CODEC write timed out.\n");
		return;
	}

	outl(data, TRID_REG(trident, address));
}

static u16 trident_ac97_get(struct trident_card *trident, u8 cmd)
{
	unsigned int data = 0;
	unsigned short count = 0xffff;

	if (trident->card_type != TYPE_4DWAVE_NX)
	{
		data = (0x00008000L | (cmd & 0x000000ff));
		outl(data, TRID_REG(trident, DX_ACR1_AC97_R));
		do
		{
			data = inl(TRID_REG(trident, DX_ACR1_AC97_R));
			if ( ( data & 0x0008000 ) == 0 )
				break;
		}
		while(count--);
	}
	else  // ID_4DWAVE_NX
	{
		data = (0x00000800L | (cmd & 0x000000ff));
		outl(data, TRID_REG(trident, NX_ACR2_AC97_R_PRIMARY));
		do
		{
			data = inl(TRID_REG(trident, NX_ACR2_AC97_R_PRIMARY));
			if ( ( data & 0x00000C00 ) == 0 )
				break;
		}
		while(count--);
	}
	if ( count == 0 ) 
	{
		printk("trident: AC97 CODEC read timed out.\n");
		data = 0;
	}
	return ((unsigned short)(data >> 16));
}

/* OSS interface to the ac97s.. */

#define AC97_STEREO_MASK (SOUND_MASK_VOLUME|\
	SOUND_MASK_PCM|SOUND_MASK_LINE|SOUND_MASK_CD|\
	SOUND_MASK_VIDEO|SOUND_MASK_LINE1|SOUND_MASK_IGAIN)

#define AC97_SUPPORTED_MASK (AC97_STEREO_MASK | \
	SOUND_MASK_BASS|SOUND_MASK_TREBLE|SOUND_MASK_MIC|\
	SOUND_MASK_SPEAKER)

#define AC97_RECORD_MASK (SOUND_MASK_MIC|\
	SOUND_MASK_CD| SOUND_MASK_VIDEO| SOUND_MASK_LINE1| SOUND_MASK_LINE|\
	SOUND_MASK_PHONEIN)

#define supported_mixer(CARD,FOO) ( CARD->mix.supported_mixers & (1<<FOO) )

/* this table has default mixer values for all OSS mixers.
	be sure to fill it in if you add oss mixers
	to anyone's supported mixer defines */

/* possible __init */
static struct mixer_defaults {
	int mixer;
	unsigned int value;
} mixer_defaults[SOUND_MIXER_NRDEVICES] = {
		/* all values 0 -> 100 in bytes */
	{SOUND_MIXER_VOLUME,	0x3232},
	{SOUND_MIXER_BASS,	0x3232},
	{SOUND_MIXER_TREBLE,	0x3232},
	{SOUND_MIXER_SPEAKER,	0x3232},
	{SOUND_MIXER_MIC,	0x3232},
	{SOUND_MIXER_LINE,	0x3232},
	{SOUND_MIXER_CD,	0x3232},
	{SOUND_MIXER_VIDEO,	0x3232},
	{SOUND_MIXER_LINE1,	0x3232},
	{SOUND_MIXER_PCM,	0x3232},
	{SOUND_MIXER_IGAIN,	0x3232},
	{-1,0}
};
	
static struct ac97_mixer_hw {
	unsigned char offset;
	int scale;
} ac97_hw[SOUND_MIXER_NRDEVICES]= {
	[SOUND_MIXER_VOLUME]	=	{0x02,63},
	[SOUND_MIXER_BASS]	=	{0x08,15},
	[SOUND_MIXER_TREBLE]	=	{0x08,15},
	[SOUND_MIXER_SPEAKER]	=	{0x0a,15},
	[SOUND_MIXER_MIC]	=	{0x0e,31},
	[SOUND_MIXER_LINE]	=	{0x10,31},
	[SOUND_MIXER_CD]	=	{0x12,31},
	[SOUND_MIXER_VIDEO]	=	{0x14,31},
	[SOUND_MIXER_LINE1]	=	{0x16,31},
	[SOUND_MIXER_PCM]	=	{0x18,31},
	[SOUND_MIXER_IGAIN]	=	{0x1c,31}
};

#if 0 /* *shrug* removed simply because we never used it.
		feel free to implement again if needed */

/* reads the given OSS mixer from the ac97
	the caller must have insured that the ac97 knows
	about that given mixer, and should be holding a
	spinlock for the card */
static int ac97_read_mixer(struct trident_card *card, int mixer) 
{
	u16 val;
	int ret=0;
	struct ac97_mixer_hw *mh = &ac97_hw[mixer];

	val = trident_ac97_get(card , mh->offset);

	if(AC97_STEREO_MASK & (1<<mixer)) {
		/* nice stereo mixers .. */
		int left,right;

		left = (val >> 8)  & 0x7f;
		right = val  & 0x7f;

		if (mixer == SOUND_MIXER_IGAIN) {
			right = (right * 100) / mh->scale;
			left = (left * 100) / mh->scale;
		else {
			right = 100 - ((right * 100) / mh->scale);
			left = 100 - ((left * 100) / mh->scale);
		}

		ret = left | (right << 8);
	} else if (mixer == SOUND_MIXER_SPEAKER) {
		ret = 100 - ((((val & 0x1e)>>1) * 100) / mh->scale);
	} else if (mixer == SOUND_MIXER_MIC) {
		ret = 100 - (((val & 0x1f) * 100) / mh->scale);
	/*  the low bit is optional in the tone sliders and masking
		it lets is avoid the 0xf 'bypass'.. */
	} else if (mixer == SOUND_MIXER_BASS) {
		ret = 100 - ((((val >> 8) & 0xe) * 100) / mh->scale);
	} else if (mixer == SOUND_MIXER_TREBLE) {
		ret = 100 - (((val & 0xe) * 100) / mh->scale);
	}

	printk("read mixer %d (0x%x) %x -> %x\n",mixer,mh->offset,val,ret);

	return ret;
}
#endif

/* write the OSS encoded volume to the given OSS encoded mixer,
	again caller's job to make sure all is well in arg land,
	call with spinlock held */
static void ac97_write_mixer(struct trident_card *card, int mixer, unsigned int left, unsigned int right)
{
	u16 val=0;
	struct ac97_mixer_hw *mh = &ac97_hw[mixer];

	printk("(trident) wrote ac97 mixer %d (0x%x) %d,%d",mixer,mh->offset,left,right);

	if(AC97_STEREO_MASK & (1<<mixer)) {
		/* stereo mixers */


		if (mixer == SOUND_MIXER_IGAIN) {
			right = (right * mh->scale) / 100;
			left = (left * mh->scale) / 100;
		} else {
			right = ((100 - right) * mh->scale) / 100;
			left = ((100 - left) * mh->scale) / 100;
		}

		val = (left << 8) | right;

	} else if (mixer == SOUND_MIXER_SPEAKER) {
		val = (((100 - left) * mh->scale) / 100) << 1;
	} else if (mixer == SOUND_MIXER_MIC) {
		val = trident_ac97_get(card , mh->offset) & ~0x801f;
		val |= (((100 - left) * mh->scale) / 100);
	/*  the low bit is optional in the tone sliders and masking
		it lets us avoid the 0xf 'bypass'.. */
	} else if (mixer == SOUND_MIXER_BASS) {
		val = trident_ac97_get(card , mh->offset) & ~0x0f00;
		val |= ((((100 - left) * mh->scale) / 100) << 8) & 0x0e00;
	} else if (mixer == SOUND_MIXER_TREBLE)  {
		val = trident_ac97_get(card , mh->offset) & ~0x000f;
		val |= (((100 - left) * mh->scale) / 100) & 0x000e;
	}

	trident_ac97_set(card, mh->offset, val);
	
	printk(" -> %x\n",val);
}

/* the following tables allow us to go from 
	OSS <-> ac97 quickly. */

enum ac97_recsettings {
	AC97_REC_MIC=0,
	AC97_REC_CD,
	AC97_REC_VIDEO,
	AC97_REC_AUX,
	AC97_REC_LINE,
	AC97_REC_STEREO, /* combination of all enabled outputs..  */
	AC97_REC_MONO,        /*.. or the mono equivalent */
	AC97_REC_PHONE        
};

static unsigned int ac97_rm2oss[] = {
	[AC97_REC_MIC] = SOUND_MIXER_MIC, 
	[AC97_REC_CD] = SOUND_MIXER_CD, 
	[AC97_REC_VIDEO] = SOUND_MIXER_VIDEO, 
	[AC97_REC_AUX] = SOUND_MIXER_LINE1, 
	[AC97_REC_LINE] = SOUND_MIXER_LINE, 
	[AC97_REC_PHONE] = SOUND_MIXER_PHONEIN
};

/* indexed by bit position */
static unsigned int ac97_oss_rm[] = {
	[SOUND_MIXER_MIC] = AC97_REC_MIC,
	[SOUND_MIXER_CD] = AC97_REC_CD,
	[SOUND_MIXER_VIDEO] = AC97_REC_VIDEO,
	[SOUND_MIXER_LINE1] = AC97_REC_AUX,
	[SOUND_MIXER_LINE] = AC97_REC_LINE,
	[SOUND_MIXER_PHONEIN] = AC97_REC_PHONE
};
	
/* read or write the recmask 
	the ac97 can really have left and right recording
	inputs independantly set, but OSS doesn't seem to 
	want us to express that to the user. 
	the caller guarantees that we have a supported bit set,
	and they must be holding the card's spinlock */
	
static int ac97_recmask_io(struct trident_card *card, int rw, int mask) 
{
	unsigned int val;

	if (rw) {
		/* read it from the card */
		val = trident_ac97_get(card, 0x1a) & 0x7;
		return ac97_rm2oss[val];
	}

	/* else, write the first set in the mask as the
		output */	

	val = ffs(mask); 
	val = ac97_oss_rm[val-1];
	val |= val << 8;  /* set both channels */

	printk("trident: setting ac97 recmask to 0x%x\n",val);

	trident_ac97_set(card,0x1a,val);

	return 0;
};

/*
 *	Generic AC97 codec initialisation. Need to check there are no
 *	quirks for the Trident cards.
 */
 
static u16 trident_ac97_init(struct trident_card *trident)
{
	trident->mix.supported_mixers = AC97_SUPPORTED_MASK;
	trident->mix.stereo_mixers = AC97_STEREO_MASK;
	trident->mix.record_sources = AC97_RECORD_MASK;
/*	trident->mix.read_mixer = ac97_read_mixer;*/
	trident->mix.write_mixer = ac97_write_mixer;
	trident->mix.recmask_io = ac97_recmask_io;

	if(trident->card_type == TYPE_4DWAVE_NX)
	{
		unsigned short VendorID1, VendorID2;

		outl(0x02, TRID_REG(trident, NX_ACR0_AC97_COM_STAT));

		// 4 Speaker Codec initialization
		VendorID1 = trident_ac97_get(trident, AC97_VENDOR_ID1);
		VendorID2 = trident_ac97_get(trident, AC97_VENDOR_ID2);

		if( (VendorID1 == 0x8384) && (VendorID2 == 0x7608) )
		{
			// Sigmatel 9708.
            
                	unsigned short TestReg;
                	unsigned int DTemp;

                	trident_ac97_set(trident, AC97_SIGMATEL_CIC1, 0xABBAL);  
                	trident_ac97_set(trident, AC97_SIGMATEL_CIC2, 0x1000L);

			TestReg = trident_ac97_get(trident, AC97_SIGMATEL_BIAS2);

			if( TestReg != 0x8000 ) // Errata Notice.
			{
				trident_ac97_set(trident, AC97_SIGMATEL_BIAS1, 0xABBAL );
				trident_ac97_set(trident, AC97_SIGMATEL_BIAS2, 0x0007L );
			}
			else // Newer version
			{
				trident_ac97_set(trident, AC97_SIGMATEL_CIC2, 0x1001L );  // recommended
				trident_ac97_set(trident, AC97_SIGMATEL_DAC2INVERT, 0x0008L );
			}

			trident_ac97_set( trident, AC97_SURROUND_MASTER, 0x0000L );
			trident_ac97_set( trident, AC97_HEADPHONE_VOL,   0x8000L );

			DTemp = (unsigned int)trident_ac97_get( trident, AC97_GENERAL_PURPOSE );
			trident_ac97_set( trident, AC97_GENERAL_PURPOSE, DTemp & 0x0000FDFFL );  // bit9 = 0.

			DTemp = (unsigned int)trident_ac97_get( trident, AC97_MIC_VOL );
			trident_ac97_set( trident, AC97_MIC_VOL, DTemp | 0x00008000L );  // bit15 = 1.

			DTemp = inl(TRID_REG(trident, NX_ACR0_AC97_COM_STAT));
			outl(DTemp | 0x0010, TRID_REG(trident, NX_ACR0_AC97_COM_STAT));

		}
		else if((VendorID1 == 0x5452) && (VendorID2 == 0x4108) )
		{   // TriTech TR28028
			trident_ac97_set( trident, AC97_SURROUND_MASTER, 0x0000L );
	                trident_ac97_set( trident, AC97_EXTENDED_STATUS, 0x0000L );
		}
		else if((VendorID1 == 0x574D) && 
                	(VendorID2 >= 0x4C00) && (VendorID2 <= 0x4C0f))
		{   // Wolfson WM9704
			trident_ac97_set( trident, AC97_SURROUND_MASTER, 0x0000L );
		}
		else
		{
#if 0
			printk("trident: No four Speaker Support with on board CODEC\n") ;
#endif
		}

		// S/PDIF C Channel bits 0-31 : 48khz, SCMS disabled
		outl(0x200004, TRID_REG(trident, NX_SPCSTATUS));
		// Enable S/PDIF out, 48khz only from ac97 fifo
		outb(0x28, TRID_REG(trident, NX_SPCTRL_SPCSO+3));
	}
        else
        {
		outl(0x02, TRID_REG(trident, DX_ACR2_AC97_COM_STAT));
        }
	return 0;
}


/* this only fixes the output apu mode to be later set by start_dac and
	company.  output apu modes are set in trident_rec_setup */
static void set_fmt(struct trident_state *s, unsigned char mask, unsigned char data)
{
	s->fmt = (s->fmt & mask) | data;
	/* Set the chip ? */
}


/*
 *	Native play back driver 
 */

/* the mode passed should be already shifted and masked */

static void trident_play_setup(struct trident_state *trident, int mode, u32 rate, void *buffer, int size)
{
	unsigned int LBA;
	unsigned int Delta;
	unsigned int ESO;
	unsigned int CTRL;
	unsigned int FMC_RVOL_CVOL;
	unsigned int GVSEL;
	unsigned int PAN;
	unsigned int VOL;
	unsigned int EC;



	/* set Loop Back Address */
	LBA = virt_to_bus(buffer);

	Delta = compute_rate(rate);

	M_printk("(trident) rate, delta = %d %d\n", rate, Delta);

	/* set ESO */
	ESO = size;
	if (mode & TRIDENT_FMT_16BIT)
		ESO /= 2;
	if (mode & TRIDENT_FMT_STEREO)
		ESO /= 2;

	ESO = ESO - 1;
	//snd_printk("trid: ESO = %d\n", ESO);

	/* set ctrl mode
	   CTRL default: 8-bit (unsigned) mono, loop mode enabled
	 */
	CTRL = 0x00000001;
	if (mode & TRIDENT_FMT_16BIT)
	{
		CTRL |= 0x00000008;	// 16-bit data
		CTRL |= 0x00000002;	// signed data
	}
	if (mode&TRIDENT_FMT_STEREO)
		CTRL |= 0x00000004;	// stereo data

	//FMC_RVOL_CVOL = 0x0000c000;
	FMC_RVOL_CVOL = 0x0000ffff;
	GVSEL = 1;
	PAN = 0;
	VOL = 0;
	EC = 0;

	trident_write_voice_regs(trident->card,
			     trident->dma_dac.chan[1],
			     LBA,
			     0,	/* cso */
			     ESO,
			     Delta,
			     0,	/* alpha */
			     FMC_RVOL_CVOL,
			     GVSEL,
			     PAN,
			     VOL,
			     CTRL,
			     EC);

}

/*
 *	Native record driver 
 */

/* again, passed mode is alrady shifted/masked */

static void trident_rec_setup(struct trident_state *trident, int mode, u32 rate, void *buffer, int size)
{
	unsigned int LBA;
	unsigned int Delta;
	unsigned int ESO;
	unsigned int CTRL;
	unsigned int FMC_RVOL_CVOL;
	unsigned int GVSEL;
	unsigned int PAN;
	unsigned int VOL;
	unsigned int EC;
	unsigned char bValue;
	unsigned short wValue;
	unsigned int dwValue;
	unsigned short wRecCODECSamples;
	unsigned int dwChanFlags;
	struct trident_card *card = trident->card;

	// Enable AC-97 ADC (capture), disable capture interrupt
	if (trident->card->card_type != TYPE_4DWAVE_NX) 
	{
		bValue = inb(TRID_REG(card, DX_ACR2_AC97_COM_STAT));
		outb(bValue | 0x48, TRID_REG(card, DX_ACR2_AC97_COM_STAT));
	}
	else 
	{
		wValue = inw(TRID_REG(card, T4D_MISCINT));
		outw(wValue | 0x1000, TRID_REG(card, T4D_MISCINT));
	}

	// Initilize the channel and set channel Mode
	outb(0, TRID_REG(card, LEGACY_DMAR15));

	// Set DMA channel operation mode register
	bValue = inb(TRID_REG(card, LEGACY_DMAR11)) & 0x03;
	outb(bValue | 0x54, TRID_REG(card, LEGACY_DMAR11));

	// Set channel buffer Address
	LBA = virt_to_bus(buffer);
	outl(LBA, TRID_REG(card, LEGACY_DMAR0));

	/* set ESO */
	ESO = size;

	dwValue = inl(TRID_REG(card, LEGACY_DMAR4)) & 0xff000000;
	dwValue |= (ESO - 1) & 0x0000ffff;
	outl(dwValue, TRID_REG(card, LEGACY_DMAR4));

	// Set channel sample rate , 4.12 format
	dwValue = (((unsigned int) 48000L << 12) / (unsigned long) (rate));
	outw((unsigned short) dwValue, TRID_REG(card, T4D_SBDELTA_DELTA_R));

	// Set channel interrupt blk length
	if (mode & TRIDENT_FMT_16BIT) {
		wRecCODECSamples = (unsigned short) ((ESO >> 1) - 1);
		dwChanFlags = 0xffffb000;
	} else {
		wRecCODECSamples = (unsigned short) (ESO - 1);
		dwChanFlags = 0xffff1000;
	}

	dwValue = ((unsigned int) wRecCODECSamples) << 16;
	dwValue |= (unsigned int) (wRecCODECSamples) & 0x0000ffff;
	outl(dwValue, TRID_REG(card, T4D_SBBL_SBCL));

	// Right now, set format and start to run capturing, 
	// continuous run loop enable.
	trident->bDMAStart = 0x19;	// 0001 1001b

	if (mode & TRIDENT_FMT_16BIT)
		trident->bDMAStart |= 0xa0;
	if (mode & TRIDENT_FMT_STEREO)
		trident->bDMAStart |= 0x40;

	// Prepare capture intr channel

	Delta = ((((unsigned int) rate) << 12) / ((unsigned long) (48000L)));

	/* set Loop Back Address */
	LBA = virt_to_bus(buffer);

	/* set ESO */
	ESO = size;
	if (mode & TRIDENT_FMT_16BIT)
		ESO /= 2;
	if (mode & TRIDENT_FMT_STEREO)
		ESO /= 2;

	ESO = ESO - 1;
	//snd_printk("trid: ESO = %d\n", ESO);

	/* set ctrl mode
	   CTRL default: 8-bit (unsigned) mono, loop mode enabled
	 */
	CTRL = 0x00000001;
	if (mode & TRIDENT_FMT_16BIT)
		CTRL |= 0x00000008;	// 16-bit data
	/* XXX DO UNSIGNED XXX */
	//if (!(mode & SND_PCM1_MODE_U))
	//	CTRL |= 0x00000002;	// signed data
	if (mode& TRIDENT_FMT_STEREO)
		CTRL |= 0x00000004;	// stereo data

	FMC_RVOL_CVOL = 0x0000ffff;
	GVSEL = 1;
	PAN = 0xff;
	VOL = 0xff;
	EC = 0;

	trident_write_voice_regs(card,
			     trident->dma_adc.chan[0],
			     LBA,
			     0,	/* cso */
			     ESO,
			     Delta,
			     0,	/* alpha */
			     FMC_RVOL_CVOL,
			     GVSEL,
			     PAN,
			     VOL,
			     CTRL,
			     EC);

}

/* Playback pointer */
__inline__ unsigned int get_dmaa(struct trident_state *trident)
{
	unsigned int cso;
	unsigned int eso;

#if 0
	if (!(trident->enable & ADC_RUNNING))
		return 0;
#endif

	outb(trident->dma_dac.chan[1], TRID_REG(trident->card, T4D_LFO_GC_CIR));

	if (trident->card->card_type != TYPE_4DWAVE_NX) 
	{
		cso = inw(TRID_REG(trident->card, CH_DX_CSO_ALPHA_FMS + 2));
		eso = inw(TRID_REG(trident->card, CH_DX_ESO_DELTA + 2));
	}
	else			// ID_4DWAVE_NX
	{
		cso = (unsigned int) inl(TRID_REG(trident->card, CH_NX_DELTA_CSO)) & 0x00ffffff;
		eso = (unsigned int) inl(TRID_REG(trident->card, CH_NX_DELTA_ESO)) & 0x00ffffff;
	}
	M_printk("(trident) get_dmaa: chip reported %d.%d\n", cso, eso);
	cso++;

	if (cso > eso)
		cso = eso;

	if (trident->fmt & TRIDENT_FMT_16BIT)
		cso *= 2;
	if (trident->fmt & TRIDENT_FMT_STEREO)
		cso *= 2;
	return cso;
}

/* Record pointer */
extern __inline__ unsigned get_dmac(struct trident_state *trident)
{
	unsigned int cso;

	if (!(trident->enable&DAC_RUNNING))
		return 0;

	outb(trident->dma_adc.chan[0], TRID_REG(trident->card, T4D_LFO_GC_CIR));

	if (trident->card->card_type != TYPE_4DWAVE_NX) {
		cso = inw(TRID_REG(trident->card, CH_DX_CSO_ALPHA_FMS + 2));
	} else {		// ID_4DWAVE_NX 
		cso = (unsigned int) inl(TRID_REG(trident->card, CH_NX_DELTA_CSO)) & 0x00ffffff;
	}

	printk("(trident) get_dmac: chip reported %d\n", cso);
	
	cso++;

	if (trident->fmt & TRIDENT_FMT_16BIT)
		cso *= 2;
	if (trident->fmt & TRIDENT_FMT_STEREO)
		cso *= 2;
	return cso;
}

static void trident_interrupt(int irq, void *dev_id, struct pt_regs *regs);

static void trident_kick(unsigned long plop)
{
	trident_interrupt(5, (void *)plop, NULL);
	debug_timer.expires=jiffies+1;
	add_timer(&debug_timer);
}

/* Stop recording (lock held) */

extern inline void __stop_adc(struct trident_state *s)
{
	struct trident_card *trident = s->card;

	M_printk("(trident) stopping ADC\n");
	

	s->enable &= ~ADC_RUNNING;
	trident_disable_voice_irq(trident, s->dma_adc.chan[0]);
	outb(0x00, TRID_REG(trident, T4D_SBCTRL_SBE2R_SBDD));
	trident_disable_voice_irq(trident, s->dma_adc.chan[0]);
	trident_stop_voice(trident, s->dma_adc.chan[0]);
	ResetAinten(trident, s->dma_adc.chan[0]);
}	

extern inline void stop_adc(struct trident_state *s)
{
	unsigned long flags;
	struct trident_card *trident = s->card;

	spin_lock_irqsave(&trident->lock, flags);
	__stop_adc(s);
	spin_unlock_irqrestore(&trident->lock, flags);
}	

/* stop playback (lock held) */

extern inline void __stop_dac(struct trident_state *s)
{
	struct trident_card *trident = s->card;

	M_printk("(trident) stopping DAC\n");
	
	//trident_stop_voice(trident, s->dma_dac.chan[0]);
	//trident_disable_voice_irq(trident, s->dma_dac.chan[0]);
	trident_stop_voice(trident, s->dma_dac.chan[1]);
	trident_disable_voice_irq(trident, s->dma_dac.chan[1]);
	s->enable &= ~DAC_RUNNING;
}	

extern inline void stop_dac(struct trident_state *s)
{
	struct trident_card *trident = s->card;
	unsigned long flags;

	spin_lock_irqsave(&trident->lock, flags);
	__stop_dac(s);
	spin_unlock_irqrestore(&trident->lock, flags);
}	

static void start_dac(struct trident_state *s)
{
	unsigned long flags;
	struct trident_card *trident = s->card;

	spin_lock_irqsave(&s->card->lock, flags);
	if ((s->dma_dac.mapped || s->dma_dac.count > 0) && s->dma_dac.ready) 
	{
		s->enable |= DAC_RUNNING;
		trident_enable_voice_irq(trident, s->dma_dac.chan[1]);
		trident_start_voice(trident, s->dma_dac.chan[1]);
		//trident_start_voice(trident, s->dma_dac.chan[0]);
		M_printk("(trident) starting DAC\n");
	
	}
	spin_unlock_irqrestore(&s->card->lock, flags);
}	

static void start_adc(struct trident_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->card->lock, flags);
	if ((s->dma_adc.mapped || s->dma_adc.count < (signed)(s->dma_adc.dmasize - 2*s->dma_adc.fragsize)) 
	    && s->dma_adc.ready) {
		s->enable |= ADC_RUNNING;
		trident_enable_voice_irq(s->card, s->dma_adc.chan[0]);
		outb(s->bDMAStart, TRID_REG(s->card, T4D_SBCTRL_SBE2R_SBDD));
		trident_start_voice(s->card, s->dma_adc.chan[0]);
		M_printk("(trident) starting ADC\n");
	
	}
	spin_unlock_irqrestore(&s->card->lock, flags);
}	

/* --------------------------------------------------------------------- */

/* we allocate both buffers at once */
#define DMABUF_DEFAULTORDER (15-PAGE_SHIFT)
#define DMABUF_MINORDER 2

static void dealloc_dmabuf(struct dmabuf *db)
{
	unsigned long map, mapend;

	if (db->rawbuf) 
	{
		M_printk("(trident) freeing %p\n",db->rawbuf);
		/* undo marking the pages as reserved */
		mapend = MAP_NR(db->rawbuf + (PAGE_SIZE << db->buforder) - 1);
		for (map = MAP_NR(db->rawbuf); map <= mapend; map++)
			clear_bit(PG_reserved, &mem_map[map].flags);	
		free_pages((unsigned long)db->rawbuf, db->buforder);
	}
	db->rawbuf = NULL;
	db->mapped = db->ready = 0;
}

static int prog_dmabuf(struct trident_state *s, unsigned rec)
{
	struct dmabuf *db = rec ? &s->dma_adc : &s->dma_dac;
	unsigned rate = rec ? s->rateadc : s->ratedac;
	int order;
	unsigned bytepersec;
	unsigned bufs;
	unsigned long map, mapend;
	unsigned char fmt;
	unsigned long flags;

	spin_lock_irqsave(&s->card->lock, flags);
	fmt = s->fmt;
	if (rec) {
		s->enable &= ~TRIDENT_ENABLE_RE;
		fmt >>= TRIDENT_ADC_SHIFT;
	} else {
		s->enable &= ~TRIDENT_ENABLE_PE;
		fmt >>= TRIDENT_DAC_SHIFT;
	}
	spin_unlock_irqrestore(&s->card->lock, flags);
	fmt &= TRIDENT_FMT_MASK;

	db->hwptr = db->swptr = db->total_bytes = db->count = db->error = db->endcleared = 0;

	if (!db->rawbuf) {
		void *rawbuf;
		/* haha, this thing is hacked to hell and back.
			this is so ugly. */
		s->dma_dac.ready = s->dma_dac.mapped = 0;
		s->dma_adc.ready = s->dma_adc.mapped = 0;

		/* alloc as big a chunk as we can */
		for (order = DMABUF_DEFAULTORDER; order >= DMABUF_MINORDER; order--)
			if((rawbuf = (void *)__get_free_pages(GFP_KERNEL|GFP_DMA, order)))

				break;

		if (!rawbuf)
			return -ENOMEM;


		/* we allocated both buffers */
		s->dma_adc.rawbuf = rawbuf;
		s->dma_dac.rawbuf = rawbuf + ( PAGE_SIZE << (order - 1) );

		M_printk("(trident) allocated %ld bytes at %p\n",PAGE_SIZE<<order, db->rawbuf);

		s->dma_adc.buforder = s->dma_dac.buforder = order - 1;

		/* XXX these checks are silly now */
#if 0
		if ((virt_to_bus(db->rawbuf) ^ (virt_to_bus(db->rawbuf) + (PAGE_SIZE << order) - 1)) & ~0xffff)
			printk(KERN_DEBUG "trident: DMA buffer crosses 64k boundary: busaddr 0x%lx  size %ld\n", 
			       virt_to_bus(db->rawbuf), PAGE_SIZE << order);

#endif
		if ((virt_to_bus(db->rawbuf) + (PAGE_SIZE << order) - 1) & ~0xffffff)
			M_printk(KERN_DEBUG "(trident) DMA buffer beyond 16MB: busaddr 0x%lx  size %ld\n", 
			       virt_to_bus(db->rawbuf), PAGE_SIZE << order);

		/* now mark the pages as reserved; otherwise remap_page_range doesn't do what we want */
		mapend = MAP_NR(db->rawbuf + (PAGE_SIZE << order) - 1);
		for (map = MAP_NR(db->rawbuf); map <= mapend; map++)
			set_bit(PG_reserved, &mem_map[map].flags);
	}
	bytepersec = rate << sample_shift[fmt];
	bufs = PAGE_SIZE << db->buforder;
	if (db->ossfragshift) {
		if ((1000 << db->ossfragshift) < bytepersec)
			db->fragshift = ld2(bytepersec/1000);
		else
			db->fragshift = db->ossfragshift;
	} else {
	/*	lets hand out reasonable big ass buffers by default */
		db->fragshift = (db->buforder + PAGE_SHIFT -2);
#if 0
		db->fragshift = ld2(bytepersec/100/(db->subdivision ? db->subdivision : 1));
		if (db->fragshift < 3)
			db->fragshift = 3; 
#endif
	}
	db->numfrag = bufs >> db->fragshift;
	while (db->numfrag < 4 && db->fragshift > 3) {
		db->fragshift--;
		db->numfrag = bufs >> db->fragshift;
	}
	db->fragsize = 1 << db->fragshift;
	if (db->ossmaxfrags >= 4 && db->ossmaxfrags < db->numfrag)
		db->numfrag = db->ossmaxfrags;
	db->fragsamples = db->fragsize >> sample_shift[fmt];
	db->dmasize = db->numfrag << db->fragshift;

	memset(db->rawbuf, (fmt & TRIDENT_FMT_16BIT) ? 0 : 0x80, db->dmasize);

	spin_lock_irqsave(&s->card->lock, flags);
	if (rec) {
		trident_rec_setup(s, fmt, s->rateadc, 
			db->rawbuf, db->numfrag << db->fragshift);
	} else {
		trident_play_setup(s, fmt, s->ratedac, 
			db->rawbuf, db->numfrag << db->fragshift);
	}
	spin_unlock_irqrestore(&s->card->lock, flags);
	db->ready = 1;

	return 0;
}

/* only called by trident_write */

extern __inline__ void clear_advance(struct trident_state *s)
{
	unsigned char c = ((s->fmt >> TRIDENT_DAC_SHIFT) & TRIDENT_FMT_16BIT) ? 0 : 0x80;
	unsigned char *buf = s->dma_dac.rawbuf;
	unsigned bsize = s->dma_dac.dmasize;
	unsigned bptr = s->dma_dac.swptr;
	unsigned len = s->dma_dac.fragsize;

	if (bptr + len > bsize) {
		unsigned x = bsize - bptr;
		memset(buf + bptr, c, x);
		/* account for wrapping? */
		bptr = 0;
		len -= x;
	}
	memset(buf + bptr, c, len);
}

/* call with spinlock held! */
static void trident_update_ptr(struct trident_state *s)
{
	unsigned hwptr;
	int diff;

	/* update ADC pointer */
	if (s->dma_adc.ready) {
		hwptr = get_dmac(s) % s->dma_adc.dmasize;
		diff = (s->dma_adc.dmasize + hwptr - s->dma_adc.hwptr) % s->dma_adc.dmasize;
		s->dma_adc.hwptr = hwptr;
		s->dma_adc.total_bytes += diff;
		s->dma_adc.count += diff;
		if (s->dma_adc.count >= (signed)s->dma_adc.fragsize) 
			wake_up(&s->dma_adc.wait);
		if (!s->dma_adc.mapped) {
			if (s->dma_adc.count > (signed)(s->dma_adc.dmasize - ((3 * s->dma_adc.fragsize) >> 1))) {
				s->enable &= ~TRIDENT_ENABLE_RE;
				__stop_adc(s); 
				s->dma_adc.error++;
			}
		}
	}
	/* update DAC pointer */
	if (s->dma_dac.ready) 
	{
		/* this is so gross.  */
		hwptr = (/*s->dma_dac.dmasize -*/ get_dmaa(s)) % s->dma_dac.dmasize; 
		diff = (s->dma_dac.dmasize + hwptr - s->dma_dac.hwptr) % s->dma_dac.dmasize;
		M_printk("(trident) updating dac: hwptr: %d diff: %d\n",hwptr,diff);
		s->dma_dac.hwptr = hwptr;
		s->dma_dac.total_bytes += diff;
		if (s->dma_dac.mapped) 
		{
			s->dma_dac.count += diff;
			if (s->dma_dac.count >= (signed)s->dma_dac.fragsize) 
				wake_up(&s->dma_dac.wait);
		} 
		else 
		{
			s->dma_dac.count -= diff;
			M_printk("(trident) trident_update_ptr: diff: %d, count: %d\n", diff, s->dma_dac.count); 
			if (s->dma_dac.count <= 0) 
			{
				s->enable &= ~TRIDENT_ENABLE_PE;
				/* Lock already held */
				__stop_dac(s);
				/* brute force everyone back in sync, sigh */
				s->dma_dac.count = 0; 
				s->dma_dac.swptr = 0; 
				s->dma_dac.hwptr = 0; 
				s->dma_dac.error++;
			} 
			else if (s->dma_dac.count <= (signed)s->dma_dac.fragsize && 
					!s->dma_dac.endcleared) 
			{
				clear_advance(s);
				s->dma_dac.endcleared = 1;
			}

			if (s->dma_dac.count + (signed)s->dma_dac.fragsize <= (signed)s->dma_dac.dmasize) 
				wake_up(&s->dma_dac.wait);
		}
	}
}

/*
 *	Trident interrupt handlers.
 */
 
static void trident_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
        struct trident_state *s;
        struct trident_card *c = (struct trident_card *)dev_id;
	int i;
	u32 event;

	spin_lock(&c->lock);
	
	event = inl(TRID_REG(c, T4D_MISCINT));
	
//	if(event & 0x28)
//		printk("IRQ %04X (%08lX, %08lX)\n", event, c->iobase, TRID_REG(c, T4D_MISCINT));
	
	if(event & 8)
	{
		/* Midi - TODO */
	}	
	
	if(event & 0x20)
	{
		/*
		 *	Update the pointers for all channels we are running.
		 */
	 
		for(i=0;i<NR_DSPS;i++)
		{
			s=&c->channels[i];
			if(DidChannelInterrupt(c, i))
			{
				AckChannelInterrupt(c,i);
				if(s->dev_audio != -1)
					trident_update_ptr(s);
				else
				{
					/* Spurious ? */
					M_printk("(trident) spurious channel irq %d.\n", i);
					trident_stop_voice(c, i);
					trident_disable_voice_irq(c,i);
				}
			}
		}
		
	}	
	spin_unlock(&c->lock);
}


/* --------------------------------------------------------------------- */

static const char invalid_magic[] = KERN_CRIT "trident: invalid magic value in %s\n";

#define VALIDATE_MAGIC(FOO,MAG)                         \
({                                                \
	if (!(FOO) || (FOO)->magic != MAG) { \
		printk(invalid_magic,__FUNCTION__);            \
		return -ENXIO;                    \
	}                                         \
})

#define VALIDATE_STATE(a) VALIDATE_MAGIC(a,TRIDENT_STATE_MAGIC)
#define VALIDATE_CARD(a) VALIDATE_MAGIC(a,TRIDENT_CARD_MAGIC)

static void set_mixer(struct trident_card *card,unsigned int mixer, unsigned int val ) 
{
	unsigned int left,right;
	/* cleanse input a little */
	right = ((val >> 8)  & 0xff) ;
	left = (val  & 0xff) ;

	if(right > 100) right = 100;
	if(left > 100) left = 100;

	card->mix.mixer_state[mixer]=(right << 8) | left;
	card->mix.write_mixer(card,mixer,left,right);
}

static int mixer_ioctl(struct trident_card *card, unsigned int cmd, unsigned long arg)
{
	unsigned long flags;
	int i, val=0;

	VALIDATE_CARD(card);
        if (cmd == SOUND_MIXER_INFO) {
		mixer_info info;
		strncpy(info.id, card_names[card->card_type], sizeof(info.id));
		strncpy(info.name,card_names[card->card_type],sizeof(info.name));
		info.modify_counter = card->mix.modcnt;
		if (copy_to_user((void *)arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (cmd == SOUND_OLD_MIXER_INFO) {
		_old_mixer_info info;
		strncpy(info.id, card_names[card->card_type], sizeof(info.id));
		strncpy(info.name,card_names[card->card_type],sizeof(info.name));
		if (copy_to_user((void *)arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (cmd == OSS_GETVERSION)
		return put_user(SOUND_VERSION, (int *)arg);

	if (_IOC_TYPE(cmd) != 'M' || _IOC_SIZE(cmd) != sizeof(int))
                return -EINVAL;

        if (_IOC_DIR(cmd) == _IOC_READ) {
                switch (_IOC_NR(cmd)) {
                case SOUND_MIXER_RECSRC: /* give them the current record source */

			if(!card->mix.recmask_io) {
				val = 0;
			} else {
				spin_lock_irqsave(&card->lock, flags);
				val = card->mix.recmask_io(card,1,0);
				spin_unlock_irqrestore(&card->lock, flags);
			}
			break;
			
                case SOUND_MIXER_DEVMASK: /* give them the supported mixers */
			val = card->mix.supported_mixers;
			break;

                case SOUND_MIXER_RECMASK: /* Arg contains a bit for each supported recording source */
			val = card->mix.record_sources;
			break;
			
                case SOUND_MIXER_STEREODEVS: /* Mixer channels supporting stereo */
			val = card->mix.stereo_mixers;
			break;
			
                case SOUND_MIXER_CAPS:
			val = SOUND_CAP_EXCL_INPUT;
			break;

		default: /* read a specific mixer */
			i = _IOC_NR(cmd);

			if ( ! supported_mixer(card,i)) 
				return -EINVAL;

			/* do we ever want to touch the hardware? */
/*			spin_lock_irqsave(&s->lock, flags);
			val = card->mix.read_mixer(card,i);
			spin_unlock_irqrestore(&s->lock, flags);*/

			val = card->mix.mixer_state[i];
/*			printk("returned 0x%x for mixer %d\n",val,i);*/

			break;
		}
		return put_user(val,(int *)arg);
	}
	
        if (_IOC_DIR(cmd) != (_IOC_WRITE|_IOC_READ))
		return -EINVAL;
	
	card->mix.modcnt++;

	get_user_ret(val, (int *)arg, -EFAULT);

	switch (_IOC_NR(cmd)) {
	case SOUND_MIXER_RECSRC: /* Arg contains a bit for each recording source */

		if (!card->mix.recmask_io) return -EINVAL;
		if(! (val &= card->mix.record_sources)) return -EINVAL;

		spin_lock_irqsave(&card->lock, flags);
		card->mix.recmask_io(card,0,val);
		spin_unlock_irqrestore(&card->lock, flags);
		return 0;

	default:
		i = _IOC_NR(cmd);

		if ( ! supported_mixer(card,i)) 
			return -EINVAL;

		spin_lock_irqsave(&card->lock, flags);
		set_mixer(card,i,val);
		spin_unlock_irqrestore(&card->lock, flags);

		return 0;
	}
}

/* --------------------------------------------------------------------- */

static loff_t trident_llseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}

/* --------------------------------------------------------------------- */

static int trident_open_mixdev(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	struct trident_card *card = devs;

	while (card && card->dev_mixer != minor)
		card = card->next;
	if (!card)
		return -ENODEV;

	file->private_data = card;
	//FIXME put back in
	//MOD_INC_USE_COUNT;
	return 0;
}

static int trident_release_mixdev(struct inode *inode, struct file *file)
{
	struct trident_card *card = (struct trident_card *)file->private_data;

	VALIDATE_CARD(card);
	
	//FIXME put back in
	//MOD_DEC_USE_COUNT;
	return 0;
}

static int trident_ioctl_mixdev(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct trident_card *card = (struct trident_card *)file->private_data;

	VALIDATE_CARD(card);

	return mixer_ioctl(card, cmd, arg);
}

static /*const*/ struct file_operations trident_mixer_fops = {
	&trident_llseek,
	NULL,  /* read */
	NULL,  /* write */
	NULL,  /* readdir */
	NULL,  /* poll */
	&trident_ioctl_mixdev,
	NULL,  /* mmap */
	&trident_open_mixdev,
	NULL,	/* flush */
	&trident_release_mixdev,
	NULL,  /* fsync */
	NULL,  /* fasync */
	NULL,  /* check_media_change */
	NULL,  /* revalidate */
	NULL,  /* lock */
};

/* --------------------------------------------------------------------- */

static int drain_dac(struct trident_state *s, int nonblock)
{
        DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	int count;
	signed long tmo;

	if (s->dma_dac.mapped || !s->dma_dac.ready)
		return 0;
	current->state = TASK_INTERRUPTIBLE;
	add_wait_queue(&s->dma_dac.wait, &wait);

	for (;;) 
	{
		spin_lock_irqsave(&s->card->lock, flags);
		count = s->dma_dac.count;
		spin_unlock_irqrestore(&s->card->lock, flags);

		if (count <= 0)
			break;

		if (signal_pending(current))
			break;

		if (nonblock) 
		{
			remove_wait_queue(&s->dma_dac.wait, &wait);
			current->state = TASK_RUNNING;
			return -EBUSY;
		}

		tmo = (count * HZ) / s->ratedac;
		tmo >>= sample_shift[(s->fmt >> TRIDENT_DAC_SHIFT) & TRIDENT_FMT_MASK];

		/* XXX this is just broken.  someone is waking us up alot, or schedule_timeout is broken.
		or something.  who cares. - zach */
		if (!schedule_timeout(tmo ? tmo : 1) && tmo)
			printk(KERN_DEBUG "trident: dma timed out?? %ld\n",jiffies);
	}
	remove_wait_queue(&s->dma_dac.wait, &wait);
	current->state = TASK_RUNNING;
	if (signal_pending(current))
		return -ERESTARTSYS;
	return 0;
}

/* --------------------------------------------------------------------- */

/* in this loop, dma_adc.count signifies the amount of data thats waiting
	to be copied to the user's buffer.  it is filled by the interrupt
	handler and drained by this loop. */
static ssize_t trident_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
	struct trident_state *s = (struct trident_state *)file->private_data;
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	int cnt;
	
	VALIDATE_STATE(s);
	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (s->dma_adc.mapped)
		return -ENXIO;
	if (!s->dma_adc.ready && (ret = prog_dmabuf(s, 1)))
		return ret;
	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;
	ret = 0;

	while (count > 0) {
		spin_lock_irqsave(&s->card->lock, flags);
		/* remember, all these things are expressed in bytes to be
			sent to the user.. hence the evil / 2 down below */
		swptr = s->dma_adc.swptr;
		cnt = s->dma_adc.dmasize-swptr;
		if (s->dma_adc.count < cnt)
			cnt = s->dma_adc.count;
		spin_unlock_irqrestore(&s->card->lock, flags);

		if (cnt > count)
			cnt = count;

		if (cnt <= 0) {
			start_adc(s);
			if (file->f_flags & O_NONBLOCK) 
			{
				ret = ret ? ret : -EAGAIN;
				return ret;
			}
			if (!interruptible_sleep_on_timeout(&s->dma_adc.wait, HZ)) {
				M_printk(KERN_DEBUG "(trident) read: chip lockup? dmasz %u fragsz %u count %i hwptr %u swptr %u\n",
				       s->dma_adc.dmasize, s->dma_adc.fragsize, s->dma_adc.count, 
				       s->dma_adc.hwptr, s->dma_adc.swptr);
				stop_adc(s);
				spin_lock_irqsave(&s->card->lock, flags);
//				set_dmac(s, virt_to_bus(s->dma_adc.rawbuf), s->dma_adc.numfrag << s->dma_adc.fragshift);
				s->dma_adc.count = s->dma_adc.hwptr = s->dma_adc.swptr = 0;
				spin_unlock_irqrestore(&s->card->lock, flags);
			}
			if (signal_pending(current)) 
			{
				ret = ret ? ret : -ERESTARTSYS;
				return ret;
			}
			continue;
		}
	
		if (copy_to_user(buffer, s->dma_adc.rawbuf + swptr, cnt)) {
			ret = ret ? ret : -EFAULT;
			return ret;
		}

		swptr = (swptr + cnt) % s->dma_adc.dmasize;
		spin_lock_irqsave(&s->card->lock, flags);
		s->dma_adc.swptr = swptr;
		s->dma_adc.count -= cnt;
		spin_unlock_irqrestore(&s->card->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		start_adc(s);
	}

	return ret;
}

static ssize_t trident_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
	struct trident_state *s = (struct trident_state *)file->private_data;
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	int cnt;
	int mode = (s->fmt >> TRIDENT_DAC_SHIFT) & TRIDENT_FMT_MASK;

	M_printk("(trident) trident_write: count %d\n", count);
	
	VALIDATE_STATE(s);
	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (s->dma_dac.mapped)
		return -ENXIO;
	if (!s->dma_dac.ready && (ret = prog_dmabuf(s, 0)))
		return ret;
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
	ret = 0;

	while (count > 0) {
		spin_lock_irqsave(&s->card->lock, flags);

		if (s->dma_dac.count < 0) 
		{
			s->dma_dac.count = 0;
			s->dma_dac.swptr = s->dma_dac.hwptr;
		}
		swptr = s->dma_dac.swptr;

		cnt = s->dma_dac.dmasize-swptr;

		if (s->dma_dac.count + cnt > s->dma_dac.dmasize)
			cnt = s->dma_dac.dmasize - s->dma_dac.count;

		spin_unlock_irqrestore(&s->card->lock, flags);

		if (cnt > count)
			cnt = count;

		if (cnt <= 0) 
		{
			/* buffer is full, wait for it to be played */
			start_dac(s);
			if (file->f_flags & O_NONBLOCK) 
			{
				if(!ret) ret = -EAGAIN;
				return ret;
			}
			if (!interruptible_sleep_on_timeout(&s->dma_dac.wait, HZ)) 
			{
				M_printk(KERN_DEBUG 
						"trident: write: chip lockup? dmasz %u fragsz %u count %i hwptr %u swptr %u\n", 
						s->dma_dac.dmasize, s->dma_dac.fragsize, s->dma_dac.count, s->dma_dac.hwptr, 
						s->dma_dac.swptr);
				stop_dac(s);
				spin_lock_irqsave(&s->card->lock, flags);
//			set_dmaa(s, virt_to_bus(s->dma_dac.rawbuf), s->dma_dac.numfrag << s->dma_dac.fragshift);
				s->dma_dac.count = s->dma_dac.hwptr = s->dma_dac.swptr = 0;
				spin_unlock_irqrestore(&s->card->lock, flags);
			}
			if (signal_pending(current)) 
			{
				if (!ret) ret = -ERESTARTSYS;
				return ret;
			}
			continue;
		}
		if (copy_from_user(s->dma_dac.rawbuf + swptr, buffer, cnt)) 
		{
			if (!ret)
				ret = -EFAULT;
			return ret;
		}

		swptr = (swptr + cnt) % s->dma_dac.dmasize;

		spin_lock_irqsave(&s->card->lock, flags);
		s->dma_dac.swptr = swptr;
		s->dma_dac.count += cnt;
		s->dma_dac.endcleared = 0;
		spin_unlock_irqrestore(&s->card->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		start_dac(s);
	}
	return ret;
}

static unsigned int trident_poll(struct file *file, struct poll_table_struct *wait)
{
	struct trident_state *s = (struct trident_state *)file->private_data;
	unsigned long flags;
	unsigned int mask = 0;

	VALIDATE_STATE(s);
	if (file->f_mode & FMODE_WRITE)
		poll_wait(file, &s->dma_dac.wait, wait);
	if (file->f_mode & FMODE_READ)
		poll_wait(file, &s->dma_adc.wait, wait);
	spin_lock_irqsave(&s->card->lock, flags);
	trident_update_ptr(s);
	if (file->f_mode & FMODE_READ) {
		if (s->dma_adc.count >= (signed)s->dma_adc.fragsize)
			mask |= POLLIN | POLLRDNORM;
	}
	if (file->f_mode & FMODE_WRITE) {
		if (s->dma_dac.mapped) {
			if (s->dma_dac.count >= (signed)s->dma_dac.fragsize) 
				mask |= POLLOUT | POLLWRNORM;
		} else {
			if ((signed)s->dma_dac.dmasize >= s->dma_dac.count + (signed)s->dma_dac.fragsize)
				mask |= POLLOUT | POLLWRNORM;
		}
	}
	spin_unlock_irqrestore(&s->card->lock, flags);
	return mask;
}

/* this needs to be fixed to deal with the dual apus/buffers */
#if 0
static int trident_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct trident_state *s = (struct trident_state *)file->private_data;
	struct dmabuf *db;
	int ret;
	unsigned long size;

	VALIDATE_STATE(s);
	if (vma->vm_flags & VM_WRITE) {
		if ((ret = prog_dmabuf(s, 1)) != 0)
			return ret;
		db = &s->dma_dac;
	} else if (vma->vm_flags & VM_READ) {
		if ((ret = prog_dmabuf(s, 0)) != 0)
			return ret;
		db = &s->dma_adc;
	} else 
		return -EINVAL;
	if (vma->vm_offset != 0)
		return -EINVAL;
	size = vma->vm_end - vma->vm_start;
	if (size > (PAGE_SIZE << db->buforder))
		return -EINVAL;
	if (remap_page_range(vma->vm_start, virt_to_phys(db->rawbuf), size, vma->vm_page_prot))
		return -EAGAIN;
	db->mapped = 1;
	return 0;
}
#endif

static int trident_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct trident_state *s = (struct trident_state *)file->private_data;
	unsigned long flags;
        audio_buf_info abinfo;
        count_info cinfo;
	int val, mapped, ret;
	unsigned char fmtm, fmtd;

/*	printk("trident: trident_ioctl: cmd %d\n", cmd);*/
	
	VALIDATE_STATE(s);
  mapped = ((file->f_mode & FMODE_WRITE) && s->dma_dac.mapped) ||
		((file->f_mode & FMODE_READ) && s->dma_adc.mapped);

	switch (cmd) 
	{
		case OSS_GETVERSION:
		return put_user(SOUND_VERSION, (int *)arg);

		case SNDCTL_DSP_SYNC:
			if (file->f_mode & FMODE_WRITE)
				return drain_dac(s, file->f_flags & O_NONBLOCK);
		return 0;

		case SNDCTL_DSP_SETDUPLEX:
		/* XXX fix */
		return 0;

		case SNDCTL_DSP_GETCAPS:
			return put_user(0/*DSP_CAP_DUPLEX | DSP_CAP_REALTIME | DSP_CAP_TRIGGER | DSP_CAP_MMAP*/, (int *)arg);

		case SNDCTL_DSP_RESET:
			if (file->f_mode & FMODE_WRITE) 
			{
				stop_dac(s);
				synchronize_irq();
				s->dma_dac.swptr = s->dma_dac.hwptr = s->dma_dac.count = s->dma_dac.total_bytes = 0;
			}
			if (file->f_mode & FMODE_READ) 
			{
				stop_adc(s);
				synchronize_irq();
				s->dma_adc.swptr = s->dma_adc.hwptr = s->dma_adc.count = s->dma_adc.total_bytes = 0;
			}
		return 0;

		case SNDCTL_DSP_SPEED:
			get_user_ret(val, (int *)arg, -EFAULT);
			if (val >= 0) 
			{
				if (file->f_mode & FMODE_READ) 
				{
					stop_adc(s);
					s->dma_adc.ready = 0;
					trident_set_adc_rate(s, val, 1);
				}
				if (file->f_mode & FMODE_WRITE) 
				{
					stop_dac(s);
					s->dma_dac.ready = 0;
					trident_set_dac_rate(s, val, 1);
				}
			}
		return put_user((file->f_mode & FMODE_READ) ? s->rateadc : s->ratedac, (int *)arg);

		case SNDCTL_DSP_STEREO:
			get_user_ret(val, (int *)arg, -EFAULT);
			fmtd = 0;
			fmtm = ~0;
			if (file->f_mode & FMODE_READ) 
			{
				stop_adc(s);
				s->dma_adc.ready = 0;
				if (val)
					fmtd |= TRIDENT_FMT_STEREO << TRIDENT_ADC_SHIFT;
				else
					fmtm &= ~(TRIDENT_FMT_STEREO << TRIDENT_ADC_SHIFT);
			}
			if (file->f_mode & FMODE_WRITE) 
			{
				stop_dac(s);
				s->dma_dac.ready = 0;
				if (val)
					fmtd |= TRIDENT_FMT_STEREO << TRIDENT_DAC_SHIFT;
				else
					fmtm &= ~(TRIDENT_FMT_STEREO << TRIDENT_DAC_SHIFT);
			}
			set_fmt(s, fmtm, fmtd);

		return 0;

		case SNDCTL_DSP_CHANNELS:
			get_user_ret(val, (int *)arg, -EFAULT);
			if (val != 0) 
			{
				fmtd = 0;
				fmtm = ~0;

				if (file->f_mode & FMODE_READ) 
				{
				stop_adc(s);
				s->dma_adc.ready = 0;
				if (val >= 2)
					fmtd |= TRIDENT_FMT_STEREO << TRIDENT_ADC_SHIFT;
				else
					fmtm &= ~(TRIDENT_FMT_STEREO << TRIDENT_ADC_SHIFT);
				}

				if (file->f_mode & FMODE_WRITE) 
				{
					stop_dac(s);
					s->dma_dac.ready = 0;
					if (val >= 2)
						fmtd |= TRIDENT_FMT_STEREO << TRIDENT_DAC_SHIFT;
					else
						fmtm &= ~(TRIDENT_FMT_STEREO << TRIDENT_DAC_SHIFT);
				}
				set_fmt(s, fmtm, fmtd);
			}
		return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? 
							(TRIDENT_FMT_STEREO << TRIDENT_ADC_SHIFT) : 
							(TRIDENT_FMT_STEREO << TRIDENT_DAC_SHIFT))) ? 2 : 1, (int *)arg);

		case SNDCTL_DSP_GETFMTS: /* Returns a mask */
		return put_user(AFMT_S8|AFMT_S16_LE, (int *)arg);

		case SNDCTL_DSP_SETFMT: /* Selects ONE fmt*/
			get_user_ret(val, (int *)arg, -EFAULT);
			if (val != AFMT_QUERY) 
			{
				fmtd = 0;
				fmtm = ~0;
				if (file->f_mode & FMODE_READ) 
				{
					stop_adc(s);
					s->dma_adc.ready = 0;
					/* fixed at 16bit for now */
					fmtd |= TRIDENT_FMT_16BIT << TRIDENT_ADC_SHIFT;
#if 0
					if (val == AFMT_S16_LE)
					fmtd |= TRIDENT_FMT_16BIT << TRIDENT_ADC_SHIFT;
					else
					fmtm &= ~(TRIDENT_FMT_16BIT << TRIDENT_ADC_SHIFT);
#endif
				}
				if (file->f_mode & FMODE_WRITE) 
				{
					stop_dac(s);
					s->dma_dac.ready = 0;
					if (val == AFMT_S16_LE)
						fmtd |= TRIDENT_FMT_16BIT << TRIDENT_DAC_SHIFT;
					else
						fmtm &= ~(TRIDENT_FMT_16BIT << TRIDENT_DAC_SHIFT);
				}
				set_fmt(s, fmtm, fmtd);
			}
		return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? 
			(TRIDENT_FMT_16BIT << TRIDENT_ADC_SHIFT) : 
			(TRIDENT_FMT_16BIT << TRIDENT_DAC_SHIFT))) ? 
			AFMT_S16_LE : AFMT_S8, (int *)arg);

		case SNDCTL_DSP_POST:
		return 0;

		case SNDCTL_DSP_GETTRIGGER:
			val = 0;
			if (file->f_mode & FMODE_READ && s->enable & TRIDENT_ENABLE_RE) 
				val |= PCM_ENABLE_INPUT;
			if (file->f_mode & FMODE_WRITE && s->enable & TRIDENT_ENABLE_PE) 
				val |= PCM_ENABLE_OUTPUT;
		return put_user(val, (int *)arg);

		case SNDCTL_DSP_SETTRIGGER:
			get_user_ret(val, (int *)arg, -EFAULT);
			if (file->f_mode & FMODE_READ) 
			{
				if (val & PCM_ENABLE_INPUT) 
				{
					if (!s->dma_adc.ready && (ret =  prog_dmabuf(s, 1)))
						return ret;
					start_adc(s);
				} 
				else
					stop_adc(s);
			}
			if (file->f_mode & FMODE_WRITE) 
			{
				if (val & PCM_ENABLE_OUTPUT) 
				{
					if (!s->dma_dac.ready && (ret = prog_dmabuf(s, 0)))
						return ret;
					start_dac(s);
				} 
				else
					stop_dac(s);
			}
		return 0;

		case SNDCTL_DSP_GETOSPACE:
			if (!(file->f_mode & FMODE_WRITE))
				return -EINVAL;
			if (!(s->enable & TRIDENT_ENABLE_PE) && (val = prog_dmabuf(s, 0)) != 0)
				return val;
			spin_lock_irqsave(&s->card->lock, flags);
			trident_update_ptr(s);
			abinfo.fragsize = s->dma_dac.fragsize;
			abinfo.bytes = s->dma_dac.dmasize - s->dma_dac.count;
			abinfo.fragstotal = s->dma_dac.numfrag;
			abinfo.fragments = abinfo.bytes >> s->dma_dac.fragshift;      
			spin_unlock_irqrestore(&s->card->lock, flags);
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

		case SNDCTL_DSP_GETISPACE:
			if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
			if (!(s->enable & TRIDENT_ENABLE_RE) && (val = prog_dmabuf(s, 1)) != 0)
			return val;
			spin_lock_irqsave(&s->card->lock, flags);
			trident_update_ptr(s);
			abinfo.fragsize = s->dma_adc.fragsize;
			abinfo.bytes = s->dma_adc.count;
			abinfo.fragstotal = s->dma_adc.numfrag;
			abinfo.fragments = abinfo.bytes >> s->dma_adc.fragshift;      
			spin_unlock_irqrestore(&s->card->lock, flags);
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

		case SNDCTL_DSP_NONBLOCK:
			file->f_flags |= O_NONBLOCK;
		return 0;

		case SNDCTL_DSP_GETODELAY:
			if (!(file->f_mode & FMODE_WRITE))
				return -EINVAL;
			spin_lock_irqsave(&s->card->lock, flags);
			trident_update_ptr(s);
			val = s->dma_dac.count;
			spin_unlock_irqrestore(&s->card->lock, flags);
		return put_user(val, (int *)arg);

		case SNDCTL_DSP_GETIPTR:
			if (!(file->f_mode & FMODE_READ))
				return -EINVAL;
			spin_lock_irqsave(&s->card->lock, flags);
			trident_update_ptr(s);
			cinfo.bytes = s->dma_adc.total_bytes;
			cinfo.blocks = s->dma_adc.count >> s->dma_adc.fragshift;
			cinfo.ptr = s->dma_adc.hwptr;
			if (s->dma_adc.mapped)
				s->dma_adc.count &= s->dma_adc.fragsize-1;
			spin_unlock_irqrestore(&s->card->lock, flags);
		return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));

		case SNDCTL_DSP_GETOPTR:
			if (!(file->f_mode & FMODE_WRITE))
				return -EINVAL;
			spin_lock_irqsave(&s->card->lock, flags);
			trident_update_ptr(s);
			cinfo.bytes = s->dma_dac.total_bytes;
			cinfo.blocks = s->dma_dac.count >> s->dma_dac.fragshift;
			cinfo.ptr = s->dma_dac.hwptr;
			if (s->dma_dac.mapped)
				s->dma_dac.count &= s->dma_dac.fragsize-1;
			spin_unlock_irqrestore(&s->card->lock, flags);
		return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));

		case SNDCTL_DSP_GETBLKSIZE:
			if (file->f_mode & FMODE_WRITE) 
			{
				if ((val = prog_dmabuf(s, 0)))
					return val;
				return put_user(s->dma_dac.fragsize, (int *)arg);
			}
			if ((val = prog_dmabuf(s, 1)))
				return val;
		return put_user(s->dma_adc.fragsize, (int *)arg);

		case SNDCTL_DSP_SETFRAGMENT:
			get_user_ret(val, (int *)arg, -EFAULT);
			if (file->f_mode & FMODE_READ) 
			{
				s->dma_adc.ossfragshift = val & 0xffff;
				s->dma_adc.ossmaxfrags = (val >> 16) & 0xffff;
				if (s->dma_adc.ossfragshift < 4)
					s->dma_adc.ossfragshift = 4;
				if (s->dma_adc.ossfragshift > 15)
					s->dma_adc.ossfragshift = 15;
				if (s->dma_adc.ossmaxfrags < 4)
					s->dma_adc.ossmaxfrags = 4;
			}
			if (file->f_mode & FMODE_WRITE) 
			{
				s->dma_dac.ossfragshift = val & 0xffff;
				s->dma_dac.ossmaxfrags = (val >> 16) & 0xffff;
				if (s->dma_dac.ossfragshift < 4)
					s->dma_dac.ossfragshift = 4;
				if (s->dma_dac.ossfragshift > 15)
					s->dma_dac.ossfragshift = 15;
				if (s->dma_dac.ossmaxfrags < 4)
					s->dma_dac.ossmaxfrags = 4;
			}
		return 0;

		case SNDCTL_DSP_SUBDIVIDE:
			if ((file->f_mode & FMODE_READ && s->dma_adc.subdivision) || 
					(file->f_mode & FMODE_WRITE && s->dma_dac.subdivision))
				return -EINVAL;
			get_user_ret(val, (int *)arg, -EFAULT);
			if (val != 1 && val != 2 && val != 4)
				return -EINVAL;
			if (file->f_mode & FMODE_READ)
				s->dma_adc.subdivision = val;
			if (file->f_mode & FMODE_WRITE)
				s->dma_dac.subdivision = val;
		return 0;

		case SOUND_PCM_READ_RATE:
		return put_user((file->f_mode & FMODE_READ) ? s->rateadc : s->ratedac, (int *)arg);

		case SOUND_PCM_READ_CHANNELS:
		return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? 
						(TRIDENT_FMT_STEREO << TRIDENT_ADC_SHIFT) : 
						(TRIDENT_FMT_STEREO << TRIDENT_DAC_SHIFT))) ? 2 : 1, (int *)arg);

		case SOUND_PCM_READ_BITS:
		return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? 
						(TRIDENT_FMT_16BIT << TRIDENT_ADC_SHIFT) : 
						(TRIDENT_FMT_16BIT << TRIDENT_DAC_SHIFT))) ? 16 : 8, (int *)arg);

		case SOUND_PCM_WRITE_FILTER:
		case SNDCTL_DSP_SETSYNCRO:
		case SOUND_PCM_READ_FILTER:
		return -EINVAL;

	}
	return -EINVAL;
}

static int trident_open(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	struct trident_card *c = devs;
	struct trident_state *s = NULL, *sp;
	int i;
	unsigned char fmtm = ~0, fmts = 0;

	/*
	 *	Scan the cards and find the channel. We only
	 *	do this at open time so it is ok
	 */
	 
	while (c!=NULL)
	{
		for(i=0;i<NR_DSPS;i++)
		{
			sp=&c->channels[i];
			if(sp->dev_audio < 0)
				continue;
			if((sp->dev_audio ^ minor) & ~0xf)
				continue;
			s=sp;
		}
		c=c->next;
	}
		
	if (!s)
		return -ENODEV;
		
	VALIDATE_STATE(s);
	file->private_data = s;
	/* wait for device to become free */
	down(&s->open_sem);
	while (s->open_mode & file->f_mode) 
	{
		if (file->f_flags & O_NONBLOCK) 
		{
			up(&s->open_sem);
			return -EWOULDBLOCK;
		}
		up(&s->open_sem);
		interruptible_sleep_on(&s->open_wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
		down(&s->open_sem);
	}
	if (file->f_mode & FMODE_READ) 
	{
/*
		fmtm &= ~((TRIDENT_FMT_STEREO | TRIDENT_FMT_16BIT) << TRIDENT_ADC_SHIFT);
		if ((minor & 0xf) == SND_DEV_DSP16)
			fmts |= TRIDENT_FMT_16BIT << TRIDENT_ADC_SHIFT; */

		fmtm = (TRIDENT_FMT_STEREO|TRIDENT_FMT_16BIT) << TRIDENT_ADC_SHIFT;

		s->dma_adc.ossfragshift = s->dma_adc.ossmaxfrags = s->dma_adc.subdivision = 0;
		trident_set_adc_rate(s, 8000, 0);
	}
	if (file->f_mode & FMODE_WRITE) 
	{
		fmtm &= ~((TRIDENT_FMT_STEREO | TRIDENT_FMT_16BIT) << TRIDENT_DAC_SHIFT);
		if ((minor & 0xf) == SND_DEV_DSP16)
			fmts |= TRIDENT_FMT_16BIT << TRIDENT_DAC_SHIFT;
		s->dma_dac.ossfragshift = s->dma_dac.ossmaxfrags = s->dma_dac.subdivision = 0;
		trident_set_dac_rate(s, 8000, 1);
	}
	set_fmt(s, fmtm, fmts);
	s->open_mode |= file->f_mode & (FMODE_READ | FMODE_WRITE);

	up(&s->open_sem);
	//FIXME put back in
	//MOD_INC_USE_COUNT;
	return 0;
}

static int trident_release(struct inode *inode, struct file *file)
{
	struct trident_state *s = (struct trident_state *)file->private_data;

	VALIDATE_STATE(s);
	if (file->f_mode & FMODE_WRITE)
		drain_dac(s, file->f_flags & O_NONBLOCK);
	down(&s->open_sem);
	if (file->f_mode & FMODE_WRITE) {
		stop_dac(s);
	}
	if (file->f_mode & FMODE_READ) {
		stop_adc(s);
	}

	/* free our shared dma buffers */
	dealloc_dmabuf(&s->dma_adc);
	dealloc_dmabuf(&s->dma_dac);
		
	s->open_mode &= (~file->f_mode) & (FMODE_READ|FMODE_WRITE);
	/* we're covered by the open_sem */
	up(&s->open_sem);
	wake_up(&s->open_wait);
	//FIXME put back in
	//MOD_DEC_USE_COUNT;
	return 0;
}

static /*const*/ struct file_operations trident_audio_fops = {
	&trident_llseek,
	&trident_read,
	&trident_write,
	NULL,  /* readdir */
	&trident_poll,
	&trident_ioctl,
	NULL,	/* XXX &trident_mmap, */
	&trident_open,
	NULL,	/* flush */
	&trident_release,
	NULL,  /* fsync */
	NULL,  /* fasync */
	NULL,  /* check_media_change */
	NULL,  /* revalidate */
	NULL,  /* lock */
};

#ifdef CONFIG_APM
int trident_apm_callback(apm_event_t ae) {
}
#endif

/* --------------------------------------------------------------------- */

static int trident_install(struct pci_dev *pcidev, int card_type)
{
	u16 w;
	u32 l;
	unsigned long iobase;
	int i;
	struct trident_card *card;
	struct trident_state *trident;
	int num = 0;
	u32 ChanDwordCount;
	
	iobase = pcidev->resource[0].start;
	
	if(check_region(iobase, 256))
	{
		M_printk(KERN_WARNING "(trident) can't allocate 256 bytes I/O at 0x%4.4lx\n", iobase);
		return 0;
	}

	/* this was tripping up some machines */
	if(pcidev->irq == 0) 
	{
		printk(KERN_WARNING "(trident) pci subsystem reports irq 0, this might not be correct.\n");
	}

	/* just to be sure */
	pci_set_master(pcidev);
	
	pci_read_config_word(pcidev, PCI_COMMAND, &w);
	if((w&(PCI_COMMAND_IO|PCI_COMMAND_MASTER)) != (PCI_COMMAND_IO|PCI_COMMAND_MASTER))
	{
		printk("(trident) BIOS did not enable I/O access.\n");
		w|=PCI_COMMAND_IO|PCI_COMMAND_MASTER;
		pci_write_config_word(pcidev, PCI_COMMAND,w);
	}

	card = kmalloc(sizeof(struct trident_card), GFP_KERNEL);

	if(card == NULL)
	{
		printk(KERN_WARNING "(trident) out of memory\n");
		return 0;
	}
	
	memset(card, 0, sizeof(*card));

#ifdef CONFIG_APM
	printk("(trident) apm_reg_callback: %d\n",apm_register_callback(trident_apm_callback));
#endif

	card->iobase = iobase;
	card->card_type = card_type;
	card->irq = pcidev->irq;
	card->next = devs;
	card->magic = TRIDENT_CARD_MAGIC;
	devs = card;

	ChanDwordCount = card->ChanDwordCount = 2;
	card->ChanPCM = 32;

	card->ChRegs.lpChStart = card->ChRegs.data;
	card->ChRegs.lpChStop = card->ChRegs.lpChStart + ChanDwordCount;
	card->ChRegs.lpChAint = card->ChRegs.lpChStop + ChanDwordCount;
	card->ChRegs.lpChAinten = card->ChRegs.lpChAint + ChanDwordCount;
	card->ChRegs.lpAChStart = card->ChRegs.lpChAinten + ChanDwordCount;
	card->ChRegs.lpAChStop = card->ChRegs.lpAChStart + ChanDwordCount;
	card->ChRegs.lpAChAint = card->ChRegs.lpAChStop + ChanDwordCount;
	card->ChRegs.lpAChAinten = card->ChRegs.lpAChAint + ChanDwordCount;
	// Assign addresses.
	card->ChRegs.lpAChStart[0] = T4D_START_A;
	card->ChRegs.lpAChStop[0] = T4D_STOP_A;
	card->ChRegs.lpAChAint[0] = T4D_AINT_A;
	card->ChRegs.lpAChAinten[0] = T4D_AINTEN_A;


	card->ChRegs.lpAChStart[1] = T4D_START_B;
	card->ChRegs.lpAChStop[1] = T4D_STOP_B;
	card->ChRegs.lpAChAint[1] = T4D_AINT_B;
	card->ChRegs.lpAChAinten[1] = T4D_AINTEN_B;
	

	outl(0x00, TRID_REG(card, T4D_MUSICVOL_WAVEVOL));
	trident_ac97_set(card, 0x0L  , 0L);
	trident_ac97_set(card, 0x02L , 0L);
	trident_ac97_set(card, 0x18L , 0L);

	if(card->card_type == TYPE_4DWAVE_NX)
	{
	// Enable rear channels
		outl(0x12, TRID_REG(card, NX_ACR0_AC97_COM_STAT));
				// ...or not, since they sound ugly. :)
				// outl(0x02, TRID_REG(card, NX_ACR0_AC97_COM_STAT));
	// S/PDIF C Channel bits 0-31 : 48khz, SCMS disabled
	// outl(0x200004, TRID_REG(card, NX_SPCSTATUS));
	// Disable S/PDIF out, 48khz only from ac97 fifo
		// outb(0x00, TRID_REG(card, NX_SPCTRL_SPCSO + 3));
	}
	else 
	{
		outl(0x02, TRID_REG(card, DX_ACR2_AC97_COM_STAT));
	}
     				        	        	        					 		        	                		
	for(i=0;i<NR_DSPS;i++)
	{
		struct trident_state *s=&card->channels[i];

		s->card = card;
		init_waitqueue_head(&s->dma_adc.wait);
		init_waitqueue_head(&s->dma_dac.wait);
		init_waitqueue_head(&s->open_wait);
		init_MUTEX(&s->open_sem);
		s->magic = TRIDENT_STATE_MAGIC;
		s->channel = i;		

		if(s->dma_adc.ready || s->dma_dac.ready || s->dma_adc.rawbuf)
			printk("(trident) BOTCH!\n");
		
		/*
		 *	Now allocate the hardware resources
		 */
		
		//s->dma_dac.chan[0] = AllocateChannelPCM(card);
		s->dma_dac.chan[1] = AllocateChannelPCM(card);
		//s->dma_adc.chan[0] = AllocateChannelPCM(card);
		
		/* register devices */
		if ((s->dev_audio = register_sound_dsp(&trident_audio_fops, -1)) < 0)
			break;
	}
	
	num = i;
	
	/* clear the rest if we ran out of slots to register */
	for(;i<NR_DSPS;i++)
	{
		struct trident_state *s=&card->channels[i];
		s->dev_audio = -1;
	}
	
	trident = &card->channels[0];

	/*
	 *	Ok card ready. Begin setup proper
	 */

	printk(KERN_INFO "(trident) Configuring %s found at IO 0x%04lX IRQ %d\n", 
		card_names[card_type],card->iobase,card->irq);


	/* stake our claim on the iospace */
	request_region(iobase, 256, card_names[card_type]);
	
	/*
	 *	Reset the CODEC
	 */
	 
	trident_ac97_init(card);

	if ((card->dev_mixer = register_sound_mixer(&trident_mixer_fops, -1)) < 0) 
	{
		printk("(trident) couldn't register mixer!\n");
	} 
	else 
	{
		int i;
		for(i = 0 ; i < SOUND_MIXER_NRDEVICES ; i++) 
		{
			struct mixer_defaults *md = &mixer_defaults[i];

			if(md->mixer == -1) 
				break;
			if(!supported_mixer(card,md->mixer)) 
				continue;
			set_mixer(card,md->mixer,md->value);
		}
	}
	
	if(request_irq(card->irq, trident_interrupt, SA_SHIRQ, card_names[card_type], card))
	{
		printk(KERN_ERR "(trident) unable to allocate irq %d,\n", card->irq);
		unregister_sound_mixer(card->dev_mixer);
		for(i=0;i<NR_DSPS;i++)
		{
			struct trident_state *s = &card->channels[i];
			if(s->dev_audio != -1)
				unregister_sound_dsp(s->dev_audio);
		}
		release_region(card->iobase, 256);		
		kfree(card);
		return 0;
	}

	init_timer(&debug_timer);	
	debug_timer.function = trident_kick;
	debug_timer.data = (unsigned long)card;
	debug_timer.expires = jiffies+1;
	
//	add_timer(&debug_timer);

	printk("(trident) %d channels configured.\n", num);

	EnableEndInterrupts(card);
	return 1; 
}

#ifdef MODULE
int init_module(void)
#else
int __init init_trident(void)
#endif
{
	struct pci_dev *pcidev = NULL;
	int foundone = 0;

	if (!pci_present())   /* No PCI bus in this machine! */
		return -ENODEV;
	printk(KERN_INFO "(trident) version " DRIVER_VERSION " time " __TIME__ " " __DATE__ "\n");

	pcidev = NULL;

	/*
	 *	Find the 4DWave DX
	 */

	while( (pcidev = pci_find_device(PCI_VENDOR_ID_TRIDENT, PCI_DEVICE_ID_TRIDENT_4DWAVE_DX, pcidev))!=NULL
			&&
		( trident_install(pcidev, TYPE_4DWAVE_DX) )) {
			foundone=1;
	}

	/*
	 *	Find the 4DWave NX
	 */

	while((pcidev = pci_find_device(PCI_VENDOR_ID_TRIDENT, PCI_DEVICE_ID_TRIDENT_4DWAVE_NX, pcidev))!=NULL
			&&
		( trident_install(pcidev, TYPE_4DWAVE_NX) )) {
			foundone=1;
	}

	if( ! foundone )
		return -ENODEV;
	return 0;
}

/* --------------------------------------------------------------------- */

#ifdef MODULE

MODULE_AUTHOR("Alan Cox <alan@redhat.com>");
MODULE_DESCRIPTION("Trident 4DWave Driver");
#ifdef M_DEBUG
MODULE_PARM(debug,"i");
#endif

void cleanup_module(void)
{
	struct trident_card *s;

#ifdef CONFIG_APM
	apm_unregister_callback(trident_apm_callback);
#endif

	del_timer(&debug_timer);
	
	while ((s = devs)) {
		int i;
		devs = devs->next;
		
		
		/* Kill interrupts, and SP/DIF */
		
		DisableEndInterrupts(s);
		if(s->card_type == TYPE_4DWAVE_NX)
			outb(0x00, TRID_REG(s, NX_SPCTRL_SPCSO+3));
	
		free_irq(s->irq, s);
		unregister_sound_mixer(s->dev_mixer);
		for(i=0;i<NR_DSPS;i++)
		{
			struct trident_state *trident = &s->channels[i];
			if(trident->dev_audio != -1)
				unregister_sound_dsp(trident->dev_audio);
		}
		release_region(s->iobase, 256);
		kfree(s);
	}
	printk("(trident) unloading\n");
}

#endif /* MODULE */


