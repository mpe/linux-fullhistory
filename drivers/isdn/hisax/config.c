/* $Id: config.c,v 2.23 1999/02/17 10:53:02 cpetig Exp $

 * Author       Karsten Keil (keil@isdn4linux.de)
 *              based on the teles driver from Jan den Ouden
 *
 *
 * $Log: config.c,v $
 * Revision 2.23  1999/02/17 10:53:02  cpetig
 * Added Hisax_closecard to exported symbols.
 * As indicated by Oliver Schoett <os@sdm.de>.
 *
 * If anyone is annoyed by exporting symbols deep inside the code, please
 * contact me.
 *
 * Revision 2.22  1999/02/04 21:41:53  keil
 * Fix printk msg
 *
 * Revision 2.21  1999/02/04 10:48:52  keil
 * Fix readstat bug
 *
 * Revision 2.20  1998/11/15 23:54:28  keil
 * changes from 2.0
 *
 * Revision 2.19  1998/08/13 23:36:18  keil
 * HiSax 3.1 - don't work stable with current LinkLevel
 *
 * Revision 2.18  1998/07/30 21:01:37  niemann
 * Fixed Sedlbauer Speed Fax PCMCIA missing isdnl3new
 *
 * Revision 2.17  1998/07/15 15:01:26  calle
 * Support for AVM passive PCMCIA cards:
 *    A1 PCMCIA, FRITZ!Card PCMCIA and FRITZ!Card PCMCIA 2.0
 *
 * Revision 2.16  1998/05/25 14:10:03  keil
 * HiSax 3.0
 * X.75 and leased are working again.
 *
 * Revision 2.15  1998/05/25 12:57:43  keil
 * HiSax golden code from certification, Don't use !!!
 * No leased lines, no X75, but many changes.
 *
 * Revision 2.14  1998/04/15 16:38:25  keil
 * Add S0Box and Teles PCI support
 *
 * Revision 2.13  1998/03/09 23:19:23  keil
 * Changes for PCMCIA
 *
 * Revision 2.12  1998/02/11 17:28:02  keil
 * Niccy PnP/PCI support
 *
 * Revision 2.11  1998/02/09 21:26:13  keil
 * fix export module for 2.1
 *
 * Revision 2.10  1998/02/09 18:46:05  keil
 * Support for Sedlbauer PCMCIA (Marcus Niemann)
 *
 * Revision 2.9  1998/02/03 23:31:28  keil
 * add AMD7930 support
 *
 * Revision 2.8  1998/02/02 13:32:59  keil
 * New card support
 *
 * Revision 2.7  1998/01/31 21:41:44  keil
 * changes for newer 2.1 kernels
 *
 * Revision 2.6  1997/11/08 21:35:43  keil
 * new l1 init
 *
 * Revision 2.5  1997/11/06 17:15:08  keil
 * New 2.1 init; PCMCIA wrapper changes
 *
 * Revision 2.4  1997/10/29 19:07:52  keil
 * changes for 2.1
 *
 * Revision 2.3  1997/10/01 09:21:33  fritz
 * Removed old compatibility stuff for 2.0.X kernels.
 * From now on, this code is for 2.1.X ONLY!
 * Old stuff is still in the separate branch.
 *
 * Revision 2.2  1997/09/11 17:24:46  keil
 * Add new cards
 *
 * Revision 2.1  1997/07/27 21:41:35  keil
 * version change
 *
 * Revision 2.0  1997/06/26 11:06:28  keil
 * New card and L1 interface.
 * Eicon.Diehl Diva and Dynalink IS64PH support
 *
 * old changes removed /KKe
 *
 */
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/timer.h>
#include <linux/config.h>
#include "hisax.h"
#include <linux/module.h>
#include <linux/kernel_stat.h>
#include <linux/tqueue.h>
#include <linux/interrupt.h>
#define HISAX_STATUS_BUFSIZE 4096
#define INCLUDE_INLINE_FUNCS

/*
 * This structure array contains one entry per card. An entry looks
 * like this:
 *
 * { type, protocol, p0, p1, p2, NULL }
 *
 * type
 *    1 Teles 16.0       p0=irq p1=membase p2=iobase
 *    2 Teles  8.0       p0=irq p1=membase
 *    3 Teles 16.3       p0=irq p1=iobase
 *    4 Creatix PNP      p0=irq p1=IO0 (ISAC)  p2=IO1 (HSCX)
 *    5 AVM A1 (Fritz)   p0=irq p1=iobase
 *    6 ELSA PC          [p0=iobase] or nothing (autodetect)
 *    7 ELSA Quickstep   p0=irq p1=iobase
 *    8 Teles PCMCIA     p0=irq p1=iobase
 *    9 ITK ix1-micro    p0=irq p1=iobase
 *   10 ELSA PCMCIA      p0=irq p1=iobase
 *   11 Eicon.Diehl Diva p0=irq p1=iobase
 *   12 Asuscom ISDNLink p0=irq p1=iobase
 *   13 Teleint          p0=irq p1=iobase
 *   14 Teles 16.3c      p0=irq p1=iobase
 *   15 Sedlbauer speed  p0=irq p1=iobase
 *   15 Sedlbauer PC/104	p0=irq p1=iobase
 *   15 Sedlbauer speed pci	no parameter
 *   16 USR Sportster internal  p0=irq  p1=iobase
 *   17 MIC card                p0=irq  p1=iobase
 *   18 ELSA Quickstep 1000PCI  no parameter
 *   19 Compaq ISDN S0 ISA card p0=irq  p1=IO0 (HSCX)  p2=IO1 (ISAC) p3=IO2
 *   20 Travers Technologies NETjet PCI card
 *   21 TELES PCI               no parameter
 *   22 Sedlbauer Speed Star    p0=irq p1=iobase
 *   23 reserved
 *   24 Dr Neuhaus Niccy PnP/PCI card p0=irq p1=IO0 p2=IO1 (PnP only)
 *   25 Teles S0Box             p0=irq p1=iobase (from isapnp setup)
 *   26 AVM A1 PCMCIA (Fritz)   p0=irq p1=iobase
 *   27 AVM PnP/PCI 		p0=irq p1=iobase (PCI no parameter)
 *   28 Sedlbauer Speed Fax+ 	p0=irq p1=iobase (from isapnp setup)
 *
 * protocol can be either ISDN_PTYPE_EURO or ISDN_PTYPE_1TR6 or ISDN_PTYPE_NI1
 *
 *
 */

