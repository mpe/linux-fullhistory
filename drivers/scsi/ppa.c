/* ppa.c   --  low level driver for the IOMEGA PPA3 
 * parallel port SCSI host adapter.
 * 
 * (The PPA3 is the embedded controller in the ZIP drive.)
 * 
 * (c) 1995,1996 Grant R. Guenther, grant@torque.net,
 * under the terms of the GNU Public License.
 * 
 */

/*      This driver was developed without the benefit of any technical
 * specifications for the interface.  Instead, a modified version of
 * DOSemu was used to monitor the protocol used by the DOS driver
 * for this adapter.  I have no idea how my programming model relates
 * to IOMEGA's design.
 * 
 * IOMEGA's driver does not generate linked commands.  I've never
 * observed a SCSI message byte in the protocol transactions, so
 * I am assuming that as long as linked commands are not used
 * we won't see any.  
 * 
 * For more information, see the file drivers/scsi/README.ppa.
 * 
 */

/* 
 * this driver has been hacked by Matteo Frigo (athena@theory.lcs.mit.edu)
 * to support EPP and scatter-gather.                        [0.26-athena]
 *
 * additional hacks by David Campbell (campbell@tirian.che.curtin.edu.au)
 * in response to this driver "mis-behaving" on his machine.
 *      Fixed EPP to handle "software" changing of EPP port data direction.
 *      Chased down EPP timeouts
 *      Made this driver "kernel version friendly"           [0.28-athena]
 *
 * Really hacked it out of existance (David Campbell)
 *      EPP timeouts no longer occur (yet better handling)
 *      Probes known parallel ports
 *      Autodetects EPP / ECP / PS2 / NIBBLE
 *      Support for multiple devices (does anyone need this??)
 *                                                           [0.29-Curtin]
 * [ Stuff removed ]
 *
 *      Modified PEDANTIC for less PEDANTIC drivers as people
 *      were complaining about speed (I received a report indicating
 *      that PEDANTIC is necessary for WinBond chipsets.
 *      Updated config_ppa and Makefile
 *                                                           [0.36b-Curtin]
 *
 * First round of cleanups
 *      Remove prior 1.3.34 kernel support
 *      SMC support changed
 *              ECP+EPP detection always invoked.
 *              If compat mode => PS/2
 *              else ecp_sync() called (ECP+EPP uses FIFO).
 *      Added routine to detect interrupt channel for ECP (not used)
 *      Changed version numbering
 *              1       Major number
 *              00      Minor revision number
 *              ALPHA   Expected stability (alpha, beta, stable)
 *                                              [Curtin-1-00-ALPHA]
 * Second round of cleanups
 *      Clean up timer queues
 *      Fixed problem with non-detection of PS/2 ports
 *      SMC ECP+EPP confirmed to work, remove option from config_ppa
 *                                              [Curtin-1-01-ALPHA]
 *
 * Parport hits with a vengance!!
 *      Several internal revisions have been made with huge amounts of
 *      fixes including:
 *              ioport_2_hostno removed (unique_id is quicker)
 *              SMC compat option is history
 *              Driver name / info hardwired
 *              Played with inlines and saved 4k on module
 *      Parport support
 *              Using PnP Parport allows use of printer attached to
 *              ZIP drive.
 *              Numerous fixups for device registration and to allow
 *              proper aborts.
 *      Version jumps a few numbers here - considered BETA
 *      Shipping Parport with monolithic driver :)
 *                                              [Curtin-1-05-BETA]
 *
 * Fixed code to ensure SCSI abort will release the SCSI command
 *      if the driver is STILL trying to claim the parport (PNP ver)
 *      Now I have to fix the lp driver then there will NEVER be a
 *      problem.
 *      Got around to doing the ppa_queuecommand() clean up
 *      Fixed bug relating to SMC EPP+ECP and monolithic driver
 *                                              [Curtin-1-06-BETA]
 *
 * Where did the ppa_setup() code disappear to ??
 *      Back in now...
 *      Distribution of ppa now independent of parport (less work for me).
 *      Also cleaned up the port detection to allow for variations on
 *      IO aliasing (in an attempt to fix a few problems with some
 *      machines...)
 *                                              [Curtin-1-07-BETA]
 *
 * Rewrote detection code for monolithic driver and ported changes to
 *      parport driver. Result is more stable detection of hardware and
 *      better immunity to port aliasing (old XT cards).
 *      Parport 0.16 (or better) is required for parport operation and
 *      ECP+EPP modes, otherwise the latest parport edition is recommended.
 *
 *      When using EPP and writing to disk CPU usage > 40%, while reading <10%.
 *      This is due to ZIP drive IO scheduling, the drive does a verify after
 *      write to ensure data integrity (removable media is ALWAYS questionable
 *      since you never know where it has been).
 *      Some fancy programing *MAY* fix the problem but at 30 Mb/min is just
 *      over 10 sectors per jiffy.
 *
 *      Hmm... I think I know a way but it will send the driver into
 *      ALPHA state again.
 *                                              [Curtin-1-08-STABLE]
 */

#include <linux/config.h>

/* The following #define is to avoid a clash with hosts.c */
#define PPA_CODE 1
#include  "ppa.h"
/* batteries not included :-) */

/*
 * modes in which the driver can operate 
 */
#define   PPA_AUTODETECT        0	/* Autodetect mode                */
#define   PPA_NIBBLE            1	/* work in standard 4 bit mode    */
#define   PPA_PS2               2	/* PS/2 byte mode         */
#define   PPA_EPP_8             3	/* EPP mode, 8 bit                */
#define   PPA_EPP_16            4	/* EPP mode, 16 bit               */
#define   PPA_EPP_32            5	/* EPP mode, 32 bit               */
#define   PPA_UNKNOWN           6	/* Just in case...                */

