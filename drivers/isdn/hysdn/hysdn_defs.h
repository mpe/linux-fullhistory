/* $Id: hysdn_defs.h,v 1.1 2000/02/10 19:44:30 werner Exp $

 * Linux driver for HYSDN cards, global definitions and exported vars and functions.
 * written by Werner Cornelius (werner@titro.de) for Hypercope GmbH
 *
 * Copyright 1999  by Werner Cornelius (werner@titro.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Log: hysdn_defs.h,v $
 * Revision 1.1  2000/02/10 19:44:30  werner
 *
 * Initial release
 *
 *
 */

#include <linux/hysdn_if.h>
#include <linux/interrupt.h>
#include <linux/tqueue.h>
#include <linux/skbuff.h>

/****************************/
/* storage type definitions */
/****************************/
#define uchar unsigned char
#define uint unsigned int
#define ulong unsigned long
#define word unsigned short

#include "ince1pc.h"

/************************************************/
/* constants and bits for debugging/log outputs */
/************************************************/
#define LOG_MAX_LINELEN 120
#define DEB_OUT_SYSLOG  0x80000000	/* output to syslog instead of proc fs */
#define LOG_MEM_ERR     0x00000001	/* log memory errors like kmalloc failure */
#define LOG_POF_OPEN    0x00000010	/* log pof open and close activities */
#define LOG_POF_RECORD  0x00000020	/* log pof record parser */
#define LOG_POF_WRITE   0x00000040	/* log detailed pof write operation */
#define LOG_POF_CARD    0x00000080	/* log pof related card functions */
#define LOG_CNF_LINE    0x00000100	/* all conf lines are put to procfs */
#define LOG_CNF_DATA    0x00000200	/* non comment conf lines are shown with channel */
#define LOG_CNF_MISC    0x00000400	/* additional conf line debug outputs */
#define LOG_SCHED_ASYN  0x00001000	/* debug schedulers async tx routines */
#define LOG_PROC_OPEN   0x00100000	/* open and close from procfs are logged */
#define LOG_PROC_ALL    0x00200000	/* all actions from procfs are logged */
#define LOG_NET_INIT    0x00010000	/* network init and deinit logging */

#define DEF_DEB_FLAGS   0x7fff000f	/* everything is logged to procfs */

/**********************************/
/* proc filesystem name constants */
/**********************************/
#define PROC_SUBDIR_NAME "hysdn"
#define PROC_CONF_BASENAME "cardconf"
#define PROC_LOG_BASENAME "cardlog"

/************************/
/* PCI constant defines */
/************************/
#define PCI_VENDOR_ID_HYPERCOPE 0x1365
#define PCI_DEVICE_ID_PLX 0x9050	/* all DPRAM cards use the same id */

/*****************************/
/* sub ids determining cards */
/*****************************/
#define PCI_SUB_ID_OLD_ERGO 0x0104
#define PCI_SUB_ID_ERGO     0x0106
#define PCI_SUB_ID_METRO    0x0107
#define PCI_SUB_ID_CHAMP2   0x0108
#define PCI_SUB_ID_PLEXUS   0x0109

/***********************************/
/* PCI 32 bit parms for IO and MEM */
/***********************************/
#define PCI_REG_PLX_MEM_BASE    0
#define PCI_REG_PLX_IO_BASE     1
#define PCI_REG_MEMORY_BASE     3

/**************/
/* card types */
/**************/
#define BD_NONE         0U
#define BD_PERFORMANCE  1U
#define BD_VALUE        2U
#define BD_PCCARD       3U
#define BD_ERGO         4U
#define BD_METRO        5U
#define BD_CHAMP2       6U
#define BD_PLEXUS       7U

/******************************************************/
/* defined states for cards shown by reading cardconf */
/******************************************************/
#define CARD_STATE_UNUSED   0	/* never been used or booted */
#define CARD_STATE_BOOTING  1	/* booting is in progress */
#define CARD_STATE_BOOTERR  2	/* a previous boot was aborted */
#define CARD_STATE_RUN      3	/* card is active */

/*******************************/
/* defines for error_log_state */
/*******************************/
#define ERRLOG_STATE_OFF   0	/* error log is switched off, nothing to do */
#define ERRLOG_STATE_ON    1	/* error log is switched on, wait for data */
#define ERRLOG_STATE_START 2	/* start error logging */
#define ERRLOG_STATE_STOP  3	/* stop error logging */

