/* $Id: isdn.h,v 1.2 1996/02/11 02:10:02 fritz Exp fritz $
 *
 * Main header for the Linux ISDN subsystem (linklevel).
 *
 * Copyright 1994,95,96 by Fritz Elfert (fritz@wuemaus.franken.de)
 * Copyright 1995,96    by Thinking Objects Software GmbH Wuerzburg
 * Copyright 1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
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
 * $Log: isdn.h,v $
 * Revision 1.2  1996/02/11 02:10:02  fritz
 * Changed IOCTL-names
 * Added rx_netdev, st_netdev, first_skb, org_hcb, and org_hcu to
 * Netdevice-local struct.
 *
 * Revision 1.1  1996/01/10 20:55:07  fritz
 * Initial revision
 *
 */

#ifndef isdn_h
#define isdn_h

#include <linux/ioctl.h>

#define ISDN_TTY_MAJOR    43
#define ISDN_TTYAUX_MAJOR 44
#define ISDN_MAJOR        45

/* The minor-devicenumbers for Channel 0 and 1 are used as arguments for
 * physical Channel-Mapping, so they MUST NOT be changed without changing
 * the correspondent code in isdn.c
 */

#define ISDN_MAX_DRIVERS    16
#define ISDN_MAX_CHANNELS   16
#define ISDN_MINOR_B        0
#define ISDN_MINOR_BMAX     (ISDN_MAX_CHANNELS-1)
#define ISDN_MINOR_CTRL     ISDN_MAX_CHANNELS
#define ISDN_MINOR_CTRLMAX  (2*ISDN_MAX_CHANNELS-1)
#define ISDN_MINOR_PPP      (2*ISDN_MAX_CHANNELS)
#define ISDN_MINOR_PPPMAX   (3*ISDN_MAX_CHANNELS-1)
#define ISDN_MINOR_STATUS   128

/* New ioctl-codes */
#define IIOCNETAIF  _IO('I',1)
#define IIOCNETDIF  _IO('I',2)
#define IIOCNETSCF  _IO('I',3)
#define IIOCNETGCF  _IO('I',4)
#define IIOCNETANM  _IO('I',5)
#define IIOCNETDNM  _IO('I',6)
#define IIOCNETGNM  _IO('I',7)
#define IIOCGETSET  _IO('I',8)
#define IIOCSETSET  _IO('I',9)
#define IIOCSETVER  _IO('I',10)
#define IIOCNETHUP  _IO('I',11)
#define IIOCSETGST  _IO('I',12)
#define IIOCSETBRJ  _IO('I',13)
#define IIOCSIGPRF  _IO('I',14)
#define IIOCGETPRF  _IO('I',15)
#define IIOCSETPRF  _IO('I',16)
#define IIOCGETMAP  _IO('I',17)
#define IIOCSETMAP  _IO('I',18)
#define IIOCNETASL  _IO('I',19)
#define IIOCNETDIL  _IO('I',20)

#define IIOCNETALN  _IO('I',32)
#define IIOCNETDLN  _IO('I',33)

#define IIOCDBGVAR  _IO('I',127)

#define IIOCDRVCTL  _IO('I',128)

/* Packet encapsulations for net-interfaces */
#define ISDN_NET_ENCAP_ETHER     0
#define ISDN_NET_ENCAP_RAWIP     1
#define ISDN_NET_ENCAP_IPTYP     2
#define ISDN_NET_ENCAP_CISCOHDLC 3
#define ISDN_NET_ENCAP_SYNCPPP   4

/* Facility which currently uses an ISDN-channel */
#define ISDN_USAGE_NONE       0
#define ISDN_USAGE_RAW        1
#define ISDN_USAGE_MODEM      2
#define ISDN_USAGE_NET        3
#define ISDN_USAGE_VOICE      4
#define ISDN_USAGE_FAX        5
#define ISDN_USAGE_MASK       7 /* Mask to get plain usage */
#define ISDN_USAGE_EXCLUSIVE 64 /* This bit is set, if channel is exclusive */
#define ISDN_USAGE_OUTGOING 128 /* This bit is set, if channel is outgoing  */

#define ISDN_MODEM_ANZREG    20        /* Number of Medem-Registers        */
#define ISDN_MSNLEN          20

typedef struct {
  char drvid[25];
  unsigned long arg;
} isdn_ioctl_struct;