static char *PPA_MODE_STRING[] =
{
	"Autodetect",
	"SPP",
	"PS/2",
	"EPP 8 bit",
	"EPP 16 bit",
	"EPP 32 bit",
	"Unknown"};

typedef struct {
	struct pardevice *dev;	/* Parport device entry          */
	int speed;		/* General PPA delay constant   */
	int speed_fast;		/* Const for nibble/byte modes  */
	int epp_speed;		/* Reset time period            */
	int mode;		/* Transfer mode                */
	int timeout;		/* Number of timeouts           */
	int host;		/* Host number (for proc)       */
	int abort_flag;		/* Abort flag                   */
	int error_code;		/* Error code                   */
	int ppa_failed;		/* Failure flag                 */
	Scsi_Cmnd *cur_cmd;	/* Current queued command       */
	void (*done) (Scsi_Cmnd *);	/* Done func for queuecommand   */
	struct tq_struct ppa_tq;	/* Polling interupt stuff       */
	struct wait_queue *ppa_wait_q;	/* Used for PnP stuff           */
} ppa_struct;

static void ppa_interrupt(void *data);
/* I know that this is a mess but it works!! */
#define NO_HOSTS 4
static ppa_struct ppa_hosts[NO_HOSTS] =
{
  {0, 6, 1, CONFIG_SCSI_PPA_EPP_TIME, PPA_AUTODETECT, 0, -1, 0, DID_ERROR, 1, NULL, NULL,
   {0, 0, ppa_interrupt, NULL}, NULL},
  {0, 6, 1, CONFIG_SCSI_PPA_EPP_TIME, PPA_AUTODETECT, 0, -1, 0, DID_ERROR, 1, NULL, NULL,
   {0, 0, ppa_interrupt, NULL}, NULL},
  {0, 6, 1, CONFIG_SCSI_PPA_EPP_TIME, PPA_AUTODETECT, 0, -1, 0, DID_ERROR, 1, NULL, NULL,
   {0, 0, ppa_interrupt, NULL}, NULL},
  {0, 6, 1, CONFIG_SCSI_PPA_EPP_TIME, PPA_AUTODETECT, 0, -1, 0, DID_ERROR, 1, NULL, NULL,
   {0, 0, ppa_interrupt, NULL}, NULL}
};

/* This is a global option */
static int ppa_speed = -1;	/* Set to >0 to act as a global value */
static int ppa_speed_fast = -1;	/* ditto.. */
static int ppa_sg = SG_ALL;	/* enable/disable scatter-gather. */

/* other options */
#define   PPA_CAN_QUEUE         1	/* use "queueing" interface */
#define   PPA_BURST_SIZE        512	/* block size for bulk transfers */
#define   PPA_SELECT_TMO        5000	/* how long to wait for target ? */
#define   PPA_SPIN_TMO          500000	/* ppa_wait loop limiter */

#define IN_EPP_MODE(x) (x == PPA_EPP_8 || x == PPA_EPP_16 || x == PPA_EPP_32)

/* args to ppa_connect */
#define CONNECT_EPP_MAYBE 1
#define CONNECT_NORMAL  0

#define PPA_BASE(x)	ppa_hosts[(x)].dev->port->base

/* Port IO - Sorry Grant but I prefer the following symbols */
#define r_dtr(x)        inb(PPA_BASE(x))
#define r_str(x)        inb(PPA_BASE(x)+1)
#define r_ctr(x)        inb(PPA_BASE(x)+2)
#define r_epp(x)        inb(PPA_BASE(x)+4)
#define r_fifo(x)       inb(PPA_BASE(x)+0x400)
#define r_ecr(x)        inb(PPA_BASE(x)+0x402)

#define w_dtr(x,y)      outb(y, PPA_BASE(x))
#define w_str(x,y)      outb(y, PPA_BASE(x)+1)
#define w_ctr(x,y)      outb(y, PPA_BASE(x)+2);\
			udelay( ppa_hosts[(x)].speed)
#define w_epp(x,y)      outb(y, PPA_BASE(x)+4)
#define w_fifo(x,y)     outb(y, PPA_BASE(x)+0x400)
#define w_ecr(x,y)      outb(y, PPA_BASE(x)+0x402)

static void ppa_wakeup(void *ref)
{
	ppa_struct *ppa_dev = (ppa_struct *) ref;

	if (!ppa_dev->ppa_wait_q)
		return;	/* Wake up whom ? */

	/* Claim the Parport */
	if (parport_claim(ppa_dev->dev))
		return;	/* Shouldn't happen */

	wake_up(&ppa_dev->ppa_wait_q);
	return;
}

int ppa_release(struct Scsi_Host *host)
{
	int host_no = host->unique_id;

	printk("Releasing ppa%i\n", host_no);
	parport_unregister_device(ppa_hosts[host_no].dev);
	return 0;
}

static int ppa_pb_claim(int host_no)
{
	if (parport_claim(ppa_hosts[host_no].dev)) {
		sleep_on(&ppa_hosts[host_no].ppa_wait_q);
		ppa_hosts[host_no].ppa_wait_q = NULL;

		/* Check to see if we were aborted or reset */
		if (ppa_hosts[host_no].dev->port->cad !=
		    ppa_hosts[host_no].dev) {
			printk("Abort detected on ppa%i\n", host_no);
			return 1;
		}
	}
	return 0;
}

