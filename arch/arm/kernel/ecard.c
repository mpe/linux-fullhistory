/*
 * linux/arch/arm/kernel/ecard.c
 *
 *  Find all installed expansion cards, and handle interrupts from them.
 *
 * Copyright 1995-1998 Russell King
 *
 * Created from information from Acorns RiscOS3 PRMs
 *
 * 08-Dec-1996	RMK	Added code for the 9'th expansion card - the ether podule slot.
 * 06-May-1997  RMK	Added blacklist for cards whose loader doesn't work.
 * 12-Sep-1997	RMK	Created new handling of interrupt enables/disables - cards can
 *			now register their own routine to control interrupts (recommended).
 * 29-Sep-1997	RMK	Expansion card interrupt hardware not being re-enabled on reset from
 *			Linux. (Caused cards not to respond under RiscOS without hard reset).
 * 15-Feb-1998	RMK	Added DMA support
 * 12-Sep-1998	RMK	Added EASI support
 */

#define ECARD_C

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/ecard.h>
#include <asm/irq.h>
#include <asm/dma.h>

#ifdef CONFIG_ARCH_ARC
#include <asm/arch/oldlatches.h>
#else
#define oldlatch_init()
#endif

#define BLACKLIST_NAME(m,p,s)	{ m, p, NULL, s }
#define BLACKLIST_LOADER(m,p,l)	{ m, p, l, NULL }
#define BLACKLIST_NOLOADER(m,p)	{ m, p, noloader, blacklisted_str }
#define BUS_ADDR(x) ((((unsigned long)(x)) << 2) + IO_BASE)

extern unsigned long atomwide_serial_loader[], oak_scsi_loader[], noloader[];
static const char blacklisted_str[] = "*loader s/w is not 32-bit compliant*";

static const struct expcard_blacklist {
	unsigned short	 manufacturer;
	unsigned short	 product;
	const loader_t 	 loader;
	const char	*type;
} blacklist[] = {
/* Cards without names */
    BLACKLIST_NAME(MANU_ACORN,		PROD_ACORN_ETHER1,	"Acorn Ether1"),

/* Cards with corrected loader */
  BLACKLIST_LOADER(MANU_ATOMWIDE,	PROD_ATOMWIDE_3PSERIAL,	atomwide_serial_loader),
  BLACKLIST_LOADER(MANU_OAK,		PROD_OAK_SCSI,		oak_scsi_loader),

/* Supported cards with broken loader */
  { MANU_ALSYSTEMS, PROD_ALSYS_SCSIATAPI, noloader, "AlSystems PowerTec SCSI" },

/* Unsupported cards with no loader */
  BLACKLIST_NOLOADER(MANU_MCS,		PROD_MCS_CONNECT32)
};

extern int setup_arm_irq(int, struct irqaction *);

/*
 * from linux/arch/arm/kernel/irq.c
 */
extern void do_ecard_IRQ(int irq, struct pt_regs *);

static ecard_t expcard[MAX_ECARDS];
static signed char irqno_to_expcard[16];
static unsigned int ecard_numcards, ecard_numirqcards;
static unsigned int have_expmask;

static void ecard_def_irq_enable(ecard_t *ec, int irqnr)
{
#ifdef HAS_EXPMASK
	if (irqnr < 4 && have_expmask) {
		have_expmask |= 1 << irqnr;
		EXPMASK_ENABLE = have_expmask;
	}
#endif
}

static void ecard_def_irq_disable(ecard_t *ec, int irqnr)
{
#ifdef HAS_EXPMASK
	if (irqnr < 4 && have_expmask) {
		have_expmask &= ~(1 << irqnr);
		EXPMASK_ENABLE = have_expmask;
	}
#endif
}

static void ecard_def_fiq_enable(ecard_t *ec, int fiqnr)
{
	panic("ecard_def_fiq_enable called - impossible");
}

static void ecard_def_fiq_disable(ecard_t *ec, int fiqnr)
{
	panic("ecard_def_fiq_disable called - impossible");
}

static expansioncard_ops_t ecard_default_ops = {
	ecard_def_irq_enable,
	ecard_def_irq_disable,
	ecard_def_fiq_enable,
	ecard_def_fiq_disable
};

/*
 * Enable and disable interrupts from expansion cards.
 * (interrupts are disabled for these functions).
 *
 * They are not meant to be called directly, but via enable/disable_irq.
 */
