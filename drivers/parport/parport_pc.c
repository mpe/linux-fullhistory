/* Low-level parallel-port routines for 8255-based PC-style hardware.
 * 
 * Authors: Phil Blundell <Philip.Blundell@pobox.com>
 *          Tim Waugh <tim@cyberelk.demon.co.uk>
 *	    Jose Renau <renau@acm.org>
 *          David Campbell <campbell@torque.net>
 *          Andrea Arcangeli
 *
 * based on work by Grant Guenther <grant@torque.net> and Phil Blundell.
 *
 * Cleaned up include files - Russell King <linux@arm.uk.linux.org>
 * DMA support - Bert De Jonghe <bert@sophis.be>
 * Many ECP bugs fixed.  Fred Barnes & Jamie Lokier, 1999
 */

/* This driver should work with any hardware that is broadly compatible
 * with that in the IBM PC.  This applies to the majority of integrated
 * I/O chipsets that are commonly available.  The expected register
 * layout is:
 *
 *	base+0		data
 *	base+1		status
 *	base+2		control
 *
 * In addition, there are some optional registers:
 *
 *	base+3		EPP address
 *	base+4		EPP data
 *	base+0x400	ECP config A
 *	base+0x401	ECP config B
 *	base+0x402	ECP control
 *
 * All registers are 8 bits wide and read/write.  If your hardware differs
 * only in register addresses (eg because your registers are on 32-bit
 * word boundaries) then you can alter the constants in parport_pc.h to
 * accomodate this.
 *
 * Note that the ECP registers may not start at offset 0x400 for PCI cards,
 * but rather will start at port->base_hi.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/pci.h>
#include <linux/sysctl.h>

#include <asm/io.h>
#include <asm/dma.h>
#include <asm/uaccess.h>

#include <linux/parport.h>
#include <linux/parport_pc.h>
#include <asm/parport.h>

/* ECR modes */
#define ECR_SPP 00
#define ECR_PS2 01
#define ECR_PPF 02
#define ECR_ECP 03
#define ECR_EPP 04
#define ECR_VND 05
#define ECR_TST 06
#define ECR_CNF 07

/* frob_control, but for ECR */
static void frob_econtrol (struct parport *pb, unsigned char m,
			   unsigned char v)
{
	unsigned char ectr = inb (ECONTROL (pb));
#ifdef DEBUG_PARPORT
	printk (KERN_DEBUG "frob_econtrol(%02x,%02x): %02x -> %02x\n",
		m, v, ectr, (ectr & ~m) ^ v);
#endif
	outb ((ectr & ~m) ^ v, ECONTROL (pb));
}

#ifdef CONFIG_PARPORT_PC_FIFO
/* Safely change the mode bits in the ECR */
static int change_mode(struct parport *p, int m)
{
	const struct parport_pc_private *priv = p->physport->private_data;
	int ecr = ECONTROL(p);
	unsigned char oecr;
	int mode;

	if (!priv->ecr) {
		printk (KERN_DEBUG "change_mode: but there's no ECR!\n");
		return 0;
	}

	/* Bits <7:5> contain the mode. */
	oecr = inb (ecr);
	mode = (oecr >> 5) & 0x7;
	if (mode == m) return 0;

	if (mode >= 2 && !(priv->ctr & 0x20)) {
		/* This mode resets the FIFO, so we may
		 * have to wait for it to drain first. */
		long expire = jiffies + p->physport->cad->timeout;
		int counter;
		switch (mode) {
		case ECR_PPF: /* Parallel Port FIFO mode */
		case ECR_ECP: /* ECP Parallel Port mode */
			/* Busy wait for 200us */
			for (counter = 0; counter < 40; counter++) {
				if (inb (ECONTROL (p)) & 0x01)
					break;
				if (signal_pending (current)) break;
				udelay (5);
			}

			/* Poll slowly. */
			while (!(inb (ECONTROL (p)) & 0x01)) {
				if (time_after_eq (jiffies, expire))
					/* The FIFO is stuck. */
					return -EBUSY;
				current->state = TASK_INTERRUPTIBLE;
				schedule_timeout ((HZ + 99) / 100);
				if (signal_pending (current))
					break;
			}
		}
	}

	if (mode >= 2 && m >= 2) {
		/* We have to go through mode 001 */
		oecr &= ~(7 << 5);
		oecr |= ECR_PS2 << 5;
		outb (oecr, ecr);
	}

	/* Set the mode. */
	oecr &= ~(7 << 5);
	oecr |= m << 5;
	outb (oecr, ecr);
	return 0;
}

#ifdef CONFIG_PARPORT_1284
/* Find FIFO lossage; FIFO is reset */
static int get_fifo_residue (struct parport *p)
{
	int residue;
	int cnfga;
	const struct parport_pc_private *priv = p->physport->private_data;

	/* Prevent further data transfer. */
	parport_frob_control (p,
			      PARPORT_CONTROL_STROBE,
			      PARPORT_CONTROL_STROBE);

	/* Adjust for the contents of the FIFO. */
	for (residue = priv->fifo_depth; ; residue--) {
		if (inb (ECONTROL (p)) & 0x2)
				/* Full up. */
			break;

		outb (0, FIFO (p));
	}

	printk (KERN_DEBUG "%s: %d PWords were left in FIFO\n", p->name,
		residue);

	/* Reset the FIFO. */
	frob_econtrol (p, 0xe0, ECR_PS2 << 5);
	parport_frob_control (p, PARPORT_CONTROL_STROBE, 0);

	/* Now change to config mode and clean up. FIXME */
	frob_econtrol (p, 0xe0, ECR_CNF << 5);
	cnfga = inb (CONFIGA (p));
	printk (KERN_DEBUG "%s: cnfgA contains 0x%02x\n", p->name, cnfga);

	if (!(cnfga & (1<<2))) {
		printk (KERN_DEBUG "%s: Accounting for extra byte\n", p->name);
		residue++;
	}

	/* Don't care about partial PWords until support is added for
	 * PWord != 1 byte. */

	/* Back to PS2 mode. */
	frob_econtrol (p, 0xe0, ECR_PS2 << 5);

	return residue;
}
#endif /* IEEE 1284 support */
#endif /* FIFO support */

/*
 * Clear TIMEOUT BIT in EPP MODE
 *
 * This is also used in SPP detection.
 */
static int clear_epp_timeout(struct parport *pb)
{
	unsigned char r;

	if (!(parport_pc_read_status(pb) & 0x01))
		return 1;

	/* To clear timeout some chips require double read */
	parport_pc_read_status(pb);
	r = parport_pc_read_status(pb);
	outb (r | 0x01, STATUS (pb)); /* Some reset by writing 1 */
	outb (r & 0xfe, STATUS (pb)); /* Others by writing 0 */
	r = parport_pc_read_status(pb);

	return !(r & 0x01);
}

/*
 * Access functions.
 *
 * Most of these aren't static because they may be used by the
 * parport_xxx_yyy macros.  extern __inline__ versions of several
 * of these are in parport_pc.h.
 */

static void parport_pc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	parport_generic_irq(irq, (struct parport *) dev_id, regs);
}

void parport_pc_write_data(struct parport *p, unsigned char d)
{
	outb (d, DATA (p));
}

unsigned char parport_pc_read_data(struct parport *p)
{
	return inb (DATA (p));
}

void parport_pc_write_control(struct parport *p, unsigned char d)
{
	const unsigned char wm = (PARPORT_CONTROL_STROBE |
				  PARPORT_CONTROL_AUTOFD |
				  PARPORT_CONTROL_INIT |
				  PARPORT_CONTROL_SELECT);

	/* Take this out when drivers have adapted to the newer interface. */
	if (d & 0x20) {
		printk (KERN_DEBUG "%s (%s): use data_reverse for this!\n",
			p->name, p->cad->name);
		parport_pc_data_reverse (p);
	}

	__parport_pc_frob_control (p, wm, d & wm);
}

