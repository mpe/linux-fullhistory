/* $Id: eicon_idi.h,v 1.7 1999/08/22 20:26:46 calle Exp $
 *
 * ISDN lowlevel-module for the Eicon.Diehl active cards.
 * IDI-Interface
 *
 * Copyright 1998,99 by Armin Schindler (mac@melware.de)
 * Copyright 1999    Cytronics & Melware (info@melware.de)
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
 * $Log: eicon_idi.h,v $
 * Revision 1.7  1999/08/22 20:26:46  calle
 * backported changes from kernel 2.3.14:
 * - several #include "config.h" gone, others come.
 * - "struct device" changed to "struct net_device" in 2.3.14, added a
 *   define in isdn_compat.h for older kernel versions.
 *
 * Revision 1.6  1999/07/25 15:12:04  armin
 * fix of some debug logs.
 * enabled ISA-cards option.
 *
 * Revision 1.5  1999/07/11 17:16:26  armin
 * Bugfixes in queue handling.
 * Added DSP-DTMF decoder functions.
 * Reorganized ack_handler.
 *
 * Revision 1.4  1999/03/29 11:19:44  armin
 * I/O stuff now in seperate file (eicon_io.c)
 * Old ISA type cards (S,SX,SCOM,Quadro,S2M) implemented.
 *
 * Revision 1.3  1999/03/02 12:37:45  armin
 * Added some important checks.
 * Analog Modem with DSP.
 * Channels will be added to Link-Level after loading firmware.
 *
 * Revision 1.2  1999/01/24 20:14:18  armin
 * Changed and added debug stuff.
 * Better data sending. (still problems with tty's flip buffer)
 *
 * Revision 1.1  1999/01/01 18:09:42  armin
 * First checkin of new eicon driver.
 * DIVA-Server BRI/PCI and PRI/PCI are supported.
 * Old diehl code is obsolete.
 *
 *
 */

#ifndef IDI_H
#define IDI_H

#include <linux/config.h>

#define ASSIGN  0x01
#define REMOVE  0xff
	
#define CALL_REQ 1      /* call request                             */
#define CALL_CON 1      /* call confirmation                        */
#define CALL_IND 2      /* incoming call connected                  */
#define LISTEN_REQ 2    /* listen request                           */
#define HANGUP 3        /* hangup request/indication                */
#define SUSPEND 4       /* call suspend request/confirm             */
#define RESUME 5        /* call resume request/confirm              */
#define SUSPEND_REJ 6   /* suspend rejected indication              */
#define USER_DATA 8     /* user data for user to user signaling     */
#define CONGESTION 9    /* network congestion indication            */
#define INDICATE_REQ 10 /* request to indicate an incoming call     */
#define INDICATE_IND 10 /* indicates that there is an incoming call */
#define CALL_RES 11     /* accept an incoming call                  */
#define CALL_ALERT 12   /* send ALERT for incoming call             */
#define INFO_REQ 13     /* INFO request                             */
#define INFO_IND 13     /* INFO indication                          */
#define REJECT 14       /* reject an incoming call                  */
#define RESOURCES 15    /* reserve B-Channel hardware resources     */
#define TEL_CTRL 16     /* Telephone control request/indication     */
#define STATUS_REQ 17   /* Request D-State (returned in INFO_IND)   */
#define FAC_REG_REQ 18  /* connection idependent fac registration   */
#define FAC_REG_ACK 19  /* fac registration acknowledge             */
#define FAC_REG_REJ 20  /* fac registration reject                  */
#define CALL_COMPLETE 21/* send a CALL_PROC for incoming call       */
#define AOC_IND       26/* Advice of Charge                         */

#define IDI_N_MDATA         (0x01)
#define IDI_N_CONNECT       (0x02)
#define IDI_N_CONNECT_ACK   (0x03)
#define IDI_N_DISC          (0x04)
#define IDI_N_DISC_ACK      (0x05)
#define IDI_N_RESET         (0x06)
#define IDI_N_RESET_ACK     (0x07)
#define IDI_N_DATA          (0x08)
#define IDI_N_EDATA         (0x09)
#define IDI_N_UDATA         (0x0a)
#define IDI_N_BDATA         (0x0b)
#define IDI_N_DATA_ACK      (0x0c)
#define IDI_N_EDATA_ACK     (0x0d)