typedef struct {
  unsigned long isdndev;
  unsigned long atmodem[ISDN_MAX_CHANNELS];
  unsigned long info[ISDN_MAX_CHANNELS];
} debugvar_addr;

typedef struct {
  char name[10];
  char phone[20];
  int  outgoing;
} isdn_net_ioctl_phone;

typedef struct {
  char name[10];     /* Name of interface                     */
  char master[10];   /* Name of Master for Bundling           */
  char slave[10];    /* Name of Slave for Bundling            */
  char eaz[256];     /* EAZ/MSN                               */
  char drvid[25];    /* DriverId for Bindings                 */
  int  secure;       /* Flag: Secure                          */
  int  callback;     /* Flag: Callback                        */
  int  onhtime;      /* Hangup-Timeout                        */
  int  charge;       /* Charge-Units                          */
  int  chargehup;    /* Flag: Charge-Hangup                   */
  int  l2_proto;     /* Layer-2 protocol                      */
  int  l3_proto;     /* Layer-3 protocol                      */
  int  p_encap;      /* Encapsulation                         */
  int  ihup;         /* Flag: Hangup-Timeout on incoming line */
  int  exclusive;    /* Channel, if bound exclusive           */
  int  slavedelay;   /* Delay until slave starts up           */
} isdn_net_ioctl_cfg;

#ifdef __KERNEL__

#ifndef STANDALONE
#include <linux/config.h>
#endif
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_reg.h>
#include <linux/fcntl.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/tcp.h>

#ifdef CONFIG_ISDN_PPP

#ifdef CONFIG_ISDN_PPP_VJ
#  include "/usr/src/linux/drivers/net/slhc.h"
#endif

#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>
#include <linux/if_pppvar.h>

#include <linux/isdn_ppp.h>
#endif

#include <linux/isdnif.h>

#define ISDN_DRVIOCTL_MASK       0x7f  /* Mask for Device-ioctl */

/* Until now unused */
#define ISDN_SERVICE_VOICE 1
#define ISDN_SERVICE_AB    1<<1 
#define ISDN_SERVICE_X21   1<<2
#define ISDN_SERVICE_G4    1<<3
#define ISDN_SERVICE_BTX   1<<4
#define ISDN_SERVICE_DFUE  1<<5
#define ISDN_SERVICE_X25   1<<6
#define ISDN_SERVICE_TTX   1<<7
#define ISDN_SERVICE_MIXED 1<<8
#define ISDN_SERVICE_FW    1<<9
#define ISDN_SERVICE_GTEL  1<<10
#define ISDN_SERVICE_BTXN  1<<11
#define ISDN_SERVICE_BTEL  1<<12

/* Macros checking plain usage */
#define USG_NONE(x)     ((x & ISDN_USAGE_MASK)==ISDN_USAGE_NONE)
#define USG_RAW(x)      ((x & ISDN_USAGE_MASK)==ISDN_USAGE_RAW)
#define USG_MODEM(x)    ((x & ISDN_USAGE_MASK)==ISDN_USAGE_MODEM)
#define USG_NET(x)      ((x & ISDN_USAGE_MASK)==ISDN_USAGE_NET)
#define USG_OUTGOING(x) ((x & ISDN_USAGE_OUTGOING)==ISDN_USAGE_OUTGOING)

/* Timer-delays and scheduling-flags */
#define ISDN_TIMER_RES       3                     /* Main Timer-Resolution  */
#define ISDN_TIMER_02SEC     (HZ/ISDN_TIMER_RES/5) /* Slow-Timer1 (0.2 sec.) */
#define ISDN_TIMER_1SEC      (HZ/ISDN_TIMER_RES)   /* Slow-Timer2 (1 sec.)   */
#define ISDN_TIMER_MODEMREAD 1
#define ISDN_TIMER_MODEMPLUS 2
#define ISDN_TIMER_MODEMRING 4
#define ISDN_TIMER_MODEMXMIT 8
#define ISDN_TIMER_NETDIAL   16
#define ISDN_TIMER_NETHANGUP 32
#define ISDN_TIMER_IPPP      64
#define ISDN_TIMER_FAST      (ISDN_TIMER_MODEMREAD | ISDN_TIMER_MODEMPLUS | \
                              ISDN_TIMER_MODEMXMIT)
