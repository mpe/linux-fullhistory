/*********************************************************************
 *                
 * Filename:      irsysctl.c
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun May 24 22:12:06 1998
 * Modified at:   Thu Jan  7 10:35:02 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1997 Dag Brattli, All Rights Reserved.
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     Neither Dag Brattli nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *     
 ********************************************************************/

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/ctype.h>
#include <linux/sysctl.h>
#include <asm/segment.h>

#include <net/irda/irda.h>

#define NET_IRDA 412 /* Random number */
enum { DISCOVERY=1, DEVNAME, COMPRESSION, DEBUG };

extern int sysctl_discovery;
int sysctl_compression = 0;
extern char sysctl_devname[];

#ifdef CONFIG_IRDA_DEBUG
extern unsigned int irda_debug;
#endif

/* One file */
static ctl_table irda_table[] = {
	{ DISCOVERY, "discovery", &sysctl_discovery,
	  sizeof(int), 0644, NULL, &proc_dointvec },
	{ DEVNAME, "devname", sysctl_devname,
	  65, 0644, NULL, &proc_dostring, &sysctl_string},
	{ COMPRESSION, "compression", &sysctl_compression,
	  sizeof(int), 0644, NULL, &proc_dointvec },
#ifdef CONFIG_IRDA_DEBUG
	{ DEBUG, "debug", &irda_debug,
	  sizeof(int), 0644, NULL, &proc_dointvec },
#endif
	{ 0 }
};

/* One directory */
static ctl_table irda_net_table[] = {
	{ NET_IRDA, "irda", NULL, 0, 0555, irda_table },
	{ 0 }
};

/* The parent directory */
static ctl_table irda_root_table[] = {
	{ CTL_NET, "net", NULL, 0, 0555, irda_net_table },
	{ 0 }
};

static struct ctl_table_header *irda_table_header;

/*
 * Function irda_sysctl_register (void)
 *
 *    Register our sysctl interface
 *
 */
int irda_sysctl_register(void)
{
	irda_table_header = register_sysctl_table( irda_root_table, 0);
	if ( !irda_table_header)
		return -ENOMEM;
	return 0;
}

/*
 * Function irda_sysctl_unregister (void)
 *
 *    Unregister our sysctl interface
 *
 */
void irda_sysctl_unregister(void) 
{
	unregister_sysctl_table( irda_table_header);
}