const char *CardType[] =
{"No Card", "Teles 16.0", "Teles 8.0", "Teles 16.3", "Creatix/Teles PnP",
 "AVM A1", "Elsa ML", "Elsa Quickstep", "Teles PCMCIA", "ITK ix1-micro Rev.2",
 "Elsa PCMCIA", "Eicon.Diehl Diva", "ISDNLink", "TeleInt", "Teles 16.3c",
 "Sedlbauer Speed Card", "USR Sportster", "ith mic Linux", "Elsa PCI",
 "Compaq ISA", "NETjet", "Teles PCI", "Sedlbauer Speed Star (PCMCIA)",
 "AMD 7930", "NICCY", "S0Box", "AVM A1 (PCMCIA)", "AVM Fritz PnP/PCI",
 "Sedlbauer Speed Fax +"
};

#ifdef CONFIG_HISAX_ELSA
#define DEFAULT_CARD ISDN_CTYPE_ELSA
#define DEFAULT_CFG {0,0,0,0}
int elsa_init_pcmcia(void*, int, int*, int);
EXPORT_SYMBOL(elsa_init_pcmcia);
#endif
#ifdef CONFIG_HISAX_AVM_A1
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_A1
#define DEFAULT_CFG {10,0x340,0,0}
#endif

#ifdef CONFIG_HISAX_AVM_A1_PCMCIA
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_A1_PCMCIA
#define DEFAULT_CFG {11,0x170,0,0}
int avm_a1_init_pcmcia(void*, int, int*, int);
EXPORT_SYMBOL(avm_a1_init_pcmcia);
#endif

#ifdef CONFIG_HISAX_FRITZPCI
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_FRITZPCI
#define DEFAULT_CFG {0,0,0,0}
#endif

#ifdef CONFIG_HISAX_16_3
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_16_3
#define DEFAULT_CFG {15,0x180,0,0}
#endif
#ifdef CONFIG_HISAX_S0BOX
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_S0BOX
#define DEFAULT_CFG {7,0x378,0,0}
#endif
#ifdef CONFIG_HISAX_16_0
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_16_0
#define DEFAULT_CFG {15,0xd0000,0xd80,0}
#endif

#ifdef CONFIG_HISAX_TELESPCI
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_TELESPCI
#define DEFAULT_CFG {0,0,0,0}
#endif

#ifdef CONFIG_HISAX_IX1MICROR2
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_IX1MICROR2
#define DEFAULT_CFG {5,0x390,0,0}
#endif

#ifdef CONFIG_HISAX_DIEHLDIVA
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_DIEHLDIVA
#define DEFAULT_CFG {0,0x0,0,0}
#endif

#ifdef CONFIG_HISAX_ASUSCOM
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_ASUSCOM
#define DEFAULT_CFG {5,0x200,0,0}
#endif

#ifdef CONFIG_HISAX_TELEINT
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_TELEINT
#define DEFAULT_CFG {5,0x300,0,0}
#endif

#ifdef CONFIG_HISAX_SEDLBAUER
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_SEDLBAUER
#define DEFAULT_CFG {11,0x270,0,0}
int sedl_init_pcmcia(void*, int, int*, int);
EXPORT_SYMBOL(sedl_init_pcmcia);
#endif

#ifdef CONFIG_HISAX_SPORTSTER
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_SPORTSTER
#define DEFAULT_CFG {7,0x268,0,0}
#endif

#ifdef CONFIG_HISAX_MIC
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_MIC
#define DEFAULT_CFG {12,0x3e0,0,0}
#endif

#ifdef CONFIG_HISAX_NETJET
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_NETJET
#define DEFAULT_CFG {0,0,0,0}
#endif

#ifdef CONFIG_HISAX_TELES3C
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_TELES3C
#define DEFAULT_CFG {5,0x500,0,0}
#endif

#ifdef CONFIG_HISAX_AMD7930
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_AMD7930
#define DEFAULT_CFG {12,0x3e0,0,0}
#endif

#ifdef CONFIG_HISAX_NICCY
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_NICCY
#define DEFAULT_CFG {0,0x0,0,0}
#endif

#ifdef CONFIG_HISAX_1TR6
#define DEFAULT_PROTO ISDN_PTYPE_1TR6
#define DEFAULT_PROTO_NAME "1TR6"
#endif
#ifdef CONFIG_HISAX_EURO
#undef DEFAULT_PROTO
#define DEFAULT_PROTO ISDN_PTYPE_EURO
#undef DEFAULT_PROTO_NAME
#define DEFAULT_PROTO_NAME "EURO"
#endif
#ifdef CONFIG_HISAX_NI1
#undef DEFAULT_PROTO
#define DEFAULT_PROTO ISDN_PTYPE_NI1
#undef DEFAULT_PROTO_NAME
#define DEFAULT_PROTO_NAME "NI1"
#endif
#ifndef DEFAULT_PROTO
#define DEFAULT_PROTO ISDN_PTYPE_UNKNOWN
#define DEFAULT_PROTO_NAME "UNKNOWN"
#endif
#ifndef DEFAULT_CARD
#error "HiSax: No cards configured"
#endif

#define FIRST_CARD { \
	DEFAULT_CARD, \
	DEFAULT_PROTO, \
	DEFAULT_CFG, \
	NULL, \
}

#define EMPTY_CARD	{0, DEFAULT_PROTO, {0, 0, 0, 0}, NULL}

struct IsdnCard cards[] =
{
	FIRST_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
};

static char HiSaxID[64] HISAX_INITDATA = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" \
"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" \
"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
char *HiSax_id HISAX_INITDATA = HiSaxID;
#ifdef MODULE
/* Variables for insmod */
static int type[] HISAX_INITDATA =
{0, 0, 0, 0, 0, 0, 0, 0};
static int protocol[] HISAX_INITDATA =
{0, 0, 0, 0, 0, 0, 0, 0};
static int io[] HISAX_INITDATA =
{0, 0, 0, 0, 0, 0, 0, 0};
#undef IO0_IO1
#ifdef CONFIG_HISAX_16_3
#define IO0_IO1
#endif
#ifdef CONFIG_HISAX_NICCY
#undef IO0_IO1
#define IO0_IO1
#endif
#ifdef IO0_IO1
static int io0[] HISAX_INITDATA =
{0, 0, 0, 0, 0, 0, 0, 0};
static int io1[] HISAX_INITDATA =
{0, 0, 0, 0, 0, 0, 0, 0};
#endif
static int irq[] HISAX_INITDATA =
{0, 0, 0, 0, 0, 0, 0, 0};
static int mem[] HISAX_INITDATA =
{0, 0, 0, 0, 0, 0, 0, 0};
static char *id HISAX_INITDATA = HiSaxID;

