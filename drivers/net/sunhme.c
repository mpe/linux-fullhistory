/* sunhme.c: Sparc HME/BigMac 10/100baseT half/full duplex auto switching,
 *           auto carrier detecting ethernet driver.  Also known as the
 *           "Happy Meal Ethernet" found on SunSwift SBUS cards.
 *
 * Copyright (C) 1996 David S. Miller (davem@caipfs.rutgers.edu)
 */

static char *version =
        "sunhme.c:v1.2 10/Oct/96 David S. Miller (davem@caipfs.rutgers.edu)\n";

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/init.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/errno.h>
#include <asm/byteorder.h>

#include <asm/idprom.h>
#include <asm/sbus.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/auxio.h>
#include <asm/system.h>
#include <asm/pgtable.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include "sunhme.h"

#ifdef MODULE
static struct happy_meal *root_happy_dev = NULL;
#endif

/* #define HMEDEBUG */

#ifdef HMEDEBUG
#define HMD(x)  printk x
#else
#define HMD(x)
#endif

/* #define AUTO_SWITCH_DEBUG */

#ifdef AUTO_SWITCH_DEBUG
#define ASD(x)  printk x
#else
#define ASD(x)
#endif

#define DEFAULT_IPG0      32 /* For lance-mode only */
#define DEFAULT_IPG1       8 /* For all modes */
#define DEFAULT_IPG2       4 /* For all modes */
#define DEFAULT_JAMSIZE    4 /* Toe jam */

/* Oh yes, the MIF BitBang is mighty fun to program.  BitBucket is more like it. */
#define BB_PUT_BIT(tregs, bit) \
do { (tregs)->bb_data = (bit); (tregs)->bb_clock = 0; (tregs)->bb_clock = 1; } while(0)

#define BB_GET_BIT(tregs, internal)              \
({                                               \
	(tregs)->bb_clock = 0;                   \
	(tregs)->bb_clock = 1;                   \
	if(internal)                             \
		((tregs)->cfg & TCV_CFG_MDIO0);  \
	else                                     \
		((tregs)->cfg & TCV_CFG_MDIO1);  \
})

#define BB_GET_BIT2(tregs, internal)                      \
({                                                        \
	int retval;                                       \
	(tregs)->bb_clock = 0;                            \
	udelay(1);                                        \
	if(internal)                                      \
		retval = ((tregs)->cfg & TCV_CFG_MDIO0);  \
	else                                              \
		retval = ((tregs)->cfg & TCV_CFG_MDIO1);  \
	(tregs)->bb_clock = 1;                            \
	retval;                                           \
})

#define TCVR_FAILURE      0x80000000     /* Impossible MIF read value */

static inline int happy_meal_bb_read(struct happy_meal *hp,
				     struct hmeal_tcvregs *tregs, int reg)
{
	volatile int unused;
	unsigned long tmp;
	int retval = 0;
	int i;

	ASD(("happy_meal_bb_read: reg=%d ", reg));

	/* Enable the MIF BitBang outputs. */
	tregs->bb_oenab = 1;

	/* Force BitBang into the idle state. */
	for(i = 0; i < 32; i++)
		BB_PUT_BIT(tregs, 1);

	/* Give it the read sequence. */
	BB_PUT_BIT(tregs, 0);
	BB_PUT_BIT(tregs, 1);
	BB_PUT_BIT(tregs, 1);
	BB_PUT_BIT(tregs, 0);

	/* Give it the PHY address. */
	tmp = hp->paddr & 0xff;
	for(i = 4; i >= 0; i--)
		BB_PUT_BIT(tregs, ((tmp >> i) & 1));

	/* Tell it what register we want to read. */
	tmp = (reg & 0xff);
	for(i = 4; i >= 0; i--)
		BB_PUT_BIT(tregs, ((tmp >> i) & 1));

	/* Close down the MIF BitBang outputs. */
	tregs->bb_oenab = 0;

	/* Now read in the value. */
	unused = BB_GET_BIT2(tregs, (hp->tcvr_type == internal));
	for(i = 15; i >= 0; i--)
		retval |= BB_GET_BIT2(tregs, (hp->tcvr_type == internal));
	unused = BB_GET_BIT2(tregs, (hp->tcvr_type == internal));
	unused = BB_GET_BIT2(tregs, (hp->tcvr_type == internal));
	unused = BB_GET_BIT2(tregs, (hp->tcvr_type == internal));
	ASD(("value=%x\n", retval));
	return retval;
}

static inline void happy_meal_bb_write(struct happy_meal *hp,
				       struct hmeal_tcvregs *tregs, int reg,
				       unsigned short value)
{
	unsigned long tmp;
	int i;

	ASD(("happy_meal_bb_write: reg=%d value=%x\n", reg, value));

	/* Enable the MIF BitBang outputs. */
	tregs->bb_oenab = 1;

	/* Force BitBang into the idle state. */
	for(i = 0; i < 32; i++)
		BB_PUT_BIT(tregs, 1);

	/* Give it write sequence. */
	BB_PUT_BIT(tregs, 0);
	BB_PUT_BIT(tregs, 1);
	BB_PUT_BIT(tregs, 0);
	BB_PUT_BIT(tregs, 1);

	/* Give it the PHY address. */
	tmp = (hp->paddr & 0xff);
	for(i = 4; i >= 0; i--)
		BB_PUT_BIT(tregs, ((tmp >> i) & 1));

	/* Tell it what register we will be writing. */
	tmp = (reg & 0xff);
	for(i = 4; i >= 0; i--)
		BB_PUT_BIT(tregs, ((tmp >> i) & 1));

	/* Tell it to become ready for the bits. */
	BB_PUT_BIT(tregs, 1);
	BB_PUT_BIT(tregs, 0);

	for(i = 15; i >= 0; i--)
		BB_PUT_BIT(tregs, ((value >> i) & 1));

	/* Close down the MIF BitBang outputs. */
	tregs->bb_oenab = 0;
}

#define TCVR_READ_TRIES   16

static inline int happy_meal_tcvr_read(struct happy_meal *hp,
				       struct hmeal_tcvregs *tregs, int reg)
{
	int tries = TCVR_READ_TRIES;
	int retval;

	ASD(("happy_meal_tcvr_read: reg=0x%02x ", reg));
	if(hp->tcvr_type == none) {
		ASD(("no transceiver, value=TCVR_FAILURE\n"));
		return TCVR_FAILURE;
	}

	if(!(hp->happy_flags & HFLAG_FENABLE)) {
		ASD(("doing bit bang\n"));
		return happy_meal_bb_read(hp, tregs, reg);
	}

	tregs->frame = (FRAME_READ | (hp->paddr << 23) | ((reg & 0xff) << 18));
	while(!(tregs->frame & 0x10000) && --tries)
		udelay(20);
	if(!tries) {
		printk("happy meal: Aieee, transceiver MIF read bolixed\n");
		return TCVR_FAILURE;
	}
	retval = tregs->frame & 0xffff;
	ASD(("value=%04x\n", retval));
	return retval;
}

#define TCVR_WRITE_TRIES  16

static inline void happy_meal_tcvr_write(struct happy_meal *hp,
					 struct hmeal_tcvregs *tregs, int reg,
					 unsigned short value)
{
	int tries = TCVR_WRITE_TRIES;
	
	ASD(("happy_meal_tcvr_write: reg=0x%02x value=%04x\n", reg, value));

	/* Welcome to Sun Microsystems, can I take your order please? */
	if(!hp->happy_flags & HFLAG_FENABLE)
		return happy_meal_bb_write(hp, tregs, reg, value);

	/* Would you like fries with that? */
	tregs->frame = (FRAME_WRITE | (hp->paddr << 23) |
			((reg & 0xff) << 18) | (value & 0xffff));
	while(!(tregs->frame & 0x10000) && --tries)
		udelay(20);

	/* Anything else? */
	if(!tries)
		printk("happy meal: Aieee, transceiver MIF write bolixed\n");

	/* Fifty-two cents is your change, have a nice day. */
}

/* Auto negotiation.  The scheme is very simple.  We have a timer routine
 * that keeps watching the auto negotiation process as it progresses.
 * The DP83840 is first told to start doing it's thing, we set up the time
 * and place the timer state machine in it's initial state.
 *
 * Here the timer peeks at the DP83840 status registers at each click to see
 * if the auto negotiation has completed, we assume here that the DP83840 PHY
 * will time out at some point and just tell us what (didn't) happen.  For
 * complete coverage we only allow so many of the ticks at this level to run,
 * when this has expired we print a warning message and try another strategy.
 * This "other" strategy is to force the interface into various speed/duplex
 * configurations and we stop when we see a link-up condition before the
 * maximum number of "peek" ticks have occurred.
 *
 * Once a valid link status has been detected we configure the BigMAC and
 * the rest of the Happy Meal to speak the most efficient protocol we could
 * get a clean link for.  The priority for link configurations, highest first
 * is:
 *                 100 Base-T Full Duplex
 *                 100 Base-T Half Duplex
 *                 10 Base-T Full Duplex
 *                 10 Base-T Half Duplex
 *
 * We start a new timer now, after a successful auto negotiation status has
 * been detected.  This timer just waits for the link-up bit to get set in
 * the BMCR of the DP83840.  When this occurs we print a kernel log message
 * describing the link type in use and the fact that it is up.
 *
 * If a fatal error of some sort is signalled and detected in the interrupt
 * service routine, and the chip is reset, or the link is ifconfig'd down
 * and then back up, this entire process repeats itself all over again.
 */
static int try_next_permutation(struct happy_meal *hp, struct hmeal_tcvregs *tregs)
{
	hp->sw_bmcr = happy_meal_tcvr_read(hp, tregs, DP83840_BMCR);

	/* Downgrade from 100 to 10. */
	if(hp->sw_bmcr & BMCR_SPEED100) {
		hp->sw_bmcr &= ~(BMCR_SPEED100);
		happy_meal_tcvr_write(hp, tregs, DP83840_BMCR, hp->sw_bmcr);
		return 0;
	}

	/* We've tried everything. */
	return -1;
}

static void display_link_mode(struct happy_meal *hp, struct hmeal_tcvregs *tregs)
{
	printk("%s: Link is up using ", hp->dev->name);
	if(hp->tcvr_type == external)
		printk("external ");
	else
		printk("internal ");
	printk("transceiver at ");
	hp->sw_lpa = happy_meal_tcvr_read(hp, tregs, DP83840_LPA);
	if(hp->sw_lpa & (LPA_100HALF | LPA_100FULL)) {
		if(hp->sw_lpa & LPA_100FULL)
			printk("100Mb/s, Full Duplex.\n");
		else
			printk("100Mb/s, Half Duplex.\n");
	} else {
		if(hp->sw_lpa & LPA_10FULL)
			printk("10Mb/s, Full Duplex.\n");
		else
			printk("10Mb/s, Half Duplex.\n");
	}
}