static void ppa_pb_release(int host_no)
{
	parport_release(ppa_hosts[host_no].dev);
}


/* Placed here so everyone knows what ecp_sync does.. */
static void ecp_sync(int host_no)
{
	int i, r;

	r = r_ecr(host_no);
	if ((r & 0xe0) != 0x80)
		return;

	for (i = 0; i < 100; i++) {
		r = r_ecr(host_no);
		if (r & 0x01)
			return;
		udelay(5);
	}

	printk("ppa: ECP sync failed as data still present in FIFO.\n");
}

static inline void ppa_d_pulse(int host_no, char b)
{
	w_dtr(host_no, b);
	w_ctr(host_no, 0xc);
	w_ctr(host_no, 0xe);
	w_ctr(host_no, 0xc);
	w_ctr(host_no, 0x4);
	w_ctr(host_no, 0xc);
}

static void ppa_disconnect(int host_no)
{
	ppa_d_pulse(host_no, 0);
	ppa_d_pulse(host_no, 0x3c);
	ppa_d_pulse(host_no, 0x20);
	ppa_d_pulse(host_no, 0xf);

	ppa_pb_release(host_no);
}

static inline void ppa_c_pulse(int host_no, char b)
{
	w_dtr(host_no, b);
	w_ctr(host_no, 0x4);
	w_ctr(host_no, 0x6);
	w_ctr(host_no, 0x4);
	w_ctr(host_no, 0xc);
}

static int ppa_connect(int host_no, int flag)
{
	int retv = ppa_pb_claim(host_no);

	ppa_c_pulse(host_no, 0);
	ppa_c_pulse(host_no, 0x3c);
	ppa_c_pulse(host_no, 0x20);
	if ((flag == CONNECT_EPP_MAYBE) &&
	    IN_EPP_MODE(ppa_hosts[host_no].mode))
		ppa_c_pulse(host_no, 0xcf);
	else
		ppa_c_pulse(host_no, 0x8f);

	return retv;
}

static void ppa_do_reset(int host_no)
{
	/*
	 * SCSI reset taken from ppa_init and checked with
	 * Iomega document that Grant has (via email :(
	 */
	ppa_pb_claim(host_no);
	ppa_disconnect(host_no);

	ppa_connect(host_no, CONNECT_NORMAL);

	w_ctr(host_no, 0x6);
	w_ctr(host_no, 0x4);
	w_dtr(host_no, 0x40);
	w_ctr(host_no, 0x8);
	udelay(50);
	w_ctr(host_no, 0xc);

	ppa_disconnect(host_no);
}

static char ppa_select(int host_no, int initiator, int target)
{
	char r;
	int k;

	r = r_str(host_no);	/* TODO */

	w_dtr(host_no, (1 << target));
	w_ctr(host_no, 0xe);
	w_ctr(host_no, 0xc);
	w_dtr(host_no, (1 << initiator));
	w_ctr(host_no, 0x8);

	k = 0;
	while (!(r = (r_str(host_no) & 0xf0)) && (k++ < PPA_SELECT_TMO))
		barrier();

	if (k >= PPA_SELECT_TMO)
		return 0;

	return r;
}

static void ppa_fail(int host_no, int error_code)
{
	ppa_hosts[host_no].error_code = error_code;
	ppa_hosts[host_no].ppa_failed = 1;
	ppa_disconnect(host_no);
}

/*
 * Wait for the high bit to be set.
 * 
 * In principle, this could be tied to an interrupt, but the adapter
 * doesn't appear to be designed to support interrupts.  We spin on
 * the 0x80 ready bit. 
 */
static char ppa_wait(int host_no)
{
	int k;
	char r;

	k = 0;
	while (!((r = r_str(host_no)) & 0x80)
	       && (k++ < PPA_SPIN_TMO) && !ppa_hosts[host_no].abort_flag)
		barrier();

	/* check if we were interrupted */
	if (ppa_hosts[host_no].abort_flag) {
		if (ppa_hosts[host_no].abort_flag == 1)
			ppa_fail(host_no, DID_ABORT);
		else {
			ppa_do_reset(host_no);
			ppa_fail(host_no, DID_RESET);
		}
		return 0;
	}
	/* check if timed out */
	if (k >= PPA_SPIN_TMO) {
		ppa_fail(host_no, DID_TIME_OUT);
		return 0;	/* command timed out */
	}
	/* 
	 * return some status information.
	 * Semantics: 0xc0 = ZIP wants more data
	 *            0xd0 = ZIP wants to send more data
	 *            0xf0 = end of transfer, ZIP is sending status
	 */
	return (r & 0xf0);
}


/* 
 * This is based on a trace of what the Iomega DOS 'guest' driver does.
 * I've tried several different kinds of parallel ports with guest and
 * coded this to react in the same ways that it does.
 * 
 * The return value from this function is just a hint about where the
 * handshaking failed.
 * 
 */
static int ppa_init(int host_no)
{
	if (ppa_pb_claim(host_no))
		return 1;
	ppa_disconnect(host_no);

	ppa_connect(host_no, CONNECT_NORMAL);

	w_ctr(host_no, 0x6);
	if ((r_str(host_no) & 0xf0) != 0xf0) {
		ppa_pb_release(host_no);
		return 2;
	}
	w_ctr(host_no, 0x4);
	if ((r_str(host_no) & 0xf0) != 0x80) {
		ppa_pb_release(host_no);
		return 3;
	}
	/* This is a SCSI reset signal */
	w_dtr(host_no, 0x40);
	w_ctr(host_no, 0x8);
	udelay(50);
	w_ctr(host_no, 0xc);

	ppa_disconnect(host_no);

	return 0;
}