#define ISDN_TIMER_SLOW      (ISDN_TIMER_MODEMRING | ISDN_TIMER_NETHANGUP | \
                              ISDN_TIMER_NETDIAL)

/* Timeout-Values for isdn_net_dial() */
#define ISDN_TIMER_DTIMEOUT10 (10*HZ/(ISDN_TIMER_02SEC*ISDN_TIMER_RES))
#define ISDN_TIMER_DTIMEOUT15 (15*HZ/(ISDN_TIMER_02SEC*ISDN_TIMER_RES))

/* GLOBAL_FLAGS */
#define ISDN_GLOBAL_STOPPED 1

/*=================== Start of ip-over-ISDN stuff =========================*/

/* Feature- and status-flags for a net-interface */
#define ISDN_NET_CONNECTED  0x01       /* Bound to ISDN-Channel             */
#define ISDN_NET_SECURE     0x02       /* Accept calls from phonelist only  */
#define ISDN_NET_CALLBACK   0x04       /* callback incoming phonenumber     */
#define ISDN_NET_CLONE      0x08       /* clone a tmp interface when called */
#define ISDN_NET_TMP        0x10       /* tmp interface until getting an IP */
#define ISDN_NET_DYNAMIC    0x20       /* this link is dynamically allocted */
#define ISDN_NET_MAGIC      0x49344C02 /* for paranoia-checking             */

/* Phone-list-element */
typedef struct {
  void *next;
  char num[20];
} isdn_net_phone;

/* Local interface-data */
typedef struct isdn_net_local_s {
  ulong                  magic;
  char                   name[10];     /* Name of device                   */
  struct enet_statistics stats;        /* Ethernet Statistics              */
  int                    isdn_device;  /* Index to isdn-device             */
  int                    isdn_channel; /* Index to isdn-channel            */
  int			 ppp_minor;    /* PPPD device minor number         */
  int                    pre_device;   /* Preselected isdn-device          */
  int                    pre_channel;  /* Preselected isdn-channel         */
  int                    exclusive;    /* If non-zero idx to reserved chan.*/
  int                    flags;        /* Connection-flags                 */
  int                    dialstate;    /* State for dialing                */
  int                    dialretry;    /* Counter for Dialout-retries      */
  int                    dialmax;      /* Max. Nuber of Dial-retries       */
  char                   msn[ISDN_MSNLEN]; /* MSNs/EAZs for this interface */
  int                    dtimer;       /* Timeout-counter for dialing      */
  u_char                 p_encap;      /* Packet encapsulation             */
                                       /*   0 = Ethernet over ISDN         */
				       /*   1 = RAW-IP                     */
                                       /*   2 = IP with type field         */
  u_char                 l2_proto;     /* Layer-2-protocol                 */
				       /* See ISDN_PROTO_L2..-constants in */
                                       /* isdnif.h                         */
                                       /*   0 = X75/LAPB with I-Frames     */
				       /*   1 = X75/LAPB with UI-Frames    */
				       /*   2 = X75/LAPB with BUI-Frames   */
				       /*   3 = HDLC                       */
  u_char                 l3_proto;     /* Layer-3-protocol                 */
				       /* See ISDN_PROTO_L3..-constants in */
                                       /* isdnif.h                         */
                                       /*   0 = Transparent                */
  int                    huptimer;     /* Timeout-counter for auto-hangup  */
  int                    charge;       /* Counter for charging units       */
  int                    chargetime;   /* Timer for Charging info          */
  int                    hupflags;     /* Flags for charge-unit-hangup:    */
				       /* bit0: chargeint is invalid       */
				       /* bit1: Getting charge-interval    */
                                       /* bit2: Do charge-unit-hangup      */
  int                    outgoing;     /* Flag: outgoing call              */
  int                    onhtime;      /* Time to keep link up             */
  int                    chargeint;    /* Interval between charge-infos    */
  int                    onum;         /* Flag: at least 1 outgoing number */
  int                    cps;          /* current speed of this interface  */
  int                    transcount;   /* byte-counter for cps-calculation */
  int                    sqfull;       /* Flag: netdev-queue overloaded    */
  ulong                  sqfull_stamp; /* Start-Time of overload           */
  ulong                  slavedelay;   /* Dynamic bundling delaytime       */
  struct device          *srobin;      /* Ptr to Master device for slaves  */
  isdn_net_phone         *phone[2];    /* List of remote-phonenumbers      */
				       /* phone[0] = Incoming Numbers      */
				       /* phone[1] = Outgoing Numbers      */
  isdn_net_phone         *dial;        /* Pointer to dialed number         */
  struct device          *master;      /* Ptr to Master device for slaves  */
  struct device          *slave;       /* Ptr to Slave device for masters  */
  struct isdn_net_local_s *next;       /* Ptr to next link in bundle       */
  struct isdn_net_local_s *last;       /* Ptr to last link in bundle       */
  struct isdn_net_dev_s  *netdev;      /* Ptr to netdev                    */
  struct sk_buff         *first_skb;   /* Ptr to skb that triggers dialing */
  struct sk_buff         *sav_skb;     /* Ptr to skb, rejected by LL-driver*/
                                       /* Ptr to orig. header_cache_bind   */
  void                   (*org_hcb)(struct hh_cache **, struct device *,
                                    unsigned short, __u32);
                                       /* Ptr to orig. header_cache_update */
  void                   (*org_hcu)(struct hh_cache *, struct device *,
                                    unsigned char *);
} isdn_net_local;