static void display_forced_link_mode(struct happy_meal *hp, struct hmeal_tcvregs *tregs)
{
	printk("%s: Link has been forced up using ", hp->dev->name);
	if(hp->tcvr_type == external)
		printk("external ");
	else
		printk("internal ");
	printk("transceiver at ");
	hp->sw_bmcr = happy_meal_tcvr_read(hp, tregs, DP83840_BMCR);
	if(hp->sw_bmcr & BMCR_SPEED100)
		printk("100Mb/s, ");
	else
		printk("10Mb/s, ");
	if(hp->sw_bmcr & BMCR_FULLDPLX)
		printk("Full Duplex.\n");
	else
		printk("Half Duplex.\n");
}

static int set_happy_link_modes(struct happy_meal *hp, struct hmeal_tcvregs *tregs)
{
	int full;

	/* All we care about is making sure the bigmac tx_cfg has a
	 * proper duplex setting.
	 */
	if(hp->timer_state == arbwait) {
		hp->sw_lpa = happy_meal_tcvr_read(hp, tregs, DP83840_LPA);
		if(!(hp->sw_lpa & (LPA_10HALF | LPA_10FULL | LPA_100HALF | LPA_100FULL)))
			goto no_response;
		if(hp->sw_lpa & LPA_100FULL)
			full = 1;
		else if(hp->sw_lpa & LPA_100HALF)
			full = 0;
		else if(hp->sw_lpa & LPA_10FULL)
			full = 1;
		else
			full = 0;
	} else {
		/* Forcing a link mode. */
		hp->sw_bmcr = happy_meal_tcvr_read(hp, tregs, DP83840_BMCR);
		if(hp->sw_bmcr & BMCR_FULLDPLX)
			full = 1;
		else
			full = 0;
	}

	/* XXX This may not be enough, we may need to reinit the entire
	 * XXX Happy Meal front end for this to work every time.
	 */
	if(full)
		hp->bigmacregs->tx_cfg |= BIGMAC_TXCFG_FULLDPLX;
	else
		hp->bigmacregs->tx_cfg &= ~(BIGMAC_TXCFG_FULLDPLX);

	return 0;
no_response:
	return 1;
}

static int happy_meal_init(struct happy_meal *hp, int from_irq);

static void happy_meal_timer(unsigned long data)
{
	struct happy_meal *hp = (struct happy_meal *) data;
	struct hmeal_tcvregs *tregs = hp->tcvregs;
	int restart_timer = 0;

	hp->timer_ticks++;
	switch(hp->timer_state) {
	case arbwait:
		/* Only allow for 5 ticks, thats 10 seconds and much too
		 * long to wait for arbitration to complete.
		 */
		if(hp->timer_ticks >= 10) {
			/* Enter force mode. */
	do_force_mode:
			hp->sw_bmcr = happy_meal_tcvr_read(hp, tregs, DP83840_BMCR);
			printk("%s: Auto-Negotiation unsuccessful, trying force link mode\n",
			       hp->dev->name);
			hp->sw_bmcr = BMCR_SPEED100;
			happy_meal_tcvr_write(hp, tregs, DP83840_BMCR, hp->sw_bmcr);

			/* OK, seems we need do disable the transceiver for the first
			 * tick to make sure we get an accurate link state at the
			 * second tick.
			 */
			hp->sw_csconfig = happy_meal_tcvr_read(hp, tregs, DP83840_CSCONFIG);
			hp->sw_csconfig &= ~(CSCONFIG_TCVDISAB);
			happy_meal_tcvr_write(hp, tregs, DP83840_CSCONFIG, hp->sw_csconfig);

			hp->timer_state = ltrywait;
			hp->timer_ticks = 0;
			restart_timer = 1;
		} else {
			/* Anything interesting happen? */
			hp->sw_bmsr = happy_meal_tcvr_read(hp, tregs, DP83840_BMSR);
			if(hp->sw_bmsr & BMSR_ANEGCOMPLETE) {
				int ret;

				/* Just what we've been waiting for... */
				ret = set_happy_link_modes(hp, tregs);
				if(ret) {
					/* Ooops, something bad happened, go to force
					 * mode.
					 *
					 * XXX Broken hubs which don't support 802.3u
					 * XXX auto-negotiation make this happen as well.
					 */
					goto do_force_mode;
				}

				/* Success, at least so far, advance our state engine. */
				hp->timer_state = lupwait;
				restart_timer = 1;
			} else {
				restart_timer = 1;
			}
		}
		break;

	case lupwait:
		/* Auto negotiation was successful and we are awaiting a
		 * link up status.  I have decided to let this timer run
		 * forever until some sort of error is signalled, reporting
		 * a message to the user at 10 second intervals.
		 */
		hp->sw_bmsr = happy_meal_tcvr_read(hp, tregs, DP83840_BMSR);
		if(hp->sw_bmsr & BMSR_LSTATUS) {
			/* Wheee, it's up, display the link mode in use and put
			 * the timer to sleep.
			 */
			display_link_mode(hp, tregs);
			hp->timer_state = asleep;
			restart_timer = 0;
		} else {
			if(hp->timer_ticks >= 10) {
				printk("%s: Auto negotiation successful, link still "
				       "not completely up.\n", hp->dev->name);
				hp->timer_ticks = 0;
				restart_timer = 1;
			} else {
				restart_timer = 1;
			}
		}
		break;

	case ltrywait:
		/* Making the timeout here too long can make it take
		 * annoyingly long to attempt all of the link mode
		 * permutations, but then again this is essentially
		 * error recovery code for the most part.
		 */
		hp->sw_bmsr = happy_meal_tcvr_read(hp, tregs, DP83840_BMSR);
		hp->sw_csconfig = happy_meal_tcvr_read(hp, tregs, DP83840_CSCONFIG);
		if(hp->timer_ticks == 1) {
			/* Re-enable transceiver, we'll re-enable the transceiver next
			 * tick, then check link state on the following tick. */
			hp->sw_csconfig |= CSCONFIG_TCVDISAB;
			happy_meal_tcvr_write(hp, tregs, DP83840_CSCONFIG, hp->sw_csconfig);
			restart_timer = 1;
			break;
		}
		if(hp->timer_ticks == 2) {
			hp->sw_csconfig &= ~(CSCONFIG_TCVDISAB);
			happy_meal_tcvr_write(hp, tregs, DP83840_CSCONFIG, hp->sw_csconfig);
			restart_timer = 1;
			break;
		}
		if(hp->sw_bmsr & BMSR_LSTATUS) {
			/* Force mode selection success. */
			display_forced_link_mode(hp, tregs);
			set_happy_link_modes(hp, tregs); /* XXX error? then what? */
			hp->timer_state = asleep;
			restart_timer = 0;
		} else {
			if(hp->timer_ticks >= 4) { /* 6 seconds or so... */
				int ret;

				ret = try_next_permutation(hp, tregs);
				if(ret == -1) {
					/* Aieee, tried them all, reset the
					 * chip and try all over again.
					 */

					/* Let the user know... */
					printk("%s: Link down, cable problem?\n",
					       hp->dev->name);

					ret = happy_meal_init(hp, 0);
					if(ret) {
						/* ho hum... */
						printk("%s: Error, cannot re-init the "
						       "Happy Meal.\n", hp->dev->name);
					}
					return;
				}
				hp->sw_csconfig = happy_meal_tcvr_read(hp, tregs, DP83840_CSCONFIG);
				hp->sw_csconfig |= CSCONFIG_TCVDISAB;
				happy_meal_tcvr_write(hp, tregs, DP83840_CSCONFIG, hp->sw_csconfig);
				hp->timer_ticks = 0;
				restart_timer = 1;
			} else {
				restart_timer = 1;
			}
		}
		break;

	case asleep:
	default:
		/* Can't happens.... */
		printk("%s: Aieee, link timer is asleep but we got one anyways!\n",
		       hp->dev->name);
		restart_timer = 0;
		hp->timer_ticks = 0;
		hp->timer_state = asleep; /* foo on you */
		break;
	};

	if(restart_timer) {
		hp->happy_timer.expires = jiffies + ((12 * HZ)/10); /* 1.2 sec. */
		add_timer(&hp->happy_timer);
	}
}

#define TX_RESET_TRIES     32
#define RX_RESET_TRIES     32

static inline void happy_meal_tx_reset(struct hmeal_bigmacregs *bregs)
{
	int tries = TX_RESET_TRIES;

	HMD(("happy_meal_tx_reset: reset, "));

	/* Would you like to try our SMCC Delux? */
	bregs->tx_swreset = 0;
	while((bregs->tx_swreset & 1) && --tries)
		udelay(20);

	/* Lettuce, tomato, buggy hardware (no extra charge)? */
	if(!tries)
		printk("happy meal: Transceiver BigMac ATTACK!");

	/* Take care. */
	HMD(("done\n"));
}

static inline void happy_meal_rx_reset(struct hmeal_bigmacregs *bregs)
{
	int tries = RX_RESET_TRIES;

	HMD(("happy_meal_rx_reset: reset, "));

	/* We have a special on GNU/Viking hardware bugs today. */
	bregs->rx_swreset = 0;
	while((bregs->rx_swreset & 1) && --tries)
		udelay(20);

	/* Will that be all? */
	if(!tries)
		printk("happy meal: Receiver BigMac ATTACK!");

	/* Don't forget your vik_1137125_wa.  Have a nice day. */
	HMD(("done\n"));
}

#define STOP_TRIES         16

static inline void happy_meal_stop(struct hmeal_gregs *gregs)
{
	int tries = STOP_TRIES;

	HMD(("happy_meal_stop: reset, "));

	/* We're consolidating our STB products, it's your lucky day. */
	gregs->sw_reset = GREG_RESET_ALL;
	while(gregs->sw_reset && --tries)
		udelay(20);

	/* Come back next week when we are "Sun Microelectronics". */
	if(!tries)
		printk("happy meal: Fry guys.");

	/* Remember: "Different name, same old buggy as shit hardware." */
	HMD(("done\n"));
}

static void happy_meal_get_counters(struct happy_meal *hp,
				    struct hmeal_bigmacregs *bregs)
{
	struct net_device_stats *stats = &hp->net_stats;