unsigned char parport_pc_read_control(struct parport *p)
{
	const unsigned char wm = (PARPORT_CONTROL_STROBE |
				  PARPORT_CONTROL_AUTOFD |
				  PARPORT_CONTROL_INIT |
				  PARPORT_CONTROL_SELECT);
	const struct parport_pc_private *priv = p->physport->private_data;
	return priv->ctr & wm; /* Use soft copy */
}

unsigned char parport_pc_frob_control (struct parport *p, unsigned char mask,
				       unsigned char val)
{
	const unsigned char wm = (PARPORT_CONTROL_STROBE |
				  PARPORT_CONTROL_AUTOFD |
				  PARPORT_CONTROL_INIT |
				  PARPORT_CONTROL_SELECT);

	/* Take this out when drivers have adapted to the newer interface. */
	if (mask & 0x20) {
		printk (KERN_DEBUG "%s (%s): use data_%s for this!\n",
			p->name, p->cad->name,
			(val & 0x20) ? "reverse" : "forward");
		if (val & 0x20)
			parport_pc_data_reverse (p);
		else
			parport_pc_data_forward (p);
	}

	/* Restrict mask and val to control lines. */
	mask &= wm;
	val &= wm;

	return __parport_pc_frob_control (p, mask, val);
}

unsigned char parport_pc_read_status(struct parport *p)
{
	return inb (STATUS (p));
}

void parport_pc_disable_irq(struct parport *p)
{
	__parport_pc_frob_control (p, 0x10, 0);
}

void parport_pc_enable_irq(struct parport *p)
{
	__parport_pc_frob_control (p, 0x10, 0x10);
}

void parport_pc_data_forward (struct parport *p)
{
	__parport_pc_frob_control (p, 0x20, 0);
}

void parport_pc_data_reverse (struct parport *p)
{
	__parport_pc_frob_control (p, 0x20, 0x20);
}

void parport_pc_init_state(struct pardevice *dev, struct parport_state *s)
{
	s->u.pc.ctr = 0xc | (dev->irq_func ? 0x10 : 0x0);
	s->u.pc.ecr = 0x24;
}

void parport_pc_save_state(struct parport *p, struct parport_state *s)
{
	const struct parport_pc_private *priv = p->physport->private_data;
	s->u.pc.ctr = inb (CONTROL (p));
	if (priv->ecr)
		s->u.pc.ecr = inb (ECONTROL (p));
}

void parport_pc_restore_state(struct parport *p, struct parport_state *s)
{
	const struct parport_pc_private *priv = p->physport->private_data;
	outb (s->u.pc.ctr, CONTROL (p));
	if (priv->ecr)
		outb (s->u.pc.ecr, ECONTROL (p));
}

#ifdef CONFIG_PARPORT_1284
static size_t parport_pc_epp_read_data (struct parport *port, void *buf,
					size_t length, int flags)
{
	size_t got = 0;
	for (; got < length; got++) {
		*((char*)buf)++ = inb (EPPDATA(port));
		if (inb (STATUS(port)) & 0x01) {
			clear_epp_timeout (port);
			break;
		}
	}

	return got;
}

static size_t parport_pc_epp_write_data (struct parport *port, const void *buf,
					 size_t length, int flags)
{
	size_t written = 0;
	for (; written < length; written++) {
		outb (*((char*)buf)++, EPPDATA(port));
		if (inb (STATUS(port)) & 0x01) {
			clear_epp_timeout (port);
			break;
		}
	}

	return written;
}

static size_t parport_pc_epp_read_addr (struct parport *port, void *buf,
					size_t length, int flags)
{
	size_t got = 0;
	for (; got < length; got++) {
		*((char*)buf)++ = inb (EPPADDR (port));
		if (inb (STATUS (port)) & 0x01) {
			clear_epp_timeout (port);
			break;
		}
	}

	return got;
}

static size_t parport_pc_epp_write_addr (struct parport *port,
					 const void *buf, size_t length,
					 int flags)
{
	size_t written = 0;
	for (; written < length; written++) {
		outb (*((char*)buf)++, EPPADDR (port));
		if (inb (STATUS (port)) & 0x01) {
			clear_epp_timeout (port);
			break;
		}
	}

	return written;
}

static size_t parport_pc_ecpepp_read_data (struct parport *port, void *buf,
					   size_t length, int flags)
{
	size_t got;

	frob_econtrol (port, 0xe0, ECR_EPP << 5);
	got = parport_pc_epp_read_data (port, buf, length, flags);
	frob_econtrol (port, 0xe0, ECR_PS2 << 5);

	return got;
}

static size_t parport_pc_ecpepp_write_data (struct parport *port,
					    const void *buf, size_t length,
					    int flags)
{
	size_t written;

	frob_econtrol (port, 0xe0, ECR_EPP << 5);
	written = parport_pc_epp_write_data (port, buf, length, flags);
	frob_econtrol (port, 0xe0, ECR_PS2 << 5);

	return written;
}

static size_t parport_pc_ecpepp_read_addr (struct parport *port, void *buf,
					   size_t length, int flags)
{
	size_t got;

	frob_econtrol (port, 0xe0, ECR_EPP << 5);
	got = parport_pc_epp_read_addr (port, buf, length, flags);
	frob_econtrol (port, 0xe0, ECR_PS2 << 5);

	return got;
}

static size_t parport_pc_ecpepp_write_addr (struct parport *port,
					    const void *buf, size_t length,
					    int flags)
{
	size_t written;

	frob_econtrol (port, 0xe0, ECR_EPP << 5);
	written = parport_pc_epp_write_addr (port, buf, length, flags);
	frob_econtrol (port, 0xe0, ECR_PS2 << 5);

	return written;
}
#endif /* IEEE 1284 support */

#ifdef CONFIG_PARPORT_PC_FIFO
static size_t parport_pc_fifo_write_block_pio (struct parport *port,
					       const void *buf, size_t length)
{
	int ret = 0;
	const unsigned char *bufp = buf;
	size_t left = length;
	long expire = jiffies + port->physport->cad->timeout;
	const int fifo = FIFO (port);
	int poll_for = 8; /* 80 usecs */
	const struct parport_pc_private *priv = port->physport->private_data;
	const int fifo_depth = priv->fifo_depth;

	port = port->physport;

	/* We don't want to be interrupted every character. */
	parport_pc_disable_irq (port);
	frob_econtrol (port, (1<<4), (1<<4)); /* nErrIntrEn */

	/* Forward mode. */
	parport_pc_data_forward (port); /* Must be in PS2 mode */

	while (left) {
		unsigned char byte;
		unsigned char ecrval = inb (ECONTROL (port));
		int i = 0;

		if (current->need_resched && time_before (jiffies, expire))
			/* Can't yield the port. */
			schedule ();

		/* Anyone else waiting for the port? */
		if (port->waithead) {
			printk (KERN_DEBUG "Somebody wants the port\n");
			break;
		}

		if (ecrval & 0x02) {
			/* FIFO is full. Wait for interrupt. */

			/* Clear serviceIntr */
			outb (ecrval & ~(1<<2), ECONTROL (port));
		false_alarm:
			ret = parport_wait_event (port, HZ);
			if (ret < 0) break;
			ret = 0;
			if (!time_before (jiffies, expire)) {
				/* Timed out. */
				printk (KERN_DEBUG "Timed out\n");
				break;
			}
			ecrval = inb (ECONTROL (port));
			if (!(ecrval & (1<<2))) {
				if (current->need_resched &&
				    time_before (jiffies, expire))
					schedule ();

				goto false_alarm;
			}

			continue;
		}

		/* Can't fail now. */
		expire = jiffies + port->cad->timeout;

	poll:
		if (signal_pending (current))
			break;

		if (ecrval & 0x01) {
			/* FIFO is empty. Blast it full. */
			const int n = left < fifo_depth ? left : fifo_depth;
			outsb (fifo, bufp, n);
			bufp += n;
			left -= n;

			/* Adjust the poll time. */
			if (i < (poll_for - 2)) poll_for--;
			continue;
		} else if (i++ < poll_for) {
			udelay (10);
			ecrval = inb (ECONTROL (port));
			goto poll;
		}

		/* Half-full (call me an optimist) */
		byte = *bufp++;
		outb (byte, fifo);
		left--;
        }

	return length - left;
}

