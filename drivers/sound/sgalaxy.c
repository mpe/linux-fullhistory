/*
 * sound/sgalaxy.c
 *
 * Low level driver for Aztech Sound Galaxy cards.
 * Copyright 1998 Artur Skawina
 *
 * Supported cards:
 *    Aztech Sound Galaxy Waverider Pro 32 - 3D
 *    Aztech Sound Galaxy Washington 16
 *
 * Based on cs4232.c by Hannu Savolainen and Alan Cox.
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */

#include <linux/config.h>
#include <linux/module.h>

#include "sound_config.h"
#include "soundmodule.h"

#if defined(CONFIG_SGALAXY) || defined (MODULE)

static void sleep( unsigned howlong )
{
	current->state   = TASK_INTERRUPTIBLE;
	schedule_timeout(howlong);
}

#define DPORT 0x80

/* Sound Blaster regs */

#define SBDSP_RESET      0x6
#define SBDSP_READ       0xA
#define SBDSP_COMMAND    0xC
#define SBDSP_STATUS     SBDSP_COMMAND
#define SBDSP_DATA_AVAIL 0xE

static int sb_rst(int base)
{
	int   i;
   
	outb( 1, base+SBDSP_RESET );     /* reset the DSP */
	outb( 0, base+SBDSP_RESET );
    
	for ( i=0; i<500; i++ )          /* delay */
		inb(DPORT);
      
	for ( i=0; i<100000; i++ )
	{
		if ( inb( base+SBDSP_DATA_AVAIL )&0x80 )
			break;
	}

	if ( inb( base+SBDSP_READ )!=0xAA )
		return 0;

	return 1;
}

static int sb_cmd( int base, unsigned char val )
{
	int  i;

	for ( i=100000; i; i-- )
	{
		if ( (inb( base+SBDSP_STATUS )&0x80)==0 )
		{
        		outb( val, base+SBDSP_COMMAND );
        		break;
		}
	}
	return i;      /* i>0 == success */
}


#define ai_sgbase    driver_use_1

int probe_sgalaxy( struct address_info *ai )
{
	if ( check_region( ai->io_base, 8 ) )
	{
		printk(KERN_ERR "sgalaxy: WSS IO port 0x%03x not available\n", ai->io_base);
		return 0;
	}
        
	if ( ad1848_detect( ai->io_base+4, NULL, ai->osp ) )
		return probe_ms_sound(ai);  /* The card is already active, check irq etc... */

	if ( check_region( ai->ai_sgbase, 0x10 ) )
	{
		printk(KERN_ERR "sgalaxy: SB IO port 0x%03x not available\n", ai->ai_sgbase);
		return 0;
	}
        
	/* switch to MSS/WSS mode */
   
	sb_rst( ai->ai_sgbase );
   
	sb_cmd( ai->ai_sgbase, 9 );
	sb_cmd( ai->ai_sgbase, 0 );

	sleep( HZ/10 );

      	return probe_ms_sound(ai);
}

void attach_sgalaxy( struct address_info *ai )
{
	int n;
	
	request_region( ai->ai_sgbase, 0x10, "SoundGalaxy SB" );
 
	attach_ms_sound( ai );
	n=ai->slots[0];
	
	if (n!=-1 && audio_devs[n]->mixer_dev != -1 )
	{
		AD1848_REROUTE( SOUND_MIXER_LINE1, SOUND_MIXER_LINE );   /* Line-in */
		AD1848_REROUTE( SOUND_MIXER_LINE2, SOUND_MIXER_SYNTH );  /* FM+Wavetable*/
		AD1848_REROUTE( SOUND_MIXER_LINE3, SOUND_MIXER_CD );     /* CD */
	}
}

void unload_sgalaxy( struct address_info *ai )
{
	unload_ms_sound( ai );
	release_region( ai->ai_sgbase, 0x10 );
}

#ifdef MODULE

int   io       = -1;
int   irq      = -1;
int   dma      = -1;
int   dma2     = -1;
int   sgbase   = -1;

MODULE_PARM(io,"i");
MODULE_PARM(irq,"i");
MODULE_PARM(dma,"i");
MODULE_PARM(dma2,"i");
MODULE_PARM(sgbase,"i");

EXPORT_NO_SYMBOLS;

struct address_info ai;


int init_module(void)
{
	if ( io==-1 || irq==-1 || dma==-1 || sgbase==-1 )
	{
		printk(KERN_ERR "sgalaxy: io, irq, dma and sgbase must be set.\n");
		return -EINVAL;
	}

	ai.io_base   = io;
	ai.irq       = irq;
	ai.dma       = dma;
	ai.dma2      = dma2;
	ai.ai_sgbase = sgbase;

	if ( probe_sgalaxy( &ai )==0 )
		return -ENODEV;

	attach_sgalaxy( &ai );

	SOUND_LOCK;
	return 0;
}

void cleanup_module(void)
{
	unload_sgalaxy( &ai );
	SOUND_LOCK_END;
}

#endif
#endif
