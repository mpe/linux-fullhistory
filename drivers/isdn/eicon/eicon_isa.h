/* $Id: eicon_isa.h,v 1.8 2000/01/23 21:21:23 armin Exp $
 *
 * ISDN low-level module for Eicon active ISDN-Cards.
 *
 * Copyright 1998      by Fritz Elfert (fritz@isdn4linux.de)
 * Copyright 1998-2000 by Armin Schindler (mac@melware.de)
 * Copyright 1999,2000 Cytronics & Melware (info@melware.de)
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
 * $Log: eicon_isa.h,v $
 * Revision 1.8  2000/01/23 21:21:23  armin
 * Added new trace capability and some updates.
 * DIVA Server BRI now supports data for ISDNLOG.
 *
 * Revision 1.7  1999/11/18 21:14:30  armin
 * New ISA memory mapped IO
 *
 * Revision 1.6  1999/11/15 19:37:04  keil
 * need config.h
 *
 * Revision 1.5  1999/09/08 20:17:31  armin
 * Added microchannel patch from Erik Weber.
 *
 * Revision 1.4  1999/09/06 07:29:35  fritz
 * Changed my mail-address.
 *
 * Revision 1.3  1999/03/29 11:19:47  armin
 * I/O stuff now in seperate file (eicon_io.c)
 * Old ISA type cards (S,SX,SCOM,Quadro,S2M) implemented.
 *
 * Revision 1.2  1999/03/02 12:37:46  armin
 * Added some important checks.
 * Analog Modem with DSP.
 * Channels will be added to Link-Level after loading firmware.
 *
 * Revision 1.1  1999/01/01 18:09:44  armin
 * First checkin of new eicon driver.
 * DIVA-Server BRI/PCI and PRI/PCI are supported.
 * Old diehl code is obsolete.
 *
 *
 */

#ifndef eicon_isa_h
#define eicon_isa_h

#ifdef __KERNEL__
#include <linux/config.h>

/* Factory defaults for ISA-Cards */
#define EICON_ISA_MEMBASE 0xd0000
#define EICON_ISA_IRQ     3
/* shmem offset for Quadro parts */
#define EICON_ISA_QOFFSET 0x0800

typedef struct {
        __u16 length __attribute__ ((packed));   /* length of data/parameter field         */
        __u8  P[270];                            /* data/parameter field                   */
} eicon_scom_PBUFFER;

/* General communication buffer */
typedef struct {
        __u8   Req;                                /* request register                       */
	__u8   ReqId;                              /* request task/entity identification     */
	__u8   Rc;                                 /* return code register                   */
	__u8   RcId;                               /* return code task/entity identification */
	__u8   Ind;                                /* Indication register                    */
	__u8   IndId;                              /* Indication task/entity identification  */
	__u8   IMask;                              /* Interrupt Mask Flag                    */
	__u8   RNR;                                /* Receiver Not Ready (set by PC)         */
	__u8   XLock;                              /* XBuffer locked Flag                    */
	__u8   Int;                                /* ISDN interrupt                         */
	__u8   ReqCh;                              /* Channel field for layer-3 Requests     */
	__u8   RcCh;                               /* Channel field for layer-3 Returncodes  */
	__u8   IndCh;                              /* Channel field for layer-3 Indications  */
	__u8   MInd;                               /* more data indication field             */
	__u16  MLength;                            /* more data total packet length          */
	__u8   ReadyInt;                           /* request field for ready interrupt      */
	__u8   Reserved[12];                       /* reserved space                         */
	__u8   IfType;                             /* 1 = 16k-Interface                      */
	__u16  Signature __attribute__ ((packed)); /* ISDN adapter Signature                 */
	eicon_scom_PBUFFER XBuffer;                /* Transmit Buffer                        */
	eicon_scom_PBUFFER RBuffer;                /* Receive Buffer                         */
} eicon_isa_com;

/* struct for downloading firmware */
typedef struct {
	__u8  ctrl;
	__u8  card;
	__u8  msize;
	__u8  fill0;
	__u16 ebit __attribute__ ((packed));
	__u32 eloc __attribute__ ((packed));
	__u8  reserved[20];
	__u16 signature __attribute__ ((packed));
	__u8  fill[224];
	__u8  b[256];
} eicon_isa_boot;

/* Shared memory */
typedef union {
	unsigned char  c[0x400];
	eicon_isa_com  com;
	eicon_isa_boot boot;
} eicon_isa_shmem;

/*
 * card's description
 */
typedef struct {
	int               ramsize;
	int               irq;	    /* IRQ                        */
	unsigned long	  physmem;  /* physical memory address	  */
#ifdef CONFIG_MCA
	int		  io;	    /* IO-port for MCA brand      */
#endif /* CONFIG_MCA */
	void*             card;
	eicon_isa_shmem*  shmem;    /* Shared-memory area         */
	unsigned char*    intack;   /* Int-Acknowledge            */
	unsigned char*    stopcpu;  /* Writing here stops CPU     */
	unsigned char*    startcpu; /* Writing here starts CPU    */
	unsigned char     type;     /* card type                  */
	int		  channels; /* No. of channels		  */
	unsigned char     irqprobe; /* Flag: IRQ-probing          */
	unsigned char     mvalid;   /* Flag: Memory is valid      */
	unsigned char     ivalid;   /* Flag: IRQ is valid         */
	unsigned char     master;   /* Flag: Card ist Quadro 1/4  */
	void*             generic;  /* Ptr to generic card struct */
} eicon_isa_card;

/* Offsets for special locations on standard cards */
#define INTACK     0x03fe 
#define STOPCPU    0x0400
#define STARTCPU   0x0401
#define RAMSIZE    0x0400
/* Offsets for special location on PRI card */
#define INTACK_P   0x3ffc
#define STOPCPU_P  0x3ffe
#define STARTCPU_P 0x3fff
#define RAMSIZE_P  0x4000


extern int eicon_isa_load(eicon_isa_card *card, eicon_isa_codebuf *cb);
extern int eicon_isa_bootload(eicon_isa_card *card, eicon_isa_codebuf *cb);
extern void eicon_isa_release(eicon_isa_card *card);
extern void eicon_isa_printpar(eicon_isa_card *card);
extern void eicon_isa_transmit(eicon_isa_card *card);
extern int eicon_isa_find_card(int Mem, int Irq, char * Id);

#endif  /* __KERNEL__ */

#endif	/* eicon_isa_h */
