/*
	backpack.c (c) 2001 Micro Solutions Inc.
		Released under the terms of the GNU General Public license

	backpack.c is a low-level protocol driver for the Micro Solutions
		"BACKPACK" parallel port IDE adapter
		(Works on Series 6 drives)

	Written by: Ken Hahn     (linux-dev@micro-solutions.com)
	            Clive Turvey (linux-dev@micro-solutions.com)

*/

/*
   This is Ken's linux wrapper for the PPC library
   Version 1.0.0 is the backpack driver for which source is not available
   Version 2.0.0 is the first to have source released 
   Version 2.0.1 is the "Cox-ified" source code 
   Version 2.0.2 - fixed version string usage, and made ppc functions static 
*/


/* PARAMETERS */
int verbose=0; /* set this to 1 to see debugging messages and whatnot */

#define BACKPACK_VERSION "2.0.2"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <asm/io.h>

#if defined(CONFIG_PARPORT_MODULE)||defined(CONFIG_PARPORT)
#include <linux/parport.h>
#endif

#include "ppc6lnx.c"
#include "paride.h"

 

#define PPCSTRUCT(pi) ((PPC *)(pi->private))

/****************************************************************/
/*
 ATAPI CDROM DRIVE REGISTERS
*/
#define ATAPI_DATA       0      /* data port                  */
#define ATAPI_ERROR      1      /* error register (read)      */
#define ATAPI_FEATURES   1      /* feature register (write)   */
#define ATAPI_INT_REASON 2      /* interrupt reason register  */
#define ATAPI_COUNT_LOW  4      /* byte count register (low)  */
#define ATAPI_COUNT_HIGH 5      /* byte count register (high) */
#define ATAPI_DRIVE_SEL  6      /* drive select register      */
#define ATAPI_STATUS     7      /* status port (read)         */
#define ATAPI_COMMAND    7      /* command port (write)       */
#define ATAPI_ALT_STATUS 0x0e /* alternate status reg (read) */
#define ATAPI_DEVICE_CONTROL 0x0e /* device control (write)   */
/****************************************************************/

static int bpck6_read_regr(PIA *pi, int cont, int reg)
{
	unsigned int out;

	/* check for bad settings */
	if (reg<0 || reg>7 || cont<0 || cont>2)
	{
		return(-1);
	}
	out=ppc6_rd_port(PPCSTRUCT(pi),cont?reg|8:reg);
	return(out);
}

static void bpck6_write_regr(PIA *pi, int cont, int reg, int val)
{
	/* check for bad settings */
	if (reg>=0 && reg<=7 && cont>=0 && cont<=1)
	{
		ppc6_wr_port(PPCSTRUCT(pi),cont?reg|8:reg,(u8)val);
	}
}

static void bpck6_write_block( PIA *pi, char * buf, int len )
{
	ppc6_wr_port16_blk(PPCSTRUCT(pi),ATAPI_DATA,buf,(u32)len>>1); 
}

static void bpck6_read_block( PIA *pi, char * buf, int len )
{
	ppc6_rd_port16_blk(PPCSTRUCT(pi),ATAPI_DATA,buf,(u32)len>>1);
}

static void bpck6_connect ( PIA *pi  )
{
	if(verbose)
	{
		printk(KERN_DEBUG "connect\n");
	}

	if(pi->mode >=2)
  	{
		PPCSTRUCT(pi)->mode=4+pi->mode-2;	
	}
	else if(pi->mode==1)
	{
		PPCSTRUCT(pi)->mode=3;	
	}
	else
	{
		PPCSTRUCT(pi)->mode=1;		
	}

	ppc6_open(PPCSTRUCT(pi));  
	ppc6_wr_extout(PPCSTRUCT(pi),0x3);
}

static void bpck6_disconnect ( PIA *pi )
{
	if(verbose)
	{
		printk("disconnect\n");
	}
	ppc6_wr_extout(PPCSTRUCT(pi),0x0);
	ppc6_close(PPCSTRUCT(pi));
}

static int bpck6_test_port ( PIA *pi )   /* check for 8-bit port */
{
	if(verbose)
	{
		printk(KERN_DEBUG "PARPORT indicates modes=%x for lp=0x%lx\n",
               		((struct pardevice*)(pi->pardev))->port->modes,
			((struct pardevice *)(pi->pardev))->port->base); 
	}

	/*copy over duplicate stuff.. initialize state info*/
	PPCSTRUCT(pi)->ppc_id=pi->unit;
	PPCSTRUCT(pi)->lpt_addr=pi->port;

#ifdef CONFIG_PARPORT_PC_MODULE
#define CONFIG_PARPORT_PC
#endif

#ifdef CONFIG_PARPORT_PC
	/* look at the parport device to see if what modes we can use */
	if(((struct pardevice *)(pi->pardev))->port->modes & 
		(PARPORT_MODE_EPP)
          )
	{
		return 5; /* Can do EPP*/
	}
	else if(((struct pardevice *)(pi->pardev))->port->modes & 
			(PARPORT_MODE_TRISTATE)
               )
	{
		return 2;
	}
	else /*Just flat SPP*/
	{
		return 1;
	}
#else
	/* there is no way of knowing what kind of port we have
	   default to the highest mode possible */
	return 5;
#endif
}

