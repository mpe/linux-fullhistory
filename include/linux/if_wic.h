#ifndef _LINUX_IF_WIC_H
#define _LINUX_IF_WIC_H

#include <linux/sockios.h>

#define	SIOCDEVWIC	SIOCDEVPRIVATE

struct wicconf
{
	unsigned char 	pcmd;
	unsigned char	data[120];
	unsigned char	len;
};

/* WIC host to controller commands */

#define WIC_AYT		0x10	/* test dki */
#define WIC_RESET	0x11	/* reset controller */
#define WIC_SETSN	0x21	/* set station name */
#define WIC_SETPS	0x22	/* set power saving mode */
#define WIC_SETAF	0x23	/* set announce filter */
#define WIC_SETGPF	0x24	/* set GPSP filter */
#define WIC_GETVERH	0x61	/* get interface controller version */
#define WIC_GETNL	0x62	/* get neighbor list */
#define WIC_GETSN	0x65	/* get station name */
#define WIC_CLRSTATS	0x83	/* clear controller statistics */
#define WIC_SETNET	0x84	/* set network configuration */
#define WIC_SETSYS	0x85	/* set system configuration */
#define WIC_GETSTATS	0xc1	/* get statistics */
#define WIC_GETVERM	0xc3	/* get MAC version */
#define WIC_GETNET	0xc4	/* get network configuration */
#define WIC_GETSYS	0xc5	/* get system configuration */

/*
 * structure used for the GETNET/SETNET command
 */

struct wic_net {
	unsigned char ula[6];		/* ula of interface */
	unsigned char mode;		/* operating mode */
#define NET_MODE_ME		0x01	/* receive my ula */
#define NET_MODE_BCAST		0x02	/* receive bcasts */
#define NET_MODE_MCAST		0x04	/* receive mcasts */
#define NET_MODE_PROM		0x08	/* promiscuous */
#define NET_MODE_HC		0x10	/* is a hop coordinator */
#define NET_MODE_HC_VALID	0x20	/* hc address is valid */
#define NET_MODE_HCAP		0x40	/* hc is also ap */
#define NET_MODE_HC_KNOWN	0x80	/* hc is known */
	unsigned char rts_lo;		/* rts threshold */
	unsigned char rts_hi;		/* rts threshold */
	unsigned char retry;		/* retry limit */
	unsigned char hc_ula[6];	/* ula of hc */
	unsigned char key[4];		/* network key */
	unsigned char dsl;		/* direct send limit */
	unsigned char res1;		/* reserved */
};

/*
 * structure used for the GETSYS/SETSYS command 
 */

struct wic_sys {
	unsigned char mode;		/* set operating mode */
#define SYS_MODE_ANT_DIV	0x00	/* use antenna diversity */
#define SYS_MODE_ANT_1		0x01	/* use ant 1 for tx */
#define SYS_MODE_ANT_2		0x02	/* use ant 2 for tx */
#define SYS_MODE_HC_LOCK	0x04	/* lock onto current hc */
#define SYS_MODE_DEBUG		0x08	/* upload failed frames */
#define SYS_MODE_IAM_AP		0x10	/* I am AP */
#define SYS_MODE_IAM_HC		0x20	/* I am HC */
#define SYS_MODE_USE_SKIP	0x40	/* use skipping mechanism */
#define SYS_MODE_AUTO		0x80	/* station is in auto mode */
	unsigned char switches;		/* radio/controller switches */
#define SYS_SWITCH_STDBY	0x01	/* switch radio to standby */
#define SYS_SWITCH_TXRX		0x02	/* 1 = tx, manual mode only */
#define SYS_SWITCH_PA		0x04	/* 1 = enable PA on radio */
#define SYS_SWITCH_PWR		0x10	/* 1 = hi, 0 = lo power output */
#define SYS_SWITCH_RES1		0x20	/* reserved, must be 0 */
#define SYS_SWITCH_LIGHTS	0x40	/* light for tx & rx */
#define SYS_SWITCH_LIGHTS_HC	0x80	/* light for rx while coordinated */
	unsigned char hop_min;		/* hop range */
	unsigned char hop_max;		/* hop range */
	unsigned char pre_len;		/* preamble length (bytes) */
	unsigned char pre_match;	/* valid preamble match (bytes) */
	unsigned char mod;		/* data mod: 1 = 8:1, 0 = none */
	unsigned char cca_mode;		/* cca flags */
#define CCA_PKT_DET_BSY		0x01	/* busy if packet is detected */
#define CCA_VIRT_CARR		0x02	/* use virtual carrier */
#define CCA_RSSI_BSY		0x04	/* busy if rssi > threshold */
#define CCA_DATA_BSY		0x08	/* busy if valid data > XXX usec */
	unsigned char dwell_hi;		/* dwell time */
	unsigned char dwell_lo;		/* dwell time */
	unsigned char hc_timeout;	/* HC timeout */
	unsigned char rssi;		/* rssi threshold */
	unsigned char hc_rssi;		/* rssi of last hc frame */
	unsigned char hc_rssi_chan;	/* channel of hc rssi value */
};


#endif /* _LINUX_IF_WIC_H */