static size_t parport_pc_fifo_write_block_dma (struct parport *port,
					       const void *buf, size_t length)
{
	int ret = 0;
	unsigned long dmaflag;
	size_t left  = length;
	const struct parport_pc_private *priv = port->physport->private_data;

	port = port->physport;

	/* We don't want to be interrupted every character. */
	parport_pc_disable_irq (port);
	frob_econtrol (port, (1<<4), (1<<4)); /* nErrIntrEn */

	/* Forward mode. */
	parport_pc_data_forward (port); /* Must be in PS2 mode */

	while (left) {
		long expire = jiffies + port->physport->cad->timeout;

		size_t count = left;

		if (count > PAGE_SIZE)
			count = PAGE_SIZE;

		memcpy(priv->dma_buf, buf, count);

		dmaflag = claim_dma_lock();
		disable_dma(port->dma);
		clear_dma_ff(port->dma);
		set_dma_mode(port->dma, DMA_MODE_WRITE);
		set_dma_addr(port->dma, virt_to_bus((volatile char *) priv->dma_buf));
		set_dma_count(port->dma, count);

		/* Set DMA mode */
		frob_econtrol (port, 1<<3, 1<<3);

		/* Clear serviceIntr */
		frob_econtrol (port, 1<<2, 0);

		enable_dma(port->dma);
		release_dma_lock(dmaflag);

		/* assume DMA will be successful */
		left -= count;
		buf  += count;

		/* Wait for interrupt. */
	false_alarm:
		ret = parport_wait_event (port, HZ);
		if (ret < 0) break;
		ret = 0;
		if (!time_before (jiffies, expire)) {
			/* Timed out. */
			printk (KERN_DEBUG "Timed out\n");
			break;
		}
		/* Is serviceIntr set? */
		if (!(inb (ECONTROL (port)) & (1<<2))) {
			if (current->need_resched)
				schedule ();

			goto false_alarm;
		}

		dmaflag = claim_dma_lock();
		disable_dma(port->dma);
		clear_dma_ff(port->dma);
		count = get_dma_residue(port->dma);
		release_dma_lock(dmaflag);

		if (current->need_resched)
			/* Can't yield the port. */
			schedule ();

		/* Anyone else waiting for the port? */
		if (port->waithead) {
			printk (KERN_DEBUG "Somebody wants the port\n");
			break;
		}

		/* update for possible DMA residue ! */
		buf  -= count;
		left += count;
	}

	/* Maybe got here through break, so adjust for DMA residue! */
	dmaflag = claim_dma_lock();
	disable_dma(port->dma);
	clear_dma_ff(port->dma);
	left += get_dma_residue(port->dma);
	release_dma_lock(dmaflag);

	/* Turn off DMA mode */
	frob_econtrol (port, 1<<3, 0);

	return length - left;
}

/* Parallel Port FIFO mode (ECP chipsets) */
size_t parport_pc_compat_write_block_pio (struct parport *port,
					  const void *buf, size_t length,
					  int flags)
{
	size_t written;

	/* Special case: a timeout of zero means we cannot call schedule(). */
	if (!port->physport->cad->timeout)
		return parport_ieee1284_write_compat (port, buf,
						      length, flags);

	/* Set up parallel port FIFO mode.*/
	parport_pc_data_forward (port); /* Must be in PS2 mode */
	change_mode (port, ECR_PPF); /* Parallel port FIFO */
	port->physport->ieee1284.phase = IEEE1284_PH_FWD_DATA;

	/* Write the data to the FIFO. */
	if (port->dma != PARPORT_DMA_NONE)
		written = parport_pc_fifo_write_block_dma (port, buf, length);
	else
		written = parport_pc_fifo_write_block_pio (port, buf, length);

	/* Finish up. */
	if (change_mode (port, ECR_PS2) == -EBUSY) {
		const struct parport_pc_private *priv = 
			port->physport->private_data;

		printk (KERN_DEBUG "%s: FIFO is stuck\n", port->name);

		/* Prevent further data transfer. */
		parport_frob_control (port,
				      PARPORT_CONTROL_STROBE,
				      PARPORT_CONTROL_STROBE);

		/* Adjust for the contents of the FIFO. */
		for (written -= priv->fifo_depth; ; written++) {
			if (inb (ECONTROL (port)) & 0x2)
				/* Full up. */
				break;

			outb (0, FIFO (port));
		}

		/* Reset the FIFO and return to PS2 mode. */
		frob_econtrol (port, 0xe0, ECR_PS2 << 5);

		/* De-assert strobe. */
		parport_frob_control (port, PARPORT_CONTROL_STROBE, 0);
	}

	parport_wait_peripheral (port,
				 PARPORT_STATUS_BUSY,
				 PARPORT_STATUS_BUSY);
	port->physport->ieee1284.phase = IEEE1284_PH_FWD_IDLE;

	return written;
}

/* ECP */
#ifdef CONFIG_PARPORT_1284
size_t parport_pc_ecp_write_block_pio (struct parport *port,
				       const void *buf, size_t length,
				       int flags)
{
	size_t written;

	/* Special case: a timeout of zero means we cannot call schedule(). */
	if (!port->physport->cad->timeout)
		return parport_ieee1284_ecp_write_data (port, buf,
							length, flags);

	/* Switch to forward mode if necessary. */
	if (port->physport->ieee1284.phase != IEEE1284_PH_FWD_IDLE) {
		/* Event 47: Set nInit high. */
		parport_frob_control (port, PARPORT_CONTROL_INIT, 0);

		/* Event 40: PError goes high. */
		parport_wait_peripheral (port,
					 PARPORT_STATUS_PAPEROUT,
					 PARPORT_STATUS_PAPEROUT);
	}

	/* Set up ECP parallel port mode.*/
	parport_pc_data_forward (port); /* Must be in PS2 mode */
	change_mode (port, ECR_ECP); /* ECP FIFO */
	port->physport->ieee1284.phase = IEEE1284_PH_FWD_DATA;

	/* Write the data to the FIFO. */
	if (port->dma != PARPORT_DMA_NONE)
		written = parport_pc_fifo_write_block_dma (port, buf, length);
	else
		written = parport_pc_fifo_write_block_pio (port, buf, length);

	/* Finish up. */
	if (change_mode (port, ECR_PS2) == -EBUSY) {
		const struct parport_pc_private *priv =
			port->physport->private_data;

		printk (KERN_DEBUG "%s: FIFO is stuck\n", port->name);

		/* Prevent further data transfer. */
		parport_frob_control (port,
				      PARPORT_CONTROL_STROBE,
				      PARPORT_CONTROL_STROBE);

		/* Adjust for the contents of the FIFO. */
		for (written -= priv->fifo_depth; ; written++) {
			if (inb (ECONTROL (port)) & 0x2)
				/* Full up. */
				break;

			outb (0, FIFO (port));
		}

		/* Reset the FIFO and return to PS2 mode. */
		frob_econtrol (port, 0xe0, ECR_PS2 << 5);

		/* De-assert strobe. */
		parport_frob_control (port, PARPORT_CONTROL_STROBE, 0);

		/* Host transfer recovery. */
		parport_pc_data_reverse (port); /* Must be in PS2 mode */
		udelay (5);
		parport_frob_control (port, PARPORT_CONTROL_INIT, 0);
		parport_wait_peripheral (port, PARPORT_STATUS_PAPEROUT, 0);
		parport_frob_control (port,
				      PARPORT_CONTROL_INIT,
				      PARPORT_CONTROL_INIT);
		parport_wait_peripheral (port,
					 PARPORT_STATUS_PAPEROUT,
					 PARPORT_STATUS_PAPEROUT);
	}

	parport_wait_peripheral (port,
				 PARPORT_STATUS_BUSY, 
				 PARPORT_STATUS_BUSY);
	port->physport->ieee1284.phase = IEEE1284_PH_FWD_IDLE;

	return written;
}