#ifdef CONFIG_ISDN_PPP
struct ippp_bundle {
  int mp_mrru;                        /* unused                             */
  struct mpqueue *last;               /* currently defined in isdn_net_dev  */
  int min;                            /* currently calculated 'on the fly'  */
  long next_num;                      /* we wanna see this seq.-number next */
  struct sqqueue *sq;
  int modify:1;                       /* set to 1 while modifying sqqueue   */
  int bundled:1;                      /* bundle active ?                    */
};
#endif

/* the interface itself */
typedef struct isdn_net_dev_s {
  isdn_net_local  local;
  isdn_net_local *queue;
  void           *next;                /* Pointer to next isdn-interface   */
  struct device   dev;	               /* interface to upper levels        */
#ifdef CONFIG_ISDN_PPP
  struct mpqueue *mp_last; 
  struct ippp_bundle ib;
#endif
} isdn_net_dev;

/*===================== End of ip-over-ISDN stuff ===========================*/

/*======================= Start of ISDN-tty stuff ===========================*/

#define ISDN_ASYNC_MAGIC          0x49344C01 /* for paranoia-checking        */
#define ISDN_ASYNC_INITIALIZED	  0x80000000 /* port was initialized         */
#define ISDN_ASYNC_CALLOUT_ACTIVE 0x40000000 /* Call out device active       */
#define ISDN_ASYNC_NORMAL_ACTIVE  0x20000000 /* Normal device active         */
#define ISDN_ASYNC_CLOSING	  0x08000000 /* Serial port is closing       */
#define ISDN_ASYNC_CTS_FLOW	  0x04000000 /* Do CTS flow control          */
#define ISDN_ASYNC_CHECK_CD	  0x02000000 /* i.e., CLOCAL                 */
#define ISDN_ASYNC_HUP_NOTIFY         0x0001 /* Notify tty on hangups/closes */
#define ISDN_ASYNC_SESSION_LOCKOUT    0x0100 /* Lock cua opens on session    */
#define ISDN_ASYNC_PGRP_LOCKOUT       0x0200 /* Lock cua opens on pgrp       */
#define ISDN_ASYNC_CALLOUT_NOHUP      0x0400 /* No hangup for cui            */
#define ISDN_ASYNC_SPLIT_TERMIOS      0x0008 /* Sep. termios for dialin/out  */
#define ISDN_PORT_16550A                   4 /* Type of faked Hardware       */
#define ISDN_SERIAL_XMIT_SIZE           4000 /* Maximum bufsize for write    */
#define ISDN_SERIAL_TYPE_NORMAL	           1 /* tty-type                     */
#define ISDN_SERIAL_TYPE_CALLOUT           2 /* cua-type                     */

