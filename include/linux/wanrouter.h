/*****************************************************************************
* wanrouter.h	Definitions for the WAN Multiprotocol Router Module.
*		This module provides API and common services for WAN Link
*		Drivers and is completely hardware-independent.
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
* Jan 02, 1997	Gene Kozin	Initial version (based on wanpipe.h).
*****************************************************************************/
#ifndef	_ROUTER_H
#define	_ROUTER_H

#define	ROUTER_NAME	"wanrouter"	/* in case we ever change it */
#define	ROUTER_VERSION	1		/* version number */
#define	ROUTER_RELEASE	0		/* release (minor version) number */
#define	ROUTER_IOCTL	'W'		/* for IOCTL calls */
#define	ROUTER_MAGIC	0x524D4157L	/* signature: 'WANR' reversed */

/* IOCTL codes for /proc/router/<device> entries (up to 255) */
enum router_ioctls
{
	ROUTER_SETUP	= ROUTER_IOCTL<<8,	/* configure device */
	ROUTER_DOWN,				/* shut down device */
	ROUTER_STAT,				/* get device status */
	ROUTER_IFNEW,				/* add interface */
	ROUTER_IFDEL,				/* delete interface */
	ROUTER_IFSTAT,				/* get interface status */
	ROUTER_USER	= (ROUTER_IOCTL<<8)+16,	/* driver-specific calls */
	ROUTER_USER_MAX	= (ROUTER_IOCTL<<8)+31
};

/* NLPID for packet encapsulation (ISO/IEC TR 9577) */
#define	NLPID_IP	0xCC	/* Internet Protocol Datagram */
#define	NLPID_SNAP	0x80	/* IEEE Subnetwork Access Protocol */
#define	NLPID_CLNP	0x81	/* ISO/IEC 8473 */
#define	NLPID_ESIS	0x82	/* ISO/IEC 9542 */
#define	NLPID_ISIS	0x83	/* ISO/IEC ISIS */
#define	NLPID_Q933	0x08	/* CCITT Q.933 */

/* Miscellaneous */
#define	WAN_IFNAME_SZ	15	/* max length of the interface name */
#define	WAN_DRVNAME_SZ	15	/* max length of the link driver name */
#define	WAN_ADDRESS_SZ	31	/* max length of the WAN media address */

/****** Data Types **********************************************************/

/*----------------------------------------------------------------------------
 * X.25-specific link-level configuration.
 */
typedef struct wan_x25_conf
{
	unsigned lo_pvc;	/* lowest permanent circuit number */
	unsigned hi_pvc;	/* highest permanent circuit number */
	unsigned lo_svc;	/* lowest switched circuit number */
	unsigned hi_svc;	/* highest switched circuit number */
	unsigned hdlc_window;	/* HDLC window size (1..7) */
	unsigned pkt_window;	/* X.25 packet window size (1..7) */
	unsigned t1;		/* HDLC timer T1, sec (1..30) */
	unsigned t2;		/* HDLC timer T2, sec (0..29) */
	unsigned t4;		/* HDLC supervisory frame timer = T4 * T1 */
	unsigned n2;		/* HDLC retransmission limit (1..30) */
	unsigned t10_t20;	/* X.25 RESTART timeout, sec (1..255) */
	unsigned t11_t21;	/* X.25 CALL timeout, sec (1..255) */
	unsigned t12_t22;	/* X.25 RESET timeout, sec (1..255) */
	unsigned t13_t23;	/* X.25 CLEAR timeout, sec (1..255) */
	unsigned t16_t26;	/* X.25 INTERRUPT timeout, sec (1..255) */
	unsigned t28;		/* X.25 REGISTRATION timeout, sec (1..255) */
	unsigned r10_r20;	/* RESTART retransmission limit (0..250) */
	unsigned r12_r22;	/* RESET retransmission limit (0..250) */
	unsigned r13_r23;	/* CLEAR retransmission limit (0..250) */
	unsigned ccitt_compat;	/* compatibility mode: 1988/1984/1980 */
} wan_x25_conf_t;

/*----------------------------------------------------------------------------
 * Frame relay specific link-level configuration.
 */
typedef struct wan_fr_conf
{
	unsigned cir;		/* committed information rate */
	unsigned signalling;	/* local in-channel signalling type */
	unsigned t391;		/* link integrity verification timer */
	unsigned t392;		/* polling verification timer */
	unsigned n391;		/* full status polling cycle counter */
	unsigned n392;		/* error threshold counter */
	unsigned n393;		/* monitored events counter */
	unsigned dlci;		/* first DLC number (access node) */
	unsigned dlci_num;	/* number of DLCs (access node) */
} wan_fr_conf_t;

/*----------------------------------------------------------------------------
 * PPP-specific link-level configuration.
 */