void ecard_enableirq(unsigned int irqnr)
{
	irqnr &= 7;
	if (irqnr < MAX_ECARDS && irqno_to_expcard[irqnr] != -1) {
		ecard_t *ec = expcard + irqno_to_expcard[irqnr];

		if (!ec->ops)
			ec->ops = &ecard_default_ops;

		if (ec->claimed && ec->ops->irqenable)
			ec->ops->irqenable(ec, irqnr);
		else
			printk(KERN_ERR "ecard: rejecting request to "
				"enable IRQs for %d\n", irqnr);
	}
}

void ecard_disableirq(unsigned int irqnr)
{
	irqnr &= 7;
	if (irqnr < MAX_ECARDS && irqno_to_expcard[irqnr] != -1) {
		ecard_t *ec = expcard + irqno_to_expcard[irqnr];

		if (!ec->ops)
			ec->ops = &ecard_default_ops;

		if (ec->ops && ec->ops->irqdisable)
			ec->ops->irqdisable(ec, irqnr);
	}
}

void ecard_enablefiq(unsigned int fiqnr)
{
	fiqnr &= 7;
	if (fiqnr < MAX_ECARDS && irqno_to_expcard[fiqnr] != -1) {
		ecard_t *ec = expcard + irqno_to_expcard[fiqnr];

		if (!ec->ops)
			ec->ops = &ecard_default_ops;

		if (ec->claimed && ec->ops->fiqenable)
			ec->ops->fiqenable(ec, fiqnr);
		else
			printk(KERN_ERR "ecard: rejecting request to "
				"enable FIQs for %d\n", fiqnr);
	}
}

void ecard_disablefiq(unsigned int fiqnr)
{
	fiqnr &= 7;
	if (fiqnr < MAX_ECARDS && irqno_to_expcard[fiqnr] != -1) {
		ecard_t *ec = expcard + irqno_to_expcard[fiqnr];

		if (!ec->ops)
			ec->ops = &ecard_default_ops;

		if (ec->ops->fiqdisable)
			ec->ops->fiqdisable(ec, fiqnr);
	}
}

static void ecard_irq_noexpmask(int intr_no, void *dev_id, struct pt_regs *regs)
{
	const int num_cards = ecard_numirqcards;
	int i, called = 0;

	for (i = 0; i < num_cards; i++) {
		if (expcard[i].claimed && expcard[i].irq &&
		    (!expcard[i].irqmask ||
		     expcard[i].irqaddr[0] & expcard[i].irqmask)) {
			do_ecard_IRQ(expcard[i].irq, regs);
			called ++;
		}
	}
	cli();
	if (called == 0) {
		static int last, lockup;

		if (last == jiffies) {
			lockup += 1;
			if (lockup > 1000000) {
				printk(KERN_ERR "\nInterrupt lockup detected - disabling expansion card IRQs\n");
				disable_irq(intr_no);
				printk("Expansion card IRQ state:\n");
				for (i = 0; i < num_cards; i++)
					printk("  %d: %sclaimed, irqaddr = %p, irqmask = %X, status=%X\n", expcard[i].irq - 32,
						expcard[i].claimed ? "" : "not", expcard[i].irqaddr, expcard[i].irqmask, *expcard[i].irqaddr);
			}
		} else
			lockup = 0;

		if (!last || time_after(jiffies, last + 5*HZ)) {
			last = jiffies;
			printk(KERN_ERR "\nUnrecognised interrupt from backplane\n");
		}
	}
}

#ifdef HAS_EXPMASK
static unsigned char priority_masks[] =
{
	0xf0, 0xf1, 0xf3, 0xf7, 0xff, 0xff, 0xff, 0xff
};

static unsigned char first_set[] =
{
	0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
	0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00
};