/*******************************/
/* data structure for one card */
/*******************************/
typedef struct HYSDN_CARD {

	/* general variables for the cards */
	int myid;		/* own driver card id */
	uchar bus;		/* pci bus the card is connected to */
	uchar devfn;		/* slot+function bit encoded */
	word subsysid;		/* PCI subsystem id */
	uchar brdtype;		/* type of card */
	uint bchans;		/* number of available B-channels */
	uint faxchans;		/* number of available fax-channels */
	uchar mac_addr[6];	/* MAC Address read from card */
	uint irq;		/* interrupt number */
	uint iobase;		/* IO-port base address */
	ulong plxbase;		/* PLX memory base */
	ulong membase;		/* DPRAM memory base */
	ulong memend;		/* DPRAM memory end */
	void *dpram;		/* mapped dpram */
	int state;		/* actual state of card -> CARD_STATE_** */
	struct HYSDN_CARD *next;	/* pointer to next card */

	/* data areas for the /proc file system */
	void *proclog;		/* pointer to proclog filesystem specific data */
	void *procconf;		/* pointer to procconf filesystem specific data */

	/* debugging and logging */
	uchar err_log_state;	/* actual error log state of the card */
	ulong debug_flags;	/* tells what should be debugged and where */
	void (*set_errlog_state) (struct HYSDN_CARD *, int);

	/* interrupt handler + interrupt synchronisation */
	struct tq_struct irq_queue;	/* interrupt task queue */
	uchar volatile irq_enabled;	/* interrupt enabled if != 0 */
	uchar volatile hw_lock;	/* hardware is currently locked -> no access */

	/* boot process */
	void *boot;		/* pointer to boot private data */
	int (*writebootimg) (struct HYSDN_CARD *, uchar *, ulong);
	int (*writebootseq) (struct HYSDN_CARD *, uchar *, int);
	int (*waitpofready) (struct HYSDN_CARD *);
	int (*testram) (struct HYSDN_CARD *);

	/* scheduler for data transfer (only async parts) */
	uchar async_data[256];	/* async data to be sent (normally for config) */
	word volatile async_len;	/* length of data to sent */
	word volatile async_channel;	/* channel number for async transfer */
	int volatile async_busy;	/* flag != 0 sending in progress */
	int volatile net_tx_busy;	/* a network packet tx is in progress */

	/* network interface */
	void *netif;		/* pointer to network structure */

	/* init and deinit stopcard for booting, too */
	void (*stopcard) (struct HYSDN_CARD *);
	void (*releasehardware) (struct HYSDN_CARD *);
} hysdn_card;


/*****************/
/* exported vars */
/*****************/
extern int cardmax;		/* number of found cards */
extern hysdn_card *card_root;	/* pointer to first card */



/*************************/
/* im/exported functions */
/*************************/
extern int printk(const char *fmt,...);
extern char *hysdn_getrev(const char *);

/* hysdn_procconf.c */
extern int hysdn_procconf_init(void);	/* init proc config filesys */
extern void hysdn_procconf_release(void);	/* deinit proc config filesys */

/* hysdn_proclog.c */
extern int hysdn_proclog_init(hysdn_card *);	/* init proc log entry */
extern void hysdn_proclog_release(hysdn_card *);	/* deinit proc log entry */
extern void put_log_buffer(hysdn_card *, char *);	/* output log data */
extern void hysdn_addlog(hysdn_card *, char *,...);	/* output data to log */
extern void hysdn_card_errlog(hysdn_card *, tErrLogEntry *, int);	/* output card log */

/* boardergo.c */
extern int ergo_inithardware(hysdn_card * card);	/* get hardware -> module init */

/* hysdn_boot.c */
extern int pof_write_close(hysdn_card *);	/* close proc file after writing pof */
extern int pof_write_open(hysdn_card *, uchar **);	/* open proc file for writing pof */
extern int pof_write_buffer(hysdn_card *, int);		/* write boot data to card */
extern int EvalSysrTokData(hysdn_card *, uchar *, int);		/* Check Sysready Token Data */

/* hysdn_sched.c */
extern int hysdn_sched_tx(hysdn_card *, uchar *, word volatile *, word volatile *,
			  word);
extern int hysdn_sched_rx(hysdn_card *, uchar *, word, word);
extern int hysdn_tx_cfgline(hysdn_card *, uchar *, word);	/* send one cfg line */

/* hysdn_net.c */
extern char *hysdn_net_revision;
extern int hysdn_net_create(hysdn_card *);	/* create a new net device */
extern int hysdn_net_release(hysdn_card *);	/* delete the device */
extern char *hysdn_net_getname(hysdn_card *);	/* get name of net interface */
extern void hysdn_tx_netack(hysdn_card *);	/* acknowledge a packet tx */
extern struct sk_buff *hysdn_tx_netget(hysdn_card *);	/* get next network packet */
extern void hysdn_rx_netpkt(hysdn_card *, uchar *, word);	/* rxed packet from network */
