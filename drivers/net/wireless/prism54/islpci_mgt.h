/*  $Header: /var/lib/cvs/prism54-ng/ksrc/islpci_mgt.h,v 1.22 2004/01/30 16:24:00 ajfa Exp $
 *  
 *  Copyright (C) 2002 Intersil Americas Inc.
 *  Copyright (C) 2003 Luis R. Rodriguez <mcgrof@ruslug.rutgers.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef _ISLPCI_MGT_H
#define _ISLPCI_MGT_H

#include <linux/wireless.h>
#include <linux/skbuff.h>

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,41)
# include <linux/workqueue.h>
#else
# include <linux/tqueue.h>
# define work_struct tq_struct
# define INIT_WORK INIT_TQUEUE
# define schedule_work schedule_task
#endif

/*
 *  Function definitions
 */

#define K_DEBUG(f, m, args...) do { if(f & m) printk(KERN_DEBUG args); } while(0)
#define DEBUG(f, args...) K_DEBUG(f, pc_debug, args)

#define TRACE(devname)   K_DEBUG(SHOW_TRACING, VERBOSE, "%s:  -> " __FUNCTION__ "()\n", devname)

extern int pc_debug;
static const int init_wds = 0;	/* help compiler optimize away dead code */


/* General driver definitions */
#define PCIVENDOR_INTERSIL                      0x1260UL
#define PCIVENDOR_3COM				0x10b7UL
#define PCIVENDOR_DLINK				0x1186UL
#define PCIVENDOR_I4				0x17cfUL
#define PCIVENDOR_IODATA			0x10fcUL
#define PCIVENDOR_NETGEAR			0x1385UL
#define PCIVENDOR_SMC				0x10b8UL
#define PCIVENDOR_ACCTON			0x1113UL

#define PCIDEVICE_ISL3877                       0x3877UL
#define PCIDEVICE_ISL3890                       0x3890UL
#define	PCIDEVICE_3COM6001			0x6001UL
#define PCIDEVICE_LATENCY_TIMER_MIN 		0x40
#define PCIDEVICE_LATENCY_TIMER_VAL 		0x50

/* Debugging verbose definitions */
#define SHOW_NOTHING                            0x00	/* overrules everything */
#define SHOW_ANYTHING                           0xFF
#define SHOW_ERROR_MESSAGES                     0x01
#define SHOW_TRAPS                              0x02
#define SHOW_FUNCTION_CALLS                     0x04
#define SHOW_TRACING                            0x08
#define SHOW_QUEUE_INDEXES                      0x10
#define SHOW_PIMFOR_FRAMES                      0x20
#define SHOW_BUFFER_CONTENTS                    0x40
#define VERBOSE                                 0x01

/* Default card definitions */
#define CARD_DEFAULT_CHANNEL                    6
#define CARD_DEFAULT_MODE                       INL_MODE_CLIENT
#define CARD_DEFAULT_IW_MODE			IW_MODE_INFRA
#define CARD_DEFAULT_BSSTYPE                    DOT11_BSSTYPE_INFRA
#define CARD_DEFAULT_CLIENT_SSID		""
#define CARD_DEFAULT_AP_SSID			"default"
#define CARD_DEFAULT_KEY1                       "default_key_1"
#define CARD_DEFAULT_KEY2                       "default_key_2"
#define CARD_DEFAULT_KEY3                       "default_key_3"
#define CARD_DEFAULT_KEY4                       "default_key_4"
#define CARD_DEFAULT_WEP                        0
#define CARD_DEFAULT_FILTER                     0
# define CARD_DEFAULT_WDS                        0
#define	CARD_DEFAULT_AUTHEN                     DOT11_AUTH_OS
#define	CARD_DEFAULT_DOT1X			0
#define CARD_DEFAULT_MLME_MODE			DOT11_MLME_AUTO
#define CARD_DEFAULT_CONFORMANCE                OID_INL_CONFORMANCE_NONE

/* PIMFOR package definitions */
#define PIMFOR_ETHERTYPE                        0x8828
#define PIMFOR_HEADER_SIZE                      12
#define PIMFOR_VERSION                          1
#define PIMFOR_OP_GET                           0
#define PIMFOR_OP_SET                           1
#define PIMFOR_OP_RESPONSE                      2
#define PIMFOR_OP_ERROR                         3
#define PIMFOR_OP_TRAP                          4
#define PIMFOR_OP_RESERVED                      5	/* till 255 */
#define PIMFOR_DEV_ID_MHLI_MIB                  0
#define PIMFOR_FLAG_APPLIC_ORIGIN               0x01
#define PIMFOR_FLAG_LITTLE_ENDIAN               0x02

static inline void
add_le32p(u32 * le_number, u32 add)
{
	*le_number = cpu_to_le32(le32_to_cpup(le_number) + add);
}

void display_buffer(char *, int);

/*
 *  Type definition section
 *
 *  the structure defines only the header allowing copyless
 *  frame handling
 */
typedef struct {
	u8 version;
	u8 operation;
	u32 oid;
	u8 device_id;
	u8 flags;
	u32 length;
} __attribute__ ((packed))
pimfor_header_t;

/* A received and interrupt-processed management frame, either for
 * schedule_work(prism54_process_trap) or for priv->mgmt_received,
 * processed by islpci_mgt_transaction(). */
struct islpci_mgmtframe {
	struct net_device *ndev;      /* pointer to network device */
	pimfor_header_t *header;      /* payload header, points into buf */
	void *data;		      /* payload ex header, points into buf */
        struct work_struct ws;	      /* argument for schedule_work() */
	char buf[0];		      /* fragment buffer */
};

int
islpci_mgt_receive(struct net_device *ndev);

int
islpci_mgmt_rx_fill(struct net_device *ndev);

void
islpci_mgt_cleanup_transmit(struct net_device *ndev);

int
islpci_mgt_transaction(struct net_device *ndev,
                       int operation, unsigned long oid,
		       void *senddata, int sendlen,
		       struct islpci_mgmtframe **recvframe);

static inline void
islpci_mgt_release(struct islpci_mgmtframe *frame)
{
        kfree(frame);
}

#endif				/* _ISLPCI_MGT_H */
