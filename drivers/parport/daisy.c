/*
 * IEEE 1284.3 Parallel port daisy chain and multiplexor code
 * 
 * Copyright (C) 1999  Tim Waugh <tim@cyberelk.demon.co.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * ??-12-1998: Initial implementation.
 * 31-01-1999: Make port-cloning transparent.
 * 13-02-1999: Move DeviceID technique from parport_probe.
 * 13-03-1999: Get DeviceID from non-IEEE 1284.3 devices too.
 *
 */

#include <linux/parport.h>
#include <linux/delay.h>
#include <asm/uaccess.h>

#define DEBUG /* undef me for production */

#ifdef DEBUG
#define DPRINTK(stuff...) printk (stuff)
#else
#define DPRINTK(stuff...)
#endif

static struct daisydev {
	struct daisydev *next;
	struct parport *port;
	int daisy;
	int devnum;
} *topology = NULL;

static int numdevs = 0;

/* Forward-declaration of lower-level functions. */
static int mux_present (struct parport *port);
static int num_mux_ports (struct parport *port);
static int select_port (struct parport *port);
static int assign_addrs (struct parport *port);

/* Add a device to the discovered topology. */
static void add_dev (int devnum, struct parport *port, int daisy)
{
	struct daisydev *newdev;
	newdev = kmalloc (GFP_KERNEL, sizeof (struct daisydev));
	if (newdev) {
		newdev->port = port;
		newdev->daisy = daisy;
		newdev->devnum = devnum;
		newdev->next = topology;
		if (!topology || topology->devnum >= devnum)
			topology = newdev;
		else {
			struct daisydev *prev = topology;
			while (prev->next && prev->next->devnum < devnum)
				prev = prev->next;
			newdev->next = prev->next;
			prev->next = newdev;
		}
	}
}

/* Clone a parport (actually, make an alias). */
static struct parport *clone_parport (struct parport *real, int muxport)
{
	struct parport *extra = parport_register_port (real->base,
						       real->irq,
						       real->dma,
						       real->ops);
	if (extra) {
		extra->portnum = real->portnum;
		extra->physport = real;
		extra->muxport = muxport;
	}

	return extra;
}

/* Discover the IEEE1284.3 topology on a port -- muxes and daisy chains. */
int parport_daisy_init (struct parport *port)
{
	char *deviceid;
	static const char *th[] = { /*0*/"th", "st", "nd", "rd", "th" };
	int num_ports;
	int i;

	/* Because this is called before any other devices exist,
	 * we don't have to claim exclusive access.  */

	/* If mux present on normal port, need to create new
	 * parports for each extra port. */
	if (port->muxport < 0 && mux_present (port) &&
	    /* don't be fooled: a mux must have 2 or 4 ports. */
	    ((num_ports = num_mux_ports (port)) == 2 || num_ports == 4)) {
		/* Leave original as port zero. */
		port->muxport = 0;
		printk (KERN_INFO
			"%s: 1st (default) port of %d-way multiplexor\n",
			port->name, num_ports);
		for (i = 1; i < num_ports; i++) {
			/* Clone the port. */
			struct parport *extra = clone_parport (port, i);
			if (!extra) {
				if (signal_pending (current))
					break;

				schedule ();
				continue;
			}

			printk (KERN_INFO
				"%s: %d%s port of %d-way multiplexor on %s\n",
				extra->name, i + 1, th[i + 1], num_ports,
				port->name);

			/* Analyse that port too.  We won't recurse
			   forever because of the 'port->muxport < 0'
			   test above. */
			parport_announce_port (extra);
		}
	}

	if (port->muxport >= 0)
		select_port (port);

	parport_daisy_deselect_all (port);
	assign_addrs (port);

	/* Count the potential legacy device at the end. */
	add_dev (numdevs++, port, -1);

	/* Find out the legacy device's IEEE 1284 device ID. */
	deviceid = kmalloc (1000, GFP_KERNEL);
	if (deviceid) {
		parport_device_id (numdevs - 1, deviceid, 1000);
		kfree (deviceid);
	}

	return 0;
}

/* Forget about devices on a physical port. */
void parport_daisy_fini (struct parport *port)
{
	struct daisydev *dev, *prev = topology;
	while (prev && prev->port == port)
		prev = topology = topology->next;

	while (prev) {
		dev = prev->next;
		if (dev && dev->port == port)
			prev->next = dev->next;

		prev = prev->next;
	}

	/* Gaps in the numbering could be handled better.  How should
           someone enumerate through all IEEE1284.3 devices in the
           topology?. */
	if (!topology) numdevs = 0;
	return; }

