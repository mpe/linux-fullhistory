/*********************************************************************
 *                
 * Filename:      discovery.c
 * Version:       0.1
 * Description:   Routines for handling discoveries at the IrLMP layer
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Tue Apr  6 15:33:50 1999
 * Modified at:   Sat Oct  9 17:11:31 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Modified at:   Fri May 28  3:11 CST 1999
 * Modified by:   Horst von Brand <vonbrand@sleipnir.valparaiso.cl>
 * 
 *     Copyright (c) 1999 Dag Brattli, All Rights Reserved.
 *     
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License 
 *     along with this program; if not, write to the Free Software 
 *     Foundation, Inc., 59 Temple Place, Suite 330, Boston, 
 *     MA 02111-1307 USA
 *     
 ********************************************************************/

#include <linux/string.h>
#include <linux/socket.h>

#include <net/irda/irda.h>
#include <net/irda/irlmp.h>

#include <net/irda/discovery.h>

/*
 * Function irlmp_add_discovery (cachelog, discovery)
 *
 *    Add a new discovery to the cachelog, and remove any old discoveries
 *    from the same device
 */
void irlmp_add_discovery(hashbin_t *cachelog, discovery_t *new)
{
	discovery_t *discovery, *node;
	unsigned long flags;

	spin_lock_irqsave(&irlmp->lock, flags);

	/* 
	 * Remove all discoveries of devices that has previously been 
	 * discovered on the same link with the same name (info), or the 
	 * same daddr. We do this since some devices (mostly PDAs) change
	 * their device address between every discovery.
	 */
	discovery = (discovery_t *) hashbin_get_first(cachelog);
	while (discovery != NULL ) {
			node = discovery;

			/* Be sure to stay one item ahead */
			discovery = (discovery_t *) hashbin_get_next(cachelog);
			
			if ((node->daddr == new->daddr) || 
			    (strcmp(node->nickname, new->nickname) == 0))
			{
				/* This discovery is a previous discovery 
				 * from the same device, so just remove it
				 */
				hashbin_remove(cachelog, node->daddr, NULL);
				kfree(node);
			}
		}


	/* Insert the new and updated version */
	hashbin_insert(cachelog, (queue_t *) new, new->daddr, NULL);

	spin_unlock_irqrestore(&irlmp->lock, flags);
}

/*
 * Function irlmp_add_discovery_log (cachelog, log)
 *
 *    Merge a disovery log into the cachlog.
 *
 */
void irlmp_add_discovery_log(hashbin_t *cachelog, hashbin_t *log)
{
	discovery_t *discovery;

	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	/*
	 *  If log is missing this means that IrLAP was unable to perform the
	 *  discovery, so restart discovery again with just the half timeout
	 *  of the normal one.
	 */
	if (log == NULL) {
		/* irlmp_start_discovery_timer(irlmp, 150); */
		return;
	}

	discovery = (discovery_t *) hashbin_remove_first(log);
	while (discovery != NULL) {
		irlmp_add_discovery(cachelog, discovery);

		discovery = (discovery_t *) hashbin_remove_first(log);
	}
	
	/* Delete the now empty log */
	hashbin_delete(log, (FREE_FUNC) kfree);
}

/*
 * Function irlmp_expire_discoveries (log, saddr, force)
 *
 *    Go through all discoveries and expire all that has stayed to long
 *
 */
void irlmp_expire_discoveries(hashbin_t *log, __u32 saddr, int force)
{
	discovery_t *discovery, *curr;

	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	discovery = (discovery_t *) hashbin_get_first(log);
	while (discovery != NULL) {
		curr = discovery;

		/* Be sure to be one item ahead */
		discovery = (discovery_t *) hashbin_get_next(log);

		/* Test if it's time to expire this discovery */
		if ((curr->saddr == saddr) && (force ||
		    ((jiffies - curr->timestamp) > DISCOVERY_EXPIRE_TIMEOUT)))
		{
			curr = hashbin_remove(log, curr->daddr, NULL);
			if (curr)
				kfree(curr);
		}
	}
}

/*
 * Function irlmp_dump_discoveries (log)
 *
 *    Print out all discoveries in log
 *
 */