size_t parport_pc_ecp_read_block_pio (struct parport *port,
				      void *buf, size_t length, int flags)
{
	size_t left = length;
	size_t fifofull;
	const int fifo = FIFO(port);
	const struct parport_pc_private *priv = port->physport->private_data;
	const int fifo_depth = priv->fifo_depth;
	char *bufp = buf;

	port = port->physport;

	/* Special case: a timeout of zero means we cannot call schedule(). */
	if (!port->cad->timeout)
		return parport_ieee1284_ecp_read_data (port, buf,
						       length, flags);

	fifofull = fifo_depth;
	if (port->ieee1284.mode == IEEE1284_MODE_ECPRLE)
		/* If the peripheral is allowed to send RLE compressed
		 * data, it is possible for a byte to expand to 128
		 * bytes in the FIFO. */
		fifofull = 128;

	/* If the caller wants less than a full FIFO's worth of data,
	 * go through software emulation.  Otherwise we may have to through
	 * away data. */
	if (length < fifofull)
		return parport_ieee1284_ecp_read_data (port, buf,
						       length, flags);

	/* Switch to reverse mode if necessary. */
	if (port->ieee1284.phase != IEEE1284_PH_REV_IDLE) {
		/* Event 38: Set nAutoFd low */
		parport_frob_control (port,
				      PARPORT_CONTROL_AUTOFD,
				      PARPORT_CONTROL_AUTOFD);
		parport_pc_data_reverse (port); /* Must be in PS2 mode */
		udelay (5);

		/* Event 39: Set nInit low to initiate bus reversal */
		parport_frob_control (port,
				      PARPORT_CONTROL_INIT,
				      0);

		/* Event 40: PError goes low */
		parport_wait_peripheral (port, PARPORT_STATUS_PAPEROUT, 0);
	}

	/* Set up ECP parallel port mode.*/
	parport_pc_data_reverse (port); /* Must be in PS2 mode */
	change_mode (port, ECR_ECP); /* ECP FIFO */
	port->ieee1284.phase = IEEE1284_PH_REV_DATA;

	/* Do the transfer. */
	while (left > fifofull) {
		int ret;
		long int expire = jiffies + port->cad->timeout;
		unsigned char ecrval = inb (ECONTROL (port));

		if (current->need_resched && time_before (jiffies, expire))
			/* Can't yield the port. */
			schedule ();

		/* At this point, the FIFO may already be full.
		 * Ideally, we'd be able to tell the port to hold on
		 * for a second while we empty the FIFO, and we'd be
		 * able to ensure that no data is lost.  I'm not sure
		 * that's the case. :-(  It might be that you can play
		 * games with STB, as in the forward case; someone should
		 * look at a datasheet. */

		if (ecrval & 0x01) {
			/* FIFO is empty. Wait for interrupt. */

			/* Anyone else waiting for the port? */
			if (port->waithead) {
				printk (KERN_DEBUG
					"Somebody wants the port\n");
				break;
			}

			/* Clear serviceIntr */
			outb (ecrval & ~(1<<2), ECONTROL (port));
		false_alarm:
			ret = parport_wait_event (port, HZ);
			if (ret < 0) break;
			ret = 0;
			if (!time_before (jiffies, expire)) {
				/* Timed out. */
				printk (KERN_DEBUG "Timed out\n");
				break;
			}
			ecrval = inb (ECONTROL (port));
			if (!(ecrval & (1<<2))) {
				if (current->need_resched &&
				    time_before (jiffies, expire))
					schedule ();

				goto false_alarm;
			}

			continue;
		}

		if (ecrval & 0x02) {
			/* FIFO is full. */
			insb (fifo, bufp, fifo_depth);
			bufp += fifo_depth;
			left -= fifo_depth;
			continue;
		}

		*bufp++ = inb (fifo);
		left--;
	}

	/* Finish up. */
	if (change_mode (port, ECR_PS2) == -EBUSY) {
		int lost = get_fifo_residue (port);
		printk (KERN_DEBUG "%s: DATA LOSS (%d bytes)!\n", port->name,
			lost);
	}

	port->ieee1284.phase = IEEE1284_PH_REV_IDLE;

	return length - left;
}

#endif /* IEEE 1284 support */

#endif /* Allowed to use FIFO/DMA */

void parport_pc_inc_use_count(void)
{
#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
}

void parport_pc_dec_use_count(void)
{
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
}

struct parport_operations parport_pc_ops = 
{
	parport_pc_write_data,
	parport_pc_read_data,

	parport_pc_write_control,
	parport_pc_read_control,
	parport_pc_frob_control,

	parport_pc_read_status,

	parport_pc_enable_irq,
	parport_pc_disable_irq,

	parport_pc_data_forward,
	parport_pc_data_reverse,

	parport_pc_init_state,
	parport_pc_save_state,
	parport_pc_restore_state,

	parport_pc_inc_use_count,
	parport_pc_dec_use_count,

	parport_ieee1284_epp_write_data,
	parport_ieee1284_epp_read_data,
	parport_ieee1284_epp_write_addr,
	parport_ieee1284_epp_read_addr,

	parport_ieee1284_ecp_write_data,
	parport_ieee1284_ecp_read_data,
	parport_ieee1284_ecp_write_addr,

	parport_ieee1284_write_compat,
	parport_ieee1284_read_nibble,
	parport_ieee1284_read_byte,
};

/* --- Mode detection ------------------------------------- */

/*
 * Checks for port existence, all ports support SPP MODE
 */
static int __maybe_init parport_SPP_supported(struct parport *pb)
{
	unsigned char r, w;

	/*
	 * first clear an eventually pending EPP timeout 
	 * I (sailer@ife.ee.ethz.ch) have an SMSC chipset
	 * that does not even respond to SPP cycles if an EPP
	 * timeout is pending
	 */
	clear_epp_timeout(pb);

	/* Do a simple read-write test to make sure the port exists. */
	w = 0xc;
	outb (w, CONTROL (pb));

	/* Is there a control register that we can read from?  Some
	 * ports don't allow reads, so read_control just returns a
	 * software copy. Some ports _do_ allow reads, so bypass the
	 * software copy here.  In addition, some bits aren't
	 * writable. */
	r = inb (CONTROL (pb));
	if ((r & 0xf) == w) {
		w = 0xe;
		outb (w, CONTROL (pb));
		r = inb (CONTROL (pb));
		outb (0xc, CONTROL (pb));
		if ((r & 0xf) == w)
			return PARPORT_MODE_PCSPP;
	}

	if (user_specified)
		/* That didn't work, but the user thinks there's a
		 * port here. */
		printk (KERN_DEBUG "0x%lx: CTR: wrote 0x%02x, read 0x%02x\n",
			pb->base, w, r);

	/* Try the data register.  The data lines aren't tri-stated at
	 * this stage, so we expect back what we wrote. */
	w = 0xaa;
	parport_pc_write_data (pb, w);
	r = parport_pc_read_data (pb);
	if (r == w) {
		w = 0x55;
		parport_pc_write_data (pb, w);
		r = parport_pc_read_data (pb);
		if (r == w)
			return PARPORT_MODE_PCSPP;
	}

	if (user_specified)
		/* Didn't work, but the user is convinced this is the
		 * place. */
		printk (KERN_DEBUG "0x%lx: DATA: wrote 0x%02x, read 0x%02x\n",
			pb->base, w, r);

	/* It's possible that we can't read the control register or
	 * the data register.  In that case just believe the user. */
	if (user_specified)
		return PARPORT_MODE_PCSPP;

	return 0;
}