MODULE_AUTHOR("Karsten Keil");
MODULE_PARM(type, "1-8i");
MODULE_PARM(protocol, "1-8i");
MODULE_PARM(io, "1-8i");
MODULE_PARM(irq, "1-8i");
MODULE_PARM(mem, "1-8i");
MODULE_PARM(id, "s");
#ifdef CONFIG_HISAX_16_3	/* For Creatix/Teles PnP */
MODULE_PARM(io0, "1-8i");
MODULE_PARM(io1, "1-8i");
#endif /* CONFIG_HISAX_16_3 */

#endif /* MODULE */

int nrcards;

extern char *l1_revision;
extern char *l2_revision;
extern char *l3_revision;
extern char *lli_revision;
extern char *tei_revision;

HISAX_INITFUNC(char *
HiSax_getrev(const char *revision))
{
	char *rev;
	char *p;

	if ((p = strchr(revision, ':'))) {
		rev = p + 2;
		p = strchr(rev, '$');
		*--p = 0;
	} else
		rev = "???";
	return rev;
}

HISAX_INITFUNC(void
HiSaxVersion(void))
{
	char tmp[64];

	printk(KERN_INFO "HiSax: Linux Driver for passive ISDN cards\n");
#ifdef MODULE
	printk(KERN_INFO "HiSax: Version 3.1a (module)\n");
#else
	printk(KERN_INFO "HiSax: Version 3.1a (kernel)\n");
#endif
	strcpy(tmp, l1_revision);
	printk(KERN_INFO "HiSax: Layer1 Revision %s\n", HiSax_getrev(tmp));
	strcpy(tmp, l2_revision);
	printk(KERN_INFO "HiSax: Layer2 Revision %s\n", HiSax_getrev(tmp));
	strcpy(tmp, tei_revision);
	printk(KERN_INFO "HiSax: TeiMgr Revision %s\n", HiSax_getrev(tmp));
	strcpy(tmp, l3_revision);
	printk(KERN_INFO "HiSax: Layer3 Revision %s\n", HiSax_getrev(tmp));
	strcpy(tmp, lli_revision);
	printk(KERN_INFO "HiSax: LinkLayer Revision %s\n", HiSax_getrev(tmp));
	certification_check(1);
}

void
HiSax_mod_dec_use_count(void)
{
	MOD_DEC_USE_COUNT;
}

void
HiSax_mod_inc_use_count(void)
{
	MOD_INC_USE_COUNT;
}

#ifdef MODULE
#define HiSax_init init_module
#else
void __init 
HiSax_setup(char *str, int *ints)
{
	int i, j, argc;

	argc = ints[0];
	i = 0;
	j = 1;
	while (argc && (i < HISAX_MAX_CARDS)) {
		if (argc) {
			cards[i].typ = ints[j];
			j++;
			argc--;
		}
		if (argc) {
			cards[i].protocol = ints[j];
			j++;
			argc--;
		}
		if (argc) {
			cards[i].para[0] = ints[j];
			j++;
			argc--;
		}
		if (argc) {
			cards[i].para[1] = ints[j];
			j++;
			argc--;
		}
		if (argc) {
			cards[i].para[2] = ints[j];
			j++;
			argc--;
		}
		i++;
	}
	if (strlen(str)) {
		strcpy(HiSaxID, str);
		HiSax_id = HiSaxID;
	} else {
		strcpy(HiSaxID, "HiSax");
		HiSax_id = HiSaxID;
	}
}
#endif

#if CARD_TELES0
extern int setup_teles0(struct IsdnCard *card);
#endif

#if CARD_TELES3
extern int setup_teles3(struct IsdnCard *card);
#endif

#if CARD_S0BOX
extern int setup_s0box(struct IsdnCard *card);
#endif

#if CARD_TELESPCI
extern int setup_telespci(struct IsdnCard *card);
#endif

#if CARD_AVM_A1
extern int setup_avm_a1(struct IsdnCard *card);
#endif

#if CARD_AVM_A1_PCMCIA
extern int setup_avm_a1_pcmcia(struct IsdnCard *card);
#endif

#if CARD_FRITZPCI
extern int setup_avm_pcipnp(struct IsdnCard *card);
#endif

#if CARD_ELSA
extern int setup_elsa(struct IsdnCard *card);
#endif

#if CARD_IX1MICROR2
extern int setup_ix1micro(struct IsdnCard *card);
#endif

#if CARD_DIEHLDIVA
extern	int  setup_diva(struct IsdnCard *card);
#endif

#if CARD_ASUSCOM
extern int setup_asuscom(struct IsdnCard *card);
#endif

#if CARD_TELEINT
extern int setup_TeleInt(struct IsdnCard *card);
#endif

#if CARD_SEDLBAUER
extern int setup_sedlbauer(struct IsdnCard *card);
#endif

#if CARD_SPORTSTER
extern int setup_sportster(struct IsdnCard *card);
#endif

#if CARD_MIC
extern int setup_mic(struct IsdnCard *card);
#endif

#if CARD_NETJET
extern int setup_netjet(struct IsdnCard *card);
#endif

#if CARD_TELES3C
extern int setup_t163c(struct IsdnCard *card);
#endif

#if CARD_AMD7930
extern int setup_amd7930(struct IsdnCard *card);
#endif

#if CARD_NICCY
extern int setup_niccy(struct IsdnCard *card);
#endif

/*
 * Find card with given driverId
 */
static inline struct IsdnCardState
*hisax_findcard(int driverid)
{
	int i;

	for (i = 0; i < nrcards; i++)
		if (cards[i].cs)
			if (cards[i].cs->myid == driverid)
				return (cards[i].cs);
	return (NULL);
}