	stats->rx_crc_errors += bregs->rcrce_ctr;
	bregs->rcrce_ctr = 0;

	stats->rx_frame_errors += bregs->unale_ctr;
	bregs->unale_ctr = 0;

	stats->rx_length_errors += bregs->gle_ctr;
	bregs->gle_ctr = 0;

	stats->tx_aborted_errors += bregs->ex_ctr;

	stats->collisions += (bregs->ex_ctr + bregs->lt_ctr);
	bregs->ex_ctr = bregs->lt_ctr = 0;
}

static inline void happy_meal_poll_start(struct happy_meal *hp,
					 struct hmeal_tcvregs *tregs)
{
	unsigned long tmp;
	int speed;

	ASD(("happy_meal_poll_start: "));
	if(!(hp->happy_flags & HFLAG_POLLENABLE)) {
		HMD(("polling disabled, return\n"));
		return;
	}

	/* Start the MIF polling on the external transceiver. */
	ASD(("polling on, "));
	tmp = tregs->cfg;
	tmp &= ~(TCV_CFG_PDADDR | TCV_CFG_PREGADDR);
	tmp |= ((hp->paddr & 0x1f) << 10);
	tmp |= (TCV_PADDR_ETX << 3);
	tmp |= TCV_CFG_PENABLE;
	tregs->cfg = tmp;

	/* Let the bits set. */
	udelay(200);

	/* We are polling now. */
	ASD(("now polling, "));
	hp->happy_flags |= HFLAG_POLL;

	/* Clear the poll flags, get the basic status as of now. */
	hp->poll_flag = 0;
	hp->poll_data = tregs->status >> 16;

	if(hp->happy_flags & HFLAG_AUTO)
		speed = hp->auto_speed;
	else
		speed = hp->forced_speed;

	/* Listen only for the MIF interrupts we want to hear. */
	ASD(("mif ints on, "));
	if(speed == 100)
		tregs->int_mask = 0xfffb;
	else
		tregs->int_mask = 0xfff9;
	ASD(("done\n"));
}

static inline void happy_meal_poll_stop(struct happy_meal *hp,
					struct hmeal_tcvregs *tregs)
{
	ASD(("happy_meal_poll_stop: "));

	/* If polling disabled or not polling already, nothing to do. */
	if((hp->happy_flags & (HFLAG_POLLENABLE | HFLAG_POLL)) !=
	   (HFLAG_POLLENABLE | HFLAG_POLL)) {
		HMD(("not polling, return\n"));
		return;
	}

	/* Shut up the MIF. */
	ASD(("were polling, mif ints off, "));
	tregs->int_mask = 0xffff;

	/* Turn off polling. */
	ASD(("polling off, "));
	tregs->cfg &= ~(TCV_CFG_PENABLE);

	/* We are no longer polling. */
	hp->happy_flags &= ~(HFLAG_POLL);

	/* Let the bits set. */
	udelay(200);
	ASD(("done\n"));
}

/* Only Sun can take such nice parts and fuck up the programming interface
 * like this.  Good job guys...
 */
#define TCVR_RESET_TRIES       16 /* It should reset quickly        */
#define TCVR_UNISOLATE_TRIES   32 /* Dis-isolation can take longer. */

static int happy_meal_tcvr_reset(struct happy_meal *hp,
				 struct hmeal_tcvregs *tregs)
{
	unsigned long tconfig;
	int result, tries = TCVR_RESET_TRIES;

	tconfig = tregs->cfg;
	ASD(("happy_meal_tcvr_reset: tcfg<%08lx> ", tconfig));
	if(hp->tcvr_type == external) {
		ASD(("external<"));
		tregs->cfg = tconfig & ~(TCV_CFG_PSELECT);
		hp->tcvr_type = internal;
		hp->paddr = TCV_PADDR_ITX;
		ASD(("ISOLATE,"));
		happy_meal_tcvr_write(hp, tregs, DP83840_BMCR,
				      (BMCR_LOOPBACK|BMCR_PDOWN|BMCR_ISOLATE));
		result = happy_meal_tcvr_read(hp, tregs, DP83840_BMCR);
		if(result == TCVR_FAILURE) {
			ASD(("phyread_fail>\n"));
			return -1;
		}
		ASD(("phyread_ok,PSELECT>"));
		tregs->cfg = tconfig | TCV_CFG_PSELECT;
		hp->tcvr_type = external;
		hp->paddr = TCV_PADDR_ETX;
	} else {
		if(tconfig & TCV_CFG_MDIO1) {
			ASD(("internal<PSELECT,"));
			tregs->cfg = (tconfig | TCV_CFG_PSELECT);
			ASD(("ISOLATE,"));
			happy_meal_tcvr_write(hp, tregs, DP83840_BMCR,
					      (BMCR_LOOPBACK|BMCR_PDOWN|BMCR_ISOLATE));
			result = happy_meal_tcvr_read(hp, tregs, DP83840_BMCR);
			if(result == TCVR_FAILURE) {
				ASD(("phyread_fail>\n"));
				return -1;
			}
			ASD(("phyread_ok,~PSELECT>"));
			tregs->cfg = (tconfig & ~(TCV_CFG_PSELECT));
			hp->tcvr_type = internal;
			hp->paddr = TCV_PADDR_ITX;
		}
	}

	ASD(("BMCR_RESET "));
	happy_meal_tcvr_write(hp, tregs, DP83840_BMCR, BMCR_RESET);

	while(--tries) {
		result = happy_meal_tcvr_read(hp, tregs, DP83840_BMCR);
		if(result == TCVR_FAILURE)
			return -1;
		hp->sw_bmcr = result;
		if(!(result & BMCR_RESET))
			break;
		udelay(20);
	}
	if(!tries) {
		ASD(("BMCR RESET FAILED!\n"));
		return -1;
	}
	ASD(("RESET_OK\n"));

	/* Get fresh copies of the PHY registers. */
	hp->sw_bmsr      = happy_meal_tcvr_read(hp, tregs, DP83840_BMSR);
	hp->sw_physid1   = happy_meal_tcvr_read(hp, tregs, DP83840_PHYSID1);
	hp->sw_physid2   = happy_meal_tcvr_read(hp, tregs, DP83840_PHYSID2);
	hp->sw_advertise = happy_meal_tcvr_read(hp, tregs, DP83840_ADVERTISE);

	ASD(("UNISOLATE"));
	hp->sw_bmcr &= ~(BMCR_ISOLATE);
	happy_meal_tcvr_write(hp, tregs, DP83840_BMCR, hp->sw_bmcr);

	tries = TCVR_UNISOLATE_TRIES;
	while(--tries) {
		result = happy_meal_tcvr_read(hp, tregs, DP83840_BMCR);
		if(result == TCVR_FAILURE)
			return -1;
		if(!(result & BMCR_ISOLATE))
			break;
		udelay(20);
	}
	if(!tries) {
		ASD((" FAILED!\n"));
		return -1;
	}
	ASD((" SUCCESS and CSCONFIG_DFBYPASS\n"));
	result = happy_meal_tcvr_read(hp, tregs, DP83840_CSCONFIG);
	happy_meal_tcvr_write(hp, tregs, DP83840_CSCONFIG, (result | CSCONFIG_DFBYPASS));
	return 0;
}

/* Figure out whether we have an internal or external transceiver. */
static void happy_meal_transceiver_check(struct happy_meal *hp,
					 struct hmeal_tcvregs *tregs)
{
	unsigned long tconfig = tregs->cfg;

	ASD(("happy_meal_transceiver_check: tcfg=%08lx ", tconfig));
	if(hp->happy_flags & HFLAG_POLL) {
		/* If we are polling, we must stop to get the transceiver type. */
		ASD(("<polling> "));
		if(hp->tcvr_type == internal) {
			if(tconfig & TCV_CFG_MDIO1) {
				ASD(("<internal> <poll stop> "));
				happy_meal_poll_stop(hp, tregs);
				hp->paddr = TCV_PADDR_ETX;
				hp->tcvr_type = external;
				ASD(("<external>\n"));
				tconfig &= ~(TCV_CFG_PENABLE);
				tconfig |= TCV_CFG_PSELECT;
				tregs->cfg = tconfig;
			}
		} else {
			if(hp->tcvr_type == external) {
				ASD(("<external> "));
				if(!(tregs->status >> 16)) {
					ASD(("<poll stop> "));
					happy_meal_poll_stop(hp, tregs);
					hp->paddr = TCV_PADDR_ITX;
					hp->tcvr_type = internal;
					ASD(("<internal>\n"));
					tregs->cfg &= ~(TCV_CFG_PSELECT);
				}
				ASD(("\n"));
			} else {
				ASD(("<none>\n"));
			}
		}
	} else {
		unsigned long reread = tregs->cfg;

		/* Else we can just work off of the MDIO bits. */
		ASD(("<not polling> "));
		if(reread & TCV_CFG_MDIO1) {
			tregs->cfg = tconfig | TCV_CFG_PSELECT;
			hp->paddr = TCV_PADDR_ETX;
			hp->tcvr_type = external;
			ASD(("<external>\n"));
		} else {
			if(reread & TCV_CFG_MDIO0) {
				tregs->cfg = tconfig & ~(TCV_CFG_PSELECT);
				hp->paddr = TCV_PADDR_ITX;
				hp->tcvr_type = internal;
				ASD(("<internal>\n"));
			} else {
				printk("happy meal: Transceiver and a coke please.");
				hp->tcvr_type = none; /* Grrr... */
				ASD(("<none>\n"));
			}
		}
	}
}