#define N_Q_BIT         0x10    /* Q-bit for req/ind                */
#define N_M_BIT         0x20    /* M-bit for req/ind                */
#define N_D_BIT         0x40    /* D-bit for req/ind                */


#define SHIFT 0x90              /* codeset shift                    */
#define MORE 0xa0               /* more data                        */
#define CL 0xb0                 /* congestion level                 */

        /* codeset 0                                                */

#define BC  0x04                /* Bearer Capability                */
#define CAU 0x08                /* cause                            */
#define CAD 0x0c                /* Connected address                */
#define CAI 0x10                /* call identity                    */
#define CHI 0x18                /* channel identification           */
#define LLI 0x19                /* logical link id                  */
#define CHA 0x1a                /* charge advice                    */
#define FTY 0x1c
#define PI  0x1e		/* Progress Indicator		    */
#define NI  0x27		/* Notification Indicator	    */
#define DT  0x29                /* ETSI date/time                   */
#define KEY 0x2c                /* keypad information element       */
#define DSP 0x28                /* display                          */
#define OAD 0x6c                /* origination address              */
#define OSA 0x6d                /* origination sub-address          */
#define CPN 0x70                /* called party number              */
#define DSA 0x71                /* destination sub-address          */
#define RDN 0x74		/* redirecting number		    */
#define LLC 0x7c                /* low layer compatibility          */
#define HLC 0x7d                /* high layer compatibility         */
#define UUI 0x7e                /* user user information            */
#define ESC 0x7f                /* escape extension                 */

#define DLC 0x20                /* data link layer configuration    */
#define NLC 0x21                /* network layer configuration      */

        /* codeset 6                                                */

#define SIN 0x01                /* service indicator                */
#define CIF 0x02                /* charging information             */
#define DATE 0x03               /* date                             */
#define CPS 0x07                /* called party status              */

/*------------------------------------------------------------------*/
/* return code coding                                               */
/*------------------------------------------------------------------*/

#define UNKNOWN_COMMAND         0x01    /* unknown command          */
#define WRONG_COMMAND           0x02    /* wrong command            */
#define WRONG_ID                0x03    /* unknown task/entity id   */
#define WRONG_CH                0x04    /* wrong task/entity id     */
#define UNKNOWN_IE              0x05    /* unknown information el.  */
#define WRONG_IE                0x06    /* wrong information el.    */
#define OUT_OF_RESOURCES        0x07    /* card out of res.         */
#define N_FLOW_CONTROL          0x10    /* Flow-Control, retry      */
#define ASSIGN_RC               0xe0    /* ASSIGN acknowledgement   */
#define ASSIGN_OK               0xef    /* ASSIGN OK                */
#define OK_FC                   0xfc    /* Flow-Control RC          */
#define READY_INT               0xfd    /* Ready interrupt          */
#define TIMER_INT               0xfe    /* timer interrupt          */
#define OK                      0xff    /* command accepted         */

/*------------------------------------------------------------------*/

typedef struct {
	char cpn[32];
	char oad[32];
	char dsa[32];
	char osa[32];
	__u8 plan;
	__u8 screen;
	__u8 sin[4];
	__u8 chi[4];
	__u8 e_chi[4];
	__u8 bc[12];
	__u8 e_bc[12];
 	__u8 llc[18];
	__u8 hlc[5];
	__u8 cau[4];
	__u8 e_cau[2];
	__u8 e_mt;
	__u8 dt[6];
	char display[83];
	char keypad[35];
	char rdn[32];
} idi_ind_message;

typedef struct { 
  __u16 next            __attribute__ ((packed));
  __u8  Req             __attribute__ ((packed));
  __u8  ReqId           __attribute__ ((packed));
  __u8  ReqCh           __attribute__ ((packed));
  __u8  Reserved1       __attribute__ ((packed));
  __u16 Reference       __attribute__ ((packed));
  __u8  Reserved[8]     __attribute__ ((packed));
  eicon_PBUFFER XBuffer; 
} eicon_REQ;