/* 
 * check the epp status. After a EPP transfer, it should be true that
 * 1) the TIMEOUT bit (SPP_STR.0) is clear
 * 2) the READY bit (SPP_STR.7) is set
 */
static int ppa_check_epp_status(int host_no)
{
	char r;
	r = r_str(host_no);

	if (r & 1) {
		/* EPP timeout, according to the PC87332 manual */
		/* Semantics of clearing EPP timeout bit.
		 * PC87332 - reading SPP_STR does it...
		 * SMC     - write 1 to EPP timeout bit
		 * Others  - (???) write 0 to EPP timeout bit
		 */
		w_str(host_no, r);
		w_str(host_no, r & 0xfe);
		ppa_hosts[host_no].timeout++;
		printk("PPA: EPP timeout on port 0x%04x\n",
		       PPA_BASE(host_no));
		ppa_fail(host_no, DID_BUS_BUSY);
		return 0;
	}
	if (!(r & 0x80)) {
		ppa_fail(host_no, DID_ERROR);
		return 0;
	}
	return 1;
}

static inline int ppa_force_epp_byte(int host_no, char x)
{
/* This routine forces a byte down the EPP data port whether the
 * device is ready or not...
 */
	char r;

	w_epp(host_no, x);

	r = r_str(host_no);
	if (!(r & 1))
		return 1;

	if (ppa_hosts[host_no].epp_speed > 0) {
		/* EPP timeout, according to the PC87332 manual */
		/* Semantics of clearing EPP timeout bit.
		 * PC87332 - reading SPP_STR does it...
		 * SMC     - write 1 to EPP timeout bit
		 * Others  - (???) write 0 to EPP timeout bit
		 */
		w_str(host_no, r);
		w_str(host_no, r & 0xfe);

		/* Take a deep breath, count to 10 and then... */
		udelay(ppa_hosts[host_no].epp_speed);

		/* Second time around */
		w_epp(host_no, x);

		r = r_str(host_no);
	}
	if (r & 1) {
		w_str(host_no, r);
		w_str(host_no, r & 0xfe);
		ppa_hosts[host_no].timeout++;
		printk("PPA: warning: EPP timeout\n");
		ppa_fail(host_no, DID_BUS_BUSY);
		return 0;
	} else
		return 1;
}

static inline int ppa_send_command_epp(Scsi_Cmnd * cmd)
{
	int host_no = cmd->host->unique_id;
	int k;

	w_ctr(host_no, 0x4);
	for (k = 0; k < cmd->cmd_len; k++) {	/* send the command */
		if (!ppa_force_epp_byte(host_no, cmd->cmnd[k]))
			return 0;
	}
	w_ctr(host_no, 0xc);
	ecp_sync(host_no);
	return 1;
}

static inline int ppa_send_command_normal(Scsi_Cmnd * cmd)
{
	int host_no = cmd->host->unique_id;
	int k;

	w_ctr(host_no, 0xc);

	for (k = 0; k < cmd->cmd_len; k++) {	/* send the command */
		if (!ppa_wait(host_no))
			return 0;
		w_dtr(host_no, cmd->cmnd[k]);
		w_ctr(host_no, 0xe);
		w_ctr(host_no, 0xc);
	}
	return 1;
}

static int ppa_start(Scsi_Cmnd * cmd)
{
	int host_no = cmd->host->unique_id;

	/* 
	 * by default, the command failed,
	 * unless explicitly completed in ppa_completion().
	 */
	ppa_hosts[host_no].error_code = DID_ERROR;
	ppa_hosts[host_no].abort_flag = 0;
	ppa_hosts[host_no].ppa_failed = 0;

	if (cmd->target == PPA_INITIATOR) {
		ppa_hosts[host_no].error_code = DID_BAD_TARGET;
		ppa_hosts[host_no].ppa_failed = 1;
		return 0;
	}
	if (ppa_connect(host_no, CONNECT_EPP_MAYBE))
		return 0;

	if (!ppa_select(host_no, PPA_INITIATOR, cmd->target)) {
		ppa_fail(host_no, DID_NO_CONNECT);
		return 0;
	}
	if (IN_EPP_MODE(ppa_hosts[host_no].mode))
		return ppa_send_command_epp(cmd);
	else
		return ppa_send_command_normal(cmd);
}

/*
 * output a string, in whatever mode is available, according to the
 * PPA protocol. 
 */