typedef struct wan_ppp_conf
{
	unsigned restart_tmr;	/* restart timer */
	unsigned auth_rsrt_tmr;	/* authentication timer */
	unsigned auth_wait_tmr;	/* authentication timer */
	unsigned mdm_fail_tmr;	/* modem failure timer */
	unsigned dtr_drop_tmr;	/* DTR drop timer */
	unsigned connect_tmout;	/* connection timeout */
	unsigned conf_retry;	/* max. retry */
	unsigned term_retry;	/* max. retry */
	unsigned fail_retry;	/* max. retry */
	unsigned auth_retry;	/* max. retry */
	unsigned auth_options;	/* authentication opt. */
	unsigned ip_options;	/* IP options */
} wan_ppp_conf_t;

/*----------------------------------------------------------------------------
 * WAN device configuration. Passed to ROUTER_SETUP IOCTL.
 */
typedef struct wandev_conf
{
	unsigned magic;		/* magic number (for verification) */
	unsigned config_id;	/* configuration structure identifier */
				/****** hardware configuration ******/
	unsigned ioport;	/* adapter I/O port base */
	unsigned long maddr;	/* dual-port memory address */
	unsigned msize;		/* dual-port memory size */
	int irq;		/* interrupt request level */
	int dma;		/* DMA request level */
	unsigned bps;		/* data transfer rate */
	unsigned mtu;		/* maximum transmit unit size */
	char interface;		/* RS-232/V.35, etc. */
	char clocking;		/* external/internal */
	char line_coding;	/* NRZ/NRZI/FM0/FM1, etc. */
	char station;		/* DTE/DCE, primary/secondary, etc. */
	char connection;	/* permanent/switched/on-demand */
	unsigned hw_opt[4];	/* other hardware options */
	unsigned reserved[4];
				/****** arbitrary data ***************/
	unsigned data_size;	/* data buffer size */
	void* data;		/* data buffer, e.g. firmware */
	union			/****** protocol-specific ************/
	{
		wan_x25_conf_t x25;	/* X.25 configuration */
		wan_ppp_conf_t ppp;	/* PPP configuration */
		wan_fr_conf_t fr;	/* frame relay configuration */
	} u;
} wandev_conf_t;

/* 'config_id' definitions */
#define	WANCONFIG_X25	101	/* X.25 link */
#define	WANCONFIG_FR	102	/* frame relay link */
#define	WANCONFIG_PPP	103	/* synchronous PPP link */

/*
 * Configuration options defines.
 */
/* general options */
#define	WANOPT_OFF	0
#define	WANOPT_ON	1
#define	WANOPT_NO	0
#define	WANOPT_YES	1

/* intercace options */
#define	WANOPT_RS232	0
#define	WANOPT_V35	1

/* data encoding options */
#define	WANOPT_NRZ	0
#define	WANOPT_NRZI	1
#define	WANOPT_FM0	2
#define	WANOPT_FM1	3

/* link type options */
#define	WANOPT_POINTTOPOINT	0	/* RTS always active */
#define	WANOPT_MULTIDROP	1	/* RTS is active when transmitting */

/* clocking options */
#define	WANOPT_EXTERNAL	0
#define	WANOPT_INTERNAL	1

/* station options */
#define	WANOPT_DTE		0
#define	WANOPT_DCE		1
#define	WANOPT_CPE		0
#define	WANOPT_NODE		1
#define	WANOPT_SECONDARY	0
#define	WANOPT_PRIMARY		1

/* connection options */
#define	WANOPT_PERMANENT	0	/* DTR always active */
#define	WANOPT_SWITCHED		1	/* use DTR to setup link (dial-up) */
#define	WANOPT_ONDEMAND		2	/* activate DTR only before sending */

/* frame relay in-channel signalling */
#define	WANOPT_FR_ANSI		0	/* ANSI T1.617 Annex D */
#define	WANOPT_FR_Q933		1	/* ITU Q.933A */
#define	WANOPT_FR_LMI		2	/* LMI */

/*----------------------------------------------------------------------------
 * WAN Link Status Info (for ROUTER_STAT IOCTL).
 */
typedef struct wandev_stat
{
	unsigned state;		/* link state */
	unsigned ndev;		/* number of configured interfaces */

	/* link/interface configuration */
	unsigned connection;	/* permanent/switched/on-demand */
	unsigned media_type;	/* Frame relay/PPP/X.25/SDLC, etc. */
	unsigned mtu;		/* max. transmit unit for this device */

	/* physical level statistics */
	unsigned modem_status;	/* modem status */
	unsigned rx_frames;	/* received frames count */
	unsigned rx_overruns;	/* receiver overrun error count */
	unsigned rx_crc_err;	/* receive CRC error count */
	unsigned rx_aborts;	/* received aborted frames count */
	unsigned rx_bad_length;	/* unexpetedly long/short frames count */
	unsigned rx_dropped;	/* frames discarded at device level */
	unsigned tx_frames;	/* transmitted frames count */
	unsigned tx_underruns;	/* aborted transmissions (underruns) count */
	unsigned tx_timeouts;	/* transmission timeouts */
	unsigned tx_rejects;	/* other transmit errors */

	/* media level statistics */
	unsigned rx_bad_format;	/* frames with invalid format */
	unsigned rx_bad_addr;	/* frames with invalid media address */
	unsigned tx_retries;	/* frames re-transmitted */
	unsigned reserved[16];	/* reserved for future use */
} wandev_stat_t;

