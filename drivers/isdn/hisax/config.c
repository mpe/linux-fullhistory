/* $Id: config.c,v 1.11 1997/02/14 12:23:12 fritz Exp $

 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *              based on the teles driver from Jan den Ouden
 *
 *
 * $Log: config.c,v $
 * Revision 1.11  1997/02/14 12:23:12  fritz
 * Added support for new insmod parameter handling.
 *
 * Revision 1.10  1997/02/14 09:22:09  keil
 * Final 2.0 version
 *
 * Revision 1.9  1997/02/10 11:45:09  fritz
 * More changes for Kernel 2.1.X compatibility.
 *
 * Revision 1.8  1997/02/09 00:28:05  keil
 * new interface handling, one interface per card
 * default protocol now works again
 *
 * Revision 1.7  1997/01/27 15:56:57  keil
 * Teles PCMCIA ITK ix1 micro added
 *
 * Revision 1.6  1997/01/21 22:17:56  keil
 * new module load syntax
 *
 * Revision 1.5  1997/01/09 18:28:20  keil
 * cosmetic cleanups
 *
 * Revision 1.4  1996/11/05 19:35:17  keil
 * using config.h; some spelling fixes
 *
 * Revision 1.3  1996/10/23 17:23:28  keil
 * default config changes
 *
 * Revision 1.2  1996/10/23 11:58:48  fritz
 * Changed default setup to reflect user's selection of supported
 * cards/protocols.
 *
 * Revision 1.1  1996/10/13 20:04:51  keil
 * Initial revision
 *
 *
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
 *    1 Teles 16.0      p0=irq p1=membase p2=iobase
 *    2 Teles  8.0      p0=irq p1=membase
 *    3 Teles 16.3      p0=irq p1=iobase
 *    4 Creatix PNP     p0=irq p1=IO0 (ISAC)  p2=IO1 (HSCX)
 *    5 AVM A1 (Fritz)  p0=irq p1=iobase
 *    6 ELSA PC         [p0=iobase] or nothing (autodetect)
 *    7 ELSA Quickstep  p0=irq p1=iobase
 *    8 Teles PCMCIA    p0=irq p1=iobase
 *    9 ITK ix1-micro   p0=irq p1=iobase
 *
 *
 * protocol can be either ISDN_PTYPE_EURO or ISDN_PTYPE_1TR6
 *
 *
 */

#ifdef CONFIG_HISAX_ELSA_PCC
#define DEFAULT_CARD ISDN_CTYPE_ELSA
#define DEFAULT_CFG {0,0,0}
#endif
#ifdef CONFIG_HISAX_AVM_A1
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_A1
#define DEFAULT_CFG {10,0x340,0}
#endif
#ifdef CONFIG_HISAX_16_3
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_16_3
#define DEFAULT_CFG {15,0x180,0}
#endif
#ifdef CONFIG_HISAX_16_0
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_16_0
#define DEFAULT_CFG {15,0xd0000,0xd80}
#endif

#ifdef CONFIG_HISAX_IX1MICROR2
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_IX1MICROR2
#define DEFAULT_CFG {5,0x390,0}
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

#define EMPTY_CARD	{0, DEFAULT_PROTO, {0, 0, 0}, NULL}

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

static char HiSaxID[96] = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" \
"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" \
"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" \
"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
char *HiSax_id = HiSaxID;
#ifdef MODULE
/* Variables for insmod */
int type[] =
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int protocol[] =
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int io[] =
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
#ifdef CONFIG_HISAX_16_3	/* For Creatix/Teles PnP */
int io0[] =
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int io1[] =
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
#endif
int irq[] =
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int mem[] =
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
char *id = HiSaxID;

#if (LINUX_VERSION_CODE > 0x020111)
MODULE_AUTHOR("Karsten Keil");
MODULE_PARM(type, "1-16i");
MODULE_PARM(protocol, "1-16i");
MODULE_PARM(io, "1-16i");
MODULE_PARM(irq, "1-16i");
MODULE_PARM(mem, "1-16i");
MODULE_PARM(id, "s");
#ifdef CONFIG_HISAX_16_3	/* For Creatix/Teles PnP */
MODULE_PARM(io0, "1-16i");
MODULE_PARM(io1, "1-16i");
#endif
#endif

#endif

extern char *l1_revision;
extern char *l2_revision;
extern char *l3_revision;
extern char *l4_revision;
extern char *tei_revision;

char *
HiSax_getrev(const char *revision)
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

int nrcards;

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
void
HiSax_setup(char *str, int *ints)
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

int
HiSax_init(void)
{
	int i;
	char tmp[64], rev[64];
	char *r = rev;
	int nzproto = 0;

	nrcards = 0;
	strcpy(tmp, l1_revision);
	r += sprintf(r, "%s/", HiSax_getrev(tmp));
	strcpy(tmp, l2_revision);
	r += sprintf(r, "%s/", HiSax_getrev(tmp));
	strcpy(tmp, l3_revision);
	r += sprintf(r, "%s/", HiSax_getrev(tmp));
	strcpy(tmp, l4_revision);
	r += sprintf(r, "%s/", HiSax_getrev(tmp));
	strcpy(tmp, tei_revision);
	r += sprintf(r, "%s", HiSax_getrev(tmp));

	printk(KERN_NOTICE "HiSax: Driver for Siemens chip set ISDN cards\n");
	printk(KERN_NOTICE "HiSax: Version 2.0\n");
	printk(KERN_NOTICE "HiSax: Revisions %s\n", rev);

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

			case ISDN_CTYPE_16_3:
			case ISDN_CTYPE_TELESPCMCIA:
				cards[i].para[0] = irq[i];
				cards[i].para[1] = io[i];
				break;

#ifdef CONFIG_HISAX_16_3	/* For Creatix/Teles PnP */
			case ISDN_CTYPE_PNP:
				cards[i].para[0] = irq[i];
				cards[i].para[1] = io0[i];
				cards[i].para[2] = io1[i];
				break;
#endif
			case ISDN_CTYPE_A1:
				cards[i].para[0] = irq[i];
				cards[i].para[1] = io[i];
				break;

			case ISDN_CTYPE_ELSA:
				cards[i].para[0] = io[i];
				break;
			case ISDN_CTYPE_ELSA_QS1000:
				cards[i].para[0] = irq[i];
				cards[i].para[1] = io[i];
				break;

			case ISDN_CTYPE_IX1MICROR2:
				cards[i].para[0] = irq[i];
				cards[i].para[1] = io[i];
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
	if (HiSax_inithardware()) {
		/* Install only, if at least one card found */
		/* No symbols to export, hide all symbols */

#ifdef MODULE
#if (LINUX_VERSION_CODE < 0x020111)
		register_symtab(NULL);
#else
		EXPORT_NO_SYMBOLS;
#endif
		printk(KERN_NOTICE "HiSax: module installed\n");
#endif
		return (0);
	} else {
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
	printk(KERN_NOTICE "HiSax module removed\n");
}

#endif