/* The receive ring buffers are a bit tricky to get right.  Here goes...
 *
 * The buffers we dma into must be 64 byte aligned.  So we use a special
 * alloc_skb() routine for the happy meal to allocate 64 bytes more than
 * we really need.
 *
 * We use skb_reserve() to align the data block we get in the skb.  We
 * also program the etxregs->cfg register to use an offset of 2.  This
 * imperical constant plus the ethernet header size will always leave
 * us with a nicely aligned ip header once we pass things up to the
 * protocol layers.
 *
 * The numbers work out to:
 *
 *         Max ethernet frame size         1518
 *         Ethernet header size              14
 *         Happy Meal base offset             2
 *
 * Say a skb data area is at 0xf001b010, and its size alloced is
 * (ETH_FRAME_LEN + 64 + 2) = (1514 + 64 + 2) = 1580 bytes.
 *
 * First our alloc_skb() routine aligns the data base to a 64 byte
 * boundry.  We now have 0xf001b040 as our skb data address.  We
 * plug this into the receive descriptor address.
 *
 * Next, we skb_reserve() 2 bytes to account for the Happy Meal offset.
 * So now the data we will end up looking at starts at 0xf001b042.  When
 * the packet arrives, we will check out the size received and subtract
 * this from the skb->length.  Then we just pass the packet up to the
 * protocols as is, and allocate a new skb to replace this slot we have
 * just received from.
 *
 * The ethernet layer will strip the ether header from the front of the
 * skb we just sent to it, this leaves us with the ip header sitting
 * nicely aligned at 0xf001b050.  Also, for tcp and udp packets the
 * Happy Meal has even checksummed the tcp/udp data for us.  The 16
 * bit checksum is obtained from the low bits of the receive descriptor
 * flags, thus:
 *
 * 	skb->csum = rxd->rx_flags & 0xffff;
 * 	skb->ip_summed = CHECKSUM_HW;
 *
 * before sending off the skb to the protocols, and we are good as gold.
 */
static inline void happy_meal_clean_rings(struct happy_meal *hp)
{
	int i;

	for(i = 0; i < RX_RING_SIZE; i++) {
		if(hp->rx_skbs[i] != NULL) {
			dev_kfree_skb(hp->rx_skbs[i], FREE_READ);
			hp->rx_skbs[i] = NULL;
		}
	}

	for(i = 0; i < TX_RING_SIZE; i++) {
		if(hp->tx_skbs[i] != NULL) {
			dev_kfree_skb(hp->tx_skbs[i], FREE_WRITE);
			hp->tx_skbs[i] = NULL;
		}
	}
}

static void happy_meal_init_rings(struct happy_meal *hp, int from_irq)
{
	struct hmeal_init_block *hb = hp->happy_block;
	struct device *dev = hp->dev;
	int i, gfp_flags = GFP_KERNEL;

	if(from_irq || in_interrupt())
		gfp_flags = GFP_ATOMIC;

	HMD(("happy_meal_init_rings: counters to zero, "));
	hp->rx_new = hp->rx_old = hp->tx_new = hp->tx_old = 0;

	/* Free any skippy bufs left around in the rings. */
	HMD(("clean, "));
	happy_meal_clean_rings(hp);

	/* Now get new skippy bufs for the receive ring. */
	HMD(("init rxring, "));
	for(i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb;

		skb = happy_meal_alloc_skb(RX_BUF_ALLOC_SIZE, gfp_flags);
		if(!skb)
			continue;
		hp->rx_skbs[i] = skb;
		skb->dev = dev;

		/* Because we reserve afterwards. */
		skb_put(skb, (ETH_FRAME_LEN + RX_OFFSET));

		hb->happy_meal_rxd[i].rx_addr =
			(u32) ((unsigned long)skb->data);
		skb_reserve(skb, RX_OFFSET);
		hb->happy_meal_rxd[i].rx_flags =
			(RXFLAG_OWN | ((RX_BUF_ALLOC_SIZE - RX_OFFSET) << 16));
	}

	HMD(("init txring, "));
	for(i = 0; i < TX_RING_SIZE; i++)
		hb->happy_meal_txd[i].tx_flags = 0;
	HMD(("done\n"));
}

static void sun4c_happy_meal_init_rings(struct happy_meal *hp)
{
	struct hmeal_init_block *hb = hp->happy_block;
	__u32 hbufs = hp->s4c_buf_dvma;
	int i;

	HMD(("happy_meal_init_rings: counters to zero, "));
	hp->rx_new = hp->rx_old = hp->tx_new = hp->tx_old = 0;

	HMD(("init rxring, "));
	for(i = 0; i < RX_RING_SIZE; i++) {
		hb->happy_meal_rxd[i].rx_addr = hbufs + hbuf_offset(rx_buf, i);
		hb->happy_meal_rxd[i].rx_flags =
			(RXFLAG_OWN | ((SUN4C_RX_BUFF_SIZE - RX_OFFSET) << 16));
	}

	HMD(("init txring, "));
	for(i = 0; i < TX_RING_SIZE; i++)
		hb->happy_meal_txd[i].tx_flags = 0;
	HMD(("done\n"));
}

static void happy_meal_begin_auto_negotiation(struct happy_meal *hp,
					      struct hmeal_tcvregs *tregs)
{
	int timeout;

	/* Read all of the registers we are interested in now. */
	hp->sw_bmsr      = happy_meal_tcvr_read(hp, tregs, DP83840_BMSR);
	hp->sw_bmcr      = happy_meal_tcvr_read(hp, tregs, DP83840_BMCR);
	hp->sw_physid1   = happy_meal_tcvr_read(hp, tregs, DP83840_PHYSID1);
	hp->sw_physid2   = happy_meal_tcvr_read(hp, tregs, DP83840_PHYSID2);
	hp->sw_advertise = happy_meal_tcvr_read(hp, tregs, DP83840_ADVERTISE);

	/* XXX Check BMSR_ANEGCAPABLE, should not be necessary though. */

	/* Advertise everything we can support. */
	if(hp->sw_bmsr & BMSR_10HALF)
		hp->sw_advertise |= (ADVERTISE_10HALF);
	else
		hp->sw_advertise &= ~(ADVERTISE_10HALF);

	if(hp->sw_bmsr & BMSR_10FULL)
		hp->sw_advertise |= (ADVERTISE_10FULL);
	else
		hp->sw_advertise &= ~(ADVERTISE_10FULL);
	if(hp->sw_bmsr & BMSR_100HALF)
		hp->sw_advertise |= (ADVERTISE_100HALF);
	else
		hp->sw_advertise &= ~(ADVERTISE_100HALF);
	if(hp->sw_bmsr & BMSR_100FULL)
		hp->sw_advertise |= (ADVERTISE_100FULL);
	else
		hp->sw_advertise &= ~(ADVERTISE_100FULL);

	/* XXX Currently no Happy Meal cards I know off support 100BaseT4,
	 * XXX and this is because the DP83840 does not support it, changes
	 * XXX would need to be made to the tx/rx logic in the driver as well
	 * XXX so I completely skip checking for it in the BMSR for now.
	 */

#ifdef AUTO_SWITCH_DEBUG
	ASD(("%s: Advertising [ ", hp->dev->name));
	if(hp->sw_advertise & ADVERTISE_10HALF)
		ASD(("10H "));
	if(hp->sw_advertise & ADVERTISE_10FULL)
		ASD(("10F "));
	if(hp->sw_advertise & ADVERTISE_100HALF)
		ASD(("100H "));
	if(hp->sw_advertise & ADVERTISE_100FULL)
		ASD(("100F "));
#endif

	happy_meal_tcvr_write(hp, tregs, DP83840_ADVERTISE, hp->sw_advertise);

	/* Enable Auto-Negotiation, this is usually on already... */
	hp->sw_bmcr |= BMCR_ANENABLE;
	happy_meal_tcvr_write(hp, tregs, DP83840_BMCR, hp->sw_bmcr);

	/* Restart it to make sure it is going. */
	hp->sw_bmcr |= BMCR_ANRESTART;
	happy_meal_tcvr_write(hp, tregs, DP83840_BMCR, hp->sw_bmcr);

	/* BMCR_ANRESTART self clears when the process has begun. */

	timeout = 64;  /* More than enough. */
	while(--timeout) {
		hp->sw_bmcr = happy_meal_tcvr_read(hp, tregs, DP83840_BMCR);
		if(!(hp->sw_bmcr & BMCR_ANRESTART))
			break; /* got it. */
		udelay(10);
	}
	if(!timeout) {
		printk("%s: Happy Meal would not start auto negotiation BMCR=0x%04x\n",
		       hp->dev->name, hp->sw_bmcr);
		printk("%s: Performing force link detection.\n", hp->dev->name);

		/* Disable auto-negotiation in BMCR, enable FULL duplex and 100mb/s,
		 * setup the timer state machine, and fire it off.
		 *
		 * XXX Should probably reset the DP83840 first
		 * XXX as this is a gross fatal error...
		 */
		hp->sw_bmcr = BMCR_SPEED100;
		happy_meal_tcvr_write(hp, tregs, DP83840_BMCR, hp->sw_bmcr);

		/* OK, seems we need do disable the transceiver for the first
		 * tick to make sure we get an accurate link state at the
		 * second tick.
		 */
		hp->sw_csconfig = happy_meal_tcvr_read(hp, tregs, DP83840_CSCONFIG);
		printk("%s: CSCONFIG [%04x], disabling transceiver\n", hp->dev->name,
		       hp->sw_csconfig);
		hp->sw_csconfig &= ~(CSCONFIG_TCVDISAB);
		happy_meal_tcvr_write(hp, tregs, DP83840_CSCONFIG, hp->sw_csconfig);

		hp->timer_state = ltrywait;
	} else {
		hp->timer_state = arbwait;
	}

	hp->timer_ticks = 0;
	hp->happy_timer.expires = jiffies + (12 * HZ)/10;  /* 1.2 sec. */
	hp->happy_timer.data = (unsigned long) hp;
	hp->happy_timer.function = &happy_meal_timer;
	add_timer(&hp->happy_timer);
}

