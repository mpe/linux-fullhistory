/*
 * slip.h	Define the SLIP device driver interface and constants.
 *
 * NOTE:	THIS FILE WILL BE MOVED TO THE LINUX INCLUDE DIRECTORY
 *		AS SOON AS POSSIBLE!
 *
 * Version:	@(#)slip.h	1.2.0	03/28/93
 *
 * Author:	Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 */
#ifndef _LINUX_SLIP_H
#define _LINUX_SLIP_H

/* SLIP configuration. */
#define SL_NRUNIT	4		/* number of SLIP channels	*/
#define SL_MTU		296		/* 296; I am used to 600- FvK	*/

/* SLIP protocol characters. */
#define END             0300		/* indicates end of frame	*/
#define ESC             0333		/* indicates byte stuffing	*/
#define ESC_END         0334		/* ESC ESC_END means END 'data'	*/
#define ESC_ESC         0335		/* ESC ESC_ESC means ESC 'data'	*/


struct slip {
  /* Bitmapped flag fields. */
  char			inuse;		/* are we allocated?		*/
  char			sending;	/* "channel busy" indicator	*/
  char			escape;		/* SLIP state machine		*/
  char			unused;		/* fillers			*/

  /* Various fields. */
  int			line;		/* SLIP channel number		*/
  struct tty_struct	*tty;		/* ptr to TTY structure		*/
  struct device		*dev;		/* easy for intr handling	*/

  /* These are pointers to the malloc()ed frame buffers. */
  unsigned char		*rbuff;		/* receiver buffer		*/
  unsigned char		*xbuff;		/* transmitter buffer		*/

  /* These are the various pointers into the buffers. */
  unsigned char		*rhead;		/* RECV buffer pointer (head)	*/
  unsigned char		*rend;		/* RECV buffer pointer (end)	*/
  int			rcount;		/* SLIP receive counter		*/

  /* SLIP interface statistics. */
  unsigned long		rpacket;	/* inbound frame counter	*/
  unsigned long		roverrun;	/* "buffer overrun" counter	*/
  unsigned long		spacket;	/* outbound frames counter	*/
  unsigned long		sbusy;		/* "transmitter busy" counter	*/
  unsigned long		errors;		/* error count			*/
};


extern int	slip_init(struct device *dev);


#endif	/* _LINUX_SLIP.H */