int
HiSax_readstatus(u_char * buf, int len, int user, int id, int channel)
{
	int count,cnt;
	u_char *p = buf;
	struct IsdnCardState *cs = hisax_findcard(id);

	if (cs) {
		if (len > HISAX_STATUS_BUFSIZE) {
			printk(KERN_WARNING "HiSax: status overflow readstat %d/%d\n",
				len, HISAX_STATUS_BUFSIZE);
		}
		count = cs->status_end - cs->status_read +1;
		if (count >= len)
			count = len;
		if (user)
			copy_to_user(p, cs->status_read, count);
		else
			memcpy(p, cs->status_read, count);
		cs->status_read += count;
		if (cs->status_read > cs->status_end)
			cs->status_read = cs->status_buf;
		p += count;
		count = len - count;
		while (count) {
			if (count > HISAX_STATUS_BUFSIZE)
				cnt = HISAX_STATUS_BUFSIZE;
			else
				cnt = count;
			if (user)
				copy_to_user(p, cs->status_read, cnt);
			else
				memcpy(p, cs->status_read, cnt);
			p += cnt;
			cs->status_read += cnt % HISAX_STATUS_BUFSIZE;
			count -= cnt;
		}
		return len;
	} else {
		printk(KERN_ERR
		 "HiSax: if_readstatus called with invalid driverId!\n");
		return -ENODEV;
	}
}

inline int
jiftime(char *s, long mark)
{
	s += 8;

	*s-- = '\0';
	*s-- = mark % 10 + '0';
	mark /= 10;
	*s-- = mark % 10 + '0';
	mark /= 10;
	*s-- = '.';
	*s-- = mark % 10 + '0';
	mark /= 10;
	*s-- = mark % 6 + '0';
	mark /= 6;
	*s-- = ':';
	*s-- = mark % 10 + '0';
	mark /= 10;
	*s-- = mark % 10 + '0';
	return(8);
}

static u_char tmpbuf[HISAX_STATUS_BUFSIZE];

void
VHiSax_putstatus(struct IsdnCardState *cs, char *head, char *fmt, va_list args)
{
/* if head == NULL the fmt contains the full info */

	long flags;
	int count, i;
	u_char *p;
	isdn_ctrl ic;
	int len;

	save_flags(flags);
	cli();
	p = tmpbuf;
	if (head) {
		p += jiftime(p, jiffies);
		p += sprintf(p, " %s", head);
		p += vsprintf(p, fmt, args);
		*p++ = '\n';
		*p = 0;
		len = p - tmpbuf;
		p = tmpbuf;
	} else {
		p = fmt;
		len = strlen(fmt);
	}
	if (!cs) {
		printk(KERN_WARNING "HiSax: No CardStatus for message %s", p);
		restore_flags(flags);
		return;
	}
	if (len > HISAX_STATUS_BUFSIZE) {
		printk(KERN_WARNING "HiSax: status overflow %d/%d\n",
			len, HISAX_STATUS_BUFSIZE);
		restore_flags(flags);
		return;
	}
	count = len;
	i = cs->status_end - cs->status_write +1;
	if (i >= len)
		i = len;
	len -= i;
	memcpy(cs->status_write, p, i);
	cs->status_write += i;
	if (cs->status_write > cs->status_end)
		cs->status_write = cs->status_buf;
	p += i;
	if (len) {
		memcpy(cs->status_write, p, len);
		cs->status_write += len;
	}
#ifdef KERNELSTACK_DEBUG
	i = (ulong)&len - current->kernel_stack_page;
	sprintf(tmpbuf, "kstack %s %lx use %ld\n", current->comm,
		current->kernel_stack_page, i);
	len = strlen(tmpbuf);
	for (p = tmpbuf, i = len; i > 0; i--, p++) {
		*cs->status_write++ = *p;
		if (cs->status_write > cs->status_end)
			cs->status_write = cs->status_buf;
		count++;
	}
#endif
	restore_flags(flags);
	if (count) {
		ic.command = ISDN_STAT_STAVAIL;
		ic.driver = cs->myid;
		ic.arg = count;
		cs->iif.statcallb(&ic);
	}
}

void
HiSax_putstatus(struct IsdnCardState *cs, char *head, char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	VHiSax_putstatus(cs, head, fmt, args);
	va_end(args);
}

int
ll_run(struct IsdnCardState *cs)
{
	long flags;
	isdn_ctrl ic;

	save_flags(flags);
	cli();
	ic.driver = cs->myid;
	ic.command = ISDN_STAT_RUN;
	cs->iif.statcallb(&ic);
	restore_flags(flags);
	return 0;
}

void
ll_stop(struct IsdnCardState *cs)
{
	isdn_ctrl ic;

	ic.command = ISDN_STAT_STOP;
	ic.driver = cs->myid;
	cs->iif.statcallb(&ic);
	CallcFreeChan(cs);
}

static void
ll_unload(struct IsdnCardState *cs)
{
	isdn_ctrl ic;

	ic.command = ISDN_STAT_UNLOAD;
	ic.driver = cs->myid;
	cs->iif.statcallb(&ic);
	if (cs->status_buf)
		kfree(cs->status_buf);
	cs->status_read = NULL;
	cs->status_write = NULL;
	cs->status_end = NULL;
	kfree(cs->dlog);
}

static void
closecard(int cardnr)
{
	struct IsdnCardState *csta = cards[cardnr].cs;

	if (csta->bcs->BC_Close != NULL) {
		csta->bcs->BC_Close(csta->bcs + 1);
		csta->bcs->BC_Close(csta->bcs);
	}

	if (csta->rcvbuf) {
		kfree(csta->rcvbuf);
		csta->rcvbuf = NULL;
	}
	discard_queue(&csta->rq);
	discard_queue(&csta->sq);
	if (csta->tx_skb) {
		dev_kfree_skb(csta->tx_skb);
		csta->tx_skb = NULL;
	}
	if (csta->mon_rx) {
		kfree(csta->mon_rx);
		csta->mon_rx = NULL;
	}
	if (csta->mon_tx) {
		kfree(csta->mon_tx);
		csta->mon_tx = NULL;
	}
	csta->cardmsg(csta, CARD_RELEASE, NULL);
	if (csta->dbusytimer.function != NULL)
		del_timer(&csta->dbusytimer);
	ll_unload(csta);
}

