/*
 *	AX.25 release 036
 *
 *	This is ALPHA test software. This code may break your machine, randomly fail to work with new 
 *	releases, misbehave and/or generally screw up. It might even work. 
 *
 *	This code REQUIRES 2.1.15 or higher/ NET3.038
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	AX.25 028a	Jonathan(G4KLX)	New state machine based on SDL diagrams.
 *	AX.25 028b	Jonathan(G4KLX)	Extracted AX25 control block from the
 *					sock structure.
 *	AX.25 029	Alan(GW4PTS)	Switched to KA9Q constant names.
 *	AX.25 031	Joerg(DL1BKE)	Added DAMA support
 *	AX.25 032	Joerg(DL1BKE)	Fixed DAMA timeout bug
 *	AX.25 033	Jonathan(G4KLX)	Modularisation functions.
 *	AX.25 035	Frederic(F1OAT)	Support for pseudo-digipeating.
 *	AX.25 036	Jonathan(G4KLX)	Split Standard and DAMA code into seperate files.
 */

#include <linux/config.h>
#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

static void ax25_timer(unsigned long);

/*
 *	Linux set timer
 */
void ax25_set_timer(ax25_cb *ax25)
{
	unsigned long flags;	

	save_flags(flags); cli();
	del_timer(&ax25->timer);
	restore_flags(flags);

	ax25->timer.data     = (unsigned long)ax25;
	ax25->timer.function = &ax25_timer;
	ax25->timer.expires  = jiffies + 10;

	add_timer(&ax25->timer);
}

/*
 *	AX.25 TIMER 
 *
 *	This routine is called every 100ms. Decrement timer by this
 *	amount - if expired then process the event.
 */
static void ax25_timer(unsigned long param)
{
	ax25_cb *ax25 = (ax25_cb *)param;

	switch (ax25->ax25_dev->values[AX25_VALUES_PROTOCOL]) {
		case AX25_PROTO_STD:
			ax25_std_timer(ax25);
			break;
#ifdef CONFIG_AX25_DAMA_SLAVE
		case AX25_PROTO_DAMA_SLAVE:
			ax25_ds_timer(ax25);
			break;
#endif
	}
}

#endif