/* Private data (similar to async_struct in <linux/serial.h>) */
typedef struct {
  int			magic;
  int			flags;		 /* defined in tty.h               */
  int			type;		 /* UART type                      */
  struct tty_struct 	*tty;
  int			x_char;		 /* xon/xoff character             */
  int			close_delay;
  int			MCR;		 /* Modem control register         */
  int			line;
  int			count;		 /* # of fd on device              */
  int			blocked_open;	 /* # of blocked opens             */
  long			session;	 /* Session of opening process     */
  long			pgrp;		 /* pgrp of opening process        */
  int                   isdn_driver;	 /* Index to isdn-driver           */
  int                   isdn_channel;    /* Index to isdn-channel          */
  int                   drv_index;       /* Index to dev->usage            */
#if FUTURE
  int                   send_outstanding;/* # of outstanding send-requests */
#endif
  int                   xmit_size;       /* max. # of chars in xmit_buf    */
  int                   xmit_count;      /* # of chars in xmit_buf         */
  u_char                *xmit_buf;       /* transmit-buffer                */
  struct termios	normal_termios;
  struct termios	callout_termios;
  struct wait_queue	*open_wait;
  struct wait_queue	*close_wait;
} modem_info;

#define ISDN_MODEM_WINSIZE 8

/* Private data of AT-command-interpreter */
typedef struct {
  u_char              profile[ISDN_MODEM_ANZREG]; /* Modem-Regs. Profile 0  */
  u_char              mdmreg[ISDN_MODEM_ANZREG];  /* Modem-Registers        */
  char                pmsn[ISDN_MSNLEN]; /* EAZ/MSNs Profile 0             */
  char                msn[ISDN_MSNLEN];/* EAZ/MSN                          */
  int                 mdmcmdl;         /* Length of Modem-Commandbuffer    */
  int                 pluscount;       /* Counter for +++ sequence         */
  int                 lastplus;        /* Timestamp of last +              */
  char                mdmcmd[255];     /* Modem-Commandbuffer              */
} atemu;

/* Descripion of one ISDN-tty */
typedef struct {
  int                msr[ISDN_MAX_CHANNELS];	  /* Modem-statusregister   */
  int                mlr[ISDN_MAX_CHANNELS];	  /* Line-statusregister    */
  int                refcount;			  /* Number of opens        */
  int                online[ISDN_MAX_CHANNELS];	  /* B-Channel is up        */
  int                dialing[ISDN_MAX_CHANNELS];  /* Dial in progress       */
  int                rcvsched[ISDN_MAX_CHANNELS]; /* Receive needs schedule */
  int                ncarrier[ISDN_MAX_CHANNELS]; /* Output NO CARRIER      */
  struct tty_driver  tty_modem;			  /* tty-device             */
  struct tty_driver  cua_modem;			  /* cua-device             */
  struct tty_struct  *modem_table[ISDN_MAX_CHANNELS]; /* ?? copied from Orig */
  struct termios     *modem_termios[ISDN_MAX_CHANNELS];
  struct termios     *modem_termios_locked[ISDN_MAX_CHANNELS];
  atemu              atmodem[ISDN_MAX_CHANNELS];  /* AT-Command-parser      */
  modem_info         info[ISDN_MAX_CHANNELS];	  /* Private data           */
} modem;

/*======================= End of ISDN-tty stuff ============================*/

/*======================= Start of sync-ppp stuff ==========================*/


#define NUM_RCV_BUFFS     64
#define PPP_HARD_HDR_LEN 4

#define IPPP_OPEN        0x1
#define IPPP_CONNECT     0x2
#define IPPP_CLOSEWAIT   0x4
#define IPPP_NOBLOCK     0x8

#ifdef CONFIG_ISDN_PPP

struct sqqueue {
  struct sqqueue *next;
  int sqno_start;
  int sqno_end;
  struct sk_buff *skb;
  long timer;
};
  
struct mpqueue {
  struct mpqueue *next;
  struct mpqueue *last;
  int    sqno;
  struct sk_buff *skb;
  int BEbyte;
  unsigned long time;
}; 

struct ippp_buf_queue {
  struct ippp_buf_queue *next;
  struct ippp_buf_queue *last;
  char *buf;                 /* NULL here indicates end of queue */
  int len;
};