HISAX_INITFUNC(static int init_card(struct IsdnCardState *cs))
{
	int irq_cnt, cnt = 3;
	long flags;

	save_flags(flags);
	cli();
	irq_cnt = kstat_irqs(cs->irq);
	printk(KERN_INFO "%s: IRQ %d count %d\n", CardType[cs->typ], cs->irq,
		irq_cnt);
	if (cs->cardmsg(cs, CARD_SETIRQ, NULL)) {
		printk(KERN_WARNING "HiSax: couldn't get interrupt %d\n",
			cs->irq);
		restore_flags(flags);
		return(1);
	}
	while (cnt) {
		cs->cardmsg(cs, CARD_INIT, NULL);
		sti();
		current->state = TASK_INTERRUPTIBLE;
		/* Timeout 10ms */
		schedule_timeout((10 * HZ) / 1000);
		restore_flags(flags);
		printk(KERN_INFO "%s: IRQ %d count %d\n", CardType[cs->typ],
			cs->irq, kstat_irqs(cs->irq));
		if (kstat_irqs(cs->irq) == irq_cnt) {
			printk(KERN_WARNING
			       "%s: IRQ(%d) getting no interrupts during init %d\n",
			       CardType[cs->typ], cs->irq, 4 - cnt);
			if (cnt == 1) {
				free_irq(cs->irq, cs);
				return (2);
			} else {
				cs->cardmsg(cs, CARD_RESET, NULL);
				cnt--;
			}
		} else {
			cs->cardmsg(cs, CARD_TEST, NULL);
			return(0);
		}
	}
	restore_flags(flags);
	return(3);
}