static inline int ppa_outs(int host_no, char *buffer, int len)
{
	int k;
#if CONFIG_SCSI_PPA_HAVE_PEDANTIC > 0
	int r;
#endif

	switch (ppa_hosts[host_no].mode) {
	case PPA_NIBBLE:
	case PPA_PS2:
		/* 8 bit output, with a loop */
		for (k = len; k; k--) {
			w_dtr(host_no, *buffer++);
			w_ctr(host_no, 0xe);
			w_ctr(host_no, 0xc);
		}
		return 1;	/* assume transfer went OK */

#if CONFIG_SCSI_PPA_HAVE_PEDANTIC > 0
	case PPA_EPP_32:
#if CONFIG_SCSI_PPA_HAVE_PEDANTIC < 2
		w_ctr(host_no, 0x4);
		for (k = len; k; k -= 4) {
			w_epp(host_no, *buffer++);
			w_epp(host_no, *buffer++);
			w_epp(host_no, *buffer++);
			w_epp(host_no, *buffer++);
			r = ppa_check_epp_status(host_no);
			if (!r)
				return r;
		}
		w_ctr(host_no, 0xc);
		ecp_sync(host_no);
		return 1;
#endif
	case PPA_EPP_16:
#if CONFIG_SCSI_PPA_HAVE_PEDANTIC < 3
		w_ctr(host_no, 0x4);
		for (k = len; k; k -= 2) {
			w_epp(host_no, *buffer++);
			w_epp(host_no, *buffer++);
			r = ppa_check_epp_status(host_no);
			if (!r)
				return r;
		}
		w_ctr(host_no, 0xc);
		ecp_sync(host_no);
		return 1;
#endif
	case PPA_EPP_8:
		w_ctr(host_no, 0x4);
		for (k = len; k; k--) {
			w_epp(host_no, *buffer++);
			r = ppa_check_epp_status(host_no);
			if (!r)
				return r;
		}
		w_ctr(host_no, 0xc);
		ecp_sync(host_no);
		return 1;
#else
	case PPA_EPP_32:
	case PPA_EPP_16:
	case PPA_EPP_8:
		w_ctr(host_no, 0x4);
		switch (ppa_hosts[host_no].mode) {
		case PPA_EPP_8:
			outsb(PPA_BASE(host_no) + 0x04,
			      buffer, len);
			break;
		case PPA_EPP_16:
			outsw(PPA_BASE(host_no) + 0x04,
			      buffer, len / 2);
			break;
		case PPA_EPP_32:
			outsl(PPA_BASE(host_no) + 0x04,
			      buffer, len / 4);
			break;
		}
		k = ppa_check_epp_status(host_no);
		w_ctr(host_no, 0xc);
		ecp_sync(host_no);
		return k;
#endif

	default:
		printk("PPA: bug in ppa_outs()\n");
	}
	return 0;
}

static inline int ppa_outb(int host_no, char d)
{
	int k;

	switch (ppa_hosts[host_no].mode) {
	case PPA_NIBBLE:
	case PPA_PS2:
		w_dtr(host_no, d);
		w_ctr(host_no, 0xe);
		w_ctr(host_no, 0xc);
		return 1;	/* assume transfer went OK */

	case PPA_EPP_8:
	case PPA_EPP_16:
	case PPA_EPP_32:
		w_ctr(host_no, 0x4);
		w_epp(host_no, d);
		k = ppa_check_epp_status(host_no);
		w_ctr(host_no, 0xc);
		ecp_sync(host_no);
		return k;

	default:
		printk("PPA: bug in ppa_outb()\n");
	}
	return 0;
}

static inline int ppa_ins(int host_no, char *buffer, int len)
{
	int k, h, l;
#if CONFIG_SCSI_PPA_HAVE_PEDANTIC > 0
	int r;
#endif

	switch (ppa_hosts[host_no].mode) {
	case PPA_NIBBLE:
		/* 4 bit input, with a loop */
		for (k = len; k; k--) {
			w_ctr(host_no, 0x4);
			h = r_str(host_no);
			w_ctr(host_no, 0x6);
			l = r_str(host_no);
			*buffer++ = ((l >> 4) & 0x0f) + (h & 0xf0);
		}
		w_ctr(host_no, 0xc);
		return 1;	/* assume transfer went OK */

	case PPA_PS2:
		/* 8 bit input, with a loop */
		for (k = len; k; k--) {
			w_ctr(host_no, 0x25);
			*buffer++ = r_dtr(host_no);
			w_ctr(host_no, 0x27);
		}
		w_ctr(host_no, 0x5);
		w_ctr(host_no, 0x4);
		w_ctr(host_no, 0xc);
		return 1;	/* assume transfer went OK */

#if CONFIG_SCSI_PPA_HAVE_PEDANTIC > 0
	case PPA_EPP_32:
#if CONFIG_SCSI_PPA_HAVE_PEDANTIC < 2
		w_ctr(host_no, 0x24);
		for (k = len; k; k -= 4) {
			*buffer++ = r_epp(host_no);
			*buffer++ = r_epp(host_no);
			*buffer++ = r_epp(host_no);
			*buffer++ = r_epp(host_no);
			r = ppa_check_epp_status(host_no);
			if (!r)
				return r;
		}
		w_ctr(host_no, 0x2c);
		ecp_sync(host_no);
		return 1;
#endif
	case PPA_EPP_16:
#if CONFIG_SCSI_PPA_HAVE_PEDANTIC < 3
		w_ctr(host_no, 0x24);
		for (k = len; k; k -= 2) {
			*buffer++ = r_epp(host_no);
			*buffer++ = r_epp(host_no);
			r = ppa_check_epp_status(host_no);
			if (!r)
				return r;
		}
		w_ctr(host_no, 0x2c);
		ecp_sync(host_no);
		return 1;
#endif
	case PPA_EPP_8:
		w_ctr(host_no, 0x24);
		for (k = len; k; k--) {
			*buffer++ = r_epp(host_no);
			r = ppa_check_epp_status(host_no);
			if (!r)
				return r;
		}
		w_ctr(host_no, 0x2c);
		ecp_sync(host_no);
		return 1;
		break;
#else
	case PPA_EPP_8:
	case PPA_EPP_16:
	case PPA_EPP_32:
		w_ctr(host_no, 0x24);
		switch (ppa_hosts[host_no].mode) {
		case PPA_EPP_8:
			insb(PPA_BASE(host_no) + 0x04,
			     buffer, len);
			break;
		case PPA_EPP_16:
			insw(PPA_BASE(host_no) + 0x04,
			     buffer, len / 2);
			break;
		case PPA_EPP_32:
			insl(PPA_BASE(host_no) + 0x04,
			     buffer, len / 4);
			break;
		}
		k = ppa_check_epp_status(host_no);
		w_ctr(host_no, 0x2c);
		ecp_sync(host_no);
		return k;
#endif

	default:
		printk("PPA: bug in ppa_ins()\n");
	}
	return 0;
}

