/* $Id: config.c,v 2.12 1998/02/11 17:28:02 keil Exp $

 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *              based on the teles driver from Jan den Ouden
 *
 *
 * $Log: config.c,v $
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
 *   16 USR Sportster internal  p0=irq  p1=iobase
 *   17 MIC card                p0=irq  p1=iobase
 *   18 ELSA Quickstep 1000PCI  no parameter
 *   19 Compaq ISDN S0 ISA card p0=irq  p1=IO0 (HSCX)  p2=IO1 (ISAC) p3=IO2
 *   20 Travers Technologies NETjet PCI card
 *   21 reserved TELES PCI
 *   22 Sedlbauer Speed Star    p0=irq p1=iobase
 *   23 reserved
 *   24 Dr Neuhaus Niccy PnP/PCI card p0=irq p1=IO0 p2=IO1 (PnP only)
 *
 *
 * protocol can be either ISDN_PTYPE_EURO or ISDN_PTYPE_1TR6 or ISDN_PTYPE_NI1
 *
 *
 */

#ifdef CONFIG_HISAX_ELSA
#define DEFAULT_CARD ISDN_CTYPE_ELSA
#define DEFAULT_CFG {0,0,0,0}
#ifdef MODULE
int elsa_init_pcmcia(void*, int, int*, int);
EXPORT_SYMBOL(elsa_init_pcmcia);
#endif
#endif
#ifdef CONFIG_HISAX_AVM_A1
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_A1
#define DEFAULT_CFG {10,0x340,0,0}
#endif
#ifdef CONFIG_HISAX_16_3
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_16_3
#define DEFAULT_CFG {15,0x180,0,0}
#endif
#ifdef CONFIG_HISAX_16_0
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_16_0
#define DEFAULT_CFG {15,0xd0000,0xd80,0}
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
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
};

static char HiSaxID[96] HISAX_INITDATA = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" \
"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" \
"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" \
"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
char *HiSax_id HISAX_INITDATA = HiSaxID;
#ifdef MODULE
/* Variables for insmod */
static int type[] HISAX_INITDATA =
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static int protocol[] HISAX_INITDATA =
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static int io[] HISAX_INITDATA =
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
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
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static int io1[] HISAX_INITDATA =
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
#endif
static int irq[] HISAX_INITDATA =
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static int mem[] HISAX_INITDATA =
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static char *id HISAX_INITDATA = HiSaxID;

MODULE_AUTHOR("Karsten Keil");
MODULE_PARM(type, "1-3i");
MODULE_PARM(protocol, "1-2i");
MODULE_PARM(io, "1-8i");
MODULE_PARM(irq, "1-2i");
MODULE_PARM(mem, "1-12i");
MODULE_PARM(id, "s");
#ifdef CONFIG_HISAX_16_3	/* For Creatix/Teles PnP */
MODULE_PARM(io0, "1-8i");
MODULE_PARM(io1, "1-8i");
#endif

