/*****************************************************************************
* wanpipe.h	WANPIPE(tm) Multiprotocol WAN Link Driver.
*		User-level API definitions.
*
* Author:	Gene Kozin	<genek@compuserve.com>
*
* Copyright:	(c) 1995-1997 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Jan 02, 1997	Gene Kozin	Version 3.0.0
*****************************************************************************/
#ifndef	_WANPIPE_H
#define	_WANPIPE_H

#include <linux/wanrouter.h>

/* Defines */
#define	WANPIPE_MAGIC	0x414C4453L	/* signatire: 'SDLA' reversed */

/* IOCTL numbers (up to 16) */
#define	WANPIPE_DUMP	(ROUTER_USER+0)	/* dump adapter's memory */
#define	WANPIPE_EXEC	(ROUTER_USER+1)	/* execute firmware command */

/*
 * Data structures for IOCTL calls.
 */

typedef struct sdla_dump	/* WANPIPE_DUMP */
{
	unsigned long magic;	/* for verification */
	unsigned long offset;	/* absolute adapter memory address */
	unsigned long length;	/* block length */
	void* ptr;		/* -> buffer */
} sdla_dump_t;

typedef struct sdla_exec	/* WANPIPE_EXEC */
{
	unsigned long magic;	/* for verification */
	void* cmd;		/* -> command structure */
	void* data;		/* -> data buffer */
} sdla_exec_t;

#ifdef	__KERNEL__
/****** Kernel Interface ****************************************************/

#include <linux/sdladrv.h>	/* SDLA support module API definitions */
#include <linux/sdlasfm.h>	/* SDLA firmware module definitions */

#ifndef	min
#define min(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef	max
#define max(a,b) (((a)>(b))?(a):(b))
#endif

#define	is_digit(ch) (((ch)>=(unsigned)'0'&&(ch)<=(unsigned)'9')?1:0)
#define	is_alpha(ch) ((((ch)>=(unsigned)'a'&&(ch)<=(unsigned)'z')||\
	 	  ((ch)>=(unsigned)'A'&&(ch)<=(unsigned)'Z'))?1:0)
#define	is_hex_digit(ch) ((((ch)>=(unsigned)'0'&&(ch)<=(unsigned)'9')||\
	 	  ((ch)>=(unsigned)'a'&&(ch)<=(unsigned)'f')||\
	 	  ((ch)>=(unsigned)'A'&&(ch)<=(unsigned)'F'))?1:0)

/****** Data Structures *****************************************************/

/* Adapter Data Space.
 * This structure is needed because we handle multiple cards, otherwise
 * static data would do it.
 */
typedef struct sdla
{
	char devname[WAN_DRVNAME_SZ+1];	/* card name */
	sdlahw_t hw;			/* hardware configuration */
	wan_device_t wandev;		/* WAN device data space */
	unsigned open_cnt;		/* number of open interfaces */
	unsigned long state_tick;	/* link state timestamp */
	char in_isr;			/* interrupt-in-service flag */
	void* mbox;			/* -> mailbox */
	void* rxmb;			/* -> receive mailbox */
	void* flags;			/* -> adapter status flags */
	void (*isr)(struct sdla* card);	/* interrupt service routine */
	void (*poll)(struct sdla* card); /* polling routine */
	int (*exec)(struct sdla* card, void* u_cmd, void* u_data);
	union
	{
		struct
		{			/****** X.25 specific data **********/
			unsigned lo_pvc;
			unsigned hi_pvc;
			unsigned lo_svc;
			unsigned hi_svc;
		} x;
		struct
		{			/****** frame relay specific data ***/
			void* rxmb_base;	/* -> first Rx buffer */
			void* rxmb_last;	/* -> last Rx buffer */
			unsigned rx_base;	/* S508 receive buffer base */
			unsigned rx_top;	/* S508 receive buffer end */
			unsigned short node_dlci;
			unsigned short dlci_num;
		} f;
		struct			/****** PPP-specific data ***********/
		{
			char if_name[WAN_IFNAME_SZ+1];	/* interface name */
			void* txbuf;		/* -> current Tx buffer */
			void* txbuf_base;	/* -> first Tx buffer */
			void* txbuf_last;	/* -> last Tx buffer */
			void* rxbuf_base;	/* -> first Rx buffer */
			void* rxbuf_last;	/* -> last Rx buffer */
			unsigned rx_base;	/* S508 receive buffer base */
			unsigned rx_top;	/* S508 receive buffer end */
		} p;
	} u;
} sdla_t;

/****** Public Functions ****************************************************/

void wanpipe_open      (sdla_t* card);			/* wpmain.c */
void wanpipe_close     (sdla_t* card);			/* wpmain.c */
void wanpipe_set_state (sdla_t* card, int state);	/* wpmain.c */

int wpx_init (sdla_t* card, wandev_conf_t* conf);	/* wpx.c */
int wpf_init (sdla_t* card, wandev_conf_t* conf);	/* wpf.c */
int wpp_init (sdla_t* card, wandev_conf_t* conf);	/* wpp.c */

#endif	/* __KERNEL__ */
#endif	/* _WANPIPE_H */