static int happy_meal_init(struct happy_meal *hp, int from_irq)
{
	struct hmeal_gregs   *gregs        = hp->gregs;
	struct hmeal_etxregs *etxregs      = hp->etxregs;
	struct hmeal_erxregs *erxregs      = hp->erxregs;
	struct hmeal_bigmacregs *bregs     = hp->bigmacregs;
	struct hmeal_tcvregs *tregs        = hp->tcvregs;
	unsigned long regtmp;
	unsigned char *e = &hp->dev->dev_addr[0];

	HMD(("happy_meal_init: "));
	if(!(hp->happy_flags & HFLAG_INIT)) {
		HMD(("set HFLAG_INIT, "));
		hp->happy_flags |= HFLAG_INIT;
		happy_meal_get_counters(hp, bregs);
	}

	/* Stop polling. */
	HMD(("to happy_meal_poll_stop\n"));
	happy_meal_poll_stop(hp, tregs);

	/* Stop transmitter and receiver. */
	HMD(("happy_meal_init: to happy_meal_stop\n"));
	happy_meal_stop(gregs);

	/* Alloc and reset the tx/rx descriptor chains. */
	HMD(("happy_meal_init: to happy_meal_init_rings\n"));
	if(sparc_cpu_model == sun4c)
		sun4c_happy_meal_init_rings(hp);
	else
		happy_meal_init_rings(hp, from_irq);

	/* Shut up the MIF. */
	HMD(("happy_meal_init: Disable all MIF irqs, "));
	tregs->int_mask = 0xffff;

	/* See if we can enable the MIF frame on this card to speak to the DP83840. */
	if(hp->happy_flags & HFLAG_FENABLE) {
		HMD(("use frame, "));
		tregs->cfg &= ~(TCV_CFG_BENABLE);
	} else {
		HMD(("use bitbang, "));
		tregs->cfg |= TCV_CFG_BENABLE;
	}

	/* Check the state of the transceiver. */
	HMD(("to happy_meal_transceiver_check\n"));
	happy_meal_transceiver_check(hp, tregs);

	/* Put the Big Mac into a sane state. */
	HMD(("happy_meal_init: "));
	switch(hp->tcvr_type) {
	case none:
		/* Cannot operate if we don't know the transceiver type! */
		HMD(("AAIEEE no transceiver type, EAGAIN"));
		return -EAGAIN;

	case internal:
		/* Using the MII buffers. */
		HMD(("internal, using MII, "));
		bregs->xif_cfg = 0;
		break;

	case external:
		/* Not using the MII, disable it. */
		HMD(("external, disable MII, "));
		bregs->xif_cfg = BIGMAC_XCFG_MIIDISAB;
		break;
	};

	if(happy_meal_tcvr_reset(hp, tregs))
		return -EAGAIN;

	/* Reset the Happy Meal Big Mac transceiver and the receiver. */
	HMD(("tx/rx reset, "));
	happy_meal_tx_reset(bregs);
	happy_meal_rx_reset(bregs);

	/* Set jam size and inter-packet gaps to reasonable defaults. */
	HMD(("jsize/ipg1/ipg2, "));
	bregs->jsize     = DEFAULT_JAMSIZE;
	bregs->ipkt_gap1 = DEFAULT_IPG1;
	bregs->ipkt_gap2 = DEFAULT_IPG2;

	/* Load up the MAC address and random seed. */
	HMD(("rseed/macaddr, "));

	/* XXX use something less deterministic... */
	bregs->rand_seed = 0xbd;

	bregs->mac_addr2 = ((e[4] << 8) | e[5]);
	bregs->mac_addr1 = ((e[2] << 8) | e[3]);
	bregs->mac_addr0 = ((e[0] << 8) | e[1]);

	/* Ick, figure out how to properly program the hash table later... */
	HMD(("htable, "));
	bregs->htable3 = 0;
	bregs->htable2 = 0;
	bregs->htable1 = 0;
	bregs->htable0 = 0;

	/* Set the RX and TX ring ptrs. */
	HMD(("ring ptrs\n"));
	erxregs->rx_ring = hp->hblock_dvma + hblock_offset(happy_meal_rxd, 0);
	etxregs->tx_ring = hp->hblock_dvma + hblock_offset(happy_meal_txd, 0);

	/* Set the supported SBUS burst sizes. */
	HMD(("happy_meal_init: bursts<"));

#if 0 /* XXX take down huahaga and debug this... */
	/* Grrr... */
	if(hp->happy_bursts & DMA_BURST64) {
		HMD(("64>"));
		gregs->cfg = GREG_CFG_BURST64;
	} else
#endif
	if(hp->happy_bursts & DMA_BURST32) {
		HMD(("32>"));
		gregs->cfg = GREG_CFG_BURST32;
	} else if(hp->happy_bursts & DMA_BURST16) {
		HMD(("16>"));
		gregs->cfg = GREG_CFG_BURST16;
	} else {
		HMD(("XXX>"));
		gregs->cfg = 0;
	}

	/* Turn off interrupts we do not want to hear. */
	HMD((", enable global interrupts, "));
	gregs->imask = (GREG_IMASK_GOTFRAME | GREG_IMASK_RCNTEXP |
			GREG_IMASK_SENTFRAME | GREG_IMASK_TXPERR);

	/* Set the transmit ring buffer size. */
	HMD(("tx rsize=%d, ", (int)TX_RING_SIZE));
	etxregs->tx_rsize = (TX_RING_SIZE >> ETX_RSIZE_SHIFT) - 1;

	/* Enable transmitter DVMA. */
	HMD(("tx dma enable, "));
	etxregs->cfg |= ETX_CFG_DMAENABLE;

	/* This chip really rots, for the receiver sometimes when you
	 * write to it's control registers not all the bits get there
	 * properly.  I cannot think of a sane way to provide complete
	 * coverage for this hardware bug yet.
	 */
	HMD(("erx regs bug\n"));
	erxregs->cfg = ERX_CFG_DEFAULT(RX_OFFSET);
	regtmp = erxregs->cfg;
	erxregs->cfg = ERX_CFG_DEFAULT(RX_OFFSET);
	if(erxregs->cfg != ERX_CFG_DEFAULT(RX_OFFSET)) {
		printk("happy meal: Eieee, rx config register gets greasy fries.\n");
		printk("happy meal: Trying to set %08x, reread gives %08lx\n",
		       ERX_CFG_DEFAULT(RX_OFFSET), regtmp);
		/* XXX Should return failure here... */
	}

	/* Enable Big Mac hash table filter. */
	HMD(("happy_meal_init: enable hash, "));
	bregs->rx_cfg = BIGMAC_RXCFG_HENABLE;

	/* Let the bits settle in the chip. */
	udelay(10);

	/* Ok, configure the Big Mac transmitter. */
	HMD(("BIGMAC init, "));
	regtmp = 0;
	if(hp->happy_flags & HFLAG_FULL)
		regtmp |= BIGMAC_TXCFG_FULLDPLX;
	bregs->tx_cfg = regtmp | BIGMAC_TXCFG_DGIVEUP;

	/* Enable the output drivers no matter what. */
	regtmp = BIGMAC_XCFG_ODENABLE;

	/* If card can do lance mode, enable it. */
	if(hp->happy_flags & HFLAG_LANCE)
		regtmp |= (DEFAULT_IPG0 << 5) | BIGMAC_XCFG_LANCE;

	/* Disable the MII buffers if using external transceiver. */
	if(hp->tcvr_type == external)
		regtmp |= BIGMAC_XCFG_MIIDISAB;

	HMD(("XIF config, "));
	bregs->xif_cfg = regtmp;

	/* Start things up. */
	HMD(("tx and rx ON!\n"));
	bregs->tx_cfg |= BIGMAC_TXCFG_ENABLE;
	bregs->rx_cfg |= BIGMAC_RXCFG_ENABLE;

	/* Get the autonegotiation started, and the watch timer ticking. */
	happy_meal_begin_auto_negotiation(hp, tregs);

	/* Success. */
	return 0;
}

static void happy_meal_set_initial_advertisement(struct happy_meal *hp)
{
	struct hmeal_tcvregs *tregs	= hp->tcvregs;
	struct hmeal_bigmacregs *bregs	= hp->bigmacregs;
	struct hmeal_gregs *gregs	= hp->gregs;

	happy_meal_stop(gregs);
	tregs->int_mask = 0xffff;
	if(hp->happy_flags & HFLAG_FENABLE)
		tregs->cfg &= ~(TCV_CFG_BENABLE);
	else
		tregs->cfg |= TCV_CFG_BENABLE;
	happy_meal_transceiver_check(hp, tregs);
	switch(hp->tcvr_type) {
	case none:
		return;
	case internal:
		bregs->xif_cfg = 0;
		break;
	case external:
		bregs->xif_cfg = BIGMAC_XCFG_MIIDISAB;
		break;
	};
	if(happy_meal_tcvr_reset(hp, tregs))
		return;

	/* Latch PHY registers as of now. */
	hp->sw_bmsr      = happy_meal_tcvr_read(hp, tregs, DP83840_BMSR);
	hp->sw_advertise = happy_meal_tcvr_read(hp, tregs, DP83840_ADVERTISE);

	/* Advertise everything we can support. */
	if(hp->sw_bmsr & BMSR_10HALF)
		hp->sw_advertise |= (ADVERTISE_10HALF);
	else
		hp->sw_advertise &= ~(ADVERTISE_10HALF);

	if(hp->sw_bmsr & BMSR_10FULL)
		hp->sw_advertise |= (ADVERTISE_10FULL);
	else
		hp->sw_advertise &= ~(ADVERTISE_10FULL);
	if(hp->sw_bmsr & BMSR_100HALF)
		hp->sw_advertise |= (ADVERTISE_100HALF);
	else
		hp->sw_advertise &= ~(ADVERTISE_100HALF);
	if(hp->sw_bmsr & BMSR_100FULL)
		hp->sw_advertise |= (ADVERTISE_100FULL);
	else
		hp->sw_advertise &= ~(ADVERTISE_100FULL);

	/* Update the PHY advertisement register. */
	happy_meal_tcvr_write(hp, tregs, DP83840_ADVERTISE, hp->sw_advertise);
}

/* Once status is latched (by happy_meal_interrupt) it is cleared by
 * the hardware, so we cannot re-read it and get a correct value.
 */