static void ecard_irq_expmask(int intr_no, void *dev_id, struct pt_regs *regs)
{
	const unsigned int statusmask = 15;
	unsigned int status;

	status = EXPMASK_STATUS & statusmask;
	if (status) {
		unsigned int irqno;
		ecard_t *ec;
again:
		irqno = first_set[status];
		ec = expcard + irqno_to_expcard[irqno];
		if (ec->claimed) {
			unsigned int oldexpmask;
			/*
			 * this ugly code is so that we can operate a prioritorising system.
			 * Card 0 	highest priority
			 * Card 1
			 * Card 2
			 * Card 3	lowest priority
			 * Serial cards should go in 0/1, ethernet/scsi in 2/3
			 * otherwise you will lose serial data at high speeds!
			 */
			oldexpmask = have_expmask;
			EXPMASK_ENABLE = (have_expmask &= priority_masks[irqno]);
			sti();
			do_ecard_IRQ(ec->irq, regs);
			cli();
			EXPMASK_ENABLE = have_expmask = oldexpmask;
			status = EXPMASK_STATUS & statusmask;
			if (status)
				goto again;
		} else {
			printk(KERN_WARNING "card%d: interrupt from unclaimed card???\n", irqno);
			EXPMASK_ENABLE = (have_expmask &= ~(1 << irqno));
		}
	} else
		printk(KERN_WARNING "Wild interrupt from backplane (masks)\n");
}

static int ecard_checkirqhw(void)
{
	int found;

	EXPMASK_ENABLE = 0x00;
	EXPMASK_STATUS = 0xff;
	found = ((EXPMASK_STATUS & 15) == 0);
	EXPMASK_ENABLE = 0xff;

	return found;
}
#endif

static void ecard_readbytes(void *addr, ecard_t *ec, int off, int len, int useld)
{
	extern int ecard_loader_read(int off, volatile unsigned int pa, loader_t loader);
	unsigned char *a = (unsigned char *)addr;

	if (ec->slot_no == 8) {
		static unsigned int lowaddress;
		unsigned int laddr, haddr;
		unsigned char byte = 0; /* keep gcc quiet */

		laddr = off & 4095;	/* number of bytes to read from offset + base addr */
		haddr = off >> 12;	/* offset into card from base addr */

		if (haddr > 256)
			return;

		/*
		 * If we require a low address or address 0, then reset, and start again...
		 */
		if (!off || lowaddress > laddr) {
			outb(0, ec->podaddr);
			lowaddress = 0;
		}
		while (lowaddress <= laddr) {
			byte = inb(ec->podaddr + haddr);
			lowaddress += 1;
		}
		while (len--) {
			*a++ = byte;
			if (len) {
				byte = inb(ec->podaddr + haddr);
				lowaddress += 1;
			}
		}
	} else {
		if (!useld || !ec->loader) {
			while(len--)
				*a++ = inb(ec->podaddr + (off++));
		} else {
			while(len--) {
				*(unsigned long *)0x108 = 0; /* hack for some loaders!!! */
				*a++ = ecard_loader_read(off++, BUS_ADDR(ec->podaddr), ec->loader);
			}
		}
	}
}

static int ecard_prints(char *buffer, ecard_t *ec)
{
	char *start = buffer;

	buffer += sprintf(buffer, "\n  %d: ", ec->slot_no);

	if (ec->cid.id == 0) {
		struct in_chunk_dir incd;

		buffer += sprintf(buffer, "[%04X:%04X] ",
			ec->cid.manufacturer, ec->cid.product);

		if (!ec->card_desc && ec->cid.is && ec->cid.cd &&
		    ecard_readchunk(&incd, ec, 0xf5, 0))
			ec->card_desc = incd.d.string;

		if (!ec->card_desc)
			ec->card_desc = "*unknown*";

		buffer += sprintf(buffer, "%s", ec->card_desc);
	} else
		buffer += sprintf(buffer, "Simple card %d", ec->cid.id);

	return buffer - start;
}

static inline unsigned short ecard_getu16(unsigned char *v)
{
	return v[0] | v[1] << 8;
}

static inline signed long ecard_gets24(unsigned char *v)
{
	return v[0] | v[1] << 8 | v[2] << 16 | ((v[2] & 0x80) ? 0xff000000 : 0);
}

/*
 * Probe for an expansion card.
 *
 * If bit 1 of the first byte of the card is set, then the
 * card does not exist.
 */