static int ppa_inb(int host_no, char *buffer)
{
	int h, l, k;

	switch (ppa_hosts[host_no].mode) {
	case PPA_NIBBLE:
		/* 4 bit input */
		w_ctr(host_no, 0x4);
		h = r_str(host_no);
		w_ctr(host_no, 0x6);
		l = r_str(host_no);
		*buffer = ((l >> 4) & 0x0f) + (h & 0xf0);
		w_ctr(host_no, 0xc);
		return 1;	/* assume transfer went OK */

	case PPA_PS2:
		/* 8 bit input */
		w_ctr(host_no, 0x25);
		*buffer++ = r_dtr(host_no);
		w_ctr(host_no, 0x27);
		w_ctr(host_no, 0x5);
		w_ctr(host_no, 0x4);
		w_ctr(host_no, 0xc);
		return 1;	/* assume transfer went OK */

	case PPA_EPP_8:
	case PPA_EPP_16:
	case PPA_EPP_32:
		w_ctr(host_no, 0x24);
		*buffer = r_epp(host_no);
		k = ppa_check_epp_status(host_no);
		w_ctr(host_no, 0xc);
		ecp_sync(host_no);
		return k;

	default:
		printk("PPA: bug in ppa_inb()\n");
	}
	return 0;
}

/*
 * The bulk flag enables some optimisations in the data transfer loops,
 * it should be true for any command that transfers data in integral
 * numbers of sectors.
 * 
 * The driver appears to remain stable if we speed up the parallel port
 * i/o in this function, but not elsewhere.
 */
static int ppa_completion(Scsi_Cmnd * cmd)
{
	int host_no = cmd->host->unique_id;

	char r, l, h, v;
	int dir, cnt, blen, fast, bulk, status;
	char *buffer;
	struct scatterlist *sl;
	int current_segment, nsegment;

	v = cmd->cmnd[0];
	bulk = ((v == READ_6) ||
		(v == READ_10) ||
		(v == WRITE_6) ||
		(v == WRITE_10));

	/* code for scatter/gather: */
	if (cmd->use_sg) {
		/* if many buffers are available, start filling the first */
		sl = (struct scatterlist *) cmd->request_buffer;
		blen = sl->length;
		buffer = sl->address;
	} else {
		/* else fill the only available buffer */
		sl = NULL;
		buffer = cmd->request_buffer;
		blen = cmd->request_bufflen;
	}
	current_segment = 0;
	nsegment = cmd->use_sg;

	cnt = 0;

	/* detect transfer direction */
	dir = 0;
	if (!(r = ppa_wait(host_no)))
		return 0;
	if (r == (char) 0xc0)
		dir = 1;	/* d0 = read c0 = write f0 = status */

	while (r != (char) 0xf0) {
		if (((r & 0xc0) != 0xc0) || (cnt >= blen)) {
			ppa_fail(host_no, DID_ERROR);
			return 0;
		}
		/* determine if we should use burst I/O */
		fast = (bulk && ((blen - cnt) >= PPA_BURST_SIZE) &&
			((((long)buffer + cnt)) & 0x3) == 0);

		if (fast) {
			if (dir)
				status = ppa_outs(host_no, &buffer[cnt], PPA_BURST_SIZE);
			else
				status = ppa_ins(host_no, &buffer[cnt], PPA_BURST_SIZE);
			cnt += PPA_BURST_SIZE;
		} else {
			if (dir)
				status = ppa_outb(host_no, buffer[cnt]);
			else
				status = ppa_inb(host_no, &buffer[cnt]);
			cnt++;
		}

		if (!status || !(r = ppa_wait(host_no)))
			return 0;

		if (sl && cnt == blen) {
			/* if scatter/gather, advance to the next segment */
			if (++current_segment < nsegment) {
				++sl;
				blen = sl->length;
				buffer = sl->address;
				cnt = 0;
			}
			/*
			 * the else case will be captured by the (cnt >= blen)
			 * test above.
			 */
		}
	}

	/* read status and message bytes */
	if (!ppa_inb(host_no, &l))	/* read status byte */
		return 0;
	if (!(ppa_wait(host_no)))
		return 0;
	if (!ppa_inb(host_no, &h))	/* read message byte */
		return 0;

	ppa_disconnect(host_no);

	ppa_hosts[host_no].error_code = DID_OK;
	return (h << 8) | (l & STATUS_MASK);
}

/* deprecated synchronous interface */

int ppa_command(Scsi_Cmnd * cmd)
{
	int host_no = cmd->host->unique_id;
	int s;

	sti();
	s = 0;
	if (ppa_start(cmd))
		if (ppa_wait(host_no))
			s = ppa_completion(cmd);
	return s + (ppa_hosts[host_no].error_code << 16);
}

/* pseudo-interrupt queueing interface */
/*
 * Since the PPA itself doesn't generate interrupts, we use
 * the scheduler's task queue to generate a stream of call-backs and
 * complete the request when the drive is ready.
 */