#endif

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
	char tmp[64], rev[64];
	char *r = rev;

	strcpy(tmp, l1_revision);
	r += sprintf(r, "%s/", HiSax_getrev(tmp));
	strcpy(tmp, l2_revision);
	r += sprintf(r, "%s/", HiSax_getrev(tmp));
	strcpy(tmp, l3_revision);
	r += sprintf(r, "%s/", HiSax_getrev(tmp));
	strcpy(tmp, lli_revision);
	r += sprintf(r, "%s/", HiSax_getrev(tmp));
	strcpy(tmp, tei_revision);
	r += sprintf(r, "%s", HiSax_getrev(tmp));

	printk(KERN_INFO "HiSax: Driver for Siemens chip set ISDN cards\n");
	printk(KERN_INFO "HiSax: Version 2.8\n");
	printk(KERN_INFO "HiSax: Revisions %s\n", rev);
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
__initfunc(void
HiSax_setup(char *str, int *ints))
{
	int i, j, argc;

	argc = ints[0];
	i = 0;
	j = 1;
	while (argc && (i < 16)) {
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

__initfunc(int
HiSax_init(void))
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
#endif
	HiSaxVersion();
	nrcards = 0;
#ifdef MODULE
	if (id)			/* If id= string used */
		HiSax_id = id;
	for (i = 0; i < 16; i++) {
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
			case ISDN_CTYPE_ELSA_PNP:
			case ISDN_CTYPE_ELSA_PCMCIA:
			case ISDN_CTYPE_IX1MICROR2:
			case ISDN_CTYPE_DIEHLDIVA:
			case ISDN_CTYPE_ASUSCOM:
			case ISDN_CTYPE_TELEINT:
			case ISDN_CTYPE_SEDLBAUER:
			case ISDN_CTYPE_SEDLBAUER_PCMCIA:
			case ISDN_CTYPE_SPORTSTER:
			case ISDN_CTYPE_MIC:
			case ISDN_CTYPE_TELES3C:
				cards[i].para[0] = irq[i];
				cards[i].para[1] = io[i];
				break;
			case ISDN_CTYPE_ELSA_PCI:
			case ISDN_CTYPE_NETJET:
			case ISDN_CTYPE_AMD7930:
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
	for (i = 0; i < 16; i++)
		if (cards[i].typ > 0)
			nrcards++;
	printk(KERN_DEBUG "HiSax: Total %d card%s defined\n",
	       nrcards, (nrcards > 1) ? "s" : "");

	CallcNew();
	Isdnl2New();
	TeiNew();
	Isdnl1New();
	if (HiSax_inithardware(NULL)) {
		/* Install only, if at least one card found */
		/* No symbols to export, hide all symbols */

#ifdef MODULE
		EXPORT_NO_SYMBOLS;
		printk(KERN_INFO "HiSax: module installed\n");
#endif
		return (0);
	} else {
		Isdnl1Free();
		TeiFree();
		Isdnl2Free();
		CallcFree();
		return -EIO;
	}
}

#ifdef MODULE
void
cleanup_module(void)
{
	HiSax_closehardware();
	printk(KERN_INFO "HiSax module removed\n");
}

#ifdef CONFIG_HISAX_ELSA
int elsa_init_pcmcia(void *pcm_iob, int pcm_irq, int *busy_flag, int prot)
{
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
	cards[0].typ = 10;
	nzproto = 1;

	if (!HiSax_id)
		HiSax_id = HiSaxID;
	if (!HiSaxID[0])
		strcpy(HiSaxID, "HiSax");
	for (i = 0; i < 16; i++)
		if (cards[i].typ > 0)
			nrcards++;
	printk(KERN_DEBUG "HiSax: Total %d card%s defined\n",
	       nrcards, (nrcards > 1) ? "s" : "");

	Isdnl1New();
	CallcNew();
	Isdnl2New();
	TeiNew();
	HiSax_inithardware(busy_flag);
	printk(KERN_NOTICE "HiSax: module installed\n");
	return (0);
}
#endif
#ifdef CONFIG_HISAX_SEDLBAUER
int sedl_init_pcmcia(void *pcm_iob, int pcm_irq, int *busy_flag, int prot)
{
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
	cards[0].typ = ISDN_CTYPE_SEDLBAUER_PCMCIA;
	nzproto = 1;

	if (!HiSax_id)
		HiSax_id = HiSaxID;
	if (!HiSaxID[0])
		strcpy(HiSaxID, "HiSax");
	for (i = 0; i < 16; i++)
		if (cards[i].typ > 0)
			nrcards++;
	printk(KERN_DEBUG "HiSax: Total %d card%s defined\n",
	       nrcards, (nrcards > 1) ? "s" : "");

	Isdnl1New();
	CallcNew();
	Isdnl2New();
	TeiNew();
	HiSax_inithardware(busy_flag);
	printk(KERN_NOTICE "HiSax: module installed\n");
	return (0);
}
#endif
#endif 