HISAX_INITFUNC(static int
checkcard(int cardnr, char *id, int *busy_flag))
{
	long flags;
	int ret = 0;
	struct IsdnCard *card = cards + cardnr;
	struct IsdnCardState *cs;

	save_flags(flags);
	cli();
	if (!(cs = (struct IsdnCardState *)
		kmalloc(sizeof(struct IsdnCardState), GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for IsdnCardState(card %d)\n",
		       cardnr + 1);
		restore_flags(flags);
		return (0);
	}
	memset(cs, 0, sizeof(struct IsdnCardState));
	card->cs = cs;
	cs->cardnr = cardnr;
	cs->debug = L1_DEB_WARN;
	cs->HW_Flags = 0;
	cs->busy_flag = busy_flag;
#if TEI_PER_CARD
#else
	test_and_set_bit(FLG_TWO_DCHAN, &cs->HW_Flags);
#endif
	cs->protocol = card->protocol;

	if ((card->typ > 0) && (card->typ < 31)) {
		if (!((1 << card->typ) & SUPORTED_CARDS)) {
			printk(KERN_WARNING
			     "HiSax: Support for %s Card not selected\n",
			       CardType[card->typ]);
			restore_flags(flags);
			return (0);
		}
	} else {
		printk(KERN_WARNING
		       "HiSax: Card Type %d out of range\n",
		       card->typ);
		restore_flags(flags);
		return (0);
	}
	if (!(cs->dlog = kmalloc(MAX_DLOG_SPACE, GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for dlog(card %d)\n",
		       cardnr + 1);
		restore_flags(flags);
		return (0);
	}
	if (!(cs->status_buf = kmalloc(HISAX_STATUS_BUFSIZE, GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for status_buf(card %d)\n",
		       cardnr + 1);
		kfree(cs->dlog);
		restore_flags(flags);
		return (0);
	}
	cs->stlist = NULL;
	cs->mon_tx = NULL;
	cs->mon_rx = NULL;
	cs->status_read = cs->status_buf;
	cs->status_write = cs->status_buf;
	cs->status_end = cs->status_buf + HISAX_STATUS_BUFSIZE - 1;
	cs->typ = card->typ;
	strcpy(cs->iif.id, id);
	cs->iif.channels = 2;
	cs->iif.maxbufsize = MAX_DATA_SIZE;
	cs->iif.hl_hdrlen = MAX_HEADER_LEN;
	cs->iif.features =
	    ISDN_FEATURE_L2_X75I |
	    ISDN_FEATURE_L2_HDLC |
	    ISDN_FEATURE_L2_MODEM |
	    ISDN_FEATURE_L2_TRANS |
	    ISDN_FEATURE_L3_TRANS |
#ifdef	CONFIG_HISAX_1TR6
	    ISDN_FEATURE_P_1TR6 |
#endif
#ifdef	CONFIG_HISAX_EURO
	    ISDN_FEATURE_P_EURO |
#endif
#ifdef        CONFIG_HISAX_NI1
	    ISDN_FEATURE_P_NI1 |
#endif
	    0;

	cs->iif.command = HiSax_command;
	cs->iif.writecmd = NULL;
	cs->iif.writebuf_skb = HiSax_writebuf_skb;
	cs->iif.readstat = HiSax_readstatus;
	register_isdn(&cs->iif);
	cs->myid = cs->iif.channels;
	printk(KERN_INFO
	       "HiSax: Card %d Protocol %s Id=%s (%d)\n", cardnr + 1,
	       (card->protocol == ISDN_PTYPE_1TR6) ? "1TR6" :
	       (card->protocol == ISDN_PTYPE_EURO) ? "EDSS1" :
	       (card->protocol == ISDN_PTYPE_LEASED) ? "LEASED" :
	       (card->protocol == ISDN_PTYPE_NI1) ? "NI1" :
	       "NONE", cs->iif.id, cs->myid);
	switch (card->typ) {
#if CARD_TELES0
		case ISDN_CTYPE_16_0:
		case ISDN_CTYPE_8_0:
			ret = setup_teles0(card);
			break;
#endif
#if CARD_TELES3
		case ISDN_CTYPE_16_3:
		case ISDN_CTYPE_PNP:
		case ISDN_CTYPE_TELESPCMCIA:
		case ISDN_CTYPE_COMPAQ_ISA:
			ret = setup_teles3(card);
			break;
#endif
#if CARD_S0BOX
		case ISDN_CTYPE_S0BOX:
			ret = setup_s0box(card);
			break;
#endif
#if CARD_TELESPCI
		case ISDN_CTYPE_TELESPCI:
			ret = setup_telespci(card);
			break;
#endif
#if CARD_AVM_A1
		case ISDN_CTYPE_A1:
			ret = setup_avm_a1(card);
			break;
#endif
#if CARD_AVM_A1_PCMCIA
		case ISDN_CTYPE_A1_PCMCIA:
			ret = setup_avm_a1_pcmcia(card);
			break;
#endif
#if CARD_FRITZPCI
		case ISDN_CTYPE_FRITZPCI:
			ret = setup_avm_pcipnp(card);
			break;
#endif
#if CARD_ELSA
		case ISDN_CTYPE_ELSA:
		case ISDN_CTYPE_ELSA_PNP:
		case ISDN_CTYPE_ELSA_PCMCIA:
		case ISDN_CTYPE_ELSA_PCI:
			ret = setup_elsa(card);
			break;
#endif
#if CARD_IX1MICROR2
		case ISDN_CTYPE_IX1MICROR2:
			ret = setup_ix1micro(card);
			break;
#endif
#if CARD_DIEHLDIVA
		case ISDN_CTYPE_DIEHLDIVA:
			ret = setup_diva(card);
			break;
#endif
#if CARD_ASUSCOM
		case ISDN_CTYPE_ASUSCOM:
			ret = setup_asuscom(card);
			break;
#endif
#if CARD_TELEINT
		case ISDN_CTYPE_TELEINT:
			ret = setup_TeleInt(card);
			break;
#endif
#if CARD_SEDLBAUER
		case ISDN_CTYPE_SEDLBAUER:
		case ISDN_CTYPE_SEDLBAUER_PCMCIA:
		case ISDN_CTYPE_SEDLBAUER_FAX:
			ret = setup_sedlbauer(card);
			break;
#endif
#if CARD_SPORTSTER
		case ISDN_CTYPE_SPORTSTER:
			ret = setup_sportster(card);
			break;
#endif
#if CARD_MIC
		case ISDN_CTYPE_MIC:
			ret = setup_mic(card);
			break;
#endif
#if CARD_NETJET
		case ISDN_CTYPE_NETJET:
			ret = setup_netjet(card);
			break;
#endif
#if CARD_TELES3C
		case ISDN_CTYPE_TELES3C:
			ret = setup_t163c(card);
			break;
#endif
#if CARD_NICCY
		case ISDN_CTYPE_NICCY:
			ret = setup_niccy(card);
			break;
#endif
#if CARD_AMD7930
		case ISDN_CTYPE_AMD7930:
			ret = setup_amd7930(card);
			break;
#endif
		default:
			printk(KERN_WARNING "HiSax: Unknown Card Typ %d\n",
			       card->typ);
			ll_unload(cs);
			restore_flags(flags);
			return (0);
	}
	if (!ret) {
		ll_unload(cs);
		restore_flags(flags);
		return (0);
	}
	if (!(cs->rcvbuf = kmalloc(MAX_DFRAME_LEN_L1, GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for isac rcvbuf\n");
		return (1);
	}
	cs->rcvidx = 0;
	cs->tx_skb = NULL;
	cs->tx_cnt = 0;
	cs->event = 0;
	cs->tqueue.next = 0;
	cs->tqueue.sync = 0;
	cs->tqueue.data = cs;

	skb_queue_head_init(&cs->rq);
	skb_queue_head_init(&cs->sq);

	init_bcstate(cs, 0);
	init_bcstate(cs, 1);
	ret = init_card(cs);
	if (ret) {
		closecard(cardnr);
		restore_flags(flags);
		return (0);
	}
	init_tei(cs, cs->protocol);
	CallcNewChan(cs);
	/* ISAR needs firmware download first */
	if (!test_bit(HW_ISAR, &cs->HW_Flags))
		ll_run(cs);
	restore_flags(flags);
	return (1);
}

HISAX_INITFUNC(void
HiSax_shiftcards(int idx))
{
	int i;

	for (i = idx; i < (HISAX_MAX_CARDS - 1); i++)
		memcpy(&cards[i], &cards[i + 1], sizeof(cards[i]));
}

HISAX_INITFUNC(int
HiSax_inithardware(int *busy_flag))
{
	int foundcards = 0;
	int i = 0;
	int t = ',';
	int flg = 0;
	char *id;
	char *next_id = HiSax_id;
	char ids[20];

	if (strchr(HiSax_id, ','))
		t = ',';
	else if (strchr(HiSax_id, '%'))
		t = '%';

	while (i < nrcards) {
		if (cards[i].typ < 1)
			break;
		id = next_id;
		if ((next_id = strchr(id, t))) {
			*next_id++ = 0;
			strcpy(ids, id);
			flg = i + 1;
		} else {
			next_id = id;
			if (flg >= i)
				strcpy(ids, id);
			else
				sprintf(ids, "%s%d", id, i);
		}
		if (checkcard(i, ids, busy_flag)) {
			foundcards++;
			i++;
		} else {
			printk(KERN_WARNING "HiSax: Card %s not installed !\n",
			       CardType[cards[i].typ]);
			if (cards[i].cs)
				kfree((void *) cards[i].cs);
			cards[i].cs = NULL;
			HiSax_shiftcards(i);
		}
	}
	return foundcards;
}

void
HiSax_closecard(int cardnr)
{
	int 	i,last=nrcards - 1;

	if (cardnr>last)
		return;
	if (cards[cardnr].cs) {
		ll_stop(cards[cardnr].cs);
		release_tei(cards[cardnr].cs);
		closecard(cardnr);
		free_irq(cards[cardnr].cs->irq, cards[cardnr].cs);
		kfree((void *) cards[cardnr].cs);
		cards[cardnr].cs = NULL;
	}
	i = cardnr;
	while (i!=last) {
		cards[i] = cards[i+1];
		i++;
	}
	nrcards--;
}

EXPORT_SYMBOL(HiSax_closecard);

void
HiSax_reportcard(int cardnr)
{
	struct IsdnCardState *cs = cards[cardnr].cs;
	struct PStack *stptr;
	struct l3_process *pc;
	int j, i = 1;

	printk(KERN_DEBUG "HiSax: reportcard No %d\n", cardnr + 1);
	printk(KERN_DEBUG "HiSax: Type %s\n", CardType[cs->typ]);
	printk(KERN_DEBUG "HiSax: debuglevel %x\n", cs->debug);
	printk(KERN_DEBUG "HiSax: HiSax_reportcard address 0x%lX\n",
		(ulong) & HiSax_reportcard);
	printk(KERN_DEBUG "HiSax: cs 0x%lX\n", (ulong) cs);
	printk(KERN_DEBUG "HiSax: HW_Flags %x bc0 flg %x bc0 flg %x\n",
		cs->HW_Flags, cs->bcs[0].Flag, cs->bcs[1].Flag);
	printk(KERN_DEBUG "HiSax: bcs 0 mode %d ch%d\n",
		cs->bcs[0].mode, cs->bcs[0].channel);
	printk(KERN_DEBUG "HiSax: bcs 1 mode %d ch%d\n",
		cs->bcs[1].mode, cs->bcs[1].channel);
	printk(KERN_DEBUG "HiSax: cs stl 0x%lX\n", (ulong) & (cs->stlist));
	stptr = cs->stlist;
	while (stptr != NULL) {
		printk(KERN_DEBUG "HiSax: dst%d 0x%lX\n", i, (ulong) stptr);
		printk(KERN_DEBUG "HiSax: dst%d stp 0x%lX\n", i, (ulong) stptr->l1.stlistp);
		printk(KERN_DEBUG "HiSax:   tei %d sapi %d\n",
		       stptr->l2.tei, stptr->l2.sap);
		printk(KERN_DEBUG "HiSax:      man 0x%lX\n", (ulong) stptr->ma.layer);
		pc = stptr->l3.proc;
		while (pc) {
			printk(KERN_DEBUG "HiSax: l3proc %x 0x%lX\n", pc->callref,
			       (ulong) pc);
			printk(KERN_DEBUG "HiSax:    state %d  st 0x%lX chan 0x%lX\n",
			    pc->state, (ulong) pc->st, (ulong) pc->chan);
			pc = pc->next;
		}
		stptr = stptr->next;
		i++;
	}
	for (j = 0; j < 2; j++) {
		printk(KERN_DEBUG "HiSax: ch%d 0x%lX\n", j,
		       (ulong) & cs->channel[j]);
		stptr = cs->channel[j].b_st;
		i = 1;
		while (stptr != NULL) {
			printk(KERN_DEBUG "HiSax:  b_st%d 0x%lX\n", i, (ulong) stptr);
			printk(KERN_DEBUG "HiSax:    man 0x%lX\n", (ulong) stptr->ma.layer);
			stptr = stptr->next;
			i++;
		}
	}
}


int __init 
HiSax_init(void)
{
	int i;

#ifdef MODULE
	int nzproto = 0;
#ifdef CONFIG_HISAX_ELSA
	if (type[0] == ISDN_CTYPE_ELSA_PCMCIA) {
		/* we have exported  and return in this case */
		return 0;
	}
#endif
#ifdef CONFIG_HISAX_SEDLBAUER
	if (type[0] == ISDN_CTYPE_SEDLBAUER_PCMCIA) {
		/* we have to export  and return in this case */
		return 0;
	}
#endif
#ifdef CONFIG_HISAX_AVM_A1_PCMCIA
	if (type[0] == ISDN_CTYPE_A1_PCMCIA) {
		/* we have to export  and return in this case */
		return 0;
	}
#endif
#endif
	nrcards = 0;
	HiSaxVersion();
#ifdef MODULE
	if (id)			/* If id= string used */
		HiSax_id = id;
	for (i = 0; i < HISAX_MAX_CARDS; i++) {
		cards[i].typ = type[i];
		if (protocol[i]) {
			cards[i].protocol = protocol[i];
			nzproto++;
		}
		switch (type[i]) {
			case ISDN_CTYPE_16_0:
				cards[i].para[0] = irq[i];
				cards[i].para[1] = mem[i];
				cards[i].para[2] = io[i];
				break;

			case ISDN_CTYPE_8_0:
				cards[i].para[0] = irq[i];
				cards[i].para[1] = mem[i];
				break;

#ifdef IO0_IO1
			case ISDN_CTYPE_PNP:
			case ISDN_CTYPE_NICCY:
				cards[i].para[0] = irq[i];
				cards[i].para[1] = io0[i];
				cards[i].para[2] = io1[i];
				break;
			case ISDN_CTYPE_COMPAQ_ISA:
				cards[i].para[0] = irq[i];
				cards[i].para[1] = io0[i];
				cards[i].para[2] = io1[i];
				cards[i].para[3] = io[i];
				break;
#endif
			case ISDN_CTYPE_ELSA:
				cards[i].para[0] = io[i];
				break;
			case ISDN_CTYPE_16_3:
			case ISDN_CTYPE_TELESPCMCIA:
			case ISDN_CTYPE_A1:
			case ISDN_CTYPE_A1_PCMCIA:
			case ISDN_CTYPE_ELSA_PNP:
			case ISDN_CTYPE_ELSA_PCMCIA:
			case ISDN_CTYPE_IX1MICROR2:
			case ISDN_CTYPE_DIEHLDIVA:
			case ISDN_CTYPE_ASUSCOM:
			case ISDN_CTYPE_TELEINT:
			case ISDN_CTYPE_SEDLBAUER:
			case ISDN_CTYPE_SEDLBAUER_PCMCIA:
			case ISDN_CTYPE_SEDLBAUER_FAX:
			case ISDN_CTYPE_SPORTSTER:
			case ISDN_CTYPE_MIC:
			case ISDN_CTYPE_TELES3C:
			case ISDN_CTYPE_S0BOX:
			case ISDN_CTYPE_FRITZPCI:
				cards[i].para[0] = irq[i];
				cards[i].para[1] = io[i];
				break;
			case ISDN_CTYPE_ELSA_PCI:
			case ISDN_CTYPE_NETJET:
			case ISDN_CTYPE_AMD7930:
			case ISDN_CTYPE_TELESPCI:
				break;
		}
	}
	if (!nzproto) {
		printk(KERN_WARNING "HiSax: Warning - no protocol specified\n");
		printk(KERN_WARNING "HiSax: Note! module load syntax has changed.\n");
		printk(KERN_WARNING "HiSax: using protocol %s\n", DEFAULT_PROTO_NAME);
	}
#endif
	if (!HiSax_id)
		HiSax_id = HiSaxID;
	if (!HiSaxID[0])
		strcpy(HiSaxID, "HiSax");
	for (i = 0; i < HISAX_MAX_CARDS; i++)
		if (cards[i].typ > 0)
			nrcards++;
	printk(KERN_DEBUG "HiSax: Total %d card%s defined\n",
	       nrcards, (nrcards > 1) ? "s" : "");

	CallcNew();
	Isdnl3New();
	Isdnl2New();
	TeiNew();
	Isdnl1New();
	if (HiSax_inithardware(NULL)) {
		/* Install only, if at least one card found */
		/* No symbols to export, hide all symbols */

#ifdef MODULE
		printk(KERN_INFO "HiSax: module installed\n");
#endif
		return (0);
	} else {
		Isdnl1Free();
		TeiFree();
		Isdnl2Free();
		Isdnl3Free();
		CallcFree();
		return -EIO;
	}
}

#ifdef MODULE
void
cleanup_module(void)
{
	int cardnr = nrcards -1;
	long flags;

	save_flags(flags);
	cli();
	while(cardnr>=0)
		HiSax_closecard(cardnr--);
	Isdnl1Free();
	TeiFree();
	Isdnl2Free();
	Isdnl3Free();
	CallcFree();
	restore_flags(flags);
	printk(KERN_INFO "HiSax module removed\n");
}
#endif

#ifdef CONFIG_HISAX_ELSA
int elsa_init_pcmcia(void *pcm_iob, int pcm_irq, int *busy_flag, int prot)
{
#ifdef MODULE
	int i;
	int nzproto = 0;

	nrcards = 0;
	HiSaxVersion();
	if (id)			/* If id= string used */
		HiSax_id = id;
	/* Initialize all 8 structs, even though we only accept
	   two pcmcia cards
	   */
	for (i = 0; i < HISAX_MAX_CARDS; i++) {
		cards[i].para[0] = irq[i];
		cards[i].para[1] = io[i];
		cards[i].typ = type[i];
		if (protocol[i]) {
			cards[i].protocol = protocol[i];
			nzproto++;
		}
	}
	cards[0].para[0] = pcm_irq;
	cards[0].para[1] = (int)pcm_iob;
	cards[0].protocol = prot;
	cards[0].typ = 10;
	nzproto = 1;

	if (!HiSax_id)
		HiSax_id = HiSaxID;
	if (!HiSaxID[0])
		strcpy(HiSaxID, "HiSax");
	for (i = 0; i < HISAX_MAX_CARDS; i++)
		if (cards[i].typ > 0)
			nrcards++;
	printk(KERN_DEBUG "HiSax: Total %d card%s defined\n",
	       nrcards, (nrcards > 1) ? "s" : "");

	Isdnl1New();
	CallcNew();
	Isdnl3New();
	Isdnl2New();
	TeiNew();
	HiSax_inithardware(busy_flag);
	printk(KERN_NOTICE "HiSax: module installed\n");
#endif
	return (0);
}
#endif

#ifdef CONFIG_HISAX_SEDLBAUER
int sedl_init_pcmcia(void *pcm_iob, int pcm_irq, int *busy_flag, int prot)
{
#ifdef MODULE
	int i;
	int nzproto = 0;

	nrcards = 0;
	HiSaxVersion();
	if (id)			/* If id= string used */
		HiSax_id = id;
	/* Initialize all 8 structs, even though we only accept
	   two pcmcia cards
	   */
	for (i = 0; i < HISAX_MAX_CARDS; i++) {
		cards[i].para[0] = irq[i];
		cards[i].para[1] = io[i];
		cards[i].typ = type[i];
		if (protocol[i]) {
			cards[i].protocol = protocol[i];
			nzproto++;
		}
	}
	cards[0].para[0] = pcm_irq;
	cards[0].para[1] = (int)pcm_iob;
	cards[0].protocol = prot;
	cards[0].typ = ISDN_CTYPE_SEDLBAUER_PCMCIA;
	nzproto = 1;

	if (!HiSax_id)
		HiSax_id = HiSaxID;
	if (!HiSaxID[0])
		strcpy(HiSaxID, "HiSax");
	for (i = 0; i < HISAX_MAX_CARDS; i++)
		if (cards[i].typ > 0)
			nrcards++;
	printk(KERN_DEBUG "HiSax: Total %d card%s defined\n",
	       nrcards, (nrcards > 1) ? "s" : "");

	CallcNew();
	Isdnl3New();
	Isdnl2New();
	Isdnl1New();
	TeiNew();
	HiSax_inithardware(busy_flag);
	printk(KERN_NOTICE "HiSax: module installed\n");
#endif
	return (0);
}
#endif

#ifdef CONFIG_HISAX_AVM_A1_PCMCIA
int avm_a1_init_pcmcia(void *pcm_iob, int pcm_irq, int *busy_flag, int prot)
{
#ifdef MODULE
	int i;
	int nzproto = 0;

	nrcards = 0;
	HiSaxVersion();
	if (id)			/* If id= string used */
		HiSax_id = id;
	/* Initialize all 16 structs, even though we only accept
	   two pcmcia cards
	   */
	for (i = 0; i < 16; i++) {
		cards[i].para[0] = irq[i];
		cards[i].para[1] = io[i];
		cards[i].typ = type[i];
		if (protocol[i]) {
			cards[i].protocol = protocol[i];
			nzproto++;
		}
	}
	cards[0].para[0] = pcm_irq;
	cards[0].para[1] = (int)pcm_iob;
	cards[0].protocol = prot;
	cards[0].typ = ISDN_CTYPE_A1_PCMCIA;
	nzproto = 1;

	if (!HiSax_id)
		HiSax_id = HiSaxID;
	if (!HiSaxID[0])
		strcpy(HiSaxID, "HiSax");
	for (i = 0; i < HISAX_MAX_CARDS; i++)
		if (cards[i].typ > 0)
			nrcards++;
	printk(KERN_DEBUG "HiSax: Total %d card%s defined\n",
	       nrcards, (nrcards > 1) ? "s" : "");

	Isdnl1New();
	CallcNew();
	Isdnl3New();
	Isdnl2New();
	TeiNew();
	HiSax_inithardware(busy_flag);
	printk(KERN_NOTICE "HiSax: module installed\n");
#endif
	return (0);
}
#endif
