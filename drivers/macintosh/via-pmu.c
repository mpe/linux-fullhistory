/*
 * Device driver for the via-pmu on Apple Powermacs.
 *
 * The VIA (versatile interface adapter) interfaces to the PMU,
 * a 6805 microprocessor core whose primary function is to control
 * battery charging and system power on the PowerBook 3400 and 2400.
 * The PMU also controls the ADB (Apple Desktop Bus) which connects
 * to the keyboard and mouse, as well as the non-volatile RAM
 * and the RTC (real time clock) chip.
 *
 * Copyright (C) 1998 Paul Mackerras and Fabio Riccardi.
 */
#include <stdarg.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <asm/prom.h>
#include <asm/adb.h>
#include <asm/pmu.h>
#include <asm/cuda.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/init.h>

static volatile unsigned char *via;

/* VIA registers - spaced 0x200 bytes apart */
#define RS		0x200		/* skip between registers */
#define B		0		/* B-side data */
#define A		RS		/* A-side data */
#define DIRB		(2*RS)		/* B-side direction (1=output) */
#define DIRA		(3*RS)		/* A-side direction (1=output) */
#define T1CL		(4*RS)		/* Timer 1 ctr/latch (low 8 bits) */
#define T1CH		(5*RS)		/* Timer 1 counter (high 8 bits) */
#define T1LL		(6*RS)		/* Timer 1 latch (low 8 bits) */
#define T1LH		(7*RS)		/* Timer 1 latch (high 8 bits) */
#define T2CL		(8*RS)		/* Timer 2 ctr/latch (low 8 bits) */
#define T2CH		(9*RS)		/* Timer 2 counter (high 8 bits) */
#define SR		(10*RS)		/* Shift register */
#define ACR		(11*RS)		/* Auxiliary control register */
#define PCR		(12*RS)		/* Peripheral control register */
#define IFR		(13*RS)		/* Interrupt flag register */
#define IER		(14*RS)		/* Interrupt enable register */
#define ANH		(15*RS)		/* A-side data, no handshake */

/* Bits in B data register: both active low */
#define TACK		0x08		/* Transfer acknowledge (input) */
#define TREQ		0x10		/* Transfer request (output) */

/* Bits in ACR */
#define SR_CTRL		0x1c		/* Shift register control bits */
#define SR_EXT		0x0c		/* Shift on external clock */
#define SR_OUT		0x10		/* Shift out if 1 */

/* Bits in IFR and IER */
#define IER_SET		0x80		/* set bits in IER */
#define IER_CLR		0		/* clear bits in IER */
#define SR_INT		0x04		/* Shift register full/empty */
#define CB1_INT		0x10		/* transition on CB1 input */

static enum pmu_state {
	idle,
	sending,
	intack,
	reading,
	reading_intr,
} pmu_state;

static struct adb_request *current_req;
static struct adb_request *last_req;
static struct adb_request *req_awaiting_reply;
static unsigned char interrupt_data[32];
static unsigned char *reply_ptr;
static int data_index;
static int data_len;
static int adb_int_pending;
static int pmu_adb_flags;
static int adb_dev_map = 0;
static struct adb_request bright_req_1, bright_req_2;
static struct device_node *vias;

static int init_pmu(void);
static int pmu_queue_request(struct adb_request *req);
static void pmu_start(void);
static void via_pmu_interrupt(int irq, void *arg, struct pt_regs *regs);
static int pmu_adb_send_request(struct adb_request *req, int sync);
static int pmu_adb_autopoll(int on);
static void send_byte(int x);
static void recv_byte(void);
static void pmu_sr_intr(struct pt_regs *regs);
static void pmu_done(struct adb_request *req);
static void pmu_handle_data(unsigned char *data, int len,
			    struct pt_regs *regs);
static void set_brightness(int level);
static void set_volume(int level);

/*
 * This table indicates for each PMU opcode:
 * - the number of data bytes to be sent with the command, or -1
 *   if a length byte should be sent,
 * - the number of response bytes which the PMU will return, or
 *   -1 if it will send a length byte.
 */