__initfunc(static int ecard_probe(int card, int freeslot, card_type_t type))
{
	ecard_t *ec = expcard + freeslot;
	struct ex_ecid cid;
	char buffer[200];
	int i;

	irqno_to_expcard[card] = -1;

	ec->slot_no	= card;
	ec->irq		= NO_IRQ;
	ec->fiq		= NO_IRQ;
	ec->dma		= NO_DMA;
	ec->card_desc	= NULL;
	ec->ops		= &ecard_default_ops;

	if ((ec->podaddr = ecard_address(ec, type, ECARD_SYNC)) == 0)
		return 0;

	cid.r_zero = 1;
	ecard_readbytes(&cid, ec, 0, 16, 0);
	if (cid.r_zero)
		return 0;

	irqno_to_expcard[card] = freeslot;

	ec->type	= type;
	ec->cid.id	= cid.r_id;
	ec->cid.cd	= cid.r_cd;
	ec->cid.is	= cid.r_is;
	ec->cid.w	= cid.r_w;
	ec->cid.manufacturer = ecard_getu16(cid.r_manu);
	ec->cid.product = ecard_getu16(cid.r_prod);
	ec->cid.country = cid.r_country;
	ec->cid.irqmask = cid.r_irqmask;
	ec->cid.irqoff  = ecard_gets24(cid.r_irqoff);
	ec->cid.fiqmask = cid.r_fiqmask;
	ec->cid.fiqoff  = ecard_gets24(cid.r_fiqoff);
	ec->fiqaddr	=
	ec->irqaddr	= (unsigned char *)BUS_ADDR(ec->podaddr);

	if (ec->cid.cd && ec->cid.is) {
		ec->irqmask = ec->cid.irqmask;
		ec->irqaddr += ec->cid.irqoff;
		ec->fiqmask = ec->cid.fiqmask;
		ec->fiqaddr += ec->cid.fiqoff;
	} else {
		ec->irqmask = 1;
		ec->fiqmask = 4;
	}

	for (i = 0; i < sizeof(blacklist) / sizeof(*blacklist); i++)
		if (blacklist[i].manufacturer == ec->cid.manufacturer &&
		    blacklist[i].product == ec->cid.product) {
			ec->loader = blacklist[i].loader;
			ec->card_desc = blacklist[i].type;
			break;
		}

	ecard_prints(buffer, ec);
	printk("%s", buffer);

	ec->irq = 32 + card;
#ifdef IO_EC_MEMC8_BASE
	if (card == 8)
		ec->irq = 11;
#endif
#ifdef CONFIG_ARCH_RPC
	/* On RiscPC, only first two slots have DMA capability */
	if (card < 2)
		ec->dma = 2 + card;
#endif
#if 0	/* We don't support FIQs on expansion cards at the moment */
	ec->fiq = 96 + card;
#endif

	return 1;
}

/*
 * This is called to reset the loaders for each expansion card on reboot.
 *
 * This is required to make sure that the card is in the correct state
 * that RiscOS expects it to be.
 */
void ecard_reset(int card)
{
	extern int ecard_loader_reset(volatile unsigned int pa, loader_t loader);

	if (card >= ecard_numcards)
		return;
    
	if (card < 0) {
		for (card = 0; card < ecard_numcards; card++)
			if (expcard[card].loader)
				ecard_loader_reset(BUS_ADDR(expcard[card].podaddr),
							expcard[card].loader);
	} else
		if (expcard[card].loader)
			ecard_loader_reset(BUS_ADDR(expcard[card].podaddr),
						expcard[card].loader);

#ifdef HAS_EXPMASK
	if (have_expmask) {
		have_expmask |= ~0;
		EXPMASK_ENABLE = have_expmask;
	}
#endif
}

static unsigned int ecard_startcard;

void ecard_startfind(void)
{
	ecard_startcard = 0;
}

ecard_t *ecard_find(int cid, const card_ids *cids)
{
	int card;

	if (!cids) {
		for (card = ecard_startcard; card < ecard_numcards; card++)
			if (!expcard[card].claimed &&
			    (expcard[card].cid.id ^ cid) == 0)
				break;
	} else {
		for (card = ecard_startcard; card < ecard_numcards; card++) {
			unsigned int manufacturer, product;
			int i;

			if (expcard[card].claimed)
				continue;

			manufacturer = expcard[card].cid.manufacturer;
			product = expcard[card].cid.product;

			for (i = 0; cids[i].manufacturer != 65535; i++)
				if (manufacturer == cids[i].manufacturer &&
				    product == cids[i].product)
					break;

			if (cids[i].manufacturer != 65535)
				break;
		}
	}

	ecard_startcard = card + 1;

	return card < ecard_numcards ? &expcard[card] : NULL;
}