/* Find a device by canonical device number. */
struct pardevice *parport_open (int devnum, const char *name,
				int (*pf) (void *), void (*kf) (void *),
				void (*irqf) (int, void *, struct pt_regs *),
				int flags, void *handle)
{
	struct parport *port = parport_enumerate ();
	struct pardevice *dev;
	int portnum;
	int muxnum;
	int daisynum;

	if (parport_device_coords (devnum,  &portnum, &muxnum, &daisynum))
		return NULL;

	while (port && ((port->portnum != portnum) ||
			(port->muxport != muxnum)))
		port = port->next;

	if (!port)
		/* No corresponding parport. */
		return NULL;

	dev = parport_register_device (port, name, pf, kf,
				       irqf, flags, handle);
	if (dev)
		dev->daisy = daisynum;

	/* Check that there really is a device to select. */
	if (daisynum >= 0) {
		int selected;
		parport_claim_or_block (dev);
		selected = port->daisy;
		parport_release (dev);

		if (selected != port->daisy) {
			/* No corresponding device. */
			parport_unregister_device (dev);
			return NULL;
		}
	}

	return dev;
}

/* The converse of parport_open. */
void parport_close (struct pardevice *dev)
{
	parport_unregister_device (dev);
}

/* Convert device coordinates into a canonical device number. */
int parport_device_num (int parport, int mux, int daisy)
{
	struct daisydev *dev = topology;

	while (dev && dev->port->portnum != parport &&
	       dev->port->muxport != mux && dev->daisy != daisy)
		dev = dev->next;

	if (!dev)
		return -ENXIO;

	return dev->devnum;
}

/* Convert a canonical device number into device coordinates. */
int parport_device_coords (int devnum, int *parport, int *mux, int *daisy)
{
	struct daisydev *dev = topology;

	while (dev && dev->devnum != devnum)
		dev = dev->next;

	if (!dev)
		return -ENXIO;

	if (parport) *parport = dev->port->portnum;
	if (mux) *mux = dev->port->muxport;
	if (daisy) *daisy = dev->daisy;
	return 0;
}

/* Send a daisy-chain-style CPP command packet. */
static int cpp_daisy (struct parport *port, int cmd)
{
	unsigned char s;

	parport_write_data (port, 0xaa); udelay (2);
	parport_write_data (port, 0x55); udelay (2);
	parport_write_data (port, 0x00); udelay (2);
	parport_write_data (port, 0xff); udelay (2);
	s = parport_read_status (port) & (PARPORT_STATUS_BUSY
					  | PARPORT_STATUS_PAPEROUT
					  | PARPORT_STATUS_SELECT
					  | PARPORT_STATUS_ERROR);
	if (s != (PARPORT_STATUS_BUSY
		  | PARPORT_STATUS_PAPEROUT
		  | PARPORT_STATUS_SELECT
		  | PARPORT_STATUS_ERROR)) {
		DPRINTK (KERN_DEBUG "%s: cpp_daisy: aa5500ff(%02x)\n",
			 port->name, s);
		return -ENXIO;
	}

	parport_write_data (port, 0x87); udelay (2);
	s = parport_read_status (port) & (PARPORT_STATUS_BUSY
					  | PARPORT_STATUS_PAPEROUT
					  | PARPORT_STATUS_SELECT
					  | PARPORT_STATUS_ERROR);
	if (s != (PARPORT_STATUS_SELECT | PARPORT_STATUS_ERROR)) {
		DPRINTK (KERN_DEBUG "%s: cpp_daisy: aa5500ff87(%02x)\n",
			 port->name, s);
		return -ENXIO;
	}

	parport_write_data (port, 0x78); udelay (2);
	parport_write_data (port, cmd); udelay (2);
	parport_frob_control (port,
			      PARPORT_CONTROL_STROBE,
			      PARPORT_CONTROL_STROBE);
	udelay (1);
	parport_frob_control (port, PARPORT_CONTROL_STROBE, 0);
	udelay (1);
	s = parport_read_status (port);
	parport_write_data (port, 0xff); udelay (2);

	return s;
}

/* Send a mux-style CPP command packet. */
static int cpp_mux (struct parport *port, int cmd)
{
	unsigned char s;
	int rc;

	parport_write_data (port, 0xaa); udelay (2);
	parport_write_data (port, 0x55); udelay (2);
	parport_write_data (port, 0xf0); udelay (2);
	parport_write_data (port, 0x0f); udelay (2);
	parport_write_data (port, 0x52); udelay (2);
	parport_write_data (port, 0xad); udelay (2);
	parport_write_data (port, cmd); udelay (2);

	s = parport_read_status (port);
	if (!(s & PARPORT_STATUS_ACK)) {
		DPRINTK (KERN_DEBUG "%s: cpp_mux: aa55f00f52ad%02x(%02x)\n",
			 port->name, cmd, s);
		return -EIO;
	}

	rc = (((s & PARPORT_STATUS_SELECT   ? 1 : 0) << 0) |
	      ((s & PARPORT_STATUS_PAPEROUT ? 1 : 0) << 1) |
	      ((s & PARPORT_STATUS_BUSY     ? 0 : 1) << 2) |
	      ((s & PARPORT_STATUS_ERROR    ? 0 : 1) << 3));

	return rc;
}