static int happy_meal_is_not_so_happy(struct happy_meal *hp,
				      struct hmeal_gregs *gregs,
				      unsigned long status)
{
	int reset = 0;

	printk("%s: Error interrupt for happy meal, status = %08lx\n",
	       hp->dev->name, status);

	if(status &
	   (GREG_STAT_RCNTEXP|GREG_STAT_ACNTEXP|GREG_STAT_CCNTEXP|GREG_STAT_LCNTEXP)) {
		/* Some stupid counter expired, we should be not
		 * have interrupts for this enabled, but we check
		 * for it anyways.
		 */

		printk("%s: Happy Meal counters expired [ ", hp->dev->name);
		if(status & GREG_STAT_RCNTEXP)
			printk("ReceiveFrame ");
		if(status & GREG_STAT_ACNTEXP)
			printk("AlignError ");
		if(status & GREG_STAT_CCNTEXP)
			printk("CrcError ");
		if(status & GREG_STAT_LCNTEXP)
			printk("LengthError ");
		printk("]\n");
	}

	if(status & GREG_STAT_RFIFOVF) {
		/* The receive FIFO overflowwed, usually a DMA error. */
		printk("%s: Happy Meal receive FIFO overflow.\n", hp->dev->name);
		reset = 1;
	}

	if(status & GREG_STAT_CVCNTEXP) {
		/* See above about counter expiration... */
		printk("%s: Code Violation error counter expired.\n", hp->dev->name);
	}

	if(status & GREG_STAT_STSTERR) {
		/* BigMAC SQE link test failed. */
		printk("%s: Happy Meal BigMAC SQE test failed.\n", hp->dev->name);
		reset = 1;
	}

	if(status & GREG_STAT_TFIFO_UND) {
		/* Transmit FIFO underrun, again DMA error likely. */
		printk("%s: Happy Meal transmitter FIFO underrun, DMA error.\n",
		       hp->dev->name);
		reset = 1;
	}

	if(status & GREG_STAT_MAXPKTERR) {
		/* Driver error, tried to transmit something larger
		 * than ethernet max mtu.
		 */
		printk("%s: Happy Meal MAX Packet size error.\n", hp->dev->name);
		reset = 1;
	}

	if(status & (GREG_STAT_NCNTEXP|GREG_STAT_ECNTEXP|GREG_STAT_LCCNTEXP|
		     GREG_STAT_FCNTEXP)) {
		/* More stupid error counters... */
		printk("%s: Happy Meal counters expired [ ", hp->dev->name);
		if(status & GREG_STAT_NCNTEXP)
			printk("NormalCollision ");
		if(status & GREG_STAT_ECNTEXP)
			printk("ExcessCollision ");
		if(status & GREG_STAT_LCCNTEXP)
			printk("LateCollision ");
		if(status & GREG_STAT_FCNTEXP)
			printk("FirstCollision ");
		printk("]\n");
	}

	if(status & GREG_STAT_DTIMEXP) {
		/* Defer-timer expired.  Probably means the happy meal needed
		 * to back off too much before it could transmit one frame.
		 */
#if 0		/* XXX This isn't worth reporting and is in fact a normal condition. */
		printk("%s: Transmit defer timer expired, subnet congested?\n",
		       hp->dev->name);
		reset = 1;
#endif
	}

	if(status & GREG_STAT_NORXD) {
		/* AIEEE, out of receive descriptors.  Check out our drop
		 * processing in happy_meal_rx to see how we try very hard
		 * to avoid this situation.
		 */
		printk("%s: Happy Meal out of receive descriptors, aieee!\n",
		       hp->dev->name);
		reset = 1;
	}

	if(status & (GREG_STAT_RXERR|GREG_STAT_RXPERR|GREG_STAT_RXTERR)) {
		/* All sorts of DMA receive errors. */
		printk("%s: Happy Meal rx DMA errors [ ", hp->dev->name);
		if(status & GREG_STAT_RXERR)
			printk("GenericError ");
		if(status & GREG_STAT_RXPERR)
			printk("ParityError ");
		if(status & GREG_STAT_RXTERR)
			printk("RxTagBotch ");
		printk("]\n");
		reset = 1;
	}

	if(status & GREG_STAT_EOPERR) {
		/* Driver bug, didn't set EOP bit in tx descriptor given
		 * to the happy meal.
		 */
		printk("%s: EOP not set in happy meal transmit descriptor!\n",
		       hp->dev->name);
		reset = 1;
	}

	if(status & GREG_STAT_MIFIRQ) {
		/* MIF signalled an interrupt, were we polling it? */
		printk("%s: Happy Meal MIF interrupt.\n", hp->dev->name);
	}

	if(status &
	   (GREG_STAT_TXEACK|GREG_STAT_TXLERR|GREG_STAT_TXPERR|GREG_STAT_TXTERR)) {
		/* All sorts of transmit DMA errors. */
		printk("%s: Happy Meal tx DMA errors [ ", hp->dev->name);
		if(status & GREG_STAT_TXEACK)
			printk("GenericError ");
		if(status & GREG_STAT_TXLERR)
			printk("LateError ");
		if(status & GREG_STAT_TXPERR)
			printk("ParityErro ");
		if(status & GREG_STAT_TXTERR)
			printk("TagBotch ");
		printk("]\n");
		reset = 1;
	}

	if(status & (GREG_STAT_SLVERR|GREG_STAT_SLVPERR)) {
		/* Bus or parity error when cpu accessed happy meal registers
		 * or it's internal FIFO's.  Should never see this.
		 */
		printk("%s: Happy Meal register access SBUS slave (%s) error.\n",
		       hp->dev->name,
		       (status & GREG_STAT_SLVPERR) ? "parity" : "generic");
		reset = 1;
	}

	if(reset) {
		printk("%s: Resetting...\n", hp->dev->name);
		happy_meal_init(hp, 1);
		return 1;
	}
	return 0;
}

static inline void happy_meal_mif_interrupt(struct happy_meal *hp,
					    struct hmeal_gregs *gregs,
					    struct hmeal_tcvregs *tregs)
{
	printk("%s: Link status change.\n", hp->dev->name);
	hp->sw_bmcr = happy_meal_tcvr_read(hp, tregs, DP83840_BMCR);
	hp->sw_lpa = happy_meal_tcvr_read(hp, tregs, DP83840_LPA);

	/* Use the fastest transmission protocol possible. */
	if(hp->sw_lpa & LPA_100FULL) {
		printk("%s: Switching to 100Mbps at full duplex.", hp->dev->name);
		hp->sw_bmcr |= (BMCR_FULLDPLX | BMCR_SPEED100);
	} else if(hp->sw_lpa & LPA_100HALF) {
		printk("%s: Switching to 100MBps at half duplex.", hp->dev->name);
		hp->sw_bmcr |= BMCR_SPEED100;
	} else if(hp->sw_lpa & LPA_10FULL) {
		printk("%s: Switching to 10MBps at full duplex.", hp->dev->name);
		hp->sw_bmcr |= BMCR_FULLDPLX;
	} else {
		printk("%s: Using 10Mbps at half duplex.", hp->dev->name);
	}
	happy_meal_tcvr_write(hp, tregs, DP83840_BMCR, hp->sw_bmcr);

	/* Finally stop polling and shut up the MIF. */
	happy_meal_poll_stop(hp, tregs);
}

/* #define TXD(x) printk x */
#define TXD(x)

static inline void happy_meal_tx(struct happy_meal *hp)
{
	struct happy_meal_txd *txbase = &hp->happy_block->happy_meal_txd[0];
	struct happy_meal_txd *this;
	int elem = hp->tx_old;

	TXD(("TX<"));
	while(elem != hp->tx_new) {
		struct sk_buff *skb;

		TXD(("[%d]", elem));
		this = &txbase[elem];
		if(this->tx_flags & TXFLAG_OWN)
			break;
		skb = hp->tx_skbs[elem];
		hp->tx_skbs[elem] = NULL;
		hp->net_stats.tx_bytes+=skb->len;
		
		dev_kfree_skb(skb, FREE_WRITE);

		hp->net_stats.tx_packets++;
		elem = NEXT_TX(elem);
	}
	hp->tx_old = elem;
	TXD((">"));
}

static inline void sun4c_happy_meal_tx(struct happy_meal *hp)
{
	struct happy_meal_txd *txbase = &hp->happy_block->happy_meal_txd[0];
	struct happy_meal_txd *this;
	int elem = hp->tx_old;

	TXD(("TX<"));
	while(elem != hp->tx_new) {
		TXD(("[%d]", elem));

		this = &txbase[elem];

		if(this->tx_flags & TXFLAG_OWN)
			break;

		hp->net_stats.tx_packets++;
		elem = NEXT_TX(elem);
	}
	hp->tx_old = elem;
	TXD((">"));
}

/* #define RXD(x) printk x */
#define RXD(x)

/* Originally I use to handle the allocation failure by just giving back just
 * that one ring buffer to the happy meal.  Problem is that usually when that
 * condition is triggered, the happy meal expects you to do something reasonable
 * with all of the packets it has DMA'd in.  So now I just drop the entire
 * ring when we cannot get a new skb and give them all back to the happy meal,
 * maybe things will be "happier" now.
 */
static inline void happy_meal_rx(struct happy_meal *hp, struct device *dev,
				 struct hmeal_gregs *gregs)
{
	struct happy_meal_rxd *rxbase = &hp->happy_block->happy_meal_rxd[0];
	struct happy_meal_rxd *this;
	int elem = hp->rx_new, drops = 0;

	RXD(("RX<"));
	this = &rxbase[elem];
	while(!(this->rx_flags & RXFLAG_OWN)) {
		struct sk_buff *skb;
		unsigned int flags = this->rx_flags;
		int len = flags >> 16;
		u16 csum = flags & RXFLAG_CSUM;

		RXD(("[%d ", elem));

		/* Check for errors. */
		if((len < ETH_ZLEN) || (flags & RXFLAG_OVERFLOW)) {
			RXD(("ERR(%08lx)]", flags));
			hp->net_stats.rx_errors++;
			if(len < ETH_ZLEN)
				hp->net_stats.rx_length_errors++;
			if(len & (RXFLAG_OVERFLOW >> 16)) {
				hp->net_stats.rx_over_errors++;
				hp->net_stats.rx_fifo_errors++;
			}

			/* Return it to the Happy meal. */
	drop_it:
			hp->net_stats.rx_dropped++;
			this->rx_addr =
				(u32) ((unsigned long)hp->rx_skbs[elem]->data);
			this->rx_flags =
				(RXFLAG_OWN | ((RX_BUF_ALLOC_SIZE - RX_OFFSET) << 16));
			goto next;
		}
		skb = hp->rx_skbs[elem];
		if(len > RX_COPY_THRESHOLD) {
			struct sk_buff *new_skb;

			/* Now refill the entry, if we can. */
			new_skb = happy_meal_alloc_skb(RX_BUF_ALLOC_SIZE, GFP_ATOMIC);
			if(!new_skb) {
				drops++;
				goto drop_it;
			}

			hp->rx_skbs[elem] = new_skb;
			new_skb->dev = dev;
			skb_put(new_skb, (ETH_FRAME_LEN + RX_OFFSET));
			rxbase[elem].rx_addr =
				(u32) ((unsigned long)new_skb->data);
			skb_reserve(new_skb, RX_OFFSET);
			rxbase[elem].rx_flags =
				(RXFLAG_OWN | ((RX_BUF_ALLOC_SIZE - RX_OFFSET) << 16));

			/* Trim the original skb for the netif. */
			skb_trim(skb, len);
		} else {
			struct sk_buff *copy_skb = dev_alloc_skb(len+2);

			if(!copy_skb) {
				drops++;
				goto drop_it;
			}

			copy_skb->dev = dev;
			skb_reserve(copy_skb, 2);
			skb_put(copy_skb, len);
			memcpy(copy_skb->data, skb->data, len);

			/* Reuse original ring buffer. */
			rxbase[elem].rx_addr =
				(u32) ((unsigned long)skb->data);
			rxbase[elem].rx_flags =
				(RXFLAG_OWN | ((RX_BUF_ALLOC_SIZE - RX_OFFSET) << 16));

			skb = copy_skb;
		}

		/* This card is _fucking_ hot... */
		if(!~(csum))
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			skb->ip_summed = CHECKSUM_NONE;

		RXD(("len=%d csum=%4x]", len, csum));
		skb->protocol = eth_type_trans(skb, dev);
		netif_rx(skb);

		hp->net_stats.rx_packets++;
		hp->net_stats.rx_bytes+=len;
	next:
		elem = NEXT_RX(elem);
		this = &rxbase[elem];
	}
	hp->rx_new = elem;
	if(drops)
		printk("%s: Memory squeeze, deferring packet.\n", hp->dev->name);
	RXD((">"));
}

