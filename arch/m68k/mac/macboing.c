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

	if(!inited)
	{
		int i=0;
		int j=0;
		int k=0;
		int l=0;
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