struct ippp_struct {
  struct ippp_struct *next_link;
  int state;
  struct ippp_buf_queue rq[NUM_RCV_BUFFS]; /* packet queue for isdn_ppp_read() */
  struct ippp_buf_queue *first;  /* pointer to (current) first packet */
  struct ippp_buf_queue *last;   /* pointer to (current) last used packet in queue */
  struct wait_queue *wq;
  struct wait_queue *wq1;
  struct task_struct *tk;
  unsigned int mpppcfg;
  unsigned int pppcfg;
  unsigned int mru;
  unsigned int mpmru;
  unsigned int mpmtu;
  unsigned int maxcid;
  isdn_net_local *lp;
  int unit; 
  long last_link_seqno;
  long mp_seqno;
  long range;
#ifdef CONFIG_ISDN_PPP_VJ
  unsigned char *cbuf;
  struct slcompress *slcomp;
#endif
};

#endif

/*======================== End of sync-ppp stuff ===========================*/

/*======================= Start of general stuff ===========================*/

/* Packet-queue-element */
typedef struct pqueue {
  char   *next;				/* Pointer to next packet           */
  short   length;			/* Packetlength                     */
  short   size;                         /* Allocated size                   */
  u_char *rptr;				/* Read-pointer for stream-reading  */
  u_char  buffer[1];			/* The data (will be alloc'd)       */
} pqueue;

typedef struct {
  char *next;
  char *private;
} infostruct;

/* Description of hardware-level-driver */
typedef struct {
  ulong               flags;            /* Flags                            */
  int                 channels;         /* Number of channels               */
  int                 reject_bus;       /* Flag: Reject rejected call on bus*/
  struct wait_queue  *st_waitq;         /* Wait-Queue for status-read's     */
  int                 maxbufsize;       /* Maximum Buffersize supported     */
  unsigned long       pktcount;         /* Until now: unused                */
  int                 running;          /* Flag: Protocolcode running       */
  int                 loaded;           /* Flag: Driver loaded              */
  int                 stavail;          /* Chars avail on Status-device     */
  isdn_if            *interface;        /* Interface to driver              */
  int                *rcverr;           /* Error-counters for B-Ch.-receive */
  int                *rcvcount;         /* Byte-counters for B-Ch.-receive  */
  pqueue             **rpqueue;         /* Pointers to start of Rcv-Queue   */
  struct wait_queue  **rcv_waitq;       /* Wait-Queues for B-Channel-Reads  */
  struct wait_queue  **snd_waitq;       /* Wait-Queue for B-Channel-Send's  */
  char               msn2eaz[10][ISDN_MSNLEN];  /* Mapping-Table MSN->EAZ   */
} driver;

/* Main driver-data */
typedef struct isdn_devt {
  unsigned short    flags;		       /* Bitmapped Flags:           */
				               /*                            */
  int               drivers;		       /* Current number of drivers  */
  int               channels;		       /* Current number of channels */
  int               net_verbose;               /* Verbose-Flag               */
  int               modempoll;		       /* Flag: tty-read active      */
  int               tflags;                    /* Timer-Flags:               */
				               /*  see ISDN_TIMER_..defines  */
  int               global_flags;
  infostruct        *infochain;                /* List of open info-devs.    */
  struct wait_queue *info_waitq;               /* Wait-Queue for isdninfo    */
  struct timer_list timer;		       /* Misc.-function Timer       */
  int               chanmap[ISDN_MAX_CHANNELS];/* Map minor->device-channel  */
  int               drvmap[ISDN_MAX_CHANNELS]; /* Map minor->driver-index    */
  int               usage[ISDN_MAX_CHANNELS];  /* Used by tty/ip/voice       */
  char              num[ISDN_MAX_CHANNELS][20];/* Remote number of active ch.*/
  int               m_idx[ISDN_MAX_CHANNELS];  /* Index for mdm....          */
  driver            *drv[ISDN_MAX_DRIVERS];    /* Array of drivers           */
  isdn_net_dev      *netdev;		       /* Linked list of net-if's    */
  char              drvid[ISDN_MAX_DRIVERS][20];/* Driver-ID                 */
  struct task_struct *profd;                   /* For iprofd                 */
  modem             mdm;		       /* tty-driver-data            */
  isdn_net_dev      *rx_netdev[ISDN_MAX_CHANNELS]; /* rx netdev-pointers     */
  isdn_net_dev      *st_netdev[ISDN_MAX_CHANNELS]; /* stat netdev-pointers   */
} isdn_dev;

extern isdn_dev *dev;

/* Utility-Macros */
#define MIN(a,b) ((a<b)?a:b)
#define MAX(a,b) ((a>b)?a:b)

#endif /* __KERNEL__ */
#endif /* isdn_h */