static void ppa_interrupt(void *data)
{
	ppa_struct *tmp = (ppa_struct *) data;
	Scsi_Cmnd *cmd = tmp->cur_cmd;
	void (*done) (Scsi_Cmnd *) = tmp->done;
	int host_no = cmd->host->unique_id;

	if (!cmd) {
		printk("PPA: bug in ppa_interrupt\n");
		return;
	}
	/* First check for any errors that may of occured
	 * Here we check for internal errors
	 */
	if (tmp->ppa_failed) {
		printk("PPA: ppa_failed bug: ppa_error_code = %d\n",
		       tmp->error_code);
		cmd->result = DID_ERROR << 16;
		tmp->cur_cmd = 0;
		done(cmd);
		return;
	}
	/* Occasionally the mid level driver will abort a SCSI
	 * command because we are taking to long, if this occurs
	 * we should abort the command.
	 */
	if (tmp->abort_flag) {
		ppa_disconnect(host_no);
		if (tmp->abort_flag == 1)
			cmd->result = DID_ABORT << 16;
		else {
			ppa_do_reset(host_no);
			cmd->result = DID_RESET << 16;
		}
		tmp->cur_cmd = 0;
		done(cmd);
		return;
	}
	/* Check to see if the device is now free, if not
	 * then throw this function onto the scheduler queue
	 * to be called back in a jiffy.
	 * (i386: 1 jiffy = 0.01 seconds)
	 */
	if (!(r_str(host_no) & 0x80)) {
		tmp->ppa_tq.data = (void *) tmp;
		queue_task(&tmp->ppa_tq, &tq_scheduler);
		return;
	}
	/* Device is now free and no errors have occured so
	 * it is safe to do the data phase
	 */
	cmd->result = ppa_completion(cmd) + (tmp->error_code << 16);
	tmp->cur_cmd = 0;
	done(cmd);
	return;
}

int ppa_queuecommand(Scsi_Cmnd * cmd, void (*done) (Scsi_Cmnd *))
{
	int host_no = cmd->host->unique_id;

	if (ppa_hosts[host_no].cur_cmd) {
		printk("PPA: bug in ppa_queuecommand\n");
		return 0;
	}
	sti();
	ppa_hosts[host_no].cur_cmd = cmd;
	ppa_hosts[host_no].done = done;

	if (!ppa_start(cmd)) {
		cmd->result = ppa_hosts[host_no].error_code << 16;
		ppa_hosts[host_no].cur_cmd = 0;
		done(cmd);
		return 0;
	}
	ppa_interrupt(ppa_hosts + host_no);

	return 0;
}

/*
 * Apparently the the disk->capacity attribute is off by 1 sector 
 * for all disk drives.  We add the one here, but it should really
 * be done in sd.c.  Even if it gets fixed there, this will still
 * work.
 */
int ppa_biosparam(Disk * disk, kdev_t dev, int ip[])
{
	ip[0] = 0x40;
	ip[1] = 0x20;
	ip[2] = (disk->capacity + 1) / (ip[0] * ip[1]);
	if (ip[2] > 1024) {
		ip[0] = 0xff;
		ip[1] = 0x3f;
		ip[2] = (disk->capacity + 1) / (ip[0] * ip[1]);
		if (ip[2] > 1023)
			ip[2] = 1023;
	}
	return 0;
}

int ppa_abort(Scsi_Cmnd * cmd)
{
	int host_no = cmd->host->unique_id;

	ppa_hosts[host_no].abort_flag = 1;
	ppa_hosts[host_no].error_code = DID_ABORT;
	if (ppa_hosts[host_no].ppa_wait_q)
		wake_up(&ppa_hosts[host_no].ppa_wait_q);

	return SCSI_ABORT_SNOOZE;
}

int ppa_reset(Scsi_Cmnd * cmd, unsigned int x)
{
	int host_no = cmd->host->unique_id;

	ppa_hosts[host_no].abort_flag = 2;
	ppa_hosts[host_no].error_code = DID_RESET;
	if (ppa_hosts[host_no].ppa_wait_q)
		wake_up(&ppa_hosts[host_no].ppa_wait_q);

	return SCSI_RESET_PUNT;
}



/***************************************************************************
 *                   Parallel port probing routines                        *
 ***************************************************************************/


#ifdef MODULE
Scsi_Host_Template driver_template = PPA;
#include  "scsi_module.c"
#endif

/*
 * Start of Chipset kludges
 */