/* Check for ECR
 *
 * Old style XT ports alias io ports every 0x400, hence accessing ECR
 * on these cards actually accesses the CTR.
 *
 * Modern cards don't do this but reading from ECR will return 0xff
 * regardless of what is written here if the card does NOT support
 * ECP.
 *
 * We first check to see if ECR is the same as CTR.  If not, the low
 * two bits of ECR aren't writable, so we check by writing ECR and
 * reading it back to see if it's what we expect.
 */
static int __maybe_init parport_ECR_present(struct parport *pb)
{
	struct parport_pc_private *priv = pb->private_data;
	unsigned char r = 0xc;

	outb (r, CONTROL (pb));
	if ((inb (ECONTROL (pb)) & 0x3) == (r & 0x3)) {
		outb (r ^ 0x2, CONTROL (pb)); /* Toggle bit 1 */

		r = inb (CONTROL (pb));
		if ((inb (ECONTROL (pb)) & 0x2) == (r & 0x2))
			goto no_reg; /* Sure that no ECR register exists */
	}
	
	if ((inb (ECONTROL (pb)) & 0x3 ) != 0x1)
		goto no_reg;

	outb (0x34, ECONTROL (pb));
	if (inb (ECONTROL (pb)) != 0x35)
		goto no_reg;

	priv->ecr = 1;
	outb (0xc, CONTROL (pb));
	
	/* Go to mode 000 */
	frob_econtrol (pb, 0xe0, ECR_SPP << 5);

	return 1;

 no_reg:
	outb (0xc, CONTROL (pb));
	return 0; 
}

#ifdef CONFIG_PARPORT_1284
/* Detect PS/2 support.
 *
 * Bit 5 (0x20) sets the PS/2 data direction; setting this high
 * allows us to read data from the data lines.  In theory we would get back
 * 0xff but any peripheral attached to the port may drag some or all of the
 * lines down to zero.  So if we get back anything that isn't the contents
 * of the data register we deem PS/2 support to be present. 
 *
 * Some SPP ports have "half PS/2" ability - you can't turn off the line
 * drivers, but an external peripheral with sufficiently beefy drivers of
 * its own can overpower them and assert its own levels onto the bus, from
 * where they can then be read back as normal.  Ports with this property
 * and the right type of device attached are likely to fail the SPP test,
 * (as they will appear to have stuck bits) and so the fact that they might
 * be misdetected here is rather academic. 
 */

static int __maybe_init parport_PS2_supported(struct parport *pb)
{
	int ok = 0;
  
	clear_epp_timeout(pb);

	/* try to tri-state the buffer */
	parport_pc_data_reverse (pb);
	
	parport_pc_write_data(pb, 0x55);
	if (parport_pc_read_data(pb) != 0x55) ok++;

	parport_pc_write_data(pb, 0xaa);
	if (parport_pc_read_data(pb) != 0xaa) ok++;

	/* cancel input mode */
	parport_pc_data_forward (pb);

	if (ok) {
		pb->modes |= PARPORT_MODE_TRISTATE;
	} else {
		struct parport_pc_private *priv = pb->private_data;
		priv->ctr_writable &= ~0x20;
	}

	return ok;
}

static int __maybe_init parport_ECP_supported(struct parport *pb)
{
	int i;
	int config;
	int pword;
	struct parport_pc_private *priv = pb->private_data;

	/* If there is no ECR, we have no hope of supporting ECP. */
	if (!priv->ecr)
		return 0;

	/* Find out FIFO depth */
	outb (ECR_SPP << 5, ECONTROL (pb)); /* Reset FIFO */
	outb (ECR_TST << 5, ECONTROL (pb)); /* TEST FIFO */
	for (i=0; i < 1024 && !(inb (ECONTROL (pb)) & 0x02); i++)
		outb (0xaa, FIFO (pb));

	/*
	 * Using LGS chipset it uses ECR register, but
	 * it doesn't support ECP or FIFO MODE
	 */
	if (i == 1024) {
		outb (ECR_SPP << 5, ECONTROL (pb));
		return 0;
	}

	priv->fifo_depth = i;
	printk (KERN_INFO "0x%lx: FIFO is %d bytes\n", pb->base, i);

	/* Find out writeIntrThreshold */
	frob_econtrol (pb, 1<<2, 1<<2);
	frob_econtrol (pb, 1<<2, 0);
	for (i = 1; i <= priv->fifo_depth; i++) {
		inb (FIFO (pb));
		udelay (50);
		if (inb (ECONTROL (pb)) & (1<<2))
			break;
	}

	if (i <= priv->fifo_depth)
		printk (KERN_INFO "0x%lx: writeIntrThreshold is %d\n",
			pb->base, i);
	else
		/* Number of bytes we know we can write if we get an
                   interrupt. */
		i = 0;

	priv->writeIntrThreshold = i;

	/* Find out readIntrThreshold */
	frob_econtrol (pb, 0xe0, ECR_PS2 << 5); /* Reset FIFO and enable PS2 */
	parport_pc_data_reverse (pb); /* Must be in PS2 mode */
	frob_econtrol (pb, 0xe0, ECR_TST << 5); /* Test FIFO */
	frob_econtrol (pb, 1<<2, 1<<2);
	frob_econtrol (pb, 1<<2, 0);
	for (i = 1; i <= priv->fifo_depth; i++) {
		outb (0xaa, FIFO (pb));
		if (inb (ECONTROL (pb)) & (1<<2))
			break;
	}

	if (i <= priv->fifo_depth)
		printk (KERN_INFO "0x%lx: readIntrThreshold is %d\n",
			pb->base, i);
	else
		/* Number of bytes we can read if we get an interrupt. */
		i = 0;

	priv->readIntrThreshold = i;

	outb (ECR_SPP << 5, ECONTROL (pb)); /* Reset FIFO */
	outb (0xf4, ECONTROL (pb)); /* Configuration mode */
	config = inb (FIFO (pb));
	pword = (config >> 4) & 0x7;
	switch (pword) {
	case 0:
		pword = 2;
		printk (KERN_WARNING "0x%lx: Unsupported pword size!\n",
			pb->base);
		break;
	case 2:
		pword = 4;
		printk (KERN_WARNING "0x%lx: Unsupported pword size!\n",
			pb->base);
		break;
	default:
		printk (KERN_WARNING "0x%lx: Unknown implementation ID\n",
			pb->base);
		/* Assume 1 */
	case 1:
		pword = 1;
	}
	priv->pword = pword;
	printk (KERN_DEBUG "0x%lx: PWord is %d bits\n", pb->base, 8 * pword);

	config = inb (CONFIGB (pb));
	printk (KERN_DEBUG "0x%lx: Interrupts are ISA-%s\n", pb->base,
		config & 0x80 ? "Level" : "Pulses");

	if (!(config & 0x40)) {
		printk (KERN_WARNING "0x%lx: IRQ conflict!\n", pb->base);
		pb->irq = PARPORT_IRQ_NONE;
	}

	/* Go back to mode 000 */
	frob_econtrol (pb, 0xe0, ECR_SPP << 5);
	pb->modes |= PARPORT_MODE_ECP;

	return 1;
}