void irlmp_dump_discoveries(hashbin_t *log)
{
	discovery_t *discovery;

	ASSERT(log != NULL, return;);

	discovery = (discovery_t *) hashbin_get_first(log);
	while (discovery != NULL) {
		IRDA_DEBUG(0, "Discovery:\n");
		IRDA_DEBUG(0, "  daddr=%08x\n", discovery->daddr);
		IRDA_DEBUG(0, "  saddr=%08x\n", discovery->saddr); 
		IRDA_DEBUG(0, "  nickname=%s\n", discovery->nickname);

		discovery = (discovery_t *) hashbin_get_next(log);
	}
}

/*
 * Function irlmp_find_device (name, saddr)
 *
 *    Look through the discovery log at each of the links and try to find 
 *    the device with the given name. Return daddr and saddr. If saddr is
 *    specified, that look at that particular link only (not impl).
 */
__u32 irlmp_find_device(hashbin_t *cachelog, char *name, __u32 *saddr)
{
	unsigned long flags;
	discovery_t *d;

	spin_lock_irqsave(&irlmp->lock, flags);

	/* Look at all discoveries for that link */
	d = (discovery_t *) hashbin_get_first(cachelog);
	while (d != NULL) {
		IRDA_DEBUG(1, "Discovery:\n");
		IRDA_DEBUG(1, "  daddr=%08x\n", d->daddr);
		IRDA_DEBUG(1, "  nickname=%s\n", d->nickname);
		
		if (strcmp(name, d->nickname) == 0) {
			*saddr = d->saddr;
			
			spin_unlock_irqrestore(&irlmp->lock, flags);
			return d->daddr;
		}
		d = (discovery_t *) hashbin_get_next(cachelog);
	}

	spin_unlock_irqrestore(&irlmp->lock, flags);

	return 0;
}

/*
 * Function proc_discovery_read (buf, start, offset, len, unused)
 *
 *    Print discovery information in /proc file system
 *
 */
int discovery_proc_read(char *buf, char **start, off_t offset, int len, 
			int unused)
{
	discovery_t *discovery;
	unsigned long flags;
	hashbin_t *cachelog = irlmp_get_cachelog();

	if (!irlmp)
		return len;

	len = sprintf(buf, "IrLMP: Discovery log:\n\n");	
	
	save_flags(flags);
	cli();
	
	discovery = (discovery_t *) hashbin_get_first(cachelog);
	while ( discovery != NULL) {
		len += sprintf(buf+len, "nickname: %s,", discovery->nickname);
		
		len += sprintf(buf+len, " hint: 0x%02x%02x", 
			       discovery->hints.byte[0], 
			       discovery->hints.byte[1]);
#if 0
		if ( discovery->hints.byte[0] & HINT_PNP)
			len += sprintf( buf+len, "PnP Compatible ");
		if ( discovery->hints.byte[0] & HINT_PDA)
			len += sprintf( buf+len, "PDA/Palmtop ");
		if ( discovery->hints.byte[0] & HINT_COMPUTER)
			len += sprintf( buf+len, "Computer ");
		if ( discovery->hints.byte[0] & HINT_PRINTER)
			len += sprintf( buf+len, "Printer ");
		if ( discovery->hints.byte[0] & HINT_MODEM)
			len += sprintf( buf+len, "Modem ");
		if ( discovery->hints.byte[0] & HINT_FAX)
			len += sprintf( buf+len, "Fax ");
		if ( discovery->hints.byte[0] & HINT_LAN)
			len += sprintf( buf+len, "LAN Access ");
		
		if ( discovery->hints.byte[1] & HINT_TELEPHONY)
			len += sprintf( buf+len, "Telephony ");
		if ( discovery->hints.byte[1] & HINT_FILE_SERVER)
			len += sprintf( buf+len, "File Server ");       
		if ( discovery->hints.byte[1] & HINT_COMM)
			len += sprintf( buf+len, "IrCOMM ");
		if ( discovery->hints.byte[1] & HINT_OBEX)
			len += sprintf( buf+len, "IrOBEX ");
#endif		
		len += sprintf(buf+len, ", saddr: 0x%08x", 
			       discovery->saddr);

		len += sprintf(buf+len, ", daddr: 0x%08x\n", 
			       discovery->daddr);
		
		len += sprintf(buf+len, "\n");
		
		discovery = (discovery_t *) hashbin_get_next(cachelog);
	}
	restore_flags(flags);

	return len;
}
