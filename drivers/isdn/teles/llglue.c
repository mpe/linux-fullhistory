/* $Id: llglue.c,v 1.6 1996/06/03 20:03:39 fritz Exp $
 *
 * $Log: llglue.c,v $
 * Revision 1.6  1996/06/03 20:03:39  fritz
 * Fixed typos.
 *
 * Revision 1.5  1996/05/31 00:58:47  fritz
 * Errata: Reverted change from rev 1.4.
 *
 * Revision 1.4  1996/05/26 14:59:57  fritz
 * Bugfix: maxbufsize had been set without respect to possible X.75 header.
 *
 * Revision 1.3  1996/05/01 14:19:57  fritz
 * Added ISDN_FEATURE_L2_TRANS
 *
 * Revision 1.2  1996/04/29 23:01:46  fritz
 * Added driverId and channel to readstatus().
 *
 * Revision 1.1  1996/04/13 10:26:29  fritz
 * Initial revision
 *
 *
 */
#define __NO_VERSION__
#include "teles.h"
#include <linux/malloc.h>
#include <linux/timer.h>


extern struct Channel *chanlist;
int             drid;
char            *teles_id = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";

isdn_if         iif;

#define TELES_STATUS_BUFSIZE 4096
static byte    *teles_status_buf = NULL;
static byte    *teles_status_read = NULL;
static byte    *teles_status_write = NULL;
static byte    *teles_status_end = NULL;

int
teles_readstatus(byte * buf, int len, int user, int id, int channel)
{
	int             count;
	byte           *p;

	for (p = buf, count = 0; count < len; p++, count++) {
		if (user)
			put_fs_byte(*teles_status_read++, p);
		else
			*p++ = *teles_status_read++;
		if (teles_status_read > teles_status_end)
			teles_status_read = teles_status_buf;
	}
	return count;
}

void
teles_putstatus(char *buf)
{
	long            flags;
	int             len, count, i;
	byte           *p;
	isdn_ctrl       ic;

	save_flags(flags);
	cli();
	count = 0;
	len = strlen(buf);
	for (p = buf, i = len; i > 0; i--, p++) {
		*teles_status_write++ = *p;
		if (teles_status_write > teles_status_end)
			teles_status_write = teles_status_buf;
		count++;
	}
	restore_flags(flags);
	if (count) {
		ic.command = ISDN_STAT_STAVAIL;
		ic.driver = drid;
		ic.arg = count;
		iif.statcallb(&ic);
	}
}


int
ll_init(void)
{
	isdn_ctrl       ic;

	teles_status_buf = Smalloc(TELES_STATUS_BUFSIZE,
				   GFP_KERNEL, "teles_status_buf");
	if (!teles_status_buf) {
		printk(KERN_ERR "teles: Could not allocate status-buffer\n");
		return (-EIO);
	} else {
		teles_status_read = teles_status_buf;
		teles_status_write = teles_status_buf;
		teles_status_end = teles_status_buf + TELES_STATUS_BUFSIZE - 1;
	}

	iif.channels = CallcNewChan();
	iif.maxbufsize = BUFFER_SIZE(HSCX_SBUF_ORDER, HSCX_SBUF_BPPS);
	iif.features =
	    ISDN_FEATURE_L2_X75I |
	    ISDN_FEATURE_L2_HDLC |
	    ISDN_FEATURE_L2_TRANS |
	    ISDN_FEATURE_L3_TRANS |
	    ISDN_FEATURE_P_1TR6 |
	    ISDN_FEATURE_P_EURO;

	iif.command = teles_command;
	iif.writebuf = teles_writebuf;
	iif.writecmd = NULL;
	iif.readstat = teles_readstatus;
	strncpy(iif.id, teles_id, sizeof(iif.id) - 1);

	register_isdn(&iif);
	drid = iif.channels;

	ic.driver = drid;
	ic.command = ISDN_STAT_RUN;
	iif.statcallb(&ic);
	return 0;
}

void
ll_stop(void)
{
	isdn_ctrl       ic;

	ic.command = ISDN_STAT_STOP;
	ic.driver = drid;
	iif.statcallb(&ic);

	CallcFreeChan();
}

void
ll_unload(void)
{
	isdn_ctrl       ic;

	ic.command = ISDN_STAT_UNLOAD;
	ic.driver = drid;
	iif.statcallb(&ic);
}