static s8 pmu_data_len[256][2] = {
/*	   0	   1	   2	   3	   4	   5	   6	   7  */
/*00*/	{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*08*/	{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*10*/	{ 1, 0},{ 1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*18*/	{ 0, 1},{ 0, 1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{ 0, 0},
/*20*/	{-1, 0},{ 0, 0},{ 2, 0},{ 1, 0},{ 1, 0},{-1, 0},{-1, 0},{-1, 0},
/*28*/	{ 0,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*30*/	{ 4, 0},{20, 0},{ 2, 0},{ 3, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*38*/	{ 0, 4},{ 0,20},{ 1, 1},{ 2, 1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*40*/	{ 1, 0},{ 1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*48*/	{ 0, 1},{ 0, 1},{-1,-1},{-1,-1},{ 1, 0},{-1,-1},{-1,-1},{-1,-1},
/*50*/	{ 1, 0},{ 0, 0},{ 2, 0},{ 2, 0},{-1, 0},{ 1, 0},{ 3, 0},{ 1, 0},
/*58*/	{ 0, 1},{ 1, 0},{ 0, 2},{ 0, 2},{ 0,-1},{-1,-1},{-1,-1},{-1,-1},
/*60*/	{ 2, 0},{-1, 0},{ 2, 0},{ 0, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*68*/	{ 0, 3},{ 0, 3},{ 0, 2},{ 0, 8},{ 0,-1},{ 0,-1},{-1,-1},{-1,-1},
/*70*/	{ 1, 0},{ 1, 0},{ 1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*78*/	{ 0,-1},{ 0,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{ 4, 1},{ 4, 1},
/*80*/	{ 4, 0},{-1, 0},{ 0, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*88*/	{ 0, 5},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*90*/	{ 1, 0},{ 2, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*98*/	{ 0, 1},{ 0, 1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*a0*/	{ 2, 0},{ 2, 0},{ 2, 0},{ 4, 0},{-1, 0},{ 0, 0},{-1, 0},{-1, 0},
/*a8*/	{ 1, 1},{ 1, 0},{ 3, 0},{ 2, 0},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*b0*/	{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*b8*/	{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*c0*/	{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*c8*/	{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*d0*/	{ 0, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*d8*/	{ 1, 1},{ 1, 1},{-1,-1},{-1,-1},{ 0, 1},{ 0,-1},{-1,-1},{-1,-1},
/*e0*/	{-1, 0},{ 4, 0},{ 0, 1},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*e8*/	{ 3,-1},{-1,-1},{ 0, 1},{-1,-1},{ 0,-1},{-1,-1},{-1,-1},{ 0, 0},
/*f0*/	{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*f8*/	{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
};

__openfirmware

void
find_via_pmu()
{
	vias = find_devices("via-pmu");
	if (vias == 0)
		return;
	if (vias->next != 0)
		printk(KERN_WARNING "Warning: only using 1st via-pmu\n");

#if 0
	{ int i;

	printk("via_pmu_init: node = %p, addrs =", vias->node);
	for (i = 0; i < vias->n_addrs; ++i)
		printk(" %x(%x)", vias->addrs[i].address, vias->addrs[i].size);
	printk(", intrs =");
	for (i = 0; i < vias->n_intrs; ++i)
		printk(" %x", vias->intrs[i].line);
	printk("\n"); }
#endif

	if (vias->n_addrs != 1 || vias->n_intrs != 1) {
		printk(KERN_ERR "via-pmu: %d addresses, %d interrupts!\n",
		       vias->n_addrs, vias->n_intrs);
		if (vias->n_addrs < 1 || vias->n_intrs < 1)
			return;
	}
	via = (volatile unsigned char *) ioremap(vias->addrs->address, 0x2000);

	out_8(&via[IER], IER_CLR | 0x7f);	/* disable all intrs */

	pmu_state = idle;

	if (!init_pmu())
		via = NULL;

	adb_hardware = ADB_VIAPMU;
}

void
via_pmu_init(void)
{
	if (vias == NULL)
		return;

	bright_req_1.complete = 1;
	bright_req_2.complete = 1;

	if (request_irq(vias->intrs[0].line, via_pmu_interrupt, 0, "VIA-PMU",
			(void *)0)) {
		printk(KERN_ERR "VIA-PMU: can't get irq %d\n",
		       vias->intrs[0].line);
		return;
	}

	/* Enable interrupts */
	out_8(&via[IER], IER_SET | SR_INT | CB1_INT);

	/* Set function pointers */
	adb_send_request = pmu_adb_send_request;
	adb_autopoll = pmu_adb_autopoll;
}

static int
init_pmu()
{
	int timeout;
	struct adb_request req;

	out_8(&via[B], via[B] | TREQ);			/* negate TREQ */
	out_8(&via[DIRB], (via[DIRB] | TREQ) & ~TACK);	/* TACK in, TREQ out */

	pmu_request(&req, NULL, 2, PMU_SET_INTR_MASK, 0xff);
	timeout = 100000;
	while (!req.complete) {
		if (--timeout < 0) {
			printk(KERN_ERR "init_pmu: no response from PMU\n");
			return 0;
		}
		udelay(10);
		pmu_poll();
	}

	/* ack all pending interrupts */
	timeout = 100000;
	interrupt_data[0] = 1;
	while (interrupt_data[0] || pmu_state != idle) {
		if (--timeout < 0) {
			printk(KERN_ERR "init_pmu: timed out acking intrs\n");
			return 0;
		}
		if (pmu_state == idle)
			adb_int_pending = 1;
		via_pmu_interrupt(0, 0, 0);
		udelay(10);
	}

	return 1;
}

/* Send an ADB command */
static int
pmu_adb_send_request(struct adb_request *req, int sync)
{
	int i;

	for (i = req->nbytes - 1; i > 0; --i)
		req->data[i+3] = req->data[i];
	req->data[3] = req->nbytes - 1;
	req->data[2] = pmu_adb_flags;
	req->data[1] = req->data[0];
	req->data[0] = PMU_ADB_CMD;
	req->nbytes += 3;
	req->reply_expected = 1;
	req->reply_len = 0;
	i = pmu_queue_request(req);
	if (i)
		return i;
	if (sync) {
		while (!req->complete)
			pmu_poll();
	}
	return 0;
}

/* Enable/disable autopolling */
static int
pmu_adb_autopoll(int on)
{
	struct adb_request req;

	if (on) {
		pmu_request(&req, NULL, 5, PMU_ADB_CMD, 0, 0x86,
			    adb_dev_map >> 8, adb_dev_map);
		pmu_adb_flags = 2;
	} else {
		pmu_request(&req, NULL, 1, PMU_ADB_POLL_OFF);
		pmu_adb_flags = 0;
	}
	while (!req.complete)
		pmu_poll();
	return 0;
}

/* Construct and send a pmu request */
int
pmu_request(struct adb_request *req, void (*done)(struct adb_request *),
	    int nbytes, ...)
{
	va_list list;
	int i;

	if (nbytes < 0 || nbytes > 32) {
		printk(KERN_ERR "pmu_request: bad nbytes (%d)\n", nbytes);
		req->complete = 1;
		return -EINVAL;
	}
	req->nbytes = nbytes;
	req->done = done;
	va_start(list, nbytes);
	for (i = 0; i < nbytes; ++i)
		req->data[i] = va_arg(list, int);
	va_end(list);
	if (pmu_data_len[req->data[0]][1] != 0) {
		req->reply[0] = ADB_RET_OK;
		req->reply_len = 1;
	} else
		req->reply_len = 0;
	req->reply_expected = 0;
	return pmu_queue_request(req);
}

/*
 * This procedure handles requests written to /dev/adb where the
 * first byte is CUDA_PACKET or PMU_PACKET.  For CUDA_PACKET, we
 * emulate a few CUDA requests.
 */
int
pmu_send_request(struct adb_request *req)
{
	int i;

	switch (req->data[0]) {
	case PMU_PACKET:
		for (i = 0; i < req->nbytes - 1; ++i)
			req->data[i] = req->data[i+1];
		--req->nbytes;
		if (pmu_data_len[req->data[0]][1] != 0) {
			req->reply[0] = ADB_RET_OK;
			req->reply_len = 1;
		} else
			req->reply_len = 0;
		return pmu_queue_request(req);
	case CUDA_PACKET:
		switch (req->data[1]) {
		case CUDA_GET_TIME:
			if (req->nbytes != 2)
				break;
			req->data[0] = PMU_READ_RTC;
			req->nbytes = 1;
			req->reply_len = 3;
			req->reply[0] = CUDA_PACKET;
			req->reply[1] = 0;
			req->reply[2] = CUDA_GET_TIME;
			return pmu_queue_request(req);
		case CUDA_SET_TIME:
			if (req->nbytes != 6)
				break;
			req->data[0] = PMU_SET_RTC;
			req->nbytes = 5;
			for (i = 1; i <= 4; ++i)
				req->data[i] = req->data[i+1];
			req->reply_len = 0;
			return pmu_queue_request(req);
		}
		break;
	}
	return -EINVAL;
}

int
pmu_queue_request(struct adb_request *req)
{
	unsigned long flags;
	int nsend;

	if (via == NULL) {
		req->complete = 1;
		return -ENXIO;
	}
	if (req->nbytes <= 0) {
		req->complete = 1;
		return 0;
	}
	nsend = pmu_data_len[req->data[0]][0];
	if (nsend >= 0 && req->nbytes != nsend + 1) {
		req->complete = 1;
		return -EINVAL;
	}

	req->next = 0;
	req->sent = 0;
	req->complete = 0;
	save_flags(flags); cli();

	if (current_req != 0) {
		last_req->next = req;
		last_req = req;
	} else {
		current_req = req;
		last_req = req;
		if (pmu_state == idle)
			pmu_start();
	}

	restore_flags(flags);
	return 0;
}

static void
send_byte(int x)
{
	out_8(&via[ACR], 0x1c);
	out_8(&via[SR], x);
	out_8(&via[B], via[B] & ~0x10);		/* assert TREQ */
}

static void
recv_byte()
{
	out_8(&via[ACR], 0x0c);
	in_8(&via[SR]);		/* resets SR */
	out_8(&via[B], via[B] & ~0x10);
}

static void
pmu_start()
{
	unsigned long flags;
	struct adb_request *req;

	/* assert pmu_state == idle */
	/* get the packet to send */
	save_flags(flags); cli();
	req = current_req;
	if (req == 0 || pmu_state != idle
	    || (req->reply_expected && req_awaiting_reply))
		goto out;

	pmu_state = sending;
	data_index = 1;
	data_len = pmu_data_len[req->data[0]][0];

	/* set the shift register to shift out and send a byte */
	send_byte(req->data[0]);

out:
	restore_flags(flags);
}

void
pmu_poll()
{
	int ie;

	ie = _disable_interrupts();
	if (via[IFR] & (SR_INT | CB1_INT))
		via_pmu_interrupt(0, 0, 0);
	_enable_interrupts(ie);
}

static void
via_pmu_interrupt(int irq, void *arg, struct pt_regs *regs)
{
	int intr;
	int nloop = 0;

	while ((intr = (in_8(&via[IFR]) & (SR_INT | CB1_INT))) != 0) {
		if (++nloop > 1000) {
			printk(KERN_DEBUG "PMU: stuck in intr loop, "
			       "intr=%x pmu_state=%d\n", intr, pmu_state);
			break;
		}
		if (intr & SR_INT)
			pmu_sr_intr(regs);
		else if (intr & CB1_INT) {
			adb_int_pending = 1;
			out_8(&via[IFR], CB1_INT);
		}
	}
	if (pmu_state == idle) {
		if (adb_int_pending) {
			pmu_state = intack;
			send_byte(PMU_INT_ACK);
			adb_int_pending = 0;
		} else if (current_req) {
			pmu_start();
		}
	}
}

static void
pmu_sr_intr(struct pt_regs *regs)
{
	struct adb_request *req;
	int bite, timeout;

	if (via[B] & TACK)
		printk(KERN_DEBUG "PMU: sr_intr but ack still high! (%x)\n",
		       via[B]);

	/* if reading grab the byte, and reset the interrupt */
	if ((via[ACR] & SR_OUT) == 0)
		bite = in_8(&via[SR]);
	out_8(&via[IFR], SR_INT);

	/* reset TREQ and wait for TACK to go high */
	out_8(&via[B], via[B] | TREQ);
	timeout = 3200;
	while ((in_8(&via[B]) & TACK) == 0) {
		if (--timeout < 0) {
			printk(KERN_ERR "PMU not responding (!ack)\n");
			return;
		}
		udelay(10);
	}

	switch (pmu_state) {
	case sending:
		req = current_req;
		if (data_len < 0) {
			data_len = req->nbytes - 1;
			send_byte(data_len);
			break;
		}
		if (data_index <= data_len) {
			send_byte(req->data[data_index++]);
			break;
		}
		req->sent = 1;
		data_len = pmu_data_len[req->data[0]][1];
		if (data_len == 0) {
			pmu_state = idle;
			current_req = req->next;
			if (req->reply_expected)
				req_awaiting_reply = req;
			else
				pmu_done(req);
		} else {
			pmu_state = reading;
			data_index = 0;
			reply_ptr = req->reply + req->reply_len;
			recv_byte();
		}
		break;

	case intack:
		data_index = 0;
		data_len = -1;
		pmu_state = reading_intr;
		reply_ptr = interrupt_data;
		recv_byte();
		break;

	case reading:
	case reading_intr:
		if (data_len == -1) {
			data_len = bite;
			if (bite > 32)
				printk(KERN_ERR "PMU: bad reply len %d\n",
				       bite);
		} else {
			reply_ptr[data_index++] = bite;
		}
		if (data_index < data_len) {
			recv_byte();
			break;
		}

		if (pmu_state == reading_intr) {
			pmu_handle_data(interrupt_data, data_index, regs);
		} else {
			req = current_req;
			current_req = req->next;
			req->reply_len += data_index;
			pmu_done(req);
		}
		pmu_state = idle;

		break;

	default:
		printk(KERN_ERR "via_pmu_interrupt: unknown state %d?\n",
		       pmu_state);
	}
}

static void
pmu_done(struct adb_request *req)
{
	req->complete = 1;
	if (req->done)
		(*req->done)(req);
}

/* Interrupt data could be the result data from an ADB cmd */
static void
pmu_handle_data(unsigned char *data, int len, struct pt_regs *regs)
{
	static int show_pmu_ints = 1;

	if (len < 1) {
		adb_int_pending = 0;
		return;
	}
	if (data[0] & PMU_INT_ADB) {
		if ((data[0] & PMU_INT_ADB_AUTO) == 0) {
			struct adb_request *req = req_awaiting_reply;
			if (req == 0) {
				printk(KERN_ERR "PMU: extra ADB reply\n");
				return;
			}
			req_awaiting_reply = 0;
			if (len <= 2)
				req->reply_len = 0;
			else {
				memcpy(req->reply, data + 1, len - 1);
				req->reply_len = len - 1;
			}
			pmu_done(req);
		} else {
			adb_input(data+1, len-1, regs, 1);
		}
	} else {
		if (data[0] == 0x08 && len == 3) {
			/* sound/brightness buttons pressed */
			set_brightness(data[1]);
			set_volume(data[2]);
		} else if (show_pmu_ints
			   && !(data[0] == PMU_INT_TICK && len == 1)) {
			int i;
			printk(KERN_DEBUG "pmu intr");
			for (i = 0; i < len; ++i)
				printk(" %.2x", data[i]);
			printk("\n");
		}
	}
}

int backlight_bright = -1;
int backlight_enabled = 0;

#define LEVEL_TO_BRIGHT(lev)	((lev) < 8? 0x7f: 0x4a - ((lev) >> 2))

void
pmu_enable_backlight(int on)
{
	struct adb_request req;

	if (on) {
		if (backlight_bright < 0) {
			pmu_request(&req, NULL, 2, 0xd9, 0);
			while (!req.complete)
				pmu_poll();
			backlight_bright = LEVEL_TO_BRIGHT(req.reply[1]);
		}
		pmu_request(&req, NULL, 2, PMU_BACKLIGHT_BRIGHT,
			    backlight_bright);
		while (!req.complete)
			pmu_poll();
	}
	pmu_request(&req, NULL, 2, PMU_BACKLIGHT_CTRL, on? 0x81: 1);
	while (!req.complete)
		pmu_poll();
	backlight_enabled = on;
}

static void
set_brightness(int level)
{
	backlight_bright = LEVEL_TO_BRIGHT(level);
	if (bright_req_1.complete)
		pmu_request(&bright_req_1, NULL, 2, PMU_BACKLIGHT_BRIGHT,
			    backlight_bright);
	if (bright_req_2.complete) {
		backlight_enabled = backlight_bright < 0x7f;
		pmu_request(&bright_req_2, NULL, 2, PMU_BACKLIGHT_CTRL,
			    backlight_enabled? 0x81: 1);
	}
}

static void
set_volume(int level)
{
}