static inline void sun4c_happy_meal_rx(struct happy_meal *hp, struct device *dev,
				       struct hmeal_gregs *gregs)
{
	struct happy_meal_rxd *rxbase = &hp->happy_block->happy_meal_rxd[0];
	struct happy_meal_rxd *this;
	struct hmeal_buffers *hbufs = hp->sun4c_buffers;
	__u32 hbufs_dvma = hp->s4c_buf_dvma;
	int elem = hp->rx_new, drops = 0;

	RXD(("RX<"));
	this = &rxbase[elem];
	while(!(this->rx_flags & RXFLAG_OWN)) {
		struct sk_buff *skb;
		unsigned int flags = this->rx_flags;
		unsigned char *thisbuf = &hbufs->rx_buf[elem][0];
		__u32 thisbuf_dvma = hbufs_dvma + hbuf_offset(rx_buf, elem);
		int len = flags >> 16;

		RXD(("[%d ", elem));

		/* Check for errors. */
		if((len < ETH_ZLEN) || (flags & RXFLAG_OVERFLOW)) {
			RXD(("ERR(%08lx)]", flags));
			hp->net_stats.rx_errors++;
			if(len < ETH_ZLEN)
				hp->net_stats.rx_length_errors++;
			if(len & (RXFLAG_OVERFLOW >> 16)) {
				hp->net_stats.rx_over_errors++;
				hp->net_stats.rx_fifo_errors++;
			}

			hp->net_stats.rx_dropped++;
		} else {
			skb = dev_alloc_skb(len + 2);
			if(skb == 0) {
				drops++;
				hp->net_stats.rx_dropped++;
			} else {
				RXD(("len=%d]", len));
				skb->dev = hp->dev;
				skb_reserve(skb, 2);
				skb_put(skb, len);
				eth_copy_and_sum(skb, (thisbuf+2), len, 0);
				skb->protocol = eth_type_trans(skb, dev);
				netif_rx(skb);
				hp->net_stats.rx_packets++;
				hp->net_stats.rx_bytes+=len;
			}
		}
		/* Return the buffer to the Happy Meal. */
		this->rx_addr = thisbuf_dvma;
		this->rx_flags =
			(RXFLAG_OWN | ((SUN4C_RX_BUFF_SIZE - RX_OFFSET) << 16));

		elem = NEXT_RX(elem);
		this = &rxbase[elem];
	}
	hp->rx_new = elem;
	if(drops)
		printk("%s: Memory squeeze, deferring packet.\n", hp->dev->name);
	RXD((">"));
}

static void happy_meal_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct device *dev            = (struct device *) dev_id;
	struct happy_meal *hp         = (struct happy_meal *) dev->priv;
	struct hmeal_gregs *gregs     = hp->gregs;
	struct hmeal_tcvregs *tregs   = hp->tcvregs;
	unsigned long happy_status    = gregs->stat;

	HMD(("happy_meal_interrupt: status=%08lx ", happy_status));

	dev->interrupt = 1;

	if(happy_status & GREG_STAT_ERRORS) {
		HMD(("ERRORS "));
		if(happy_meal_is_not_so_happy(hp, gregs, /* un- */ happy_status)) {
			dev->interrupt = 0;
			return;
		}
	}

	if(happy_status & GREG_STAT_MIFIRQ) {
		HMD(("MIFIRQ "));
		happy_meal_mif_interrupt(hp, gregs, tregs);
	}

	if(happy_status & GREG_STAT_TXALL) {
		HMD(("TXALL "));
		happy_meal_tx(hp);
	}

	if(happy_status & GREG_STAT_RXTOHOST) {
		HMD(("RXTOHOST "));
		happy_meal_rx(hp, dev, gregs);
	}

	if(dev->tbusy && (TX_BUFFS_AVAIL(hp) >= 0)) {
		hp->dev->tbusy = 0;
		mark_bh(NET_BH);
	}

	dev->interrupt = 0;
	HMD(("done\n"));
}

static void sun4c_happy_meal_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct device *dev            = (struct device *) dev_id;
	struct happy_meal *hp         = (struct happy_meal *) dev->priv;
	struct hmeal_gregs *gregs     = hp->gregs;
	struct hmeal_tcvregs *tregs   = hp->tcvregs;
	unsigned long happy_status    = gregs->stat;

	HMD(("happy_meal_interrupt: status=%08lx ", happy_status));

	dev->interrupt = 1;

	if(happy_status & GREG_STAT_ERRORS) {
		HMD(("ERRORS "));
		if(happy_meal_is_not_so_happy(hp, gregs, /* un- */ happy_status)) {
			dev->interrupt = 0;
			return;
		}
	}

	if(happy_status & GREG_STAT_MIFIRQ) {
		HMD(("MIFIRQ "));
		happy_meal_mif_interrupt(hp, gregs, tregs);
	}

	if(happy_status & GREG_STAT_TXALL) {
		HMD(("TXALL "));
		sun4c_happy_meal_tx(hp);
	}

	if(happy_status & GREG_STAT_RXTOHOST) {
		HMD(("RXTOHOST "));
		sun4c_happy_meal_rx(hp, dev, gregs);
	}

	if(dev->tbusy && (TX_BUFFS_AVAIL(hp) >= 0)) {
		hp->dev->tbusy = 0;
		mark_bh(NET_BH);
	}

	dev->interrupt = 0;
	HMD(("done\n"));
}

static int happy_meal_open(struct device *dev)
{
	struct happy_meal *hp = (struct happy_meal *) dev->priv;
	int res;

	HMD(("happy_meal_open: "));
	if(sparc_cpu_model == sun4c) {
		if(request_irq(dev->irq, &sun4c_happy_meal_interrupt,
			       SA_SHIRQ, "HAPPY MEAL", (void *) dev)) {
			HMD(("EAGAIN\n"));
			printk("happy meal: Can't order irq %d to go.\n", dev->irq);
			return -EAGAIN;
		}
	} else {
		if(request_irq(dev->irq, &happy_meal_interrupt,
			       SA_SHIRQ, "HAPPY MEAL", (void *) dev)) {
			HMD(("EAGAIN\n"));
			printk("happy meal: Can't order irq %d to go.\n", dev->irq);
			return -EAGAIN;
		}
	}
	HMD(("Init happy timer\n"));
	init_timer(&hp->happy_timer);
	HMD(("to happy_meal_init\n"));
	res = happy_meal_init(hp, 0);
	if(!res) {
		MOD_INC_USE_COUNT;
	}
	return res;
}

static int happy_meal_close(struct device *dev)
{
	struct happy_meal *hp = (struct happy_meal *) dev->priv;

	happy_meal_stop(hp->gregs);
	happy_meal_clean_rings(hp);
	free_irq(dev->irq, (void *)dev);
	MOD_DEC_USE_COUNT;
	return 0;
}

/* #define SXD(x) printk x */
#define SXD(x)

static int happy_meal_start_xmit(struct sk_buff *skb, struct device *dev)
{
	struct happy_meal *hp = (struct happy_meal *) dev->priv;
	int len, entry;

	if(dev->tbusy) {
		int tickssofar = jiffies - dev->trans_start;
	    
		if (tickssofar < 40) {
			return 1;
		} else {
			printk ("%s: transmit timed out, resetting\n", dev->name);
			hp->net_stats.tx_errors++;
			happy_meal_init(hp, 0);
			dev->tbusy = 0;
			dev->trans_start = jiffies;
			return 0;
		}
	}

	if(test_and_set_bit(0, (void *) &dev->tbusy) != 0) {
		printk("happy meal: Transmitter access conflict.\n");
		return 1;
	}

	if(!TX_BUFFS_AVAIL(hp))
		return 1;

	len = skb->len;
	entry = hp->tx_new;

	SXD(("SX<l[%d]e[%d]>", len, entry));
	hp->tx_skbs[entry] = skb;
	hp->happy_block->happy_meal_txd[entry].tx_addr =
		(u32) ((unsigned long)skb->data);
	hp->happy_block->happy_meal_txd[entry].tx_flags =
		(TXFLAG_OWN | TXFLAG_SOP | TXFLAG_EOP | (len & TXFLAG_SIZE));
	hp->tx_new = NEXT_TX(entry);

	/* Get it going. */
	dev->trans_start = jiffies;
	hp->etxregs->tx_pnding = ETX_TP_DMAWAKEUP;

	if(TX_BUFFS_AVAIL(hp))
		dev->tbusy = 0;

	return 0;
}

static int sun4c_happy_meal_start_xmit(struct sk_buff *skb, struct device *dev)
{
	struct happy_meal *hp = (struct happy_meal *) dev->priv;
	struct hmeal_buffers *hbufs = hp->sun4c_buffers;
	__u32 txbuf_dvma, hbufs_dvma = hp->s4c_buf_dvma;
	unsigned char *txbuf;
	int len, entry;

	if(dev->tbusy) {
		int tickssofar = jiffies - dev->trans_start;
	    
		if (tickssofar < 40) {
			return 1;
		} else {
			printk ("%s: transmit timed out, resetting\n", dev->name);
			hp->net_stats.tx_errors++;
			happy_meal_init(hp, 0);
			dev->tbusy = 0;
			dev->trans_start = jiffies;
			return 0;
		}
	}

	if(skb == NULL || skb->len <= 0) {
		printk("%s: skb is NULL\n", dev->name);
		dev_tint(dev);
		return 0;
	}

	if(test_and_set_bit(0, (void *) &dev->tbusy) != 0) {
		printk("happy meal: Transmitter access conflict.\n");
		return 1;
	}

	if(!TX_BUFFS_AVAIL(hp))
		return 1;

	len = skb->len;
	entry = hp->tx_new;

	txbuf = &hbufs->tx_buf[entry][0];
	memcpy(txbuf, skb->data, len);

	SXD(("SX<l[%d]e[%d]>", len, entry));
	txbuf_dvma = hbufs_dvma + hbuf_offset(tx_buf, entry);
	hp->happy_block->happy_meal_txd[entry].tx_addr = txbuf_dvma;
	hp->happy_block->happy_meal_txd[entry].tx_flags =
		(TXFLAG_OWN | TXFLAG_SOP | TXFLAG_EOP | (len & TXFLAG_SIZE));
	hp->tx_new = NEXT_TX(entry);

	/* Get it going. */
	dev->trans_start = jiffies;
	hp->etxregs->tx_pnding = ETX_TP_DMAWAKEUP;

	dev_kfree_skb(skb, FREE_WRITE);

	if(TX_BUFFS_AVAIL(hp))
		dev->tbusy = 0;

	return 0;
}