int ecard_readchunk(struct in_chunk_dir *cd, ecard_t *ec, int id, int num)
{
	struct ex_chunk_dir excd;
	int index = 16;
	int useld = 0;

	if (!ec->cid.is || !ec->cid.cd)
		return 0;

	while(1) {
		ecard_readbytes(&excd, ec, index, 8, useld);
		index += 8;
		if (c_id(&excd) == 0) {
			if (!useld && ec->loader) {
				useld = 1;
				index = 0;
				continue;
			}
			return 0;
		}
		if (c_id(&excd) == 0xf0) { /* link */
			index = c_start(&excd);
			continue;
		}
		if (c_id(&excd) == 0x80) { /* loader */
			if (!ec->loader) {
				ec->loader = (loader_t)kmalloc(c_len(&excd), GFP_KERNEL);
				ecard_readbytes(ec->loader, ec, (int)c_start(&excd), c_len(&excd), useld);
			}
			continue;
		}
		if (c_id(&excd) == id && num-- == 0)
			break;
	}

	if (c_id(&excd) & 0x80) {
		switch (c_id(&excd) & 0x70) {
		case 0x70:
			ecard_readbytes((unsigned char *)excd.d.string, ec,
					(int)c_start(&excd), c_len(&excd), useld);
			break;
		case 0x00:
			break;
		}
	}
	cd->start_offset = c_start(&excd);
	memcpy(cd->d.string, excd.d.string, 256);
	return 1;
}

unsigned int ecard_address(ecard_t *ec, card_type_t type, card_speed_t speed)
{
	switch (ec->slot_no) {
	case 0 ... 3:
		switch (type) {
		case ECARD_MEMC:
			return IO_EC_MEMC_BASE + (ec->slot_no << 12);

		case ECARD_IOC:
			return IO_EC_IOC_BASE + (speed << 17) + (ec->slot_no << 12);

#ifdef IO_EC_EASI_BASE
		case ECARD_EASI:
			return IO_EC_EASI_BASE + (ec->slot_no << 22);
#endif
		}
		break;

	case 4 ... 7:
		switch (type) {
#ifdef IO_EC_IOC4_BASE
		case ECARD_IOC:
			return IO_EC_IOC4_BASE + (speed << 17) + ((ec->slot_no - 4) << 12);
#endif
#ifdef IO_EC_EASI_BASE
		case ECARD_EASI:
			return IO_EC_EASI_BASE + (ec->slot_no << 22);
#endif
		default:
			break;
		}
		break;

#ifdef IO_EC_MEMC8_BASE
	case 8:
		return IO_EC_MEMC8_BASE;
#endif
	}
	return 0;
}

static struct irqaction irqexpansioncard = {
	ecard_irq_noexpmask,
	SA_INTERRUPT,
	0,
	"expansion cards",
	NULL,
	NULL
};

/*
 * Initialise the expansion card system.
 * Locate all hardware - interrupt management and
 * actual cards.
 */
__initfunc(void ecard_init(void))
{
	int i, nc = 0;

	memset(expcard, 0, sizeof(expcard));

#ifdef HAS_EXPMASK
	if (ecard_checkirqhw()) {
		printk(KERN_DEBUG "Expansion card interrupt management hardware found\n");
		irqexpansioncard.handler = ecard_irq_expmask;
		irqexpansioncard.flags |= SA_IRQNOMASK;
		have_expmask = -1;
	}
#endif

	printk("Installed expansion cards:");

	/*
	 * First of all, probe all cards on the expansion card interrupt line
	 */
	for (i = 0; i < 8; i++)
		if (ecard_probe(i, nc, ECARD_IOC) || ecard_probe(i, nc, ECARD_EASI))
			nc += 1;
		else
			have_expmask &= ~(1<<i);

	ecard_numirqcards = nc;

	/* Now probe other cards with different interrupt lines
	 */
#ifdef IO_EC_MEMC8_BASE
	if (ecard_probe(8, nc, ECARD_IOC))
		nc += 1;
#endif

	printk("\n");
	ecard_numcards = nc;

	if (nc && setup_arm_irq(IRQ_EXPANSIONCARD, &irqexpansioncard)) {
		printk("Could not allocate interrupt for expansion cards\n");
		return;
	}
	
#ifdef HAS_EXPMASK
	if (nc && have_expmask)
		EXPMASK_ENABLE = have_expmask;
#endif

	oldlatch_init();
}