typedef struct {
  __u16 next            __attribute__ ((packed));
  __u8  Rc              __attribute__ ((packed));
  __u8  RcId            __attribute__ ((packed));
  __u8  RcCh            __attribute__ ((packed));
  __u8  Reserved1       __attribute__ ((packed));
  __u16 Reference       __attribute__ ((packed));
  __u8  Reserved2[8]    __attribute__ ((packed));
} eicon_RC;

typedef struct {
  __u16 next            __attribute__ ((packed));
  __u8  Ind             __attribute__ ((packed));
  __u8  IndId           __attribute__ ((packed));
  __u8  IndCh           __attribute__ ((packed));
  __u8  MInd            __attribute__ ((packed));
  __u16 MLength         __attribute__ ((packed));
  __u16 Reference       __attribute__ ((packed));
  __u8  RNR             __attribute__ ((packed));
  __u8  Reserved        __attribute__ ((packed));
  __u32 Ack             __attribute__ ((packed));
  eicon_PBUFFER RBuffer;
} eicon_IND;

typedef struct {
  __u16 NextReq  __attribute__ ((packed));  /* pointer to next Req Buffer */
  __u16 NextRc   __attribute__ ((packed));  /* pointer to next Rc Buffer  */
  __u16 NextInd  __attribute__ ((packed));  /* pointer to next Ind Buffer */
  __u8 ReqInput  __attribute__ ((packed));  /* number of Req Buffers sent */
  __u8 ReqOutput  __attribute__ ((packed)); /* number of Req Buffers returned */
  __u8 ReqReserved  __attribute__ ((packed));/*number of Req Buffers reserved */
  __u8 Int  __attribute__ ((packed));       /* ISDN-P interrupt           */
  __u8 XLock  __attribute__ ((packed));     /* Lock field for arbitration */
  __u8 RcOutput  __attribute__ ((packed));  /* number of Rc buffers received */
  __u8 IndOutput  __attribute__ ((packed)); /* number of Ind buffers received */
  __u8 IMask  __attribute__ ((packed));     /* Interrupt Mask Flag        */
  __u8 Reserved1[2]  __attribute__ ((packed)); /* reserved field, do not use */
  __u8 ReadyInt  __attribute__ ((packed));  /* request field for ready int */
  __u8 Reserved2[12]  __attribute__ ((packed)); /* reserved field, do not use */
  __u8 InterfaceType  __attribute__ ((packed)); /* interface type 1=16K    */
  __u16 Signature  __attribute__ ((packed));    /* ISDN-P initialized ind  */
  __u8 B[1];                            /* buffer space for Req,Ind and Rc */
} eicon_pr_ram;

typedef struct {
	__u8		*Data;
	unsigned int	Size;
	unsigned int	Len;
	__u8		*Next;
} eicon_OBJBUFFER;

extern int idi_do_req(eicon_card *card, eicon_chan *chan, int cmd, int layer);
extern int idi_hangup(eicon_card *card, eicon_chan *chan);
extern int idi_connect_res(eicon_card *card, eicon_chan *chan);
extern int eicon_idi_listen_req(eicon_card *card, eicon_chan *chan);
extern int idi_connect_req(eicon_card *card, eicon_chan *chan, char *phone,
	                    char *eazmsn, int si1, int si2);

extern void idi_handle_ack(eicon_card *card, struct sk_buff *skb);
extern void idi_handle_ind(eicon_card *card, struct sk_buff *skb);
extern int eicon_idi_manage(eicon_card *card, eicon_manifbuf *mb);
extern int idi_send_data(eicon_card *card, eicon_chan *chan, int ack, struct sk_buff *skb, int que);
extern void idi_audio_cmd(eicon_card *ccard, eicon_chan *chan, int cmd, u_char *value);
#ifdef CONFIG_ISDN_TTY_FAX
extern void idi_fax_cmd(eicon_card *card, eicon_chan *chan);
extern int idi_faxdata_send(eicon_card *ccard, eicon_chan *chan, struct sk_buff *skb);
#endif

#endif
