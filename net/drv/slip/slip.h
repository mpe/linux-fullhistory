/*
 * slip.h	Define the SLIP device driver interface and constants.
 *
 * NOTE:	THIS FILE WILL BE MOVED TO THE LINUX INCLUDE DIRECTORY
 *		AS SOON AS POSSIBLE!
 *
 * Version:	@(#)slip.h	1.2.0	(02/11/93)
 *
 * Author:	Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 */
#ifndef _LINUX_SLIP_H
#define _LINUX_SLIP_H

/* SLIP configuration. */
#define SL_NRUNIT	4		/* number of SLIP channels	*/
#define SL_MTU		296		/* 296; I am used to 600- FvK	*/
#define SL_BUF_SIZE	8192		/* same as TTY for now		*/
#ifdef not_any_more
#define SL_RCV_SIZE	2048
#endif

/* SLIP protocol characters. */
#define END             0300		/* indicates end of frame	*/
#define ESC             0333		/* indicates byte stuffing	*/
#define ESC_END         0334		/* ESC ESC_END means END 'data'	*/
#define ESC_ESC         0335		/* ESC ESC_ESC means ESC 'data'	*/

struct sl_queue {
  unsigned long		data;
  unsigned long		head;
  unsigned long		tail;
  struct wait_queue	*proc_list;
  unsigned char		buf[SL_BUF_SIZE];
};

struct slip {
  int			inuse;		/* are we allocated?		*/
  int			line;		/* SLIP channel number		*/
  struct tty_struct	*tty;		/* ptr to TTY structure		*/
#if 0
  struct device		*dev;		/* easy for intr handling	*/
#endif
  unsigned int		sending;	/* "channel busy" indicator	*/
  struct sl_queue	rcv_queue;
  char			snd_buf[(SL_MTU*2)+4];
  unsigned char		xbuff[(SL_MTU * 2)];
  int			escape;		/* SLIP state machine		*/
  int			received;	/* SLIP receive counter		*/
  unsigned long		sent;		/* #frames sent			*/
  unsigned long		rcvd;		/* #frames rcvd			*/
  unsigned long		errors;		/* error count			*/
};

#define SL_INC(a)	((a) = ((a)+1) & (SL_BUF_SIZE-1))
#define SL_DEC(a)	((a) = ((a)-1) & (SL_BUF_SIZE-1))
#define SL_EMPTY(a)	((a)->head == (a)->tail)
#define SL_LEFT(a)	(((a)->tail-(a)->head-1)&(SL_BUF_SIZE-1))
#define SL_LAST(a)	((a)->buf[(SL_BUF_SIZE-1)&((a)->head-1)])
#define SL_FULL(a)	(!SL_LEFT(a))
#define SL_CHARS(a)	(((a)->head-(a)->tail)&(SL_BUF_SIZE-1))

extern int slip_init(struct ddi *dev);

#endif	/* _LINUX_SLIP.H */