static int __maybe_init parport_ECPPS2_supported(struct parport *pb)
{
	const struct parport_pc_private *priv = pb->private_data;
	int result;
	unsigned char oecr;

	if (!priv->ecr)
		return 0;

	oecr = inb (ECONTROL (pb));
	outb (ECR_PS2 << 5, ECONTROL (pb));
	
	result = parport_PS2_supported(pb);

	outb (oecr, ECONTROL (pb));
	return result;
}

/* EPP mode detection  */

static int __maybe_init parport_EPP_supported(struct parport *pb)
{
	const struct parport_pc_private *priv = pb->private_data;

	/*
	 * Theory:
	 *	Bit 0 of STR is the EPP timeout bit, this bit is 0
	 *	when EPP is possible and is set high when an EPP timeout
	 *	occurs (EPP uses the HALT line to stop the CPU while it does
	 *	the byte transfer, an EPP timeout occurs if the attached
	 *	device fails to respond after 10 micro seconds).
	 *
	 *	This bit is cleared by either reading it (National Semi)
	 *	or writing a 1 to the bit (SMC, UMC, WinBond), others ???
	 *	This bit is always high in non EPP modes.
	 */

	/* If EPP timeout bit clear then EPP available */
	if (!clear_epp_timeout(pb))
		return 0;  /* No way to clear timeout */

	/* Check for Intel bug. */
	if (priv->ecr) {
		unsigned char i;
		for (i = 0x00; i < 0x80; i += 0x20) {
			outb (i, ECONTROL (pb));
			if (clear_epp_timeout (pb))
				/* Phony EPP in ECP. */
				return 0;
		}
	}

	pb->modes |= PARPORT_MODE_EPP;

	/* Set up access functions to use EPP hardware. */
	pb->ops->epp_read_data = parport_pc_epp_read_data;
	pb->ops->epp_write_data = parport_pc_epp_write_data;
	pb->ops->epp_read_addr = parport_pc_epp_read_addr;
	pb->ops->epp_write_addr = parport_pc_epp_write_addr;

	return 1;
}

static int __maybe_init parport_ECPEPP_supported(struct parport *pb)
{
	struct parport_pc_private *priv = pb->private_data;
	int result;
	unsigned char oecr;

	if (!priv->ecr)
		return 0;

	oecr = inb (ECONTROL (pb));
	/* Search for SMC style EPP+ECP mode */
	outb (0x80, ECONTROL (pb));
	
	result = parport_EPP_supported(pb);

	outb (oecr, ECONTROL (pb));

	if (result) {
		/* Set up access functions to use ECP+EPP hardware. */
		pb->ops->epp_read_data = parport_pc_ecpepp_read_data;
		pb->ops->epp_write_data = parport_pc_ecpepp_write_data;
		pb->ops->epp_read_addr = parport_pc_ecpepp_read_addr;
		pb->ops->epp_write_addr = parport_pc_ecpepp_write_addr;
	}

	return result;
}

#else /* No IEEE 1284 support */

/* Don't bother probing for modes we know we won't use. */
static int __maybe_init parport_PS2_supported(struct parport *pb) { return 0; }
static int __maybe_init parport_ECP_supported(struct parport *pb) { return 0; }
static int __maybe_init parport_EPP_supported(struct parport *pb) { return 0; }
static int __maybe_init parport_ECPEPP_supported(struct parport *pb){return 0;}
static int __maybe_init parport_ECPPS2_supported(struct parport *pb){return 0;}

#endif /* No IEEE 1284 support */

/* --- IRQ detection -------------------------------------- */

/* Only if supports ECP mode */
static int __maybe_init programmable_irq_support(struct parport *pb)
{
	int irq, intrLine;
	unsigned char oecr = inb (ECONTROL (pb));
	static const int lookup[8] = {
		PARPORT_IRQ_NONE, 7, 9, 10, 11, 14, 15, 5
	};

	outb (ECR_CNF << 5, ECONTROL (pb)); /* Configuration MODE */

	intrLine = (inb (CONFIGB (pb)) >> 3) & 0x07;
	irq = lookup[intrLine];

	outb (oecr, ECONTROL (pb));
	return irq;
}

static int __maybe_init irq_probe_ECP(struct parport *pb)
{
	int i;
	unsigned long irqs;

	sti();
	irqs = probe_irq_on();
		
	outb (ECR_SPP << 5, ECONTROL (pb)); /* Reset FIFO */
	outb ((ECR_TST << 5) | 0x04, ECONTROL (pb));
	outb (ECR_TST << 5, ECONTROL (pb));

	/* If Full FIFO sure that writeIntrThreshold is generated */
	for (i=0; i < 1024 && !(inb (ECONTROL (pb)) & 0x02) ; i++) 
		outb (0xaa, FIFO (pb));
		
	pb->irq = probe_irq_off(irqs);
	outb (ECR_SPP << 5, ECONTROL (pb));

	if (pb->irq <= 0)
		pb->irq = PARPORT_IRQ_NONE;

	return pb->irq;
}

/*
 * This detection seems that only works in National Semiconductors
 * This doesn't work in SMC, LGS, and Winbond 
 */
static int __maybe_init irq_probe_EPP(struct parport *pb)
{
#ifndef ADVANCED_DETECT
	return PARPORT_IRQ_NONE;
#else
	int irqs;
	unsigned char oecr;

	if (pb->modes & PARPORT_MODE_PCECR)
		oecr = inb (ECONTROL (pb));

	sti();
	irqs = probe_irq_on();

	if (pb->modes & PARPORT_MODE_PCECR)
		frob_econtrol (pb, 0x10, 0x10);
	
	clear_epp_timeout(pb);
	parport_pc_frob_control (pb, 0x20, 0x20);
	parport_pc_frob_control (pb, 0x10, 0x10);
	clear_epp_timeout(pb);

	/* Device isn't expecting an EPP read
	 * and generates an IRQ.
	 */
	parport_pc_read_epp(pb);
	udelay(20);

	pb->irq = probe_irq_off (irqs);
	if (pb->modes & PARPORT_MODE_PCECR)
		outb (oecr, ECONTROL (pb));
	parport_pc_write_control(pb, 0xc);

	if (pb->irq <= 0)
		pb->irq = PARPORT_IRQ_NONE;

	return pb->irq;
#endif /* Advanced detection */
}

static int __maybe_init irq_probe_SPP(struct parport *pb)
{
	/* Don't even try to do this. */
	return PARPORT_IRQ_NONE;
}

/* We will attempt to share interrupt requests since other devices
 * such as sound cards and network cards seem to like using the
 * printer IRQs.
 *
 * When ECP is available we can autoprobe for IRQs.
 * NOTE: If we can autoprobe it, we can register the IRQ.
 */
static int __maybe_init parport_irq_probe(struct parport *pb)
{
	const struct parport_pc_private *priv = pb->private_data;

	if (priv->ecr) {
		pb->irq = programmable_irq_support(pb);
		if (pb->irq != PARPORT_IRQ_NONE)
			goto out;
	}

	if (pb->modes & PARPORT_MODE_ECP)
		pb->irq = irq_probe_ECP(pb);

	if (pb->irq == PARPORT_IRQ_NONE && priv->ecr &&
	    (pb->modes & PARPORT_MODE_EPP))
		pb->irq = irq_probe_EPP(pb);

	clear_epp_timeout(pb);

	if (pb->irq == PARPORT_IRQ_NONE && (pb->modes & PARPORT_MODE_EPP))
		pb->irq = irq_probe_EPP(pb);

	clear_epp_timeout(pb);

	if (pb->irq == PARPORT_IRQ_NONE)
		pb->irq = irq_probe_SPP(pb);

out:
	return pb->irq;
}

/* --- DMA detection -------------------------------------- */