void parport_daisy_deselect_all (struct parport *port)
{
	cpp_daisy (port, 0x30);
}

int parport_daisy_select (struct parport *port, int daisy, int mode)
{
	/* mode is currently ignored. FIXME? */
	return cpp_daisy (port, 0xe0 + daisy) & PARPORT_STATUS_ERROR;
}

static int mux_present (struct parport *port)
{
	return cpp_mux (port, 0x51) == 3;
}

static int num_mux_ports (struct parport *port)
{
	return cpp_mux (port, 0x58);
}

static int select_port (struct parport *port)
{
	int muxport = port->muxport;
	return cpp_mux (port, 0x60 + muxport) == muxport;
}

static int assign_addrs (struct parport *port)
{
	unsigned char s, last_dev;
	unsigned char daisy;
	int thisdev = numdevs;
	char *deviceid;

	parport_write_data (port, 0xaa); udelay (2);
	parport_write_data (port, 0x55); udelay (2);
	parport_write_data (port, 0x00); udelay (2);
	parport_write_data (port, 0xff); udelay (2);
	s = parport_read_status (port) & (PARPORT_STATUS_BUSY
					  | PARPORT_STATUS_PAPEROUT
					  | PARPORT_STATUS_SELECT
					  | PARPORT_STATUS_ERROR);
	if (s != (PARPORT_STATUS_BUSY
		  | PARPORT_STATUS_PAPEROUT
		  | PARPORT_STATUS_SELECT
		  | PARPORT_STATUS_ERROR)) {
		DPRINTK (KERN_DEBUG "%s: assign_addrs: aa5500ff(%02x)\n",
			 port->name, s);
		return -ENXIO;
	}

	parport_write_data (port, 0x87); udelay (2);
	s = parport_read_status (port) & (PARPORT_STATUS_BUSY
					  | PARPORT_STATUS_PAPEROUT
					  | PARPORT_STATUS_SELECT
					  | PARPORT_STATUS_ERROR);
	if (s != (PARPORT_STATUS_SELECT | PARPORT_STATUS_ERROR)) {
		DPRINTK (KERN_DEBUG "%s: assign_addrs: aa5500ff87(%02x)\n",
			 port->name, s);
		return -ENXIO;
	}

	parport_write_data (port, 0x78); udelay (2);
	last_dev = 0; /* We've just been speaking to a device, so we
			 know there must be at least _one_ out there. */

	for (daisy = 0; daisy < 4; daisy++) {
		parport_write_data (port, daisy);
		udelay (2);
		parport_frob_control (port,
				      PARPORT_CONTROL_STROBE,
				      PARPORT_CONTROL_STROBE);
		udelay (1);
		parport_frob_control (port, PARPORT_CONTROL_STROBE, 0);
		udelay (1);

		if (last_dev)
			/* No more devices. */
			break;

		last_dev = !(parport_read_status (port)
			     & PARPORT_STATUS_BUSY);

		add_dev (numdevs++, port, daisy);
	}

	parport_write_data (port, 0xff); udelay (2);
	DPRINTK (KERN_DEBUG "%s: Found %d daisy-chained devices\n", port->name,
		numdevs - thisdev);

	/* Ask the new devices to introduce themselves. */
	deviceid = kmalloc (1000, GFP_KERNEL);
	if (!deviceid) return 0;

	for (daisy = 0; thisdev < numdevs; thisdev++, daisy++)
		parport_device_id (thisdev, deviceid, 1000);

	kfree (deviceid);
	return 0;
}

/* Find a device with a particular manufacturer and model string,
   starting from a given device number.  Like the PCI equivalent,
   'from' itself is skipped. */
int parport_find_device (const char *mfg, const char *mdl, int from)
{
	struct daisydev *d = topology; /* sorted by devnum */

	/* Find where to start. */
	while (d && d->devnum <= from)
		d = d->next;

	/* Search. */
	while (d) {
		struct parport_device_info *info;
		info = &d->port->probe_info[1 + d->daisy];
		if ((!mfg || !strcmp (mfg, info->mfr)) &&
		    (!mdl || !strcmp (mdl, info->model)))
			break;

		d = d->next;
	}

	if (d)
		return d->devnum;

	return -1;
}

/* Find a device in a particular class.  Like the PCI equivalent,
   'from' itself is skipped. */
int parport_find_class (parport_device_class cls, int from)
{
	struct daisydev *d = topology; /* sorted by devnum */

	/* Find where to start. */
	while (d && d->devnum <= from)
		d = d->next;

	/* Search. */
	while (d && d->port->probe_info[1 + d->daisy].class != cls)
		d = d->next;

	if (d)
		return d->devnum;

	return -1;
}
