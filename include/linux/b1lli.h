/*
 * $Id: b1lli.h,v 1.1 1997/03/04 21:27:32 calle Exp $
 *
 * ISDN lowlevel-module for AVM B1-card.
 *
 * Copyright 1996 by Carsten Paeth (calle@calle.in-berlin.de)
 *
 * $Log: b1lli.h,v $
 * Revision 1.1  1997/03/04 21:27:32  calle
 * First version in isdn4linux
 *
 * Revision 2.2  1997/02/12 09:31:39  calle
 * new version
 *
 * Revision 1.1  1997/01/31 10:32:20  calle
 * Initial revision
 *
 */

#ifndef _B1LLI_H_
#define _B1LLI_H_
/*
 * struct for loading t4 file 
 */
typedef struct avmb1_t4file {
	int len;
	unsigned char *data;
} avmb1_t4file;

typedef struct avmb1_loaddef {
	int contr;
	avmb1_t4file t4file;
} avmb1_loaddef;

typedef struct avmb1_resetdef {
	int contr;
} avmb1_resetdef;

/*
 * struct for adding new cards 
 */
typedef struct avmb1_carddef {
	int port;
	int irq;
} avmb1_carddef;

#define	AVMB1_LOAD	0	/* load image to card */
#define AVMB1_ADDCARD	1	/* add a new card */
#define AVMB1_RESETCARD	2	/* reset a card */



#ifdef __KERNEL__

/*
 * card states for startup
 */

#define CARD_NONE	0
#define CARD_DETECTED	1
#define CARD_LOADING	2
#define CARD_INITSTATE	4
#define CARD_RUNNING	5
#define CARD_ACTIVE	6

#define	AVMB1_PORTLEN	0x1f

#define AVM_MAXVERSION	8
#define AVM_NBCHAN	2

#define AVM_NAPPS	30
#define AVM_NPLCI	5
#define AVM_NNCCI	6

/*
 * Main driver data
 */

typedef struct avmb1_card {
	struct avmb1_card *next;
	int cnr;
	unsigned short port;
	unsigned irq;
	volatile unsigned short cardstate;
	int interrupt;
	int blocked;
	int versionlen;
	char versionbuf[1024];
	char *version[AVM_MAXVERSION];
	char msgbuf[128];	/* capimsg msg part */
	char databuf[2048];	/* capimsg data part */
	capi_version cversion;
	char name[10];
} avmb1_card;

/*
 * Versions
 */

#define	VER_DRIVER	0
#define	VER_CARDTYPE	1
#define	VER_HWID	2
#define	VER_SERIAL	3
#define	VER_OPTION	4
#define	VER_PROTO	5
#define	VER_PROFILE	6
#define	VER_CAPI	7


/* b1lli.c */
int B1_detect(unsigned short base);
void B1_reset(unsigned short base);
int B1_load_t4file(unsigned short base, avmb1_t4file * t4file);
int B1_loaded(unsigned short base);
unsigned char B1_assign_irq(unsigned short base, unsigned irq);
unsigned char B1_enable_irq(unsigned short base);
unsigned char B1_disable_irq(unsigned short base);
int B1_valid_irq(unsigned irq);
void B1_handle_interrupt(avmb1_card * card);
void B1_send_init(unsigned short port,
	    unsigned int napps, unsigned int nncci, unsigned int cardnr);
void B1_send_register(unsigned short port,
		      __u16 appid, __u32 nmsg,
		      __u32 nb3conn, __u32 nb3blocks, __u32 b3bsize);
void B1_send_release(unsigned short port, __u16 appid);
void B1_send_message(unsigned short port, struct sk_buff *skb);

/* b1capi.c */
void avmb1_handle_new_ncci(avmb1_card * card,
			   __u16 appl, __u32 ncci, __u32 winsize);
void avmb1_handle_free_ncci(avmb1_card * card,
			    __u16 appl, __u32 ncci);
void avmb1_handle_capimsg(avmb1_card * card, __u16 appl, struct sk_buff *skb);
void avmb1_card_ready(avmb1_card * card);

int avmb1_addcard(int port, int irq);
int avmb1_probecard(int port, int irq);

#endif				/* __KERNEL__ */

#endif				/* _B1LLI_H_ */