/* Only if supports ECP mode */
static int __maybe_init programmable_dma_support (struct parport *p)
{
	unsigned char oecr = inb (ECONTROL (p));
	int dma;

	frob_econtrol (p, 0xe0, ECR_CNF << 5);
	
	dma = inb (CONFIGB(p)) & 0x03;
	if (!dma)
		dma = PARPORT_DMA_NONE;

	outb (oecr, ECONTROL (p));
	return dma;
}

static int __maybe_init parport_dma_probe (struct parport *p)
{
	const struct parport_pc_private *priv = p->private_data;
	if (priv->ecr)
		p->dma = programmable_dma_support(p);

	return p->dma;
}

/* --- Initialisation code -------------------------------- */

struct parport *__maybe_init parport_pc_probe_port (unsigned long int base,
						    unsigned long int base_hi,
						    int irq, int dma)
{
	struct parport_pc_private *priv;
	struct parport_operations *ops;
	struct parport tmp;
	struct parport *p = &tmp;
	int probedirq = PARPORT_IRQ_NONE;
	if (check_region(base, 3)) return NULL;
	priv = kmalloc (sizeof (struct parport_pc_private), GFP_KERNEL);
	if (!priv) {
		printk (KERN_DEBUG "parport (0x%lx): no memory!\n", base);
		return NULL;
	}
	ops = kmalloc (sizeof (struct parport_operations), GFP_KERNEL);
	if (!ops) {
		printk (KERN_DEBUG "parport (0x%lx): no memory for ops!\n",
			base);
		kfree (priv);
		return NULL;
	}
	memcpy (ops, &parport_pc_ops, sizeof (struct parport_operations));
	priv->ctr = 0xc;
	priv->ctr_writable = 0xff;
	priv->ecr = 0;
	priv->fifo_depth = 0;
	priv->dma_buf = 0;
	p->base = base;
	p->base_hi = base_hi;
	p->irq = irq;
	p->dma = dma;
	p->modes = PARPORT_MODE_PCSPP | PARPORT_MODE_SAFEININT;
	p->ops = ops;
	p->private_data = priv;
	p->physport = p;
	if (base_hi && !check_region(base_hi,3)) {
		parport_ECR_present(p);
		parport_ECP_supported(p);
	}
	if (base != 0x3bc) {
		if (!check_region(base+0x3, 5)) {
			if (!parport_EPP_supported(p))
				parport_ECPEPP_supported(p);
		}
	}
	if (!parport_SPP_supported (p)) {
		/* No port. */
		kfree (priv);
		return NULL;
	}
	if (priv->ecr)
		parport_ECPPS2_supported(p);
	else
		parport_PS2_supported (p);

	if (!(p = parport_register_port(base, PARPORT_IRQ_NONE,
					PARPORT_DMA_NONE, ops))) {
		kfree (priv);
		kfree (ops);
		return NULL;
	}

	p->base_hi = base_hi;
	p->modes = tmp.modes;
	p->size = (p->modes & PARPORT_MODE_EPP)?8:3;
	p->private_data = priv;

	printk(KERN_INFO "%s: PC-style at 0x%lx", p->name, p->base);
	if (p->base_hi && (p->modes & PARPORT_MODE_ECP))
		printk(" (0x%lx)", p->base_hi);
	p->irq = irq;
	p->dma = dma;
	if (p->irq == PARPORT_IRQ_AUTO) {
		p->irq = PARPORT_IRQ_NONE;
		parport_irq_probe(p);
	} else if (p->irq == PARPORT_IRQ_PROBEONLY) {
		p->irq = PARPORT_IRQ_NONE;
		parport_irq_probe(p);
		probedirq = p->irq;
		p->irq = PARPORT_IRQ_NONE;
	}
	if (p->irq != PARPORT_IRQ_NONE) {
		printk(", irq %d", p->irq);

		if (p->dma == PARPORT_DMA_AUTO) {
			p->dma = PARPORT_DMA_NONE;
			parport_dma_probe(p);
		}
	}
	if (p->dma == PARPORT_DMA_AUTO)		
		p->dma = PARPORT_DMA_NONE;

#ifdef CONFIG_PARPORT_PC_FIFO
	if (p->dma != PARPORT_DMA_NOFIFO &&
	    priv->fifo_depth > 0 && p->irq != PARPORT_IRQ_NONE) {
		p->ops->compat_write_data = parport_pc_compat_write_block_pio;
#ifdef CONFIG_PARPORT_1284
		p->ops->ecp_write_data = parport_pc_ecp_write_block_pio;
#endif /* IEEE 1284 support */
		if (p->dma != PARPORT_DMA_NONE) {
			printk(", dma %d", p->dma);
			p->modes |= PARPORT_MODE_DMA;
		}
		else printk(", using FIFO");
	}
	else
		/* We can't use the DMA channel after all. */
		p->dma = PARPORT_DMA_NONE;
#endif /* Allowed to use FIFO/DMA */

	printk(" [");
#define printmode(x) {if(p->modes&PARPORT_MODE_##x){printk("%s%s",f?",":"",#x);f++;}}
	{
		int f = 0;
		printmode(PCSPP);
		printmode(TRISTATE);
		printmode(COMPAT)
		printmode(EPP);
		printmode(ECP);
		printmode(DMA);
	}
#undef printmode
	printk("]\n");
	if (probedirq != PARPORT_IRQ_NONE) 
		printk("%s: irq %d detected\n", p->name, probedirq);
	parport_proc_register(p);

	request_region (p->base, 3, p->name);
	if (p->size > 3)
		request_region (p->base + 3, p->size - 3, p->name);
	if (p->modes & PARPORT_MODE_ECP)
		request_region (p->base_hi, 3, p->name);

	if (p->irq != PARPORT_IRQ_NONE) {
		if (request_irq (p->irq, parport_pc_interrupt,
				 0, p->name, p)) {
			printk (KERN_WARNING "%s: irq %d in use, "
				"resorting to polled operation\n",
				p->name, p->irq);
			p->irq = PARPORT_IRQ_NONE;
			p->dma = PARPORT_DMA_NONE;
		}

		if (p->dma != PARPORT_DMA_NONE) {
			if (request_dma (p->dma, p->name)) {
				printk (KERN_WARNING "%s: dma %d in use, "
					"resorting to PIO operation\n",
					p->name, p->dma);
				p->dma = PARPORT_DMA_NONE;
			} else {
				priv->dma_buf =
					(char *)__get_dma_pages(GFP_KERNEL, 0);
				if (! priv->dma_buf) {
					printk (KERN_WARNING "%s: "
						"cannot get buffer for DMA, "
						"resorting to PIO operation\n",
						p->name);
					free_dma(p->dma);
					p->dma = PARPORT_DMA_NONE;
				}
			}
		}
	}

	/* Done probing.  Now put the port into a sensible start-up state.
	 * SELECT | INIT also puts IEEE1284-compliant devices into
	 * compatibility mode. */
	if (priv->ecr)
		/*
		 * Put the ECP detected port in PS2 mode.
		 * Do this also for ports that have ECR but don't do ECP.
		 */
		outb (0x34, ECONTROL (p));

	parport_pc_write_data(p, 0);
	parport_pc_data_forward (p);
	parport_pc_write_control(p, PARPORT_CONTROL_SELECT);
	udelay (50);
	parport_pc_write_control(p,
				 PARPORT_CONTROL_SELECT
				 | PARPORT_CONTROL_INIT);
	udelay (50);

	/* Now that we've told the sharing engine about the port, and
	   found out its characteristics, let the high-level drivers
	   know about it. */
	parport_announce_port (p);

	return p;
}

