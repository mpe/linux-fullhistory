/*
 *	Mac bong noise generator. Note - we ought to put a boingy noise
 *	here 8)
 */

#include <linux/sched.h>
#include <linux/timer.h>

#include <asm/macintosh.h>
#include <asm/mac_asc.h>

static const signed char sine_data[] = {
	0,  39,  75,  103,  121,  127,  121,  103,  75,  39,
	0, -39, -75, -103, -121, -127, -121, -103, -75, -39
};
#define DATA_SIZE	(sizeof(sine_data)/sizeof(sine_data[0]))

static void nosound( unsigned long ignored );
static struct timer_list sound_timer = { NULL, NULL, 0, 0, nosound };

static volatile unsigned char *asc_base=(void *)0x50F14000;


void mac_mksound( unsigned int hz, unsigned int ticks )
{
	static int inited = 0;
	unsigned long flags;
	int samples=512;

	if (macintosh_config->ident == MAC_MODEL_C660
	 || macintosh_config->ident == MAC_MODEL_Q840)
	{
		/*
		 * The Quadra 660AV and 840AV use the "Singer" custom ASIC for sound I/O.
		 * It appears to be similar to the "AWACS" custom ASIC in the Power Mac 
		 * [678]100.  Because Singer and AWACS may have a similar hardware 
		 * interface, this would imply that the code in drivers/sound/dmasound.c 
		 * for AWACS could be used as a basis for Singer support.  All we have to
		 * do is figure out how to do DMA on the 660AV/840AV through the PSC and 
		 * figure out where the Singer hardware sits in memory. (I'd look in the
		 * vicinity of the AWACS location in a Power Mac [678]100 first, or the 
		 * current location of the Apple Sound Chip--ASC--in other Macs.)  The 
		 * Power Mac [678]100 info can be found in MkLinux Mach kernel sources.
		 *
		 * Quoted from Apple's Tech Info Library, article number 16405:
		 *   "Among desktop Macintosh computers, only the 660AV, 840AV, and Power
		 *   Macintosh models have 16-bit audio input and output capability 
		 *   because of the AT&T DSP3210 hardware circuitry and the 16-bit Singer
		 *   codec circuitry in the AVs.  The Audio Waveform Amplifier and
		 *   Converter (AWAC) chip in the Power Macintosh performs the same 
		 *   16-bit I/O functionality.  The PowerBook 500 series computers
		 *   support 16-bit stereo output, but only mono input."
		 *
		 *   http://til.info.apple.com/techinfo.nsf/artnum/n16405
		 *
		 * --David Kilzer
		 */

		return;
	}
	
	if(!inited)
	{
		int i=0;
		int j=0;
		int k=0;
		int l=0;

		/*
		 *	The IIfx strikes again!
		 */
		 
		if(macintosh_config->ident==MAC_MODEL_IIFX)
			asc_base=(void *)0x50010000;

		for(i=0;i<samples;i++)
		{
			asc_base[i]=sine_data[j];
			asc_base[i+512]=sine_data[j];
			asc_base[i+1024]=sine_data[j];
			asc_base[i+1536]=sine_data[j];
			j++;
			if(j==DATA_SIZE)
				j=0;
			if(i&1)
				k++;
			if(k==DATA_SIZE)
				k=0;
			if((i&3)==3)
				l++;
			if(l==DATA_SIZE)
				l=0;	
		}
		inited=1;
	}
	save_flags(flags);
	cli();
	del_timer( &sound_timer );

	if (hz > 20 && hz < 32767) {
		int i;
		u_long asc_pulses=((hz<<5)*samples)/468;
		for(i=0;i<4;i++)
		{
			asc_base[ASC_FREQ(i,0)]=0x00;
			asc_base[ASC_FREQ(i,1)]=20;
			asc_base[ASC_FREQ(i,2)]=0x00;
			asc_base[ASC_FREQ(i,3)]=20;
			asc_base[ASC_FREQ(i,4)]=(asc_pulses>>24)&0xFF;
			asc_base[ASC_FREQ(i,5)]=(asc_pulses>>16)&0xFF;
			asc_base[ASC_FREQ(i,6)]=(asc_pulses>>8)&0xFF;
			asc_base[ASC_FREQ(i,7)]=(asc_pulses>>0)&0xFF;
		}
		asc_base[ASC_CHAN]=0x03;
		asc_base[ASC_VOLUME]=128;
		asc_base[ASC_MODE]=ASC_MODE_SAMPLE;
		asc_base[ASC_ENABLE]=ASC_ENABLE_SAMPLE;
		if (ticks) {
			sound_timer.expires = jiffies + ticks;
			add_timer( &sound_timer );
		}
	} else {
		nosound( 0 );
	}
	restore_flags(flags);
}


static void nosound( unsigned long ignored )
{
	asc_base[ASC_ENABLE]=0;
}	