static int bpck6_probe_unit ( PIA *pi )
{
	int out;

	if(verbose)
	{
		printk(KERN_DEBUG "PROBE UNIT %x on port:%x\n",pi->unit,pi->port);
	}

	/*SET PPC UNIT NUMBER*/
	PPCSTRUCT(pi)->ppc_id=pi->unit;

	/*LOWER DOWN TO UNIDIRECTIONAL*/
	PPCSTRUCT(pi)->mode=1;		

	out=ppc6_open(PPCSTRUCT(pi));

	if(verbose)
	{
		printk(KERN_DEBUG "ppc_open returned %2x\n",out);
	}

  	if(out)
 	{
		ppc6_close(PPCSTRUCT(pi));
		return(1);	
		if(verbose)
		{
			printk(KERN_DEBUG "leaving probe\n");
		}
	}
  	else
  	{
		if(verbose)
		{
			printk(KERN_DEBUG "Failed open\n");
		}
    		return(0);
  	}
}

static void bpck6_log_adapter( PIA *pi, char * scratch, int verbose )
{
	char *mode_string[5]=
		{"4-bit","8-bit","EPP-8","EPP-16","EPP-32"};

	printk("%s: BACKPACK Protocol Driver V"BACKPACK_VERSION"\n",pi->device);
	printk("%s: Copyright 2001 by Micro Solutions, Inc., DeKalb IL.\n",pi->device);
	printk("%s: BACKPACK %s, Micro Solutions BACKPACK Drive at 0x%x\n",
		pi->device,BACKPACK_VERSION,pi->port);
	printk("%s: Unit: %d Mode:%d (%s) Delay %d\n",pi->device,
		pi->unit,pi->mode,mode_string[pi->mode],pi->delay);
}

static void bpck6_init_proto(PIA *pi)
{
	int i;

	/* allocate a state structure for this item */
	pi->private=(int)kmalloc(sizeof(PPC),GFP_KERNEL);

	if(pi->private==(int)NULL)
	{
		printk(KERN_ERR "%s: ERROR COULDN'T ALLOCATE MEMORY\n",pi->device); 
		return;
	}
	else
	{	
		MOD_INC_USE_COUNT; 
	}

	for(i=0;i<sizeof(PPC);i++)
	{
		((unsigned char *)(pi->private))[i]=0;
	}
}

static void bpck6_release_proto(PIA *pi)
{
	MOD_DEC_USE_COUNT;
	/* free after use count decremented so that we aren't using it
		when it is decremented */
	kfree((void *)(pi->private)); 
}

struct pi_protocol bpck6 = { "bpck6", /* name for proto*/
				0, /* index into proto table */
				5, /* max mode =5 */
				2, /* 2-5 use epp (need 8 ports) */
				0, /* no delay (not used anyway) */
				255, /* we can have units  up to 255 */
				bpck6_write_regr,
				bpck6_read_regr,
				bpck6_write_block,
				bpck6_read_block,
				bpck6_connect,
				bpck6_disconnect,
				bpck6_test_port,
 				bpck6_probe_unit,
				0,
				bpck6_log_adapter,
				bpck6_init_proto,
				bpck6_release_proto
                        };


EXPORT_SYMBOL(bpck6_write_regr);
EXPORT_SYMBOL(bpck6_read_regr);
EXPORT_SYMBOL(bpck6_write_block);
EXPORT_SYMBOL(bpck6_read_block);
EXPORT_SYMBOL(bpck6_connect);
EXPORT_SYMBOL(bpck6_disconnect);
EXPORT_SYMBOL(bpck6_test_port);
EXPORT_SYMBOL(bpck6_probe_unit);
EXPORT_SYMBOL(bpck6_log_adapter);
EXPORT_SYMBOL(bpck6_init_proto);
EXPORT_SYMBOL(bpck6_release_proto);

/*---------------------------MODULE STUFF-----------------------*/

#ifdef MODULE
/*module information*/

static int init_module(void)
{
	printk(KERN_INFO "bpck6: BACKPACK Protocol Driver V"BACKPACK_VERSION"\n");
	printk(KERN_INFO "bpck6: Copyright 2001 by Micro Solutions, Inc., DeKalb IL. USA\n");

	if(verbose)
	{
		printk(KERN_DEBUG "bpck6: verbose debug enabled.\n");
	}

	return pi_register(&bpck6) - 1;  
}

void    cleanup_module(void)
{
	pi_unregister(&bpck6);
}

MODULE_AUTHOR("Micro Solutions Inc.");
MODULE_DESCRIPTION("BACKPACK Protocol module, compatible with PARIDE");
MODULE_PARM(verbose,"i");

#endif