static struct net_device_stats *happy_meal_get_stats(struct device *dev)
{
	struct happy_meal *hp = (struct happy_meal *) dev->priv;

	happy_meal_get_counters(hp, hp->bigmacregs);
	return &hp->net_stats;
}

#define CRC_POLYNOMIAL_BE 0x04c11db7UL  /* Ethernet CRC, big endian */
#define CRC_POLYNOMIAL_LE 0xedb88320UL  /* Ethernet CRC, little endian */

static void happy_meal_set_multicast(struct device *dev)
{
	struct happy_meal *hp = (struct happy_meal *) dev->priv;
	struct hmeal_bigmacregs *bregs = hp->bigmacregs;
	struct dev_mc_list *dmi = dev->mc_list;
	char *addrs;
	int i, j, bit, byte;
	u32 crc, poly = CRC_POLYNOMIAL_LE;

	/* Let the transmits drain. */
	while(dev->tbusy)
		schedule();

	/* Lock out others. */
	set_bit(0, (void *) &dev->tbusy);

	if((dev->flags & IFF_ALLMULTI) || (dev->mc_count > 64)) {
		bregs->htable0 = 0xffff;
		bregs->htable1 = 0xffff;
		bregs->htable2 = 0xffff;
		bregs->htable3 = 0xffff;
	} else if(dev->flags & IFF_PROMISC) {
		bregs->rx_cfg |= BIGMAC_RXCFG_PMISC;
	} else {
		u16 hash_table[4];

		for(i = 0; i < 4; i++)
			hash_table[i] = 0;

		for(i = 0; i < dev->mc_count; i++) {
			addrs = dmi->dmi_addr;
			dmi = dmi->next;

			if(!(*addrs & 1))
				continue;

			crc = 0xffffffffU;
			for(byte = 0; byte < 6; byte++) {
				for(bit = *addrs++, j = 0; j < 8; j++, bit >>= 1) {
					int test;

					test = ((bit ^ crc) & 0x01);
					crc >>= 1;
					if(test)
						crc = crc ^ poly;
				}
			}
			crc >>= 26;
			hash_table[crc >> 4] |= 1 << (crc & 0xf);
		}
		bregs->htable0 = hash_table[0];
		bregs->htable1 = hash_table[1];
		bregs->htable2 = hash_table[2];
		bregs->htable3 = hash_table[3];
	}

	/* Let us get going again. */
	dev->tbusy = 0;
}

static inline int happy_meal_ether_init(struct device *dev, struct linux_sbus_device *sdev)
{
	static unsigned version_printed = 0;
	struct happy_meal *hp;
	int i;

	if(dev == NULL) {
		dev = init_etherdev(0, sizeof(struct happy_meal));
	} else {
		dev->priv = kmalloc(sizeof(struct happy_meal), GFP_KERNEL);
		if(dev->priv == NULL)
			return -ENOMEM;
	}
	if(version_printed++ == 0)
		printk(version);

	printk("%s: HAPPY MEAL 10/100baseT Ethernet ", dev->name);

	dev->base_addr = (long) sdev;
	for(i = 0; i < 6; i++)
		printk("%2.2x%c",
		       dev->dev_addr[i] = idprom->id_ethaddr[i],
		       i == 5 ? ' ' : ':');
	printk("\n");

	hp = (struct happy_meal *) dev->priv;
	memset(hp, 0, sizeof(*hp));

	hp->happy_sbus_dev = sdev;

	if(sdev->num_registers != 5) {
		printk("happymeal: Device does not have 5 regs, it has %d.\n",
		       sdev->num_registers);
		printk("happymeal: Would you like that for here or to go?\n");
		return ENODEV;
	}

	prom_apply_sbus_ranges(sdev->my_bus, &sdev->reg_addrs[0],
			       sdev->num_registers, sdev);
	hp->gregs = sparc_alloc_io(sdev->reg_addrs[0].phys_addr, 0,
				   sizeof(struct hmeal_gregs),
				   "Happy Meal Global Regs",
				   sdev->reg_addrs[0].which_io, 0);
	if(!hp->gregs) {
		printk("happymeal: Cannot map Happy Meal global registers.\n");
		return ENODEV;
	}

	hp->etxregs = sparc_alloc_io(sdev->reg_addrs[1].phys_addr, 0,
				     sizeof(struct hmeal_etxregs),
				     "Happy Meal MAC TX Regs",
				     sdev->reg_addrs[1].which_io, 0);
	if(!hp->etxregs) {
		printk("happymeal: Cannot map Happy Meal MAC Transmit registers.\n");
		return ENODEV;
	}

	hp->erxregs = sparc_alloc_io(sdev->reg_addrs[2].phys_addr, 0,
				     sizeof(struct hmeal_erxregs),
				     "Happy Meal MAC RX Regs",
				     sdev->reg_addrs[2].which_io, 0);
	if(!hp->erxregs) {
		printk("happymeal: Cannot map Happy Meal MAC Receive registers.\n");
		return ENODEV;
	}

	hp->bigmacregs = sparc_alloc_io(sdev->reg_addrs[3].phys_addr, 0,
					sizeof(struct hmeal_bigmacregs),
					"Happy Meal BIGMAC Regs",
					sdev->reg_addrs[3].which_io, 0);
	if(!hp->bigmacregs) {
		printk("happymeal: Cannot map Happy Meal BIGMAC registers.\n");
		return ENODEV;
	}

	hp->tcvregs = sparc_alloc_io(sdev->reg_addrs[4].phys_addr, 0,
				     sizeof(struct hmeal_tcvregs),
				     "Happy Meal Tranceiver Regs",
				     sdev->reg_addrs[4].which_io, 0);
	if(!hp->tcvregs) {
		printk("happymeal: Cannot map Happy Meal Tranceiver registers.\n");
		return ENODEV;
	}

	hp->hm_revision = prom_getintdefault(sdev->prom_node, "hm-rev", 0xff);
	if(hp->hm_revision == 0xff)
		hp->hm_revision = 0xa0;

	/* Now enable the feature flags we can. */
	if(hp->hm_revision == 0x20 || hp->hm_revision == 0x21)
		hp->happy_flags = HFLAG_20_21;
	else if(hp->hm_revision != 0xa0)
		hp->happy_flags = HFLAG_NOT_A0;

	/* Get the supported DVMA burst sizes from our Happy SBUS. */
	hp->happy_bursts = prom_getintdefault(hp->happy_sbus_dev->my_bus->prom_node,
					      "burst-sizes", 0x00);

	hp->happy_block = (struct hmeal_init_block *)
		sparc_dvma_malloc(PAGE_SIZE, "Happy Meal Init Block",
				  &hp->hblock_dvma);

	if(sparc_cpu_model == sun4c)
		hp->sun4c_buffers = (struct hmeal_buffers *)
		    sparc_dvma_malloc(sizeof(struct hmeal_buffers), "Happy Meal Bufs",
				      &hp->s4c_buf_dvma);
	else
		hp->sun4c_buffers = 0;

	/* Force check of the link first time we are brought up. */
	hp->linkcheck = 0;

	/* Force timer state to 'asleep' with count of zero. */
	hp->timer_state = asleep;
	hp->timer_ticks = 0;

	/* Grrr, Happy Meal comes up by default not advertising
	 * full duplex 100baseT capabilities, fix this.
	 */
	happy_meal_set_initial_advertisement(hp);

	hp->dev = dev;
	dev->open = &happy_meal_open;
	dev->stop = &happy_meal_close;
	if(sparc_cpu_model == sun4c)
		dev->hard_start_xmit = &sun4c_happy_meal_start_xmit;
	else
		dev->hard_start_xmit = &happy_meal_start_xmit;
	dev->get_stats = &happy_meal_get_stats;
	dev->set_multicast_list = &happy_meal_set_multicast;

	dev->irq = (unsigned char) sdev->irqs[0].pri;
	dev->dma = 0;
	ether_setup(dev);
#ifdef MODULE
	/* We are home free at this point, link us in to the happy
	 * module device list.
	 */
	dev->ifindex = dev_new_index();
	hp->next_module = root_happy_dev;
	root_happy_dev = hp;
#endif
	return 0;
}

__initfunc(int happy_meal_probe(struct device *dev))
{
	struct linux_sbus *bus;
	struct linux_sbus_device *sdev = 0;
	static int called = 0;
	int cards = 0, v;

	if(called)
		return ENODEV;
	called++;

	for_each_sbus(bus) {
		for_each_sbusdev(sdev, bus) {
			if(cards) dev = NULL;
			if(!strcmp(sdev->prom_name, "SUNW,hme")) {
				cards++;
				if((v = happy_meal_ether_init(dev, sdev)))
					return v;
			}
		}
	}
	if(!cards)
		return ENODEV;
	return 0;
}

#ifdef MODULE

int
init_module(void)
{
	root_happy_dev = NULL;
	return happy_meal_probe(NULL);
}

void
cleanup_module(void)
{
	struct happy_meal *sunshine;

	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	while (root_happy_dev) {
		struct happy_meal *hp = root_happy_dev;
		sunshine = root_happy_dev->next_module;

		sparc_free_io(hp->gregs, sizeof(struct hmeal_gregs));
		sparc_free_io(hp->etxregs, sizeof(struct hmeal_etxregs));
		sparc_free_io(hp->erxregs, sizeof(struct hmeal_erxregs));
		sparc_free_io(hp->bigmacregs, sizeof(struct hmeal_bigmacregs));
		sparc_free_io(hp->tcvregs, sizeof(struct hmeal_tcvregs));
		unregister_netdev(hp->dev);
		kfree(hp->dev);
		root_happy_dev = sunshine;
	}
}

#endif /* MODULE */