/* 'state' defines */
enum wan_states
{
	WAN_UNCONFIGURED,	/* link/channel is not configured */
	WAN_DISCONNECTED,	/* link/channel is disconnected */
	WAN_CONNECTING,		/* connection is in progress */
	WAN_CONNECTED,		/* link/channel is operational */
	WAN_LIMIT		/* for verification only */
};

/* 'modem_status' masks */
#define	WAN_MODEM_CTS	0x0001	/* CTS line active */
#define	WAN_MODEM_DCD	0x0002	/* DCD line active */
#define	WAN_MODEM_DTR	0x0010	/* DTR line active */
#define	WAN_MODEM_RTS	0x0020	/* RTS line active */

/*----------------------------------------------------------------------------
 * WAN interface (logical channel) configuration (for ROUTER_IFNEW IOCTL).
 */
typedef struct wanif_conf
{
	unsigned magic;			/* magic number */
	unsigned config_id;		/* configuration identifier */
	char name[WAN_IFNAME_SZ+1];	/* interface name, ASCIIZ */
	char addr[WAN_ADDRESS_SZ+1];	/* media address, ASCIIZ */
	unsigned idle_timeout;		/* sec, before disconnecting */
	unsigned hold_timeout;		/* sec, before re-connecting */
	int reserved[8];		/* reserved for future extensions */
} wanif_conf_t;

#ifdef	__KERNEL__
/****** Kernel Interface ****************************************************/

#include <linux/fs.h>		/* support for device drivers */
#include <linux/proc_fs.h>	/* proc filesystem pragmatics */
#include <linux/inet.h>		/* in_aton(), in_ntoa() prototypes */
#include <linux/netdevice.h>	/* support for network drivers */

/*----------------------------------------------------------------------------
 * WAN device data space.
 */
typedef struct wan_device
{
	unsigned magic;			/* magic number */
	char* name;			/* -> WAN device name (ASCIIZ) */
	void* private;			/* -> driver private data */
					/****** hardware configuration ******/
	unsigned ioport;		/* adapter I/O port base #1 */
	unsigned long maddr;		/* dual-port memory address */
	unsigned msize;			/* dual-port memory size */
	int irq;			/* interrupt request level */
	int dma;			/* DMA request level */
	unsigned bps;			/* data transfer rate */
	unsigned mtu;			/* max physical transmit unit size */
	char interface;			/* RS-232/V.35, etc. */
	char clocking;			/* external/internal */
	char line_coding;		/* NRZ/NRZI/FM0/FM1, etc. */
	char station;			/* DTE/DCE, primary/secondary, etc. */
	char connection;		/* permanent/switched/on-demand */
	unsigned hw_opt[4];		/* other hardware options */
					/****** status and statistics *******/
	char state;			/* device state */
	unsigned modem_status;		/* modem status */
	struct enet_statistics stats;	/* interface statistics */
	unsigned reserved[16];		/* reserved for future use */
	unsigned critical;		/* critical section flag */
					/****** device management methods ***/
	int (*setup) (struct wan_device* wandev, wandev_conf_t* conf);
	int (*shutdown) (struct wan_device* wandev);
	int (*update) (struct wan_device* wandev);
	int (*ioctl) (struct wan_device* wandev, unsigned cmd,
		unsigned long arg);
	int (*new_if) (struct wan_device* wandev, struct device* dev,
		wanif_conf_t* conf);
	int (*del_if) (struct wan_device* wandev, struct device* dev);
					/****** maintained by the router ****/
	struct wan_device* next;	/* -> next device */
	struct device* dev;		/* list of network interfaces */
	unsigned ndev;			/* number of interfaces */
	struct proc_dir_entry dent;	/* proc filesystem entry */
} wan_device_t;

/* Init Point */
extern void wanrouter_init(void);

/* Public functions available for device drivers */
extern int register_wan_device (wan_device_t* wandev);
extern int unregister_wan_device (char* name);
unsigned short wanrouter_type_trans (struct sk_buff* skb, struct device* dev);
int wanrouter_encapsulate (struct sk_buff* skb, struct device* dev);

/* Proc interface functions. These must not be called by the drivers! */
extern int wanrouter_proc_init (void);
extern void wanrouter_proc_cleanup (void);
extern int wanrouter_proc_add (wan_device_t* wandev);
extern int wanrouter_proc_delete (wan_device_t* wandev);
extern int wanrouter_ioctl(struct inode* inode, struct file* file,
	unsigned int cmd, unsigned long arg);

#endif	/* __KERNEL__ */
#endif	/* _ROUTER_H */