int ppa_detect(Scsi_Host_Template * host)
{
	struct Scsi_Host *hreg;
	int rs;
	int ports;
	int i, nhosts;
	struct parport *pb = parport_enumerate();

	printk("PPA driver version: %s\n", PPA_VERSION);
	nhosts = 0;

	for (i = 0; pb; i++, pb=pb->next) {
		int modes = pb->modes;

		/* We only understand PC-style ports */
		if (modes & PARPORT_MODE_PCSPP) {

			/* transfer global values here */
			if (ppa_speed >= 0)
				ppa_hosts[i].speed = ppa_speed;
			if (ppa_speed_fast >= 0)
				ppa_hosts[i].speed_fast = ppa_speed_fast;
			
			ppa_hosts[i].dev = parport_register_device(pb, "ppa", 
				    NULL, ppa_wakeup, NULL,
				    PARPORT_DEV_TRAN, (void *) &ppa_hosts[i]);
			
			/* Claim the bus so it remembers what we do to the
			 * control registers. [ CTR and ECP ]
			 */
			ppa_pb_claim(i);
			w_ctr(i, 0x0c);

			ppa_hosts[i].mode = PPA_NIBBLE;
			if (modes & (PARPORT_MODE_PCEPP | PARPORT_MODE_PCECPEPP)) {
				ppa_hosts[i].mode = PPA_EPP_32;
				printk("PPA: Parport [ PCEPP ]\n");
			} else if (modes & PARPORT_MODE_PCECP) {
				w_ecr(i, 0x20);
				ppa_hosts[i].mode = PPA_PS2;
				printk("PPA: Parport [ PCECP in PS2 submode ]\n");
			} else if (modes & PARPORT_MODE_PCPS2) {
				ppa_hosts[i].mode = PPA_PS2;
				printk("PPA: Parport [ PCPS2 ]\n");
			}
			/* Done configuration */
			ppa_pb_release(i);

			rs = ppa_init(i);
			if (rs) {
				parport_unregister_device(ppa_hosts[i].dev);
				continue;
			}
			/* now the glue ... */
			switch (ppa_hosts[i].mode) {
			case PPA_NIBBLE:
			case PPA_PS2:
				ports = 3;
				break;
			case PPA_EPP_8:
			case PPA_EPP_16:
			case PPA_EPP_32:
				ports = 8;
				break;
			default:	/* Never gets here */
				continue;
			}
			
			host->can_queue = PPA_CAN_QUEUE;
			host->sg_tablesize = ppa_sg;
			hreg = scsi_register(host, 0);
			hreg->io_port = pb->base;
			hreg->n_io_port = ports;
			hreg->dma_channel = -1;
			hreg->unique_id = i;
			ppa_hosts[i].host = hreg->host_no;
			nhosts++;
		}
	}
	if (nhosts == 0)
		return 0;
	else
		return 1;	/* return number of hosts detected */
}

/* This is to give the ppa driver a way to modify the timings (and other
 * parameters) by writing to the /proc/scsi/ppa/0 file.
 * Very simple method really... (To simple, no error checking :( )
 * Reason: Kernel hackers HATE having to unload and reload modules for
 * testing...
 * Also gives a method to use a script to obtain optimum timings (TODO)
 */

static int ppa_strncmp(const char *a, const char *b, int len)
{
	int loop;
	for (loop = 0; loop < len; loop++)
		if (a[loop] != b[loop])
			return 1;

	return 0;
}

static int ppa_proc_write(int hostno, char *buffer, int length)
{
	unsigned long x;
	const char *inv_num = "ppa /proc entry passed invalid number\n";

	if ((length > 15) && (ppa_strncmp(buffer, "ppa_speed_fast=", 15) == 0)) {
		x = simple_strtoul(buffer + 15, NULL, 0);
		if (x <= ppa_hosts[hostno].speed)
			ppa_hosts[hostno].speed_fast = x;
		else
			printk(inv_num);
		return length;
	}
	if ((length > 10) && (ppa_strncmp(buffer, "ppa_speed=", 10) == 0)) {
		x = simple_strtoul(buffer + 10, NULL, 0);
		if (x >= ppa_hosts[hostno].speed_fast)
			ppa_hosts[hostno].speed = x;
		else
			printk(inv_num);
		return length;
	}
	if ((length > 10) && (ppa_strncmp(buffer, "epp_speed=", 10) == 0)) {
		x = simple_strtoul(buffer + 10, NULL, 0);
		ppa_hosts[hostno].epp_speed = x;
		return length;
	}
	if ((length > 12) && (ppa_strncmp(buffer, "epp_timeout=", 12) == 0)) {
		x = simple_strtoul(buffer + 12, NULL, 0);
		ppa_hosts[hostno].timeout = x;
		return length;
	}
	if ((length > 5) && (ppa_strncmp(buffer, "mode=", 5) == 0)) {
		x = simple_strtoul(buffer + 5, NULL, 0);
		ppa_hosts[hostno].mode = x;
		return length;
	}
	printk("ppa /proc: invalid variable\n");
	return (-EINVAL);
}

int ppa_proc_info(char *buffer, char **start, off_t offset,
		  int length, int hostno, int inout)
{
	int i;
	int size, len = 0;
	off_t begin = 0;
	off_t pos = 0;

	for (i = 0; i < 4; i++)
		if (ppa_hosts[i].host == hostno)
			break;

	if (inout)
		return ppa_proc_write(i, buffer, length);

	size = sprintf(buffer + len, "Version : %s\n", PPA_VERSION);
	len += size;
	pos = begin + len;
	size = sprintf(buffer + len, "Parport  : %s\n",
		       ppa_hosts[i].dev->port->name);
	len += size;
	pos = begin + len;

	size = sprintf(buffer + len, "Mode    : %s\n",
		       PPA_MODE_STRING[ppa_hosts[i].mode]);
	len += size;
	pos = begin + len;

	size = sprintf(buffer + len, "\nTiming Parameters\n");
	len += size;
	pos = begin + len;

	size = sprintf(buffer + len, "ppa_speed       %i\n",
		       ppa_hosts[i].speed);
	len += size;
	pos = begin + len;

	size = sprintf(buffer + len, "ppa_speed_fast  %i\n",
		       ppa_hosts[i].speed_fast);
	len += size;
	pos = begin + len;

	if (IN_EPP_MODE(ppa_hosts[i].mode)) {
		size = sprintf(buffer + len, "epp_speed       %i\n",
			       ppa_hosts[i].epp_speed);
		len += size;
		pos = begin + len;

		size = sprintf(buffer + len, "\nInternal Counters\n");
		len += size;
		pos = begin + len;

		size = sprintf(buffer + len, "epp_timeout     %i\n",
			       ppa_hosts[i].timeout);
		len += size;
		pos = begin + len;
	}
	*start = buffer + (offset + begin);
	len -= (offset - begin);
	if (len > length)
		len = length;
	return len;
}
/* end of ppa.c */
