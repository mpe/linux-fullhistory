/*
 * ddi.c	Implement the Device Driver Interface (DDI) routines.
 *		Currently, this is only used by the NET layer of LINUX,
 *		but it eventually might move to an upper directory of
 *		the system.
 *
 * Version:	@(#)ddi.c	1.28	27/12/93
 *
 * Author:	Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 *
 *	Unused pieces nobbled.
 */
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/ddi.h>
#include <linux/interrupt.h>

#include "socket/dev.h"


#undef	DDI_DEBUG
#ifdef	DDI_DEBUG
#   define PRINTK(x)	printk x
#else
#   define PRINTK(x)	/**/
#endif


extern struct ddi_proto		protocols[];	/* network protocols	*/


/*
 * This is the function that is called by a kernel routine during
 * system startup.  Its purpose is to walk trough the "devices"
 * table (defined above), and to call all moduled defined in it.
 */
 
void ddi_init(void)
{
  	struct ddi_proto *pro;

  	PRINTK (("DDI: Starting up!\n"));

  	/* First off, kick all configured protocols. */
  	pro = protocols;
  	while (pro->name != NULL) 
  	{
		(*pro->init)(pro);
		pro++;
  	}
  
  	dev_init();
 	/* Initialize the "Buffer Head" pointers. */
  	bh_base[INET_BH].routine = inet_bh;
  
 	/* We're all done... */
}	