/* Look for PCI parallel port cards. */
static int __init parport_pc_init_pci (int irq, int dma)
{
	struct {
		unsigned int vendor;
		unsigned int device;
		unsigned int numports;
		struct {
			unsigned long lo;
			unsigned long hi; /* -ve if not there */
		} addr[4];
	} cards[] = {
		{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S1P_10x_550, 1,
		  { { 3, 4 }, } },
		{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S1P_10x_650, 1,
		  { { 3, 4 }, } },
		{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S1P_10x_850, 1,
		  { { 3, 4 }, } },
		{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1P_10x, 1,
		  { { 2, 3 }, } },
		{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2P_10x, 2,
		  { { 2, 3 }, { 4, 5 }, } },
		{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S1P_10x_550, 1,
		  { { 4, 5 }, } },
		{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S1P_10x_650, 1,
		  { { 4, 5 }, } },
		{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S1P_10x_850, 1,
		  { { 4, 5 }, } },
		{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1P_20x, 1,
		  { { 0, 1 }, } },
		{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2P_20x, 2,
		  { { 0, 1 }, { 2, 3 }, } },
		{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2P1S_20x_550, 2,
		  { { 1, 2 }, { 3, 4 }, } },
		{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2P1S_20x_650, 2,
		  { { 1, 2 }, { 3, 4 }, } },
		{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2P1S_20x_850, 2,
		  { { 1, 2 }, { 3, 4 }, } },
		{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S1P_20x_550, 1,
		  { { 1, 2 }, } },
		{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S1P_20x_650, 1,
		  { { 1, 2 }, } },
		{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_1S1P_20x_850, 1,
		  { { 1, 2 }, } },
		{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S1P_20x_550, 1,
		  { { 2, 3 }, } },
		{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S1P_20x_650, 1,
		  { { 2, 3 }, } },
		{ PCI_VENDOR_ID_SIIG, PCI_DEVICE_ID_SIIG_2S1P_20x_850, 1,
		  { { 2, 3 }, } },
		{ PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_PARALLEL, 1,
		  { { 0, -1 }, } },
		{ PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_DUAL_PAR_A, 1,
		  { { 0, -1 }, } },
		{ PCI_VENDOR_ID_LAVA, PCI_DEVICE_ID_LAVA_DUAL_PAR_B, 1,
		  { { 0, -1 }, } },
		{ PCI_VENDOR_ID_EXSYS, PCI_DEVICE_ID_EXSYS_4014, 2,
		  { { 2, -1 }, { 3, -1 }, } },
		{ 0, }
	};

	struct pci_dev *pcidev;
	int count = 0;
	int i;

	if (!pci_present ())
		return 0;

	for (i = 0; cards[i].vendor; i++) {
		pcidev = NULL;
		while ((pcidev = pci_find_device (cards[i].vendor,
						  cards[i].device,
						  pcidev)) != NULL) {
			int n;
			for (n = 0; n < cards[i].numports; n++) {
				unsigned long lo = cards[i].addr[n].lo;
				unsigned long hi = cards[i].addr[n].hi;
				unsigned long io_lo, io_hi;
				io_lo = pcidev->resource[lo].start;
				io_hi = ((hi < 0) ? 0 :
					 pcidev->resource[hi].start);
				if (irq == PARPORT_IRQ_AUTO) {
					if (parport_pc_probe_port (io_lo,
								   io_hi,
								   pcidev->irq,
								   dma))
						count++;
				} else if (parport_pc_probe_port (io_lo, io_hi,
								  irq, dma))
					count++;
			}
		}
	}

	/* Look for parallel controllers that we don't know about. */
	for (pcidev = pci_devices; pcidev; pcidev = pcidev->next) {
		const int class_noprogif = pcidev->class & ~0xff;
		if (class_noprogif != (PCI_CLASS_COMMUNICATION_PARALLEL << 8))
			continue;

		for (i = 0; cards[i].vendor; i++)
			if ((cards[i].vendor == pcidev->vendor) &&
			    (cards[i].device == pcidev->device))
				break;
		if (cards[i].vendor)
			/* We know about this one. */
			continue;

		printk (KERN_INFO
			"Unknown PCI parallel I/O card (%04x/%04x)\n"
			"Please send 'lspci' output to "
			"tim@cyberelk.demon.co.uk\n",
			pcidev->vendor, pcidev->device);
	}

	return count;
}

/* Exported symbols. */
#ifdef CONFIG_PARPORT_PC_PCMCIA

/* parport_cs needs this in order to dyncamically get us to find ports. */
EXPORT_SYMBOL (parport_pc_probe_port);

#else

EXPORT_NO_SYMBOLS;

#endif

#ifdef MODULE
static int io[PARPORT_PC_MAX_PORTS+1] = { [0 ... PARPORT_PC_MAX_PORTS] = 0 };
static int io_hi[PARPORT_PC_MAX_PORTS+1] = { [0 ... PARPORT_PC_MAX_PORTS] = 0 };
static int dmaval[PARPORT_PC_MAX_PORTS] = { [0 ... PARPORT_PC_MAX_PORTS-1] = PARPORT_DMA_AUTO };
static int irqval[PARPORT_PC_MAX_PORTS] = { [0 ... PARPORT_PC_MAX_PORTS-1] = PARPORT_IRQ_PROBEONLY };
static const char *irq[PARPORT_PC_MAX_PORTS] = { NULL, };
static const char *dma[PARPORT_PC_MAX_PORTS] = { NULL, };
MODULE_PARM(io, "1-" __MODULE_STRING(PARPORT_PC_MAX_PORTS) "i");
MODULE_PARM(io_hi, "1-" __MODULE_STRING(PARPORT_PC_MAX_PORTS) "i");
MODULE_PARM(irq, "1-" __MODULE_STRING(PARPORT_PC_MAX_PORTS) "s");
MODULE_PARM(dma, "1-" __MODULE_STRING(PARPORT_PC_MAX_PORTS) "s");

int init_module(void)
{	
	/* Work out how many ports we have, then get parport_share to parse
	   the irq values. */
	unsigned int i;
	for (i = 0; i < PARPORT_PC_MAX_PORTS && io[i]; i++);
	if (i) {
		if (parport_parse_irqs(i, irq, irqval)) return 1;
		if (parport_parse_dmas(i, dma, dmaval)) return 1;
	}
	else {
		/* The user can make us use any IRQs or DMAs we find. */
		int val;

		if (irq[0] && !parport_parse_irqs (1, irq, &val))
			switch (val) {
			case PARPORT_IRQ_NONE:
			case PARPORT_IRQ_AUTO:
				irqval[0] = val;
			}

		if (dma[0] && !parport_parse_dmas (1, dma, &val))
			switch (val) {
			case PARPORT_DMA_NONE:
			case PARPORT_DMA_AUTO:
				dmaval[0] = val;
			}
	}

	return (parport_pc_init(io, io_hi, irqval, dmaval)?0:1);
}

void cleanup_module(void)
{
	struct parport *p = parport_enumerate(), *tmp;
	while (p) {
		tmp = p->next;
		if (p->modes & PARPORT_MODE_PCSPP) { 
			struct parport_pc_private *priv = p->private_data;
			struct parport_operations *ops = p->ops;
			if (p->dma != PARPORT_DMA_NONE)
				free_dma(p->dma);
			if (p->irq != PARPORT_IRQ_NONE)
				free_irq(p->irq, p);
			release_region(p->base, 3);
			if (p->size > 3)
				release_region(p->base + 3, p->size - 3);
			if (p->modes & PARPORT_MODE_ECP)
				release_region(p->base_hi, 3);
			parport_proc_unregister(p);
			if (priv->dma_buf)
				free_page((unsigned long) priv->dma_buf);
			kfree (p->private_data);
			parport_unregister_port(p);
			kfree (ops); /* hope no-one cached it */
		}
		p = tmp;
	}
}
#endif
