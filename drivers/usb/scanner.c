/* -*- linux-c -*- */

/* 
 * Driver for USB Scanners (linux-2.4.12)
 *
 * Copyright (C) 1999, 2000, 2001 David E. Nelson
 *
 * Portions may be copyright Brad Keryan and Michael Gee.
 *
 * David E. Nelson (dnelson@jump.net)
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Originally based upon mouse.c (Brad Keryan) and printer.c (Michael Gee).
 *
 * History
 *
 *  0.1  8/31/1999
 *
 *    Developed/tested using linux-2.3.15 with minor ohci.c changes to
 *    support short packes during bulk xfer mode.  Some testing was
 *    done with ohci-hcd but the performace was low.  Very limited
 *    testing was performed with uhci but I was unable to get it to
 *    work.  Initial relase to the linux-usb development effort.
 *
 *
 *  0.2  10/16/1999
 *
 *    - Device can't be opened unless a scanner is plugged into the USB.
 *    - Finally settled on a reasonable value for the I/O buffer's.
 *    - Cleaned up write_scanner()
 *    - Disabled read/write stats
 *    - A little more code cleanup
 *
 *
 *  0.3  10/18/1999
 *
 *    - Device registration changed to reflect new device
 *      allocation/registration for linux-2.3.22+.
 *    - Adopted David Brownell's <david-b@pacbell.net> technique for 
 *      assigning bulk endpoints.
 *    - Removed unnessesary #include's
 *    - Scanner model now reported via syslog INFO after being detected 
 *      *and* configured.
 *    - Added user specified vendor:product USB ID's which can be passed 
 *      as module parameters.
 *
 *
 *  0.3.1
 *
 *    - Applied patches for linux-2.3.25.
 *    - Error number reporting changed to reflect negative return codes.
 *
 *
 *  0.3.2
 *
 *    - Applied patches for linux-2.3.26 to scanner_init().
 *    - Debug read/write stats now report values as signed decimal.
 *
 *
 *  0.3.3
 *
 *    - Updated the bulk_msg() calls to usb usb_bulk_msg().
 *    - Added a small delay in the write_scanner() method to aid in
 *      avoiding NULL data reads on HP scanners.  We'll see how this works.
 *    - Return values from usb_bulk_msg() now ignore positive values for
 *      use with the ohci driver.
 *    - Added conditional debugging instead of commenting/uncommenting
 *      all over the place.
 *    - kfree()'d the pointer after using usb_string() as documented in
 *      linux-usb-api.txt.
 *    - Added usb_set_configuration().  It got lost in version 0.3 -- ack!
 *    - Added the HP 5200C USB Vendor/Product ID's.
 *
 *
 *  0.3.4  1/23/2000
 *
 *    - Added Greg K-H's <greg@kroah.com> patch for better handling of 
 *      Product/Vendor detection.
 *    - The driver now autoconfigures its endpoints including interrupt
 *      endpoints if one is detected.  The concept was originally based
 *      upon David Brownell's method.
 *    - Added some Seiko/Epson ID's. Thanks to Karl Heinz 
 *      Kremer <khk@khk.net>.
 *    - Added some preliminary ioctl() calls for the PV8630 which is used
 *      by the HP4200. The ioctl()'s still have to be registered. Thanks 
 *      to Adrian Perez Jorge <adrianpj@easynews.com>.
 *    - Moved/migrated stuff to scanner.h
 *    - Removed the usb_set_configuration() since this is handled by
 *      the usb_new_device() routine in usb.c.
 *    - Added the HP 3300C.  Thanks to Bruce Tenison.
 *    - Changed user specified vendor/product id so that root hub doesn't
 *      get falsely attached to. Thanks to Greg K-H.
 *    - Added some Mustek ID's. Thanks to Gernot Hoyler 
 *      <Dr.Hoyler@t-online.de>.
 *    - Modified the usb_string() reporting.  See kfree() comment above.
 *    - Added Umax Astra 2000U. Thanks to Doug Alcorn <doug@lathi.net>.
 *    - Updated the printk()'s to use the info/warn/dbg macros.
 *    - Updated usb_bulk_msg() argument types to fix gcc warnings.
 *
 *
 *  0.4  2/4/2000
 *
 *    - Removed usb_string() from probe_scanner since the core now does a
 *      good job of reporting what was connnected.  
 *    - Finally, simultaneous multiple device attachment!
 *    - Fixed some potential memory freeing issues should memory allocation
 *      fail in probe_scanner();
 *    - Some fixes to disconnect_scanner().
 *    - Added interrupt endpoint support.
 *    - Added Agfa SnapScan Touch. Thanks to Jan Van den Bergh
 *      <jan.vandenbergh@cs.kuleuven.ac.be>.
 *    - Added Umax 1220U ID's. Thanks to Maciek Klimkowski
 *      <mac@nexus.carleton.ca>.
 *    - Fixed bug in write_scanner(). The buffer was not being properly
 *      updated for writes larger than OBUF_SIZE. Thanks to Henrik 
 *      Johansson <henrikjo@post.utfors.se> for identifying it.
 *    - Added Microtek X6 ID's. Thanks to Oliver Neukum
 *      <Oliver.Neukum@lrz.uni-muenchen.de>.
 *
 * 
 *  0.4.1  2/15/2000
 *  
 *    - Fixed 'count' bug in read_scanner(). Thanks to Henrik
 *      Johansson <henrikjo@post.utfors.se> for identifying it.  Amazing
 *      it has worked this long.
 *    - Fixed '>=' bug in both read/write_scanner methods.
 *    - Cleaned up both read/write_scanner() methods so that they are 
 *      a little more readable.
 *    - Added a lot of Microtek ID's.  Thanks to Adrian Perez Jorge.
 *    - Adopted the __initcall().
 *    - Added #include <linux/init.h> to scanner.h for __initcall().
 *    - Added one liner in irq_scanner() to keep gcc from complaining 
 *      about an unused variable (data) if debugging was disabled
 *      in scanner.c.
 *    - Increased the timeout parameter in read_scanner() to 120 Secs.
 *
 *
 *  0.4.2  3/23/2000
 *
 *    - Added Umax 1236U ID.  Thanks to Philipp Baer <ph_baer@npw.net>.
 *    - Added Primax, ReadyScan, Visioneer, Colorado, and Genius ID's.
 *      Thanks to Adrian Perez Jorge <adrianpj@easynews.com>.
 *    - Fixed error number reported for non-existant devices.  Thanks to
 *      Spyridon Papadimitriou <Spyridon_Papadimitriou@gs91.sp.cs.cmu.edu>.
 *    - Added Acer Prisascan 620U ID's.  Thanks to Joao <joey@knoware.nl>.
 *    - Replaced __initcall() with module_init()/module_exit(). Updates
 *      from patch-2.3.48.
 *    - Replaced file_operations structure with new syntax.  Updates
 *      from patch-2.3.49.
 *    - Changed #include "usb.h" to #include <linux/usb.h>
 *    - Added #define SCN_IOCTL to exclude development areas 
 *      since 2.4.x is about to be released. This mainly affects the 
 *      ioctl() stuff.  See scanner.h for more details.
 *    - Changed the return value for signal_pending() from -ERESTARTSYS to
 *      -EINTR.
 *
 *
 * 0.4.3  4/30/2000
 *
 *    - Added Umax Astra 2200 ID.  Thanks to Flynn Marquardt 
 *      <flynn@isr.uni-stuttgart.de>.
 *    - Added iVina 1200U ID. Thanks to Dyson Lin <dyson@avision.com.tw>.
 *    - Added access time update for the device file courtesy of Paul
 *      Mackerras <paulus@samba.org>.  This allows a user space daemon
 *      to turn the lamp off for a Umax 1220U scanner after a prescribed
 *      time.
 *    - Fixed HP S20 ID's.  Thanks to Ruud Linders <rlinders@xs4all.nl>.
 *    - Added Acer ScanPrisa 620U ID. Thanks to Oliver
 *      Schwartz <Oliver.Schwartz@gmx.de> via sane-devel mail list.
 *    - Fixed bug in read_scanner for copy_to_user() function.  The returned
 *      value should be 'partial' not 'this_read'.
 *    - Fixed bug in read_scanner. 'count' should be decremented 
 *      by 'this_read' and not by 'partial'.  This resulted in twice as many
 *      calls to read_scanner() for small amounts of data and possibly
 *      unexpected returns of '0'.  Thanks to Karl Heinz 
 *      Kremer <khk@khk.net> and Alain Knaff <Alain.Knaff@ltnb.lu>
 *      for discovering this.
 *    - Integrated Randy Dunlap's <randy.dunlap@intel.com> patch for a
 *      scanner lookup/ident table. Thanks Randy.
 *    - Documentation updates.
 *    - Added wait queues to read_scanner().
 *
 *
 * 0.4.3.1
 *
 *    - Fixed HP S20 ID's...again..sigh.  Thanks to Ruud
 *      Linders <rlinders@xs4all.nl>.
 *
 * 0.4.4
 *    - Added addtional Mustek ID's (BearPaw 1200, 600 CU, 1200 USB,
 *      and 1200 UB.  Thanks to Henning Meier-Geinitz <henningmg@gmx.de>.
 *    - Added the Vuego Scan Brisa 340U ID's.  Apparently this scanner is
 *      marketed by Acer Peripherals as a cheap 300 dpi model. Thanks to
 *      David Gundersen <gundersd@paradise.net.nz>.
 *    - Added the Epson Expression1600 ID's. Thanks to Karl Heinz
 *      Kremer <khk@khk.net>.
 *
 * 0.4.5  2/28/2001
 *    - Added Mustek ID's (BearPaw 2400, 1200 CU Plus, BearPaw 1200F).
 *      Thanks to Henning Meier-Geinitz <henningmg@gmx.de>.
 *    - Added read_timeout module parameter to override RD_NAK_TIMEOUT
 *      when read()'ing from devices.
 *    - Stalled pipes are now checked and cleared with
 *      usb_clear_halt() for the read_scanner() function. This should
 *      address the "funky result: -32" error messages.
 *    - Removed Microtek scanner ID's.  Microtek scanners are now
 *      supported via the drivers/usb/microtek.c driver.
 *    - Added scanner specific read timeout's.
 *    - Return status errors are NEGATIVE!!!  This should address the
 *      "funky result: -110" error messages.
 *    - Replaced USB_ST_TIMEOUT with ETIMEDOUT.
 *    - rd_nak was still defined in MODULE_PARM.  It's been updated with
 *      read_timeout.  Thanks to Mark W. Webb <markwebb@adelphia.net> for
 *      reporting this bug.
 *    - Added Epson Perfection 1640SU and 1640SU Photo.  Thanks to
 *      Jean-Luc <f5ibh@db0bm.ampr.org> and Manuel
 *      Pelayo <Manuel.Pelayo@sesips.org>. Reported to work fine by Manuel.
 *
 * 0.4.6  9/27/2001
 *    - Added IOCTL's to report back scanner USB ID's.  Thanks to
 *      Karl Heinz <khk@lynx.phpwebhosting.com>
 *    - Added Umax Astra 2100U ID's.  Thanks to Ron
 *      Wellsted <ron@wellsted.org.uk>.
 *      and Manuel Pelayo <Manuel.Pelayo@sesips.org>.
 *    - Added HP 3400 ID's. Thanks to Harald Hannelius <harald@iki.fi>
 *      and Bertrik Sikken <bertrik@zonnet.nl>.  Reported to work at
 *      htpp://home.zonnet.nl/bertrik/hp3300c/hp3300c.htm.
 *    - Added Minolta Dimage Scan Dual II ID's.  Thanks to Jose Paulo
 *      Moitinho de Almeida <moitinho@civil.ist.utl.pt>
 *    - Confirmed addition for SnapScan E20.  Thanks to Steffen H�bner
 *      <hueb_s@gmx.de>.
 *    - Added Lifetec LT9385 ID's.  Thanks to Van Bruwaene Kris
 *      <krvbr@yahoo.co.uk>
 *    - Added Agfa SnapScan e26 ID's.  Reported to work with SANE
 *      1.0.5.  Thanks to Falk Sauer <falk@mgnkatze.franken.de>.
 *    - Added HP 4300 ID's.  Thanks to Stefan Schlosser
 *      <castla@grmmbl.org>.
 *    - Added Relisis Episode ID's.  Thanks to Manfred
 *      Morgner <odb-devel@gmx.net>.
 *    - Added many Acer ID's. Thanks to Oliver
 *      Schwartz <Oliver.Schwartz@gmx.de>.
 *    - Added Snapscan e40 ID's.  Thanks to Oliver
 *      Schwartz <Oliver.Schwartz@gmx.de>.
 *    - Thanks to Oliver Neukum <Oliver.Neukum@lrz.uni-muenchen.de>
 *      for helping with races.
 *    - Added Epson Perfection 1650 ID's. Thanks to Karl Heinz
 *      Kremer <khk@khk.net>.
 *    - Added Epson Perfection 2450 ID's (aka GT-9700 for the Japanese
 *      market).  Thanks to Karl Heinz Kremer <khk@khk.net>.
 *    - Added Mustek 600 USB ID's.  Thanks to Marcus
 *      Alanen <maalanen@ra.abo.fi>.
 *    - Added Acer ScanPrisa 1240UT ID's.  Thanks to Morgan
 *      Collins <sirmorcant@morcant.org>.
 *    - Incorporated devfs patches!! Thanks to Tom Rini
 *      <trini@kernel.crashing.org>, Pavel Roskin <proski@gnu.org>,
 *      Greg KH <greg@kroah.com>, Yves Duret <yduret@mandrakesoft.com>,
 *      Flavio Stanchina <flavio.stanchina@tin.it>.
 *    - Removed Minolta ScanImage II.  This scanner uses USB SCSI.  Thanks
 *      to Oliver Neukum <Oliver.Neukum@lrz.uni-muenchen.de> for pointing
 *      this out.
 *    - Added additional SMP locking.  Thanks to David Brownell and 
 *      Oliver Neukum for their help.
 *    - Added version reporting - reports for both module load and modinfo
 *    - Started path to hopefully straighten/clean out ioctl()'s.
 *    - Users are now notified to consult the Documentation/usb/scanner.txt
 *      for common error messages rather than the maintainer.
 *
 * TODO
 *    - Performance
 *    - Select/poll methods
 *    - More testing
 *    - Proper registry/assignment for LM9830 ioctl's
 *
 *
 *  Thanks to:
 *
 *    - All the folks on the linux-usb list who put up with me. :)  This 
 *      has been a great learning experience for me.
 *    - To Linus Torvalds for this great OS.
 *    - The GNU folks.
 *    - The folks that forwarded Vendor:Product ID's to me.
 *    - Johannes Erdfelt for the loaning of a USB analyzer for tracking an
 *      issue with HP-4100 and uhci.
 *    - Adolfo Montero for his assistance.
 *    - All the folks who chimed in with reports and suggestions.
 *    - All the developers that are working on USB SANE backends or other
 *      applications to use USB scanners.
 *
 *  Performance:
 *
 *    System: Pentium 120, 80 MB RAM, OHCI, Linux 2.3.23, HP 4100C USB Scanner
 *            300 dpi scan of the entire bed
 *      24 Bit Color ~ 70 secs - 3.6 Mbit/sec
 *       8 Bit Gray ~ 17 secs - 4.2 Mbit/sec */

/* 
 * Scanner definitions, macros, module info, 
 * debug/ioctl/data_dump enable, and other constants.
 */ 
#include "scanner.h"

static void
irq_scanner(struct urb *urb)
{

/*
 * For the meantime, this is just a placeholder until I figure out what
 * all I want to do with it -- or somebody else for that matter.
 */

	struct scn_usb_data *scn;
	unsigned char *data;
	scn = urb->context;
	down(&(scn->sem));
	data = &scn->button;
	data += 0;		/* Keep gcc from complaining about unused var */

	if (urb->status) {
		up(&(scn->sem));
		return;
	}

	dbg("irq_scanner(%d): data:%x", scn->scn_minor, *data);
	up(&(scn->sem));
	return;
}

static int
open_scanner(struct inode * inode, struct file * file)
{
	struct scn_usb_data *scn;
	struct usb_device *dev;

	kdev_t scn_minor;

	int err=0;

	MOD_INC_USE_COUNT;

	down(&scn_mutex);

	scn_minor = USB_SCN_MINOR(inode);

	dbg("open_scanner: scn_minor:%d", scn_minor);

	if (!p_scn_table[scn_minor]) {
		up(&scn_mutex);
		err("open_scanner(%d): Unable to access minor data", scn_minor);
		return -ENODEV;
	}

	scn = p_scn_table[scn_minor];

	dev = scn->scn_dev;

	down(&(scn->sem));	/* Now protect the scn_usb_data structure */ 

	up(&scn_mutex); /* Now handled by the above */

	if (!dev) {
		err("open_scanner(%d): Scanner device not present", scn_minor);
		err = -ENODEV;
		goto out_error;
	}

	if (!scn->present) {
		err("open_scanner(%d): Scanner is not present", scn_minor);
		err = -ENODEV;
		goto out_error;
	}

	if (scn->isopen) {
		err("open_scanner(%d): Scanner device is already open", scn_minor);
		err = -EBUSY;
		goto out_error;
	}

	init_waitqueue_head(&scn->rd_wait_q);

	scn->isopen = 1;

	file->private_data = scn; /* Used by the read and write methods */


out_error:

	up(&(scn->sem)); /* Wake up any possible contending processes */

	if (err)
		MOD_DEC_USE_COUNT;

	return err;
}

static int
close_scanner(struct inode * inode, struct file * file)
{
	struct scn_usb_data *scn;

	kdev_t scn_minor;

	scn_minor = USB_SCN_MINOR (inode);

	dbg("close_scanner: scn_minor:%d", scn_minor);

	if (!p_scn_table[scn_minor]) {
		err("close_scanner(%d): invalid scn_minor", scn_minor);
		return -ENODEV;
	}

	down(&scn_mutex);

	scn = p_scn_table[scn_minor];
	down(&(scn->sem));
	scn->isopen = 0;

	file->private_data = NULL;

	up(&scn_mutex);
	up(&(scn->sem));

	MOD_DEC_USE_COUNT;

	return 0;
}

static ssize_t
write_scanner(struct file * file, const char * buffer,
              size_t count, loff_t *ppos)
{
	struct scn_usb_data *scn;
	struct usb_device *dev;

	ssize_t bytes_written = 0; /* Overall count of bytes written */
	ssize_t ret = 0;

	kdev_t scn_minor;

	int this_write;		/* Number of bytes to write */
	int partial;		/* Number of bytes successfully written */
	int result = 0;

	char *obuf;

	scn = file->private_data;

	down(&(scn->sem));

	scn_minor = scn->scn_minor;

	obuf = scn->obuf;

	dev = scn->scn_dev;

	file->f_dentry->d_inode->i_atime = CURRENT_TIME;

	while (count > 0) {

		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}

		this_write = (count >= OBUF_SIZE) ? OBUF_SIZE : count;

		if (copy_from_user(scn->obuf, buffer, this_write)) {
			ret = -EFAULT;
			break;
		}

		result = usb_bulk_msg(dev,usb_sndbulkpipe(dev, scn->bulk_out_ep), obuf, this_write, &partial, 60*HZ);
		dbg("write stats(%d): result:%d this_write:%d partial:%d", scn_minor, result, this_write, partial);

		if (result == -ETIMEDOUT) {	/* NAK -- shouldn't happen */
			warn("write_scanner: NAK received.");
			ret = result;
			break;
		} else if (result < 0) { /* We should not get any I/O errors */
			warn("write_scanner(%d): funky result: %d. Consult Documentataion/usb/scanner.txt.", scn_minor, result);
			ret = -EIO;
			break;
		}

#ifdef WR_DATA_DUMP
		if (partial) {
			unsigned char cnt, cnt_max;
			cnt_max = (partial > 24) ? 24 : partial;
			printk(KERN_DEBUG "dump(%d): ", scn_minor);
			for (cnt=0; cnt < cnt_max; cnt++) {
				printk("%X ", obuf[cnt]);
			}
			printk("\n");
		}
#endif
		if (partial != this_write) { /* Unable to write all contents of obuf */
			ret = -EIO;
			break;
		}

		if (partial) { /* Data written */
			buffer += partial;
			count -= partial;
			bytes_written += partial;
		} else { /* No data written */
			ret = 0;
			break;
		}
	}
	up(&(scn->sem));
	mdelay(5);		/* This seems to help with SANE queries */
	return ret ? ret : bytes_written;
}

static ssize_t
read_scanner(struct file * file, char * buffer,
             size_t count, loff_t *ppos)
{
	struct scn_usb_data *scn;
	struct usb_device *dev;

	ssize_t bytes_read;	/* Overall count of bytes_read */
	ssize_t ret;

	kdev_t scn_minor;

	int partial;		/* Number of bytes successfully read */
	int this_read;		/* Max number of bytes to read */
	int result;
	int rd_expire = RD_EXPIRE;

	char *ibuf;

	scn = file->private_data;

	down(&(scn->sem));

	scn_minor = scn->scn_minor;

	ibuf = scn->ibuf;

	dev = scn->scn_dev;

	bytes_read = 0;
	ret = 0;

	file->f_dentry->d_inode->i_atime = CURRENT_TIME; /* Update the
                                                            atime of
                                                            the device
                                                            node */
	while (count > 0) {
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}

		this_read = (count >= IBUF_SIZE) ? IBUF_SIZE : count;

		result = usb_bulk_msg(dev, usb_rcvbulkpipe(dev, scn->bulk_in_ep), ibuf, this_read, &partial, scn->rd_nak_timeout);
		dbg("read stats(%d): result:%d this_read:%d partial:%d count:%d", scn_minor, result, this_read, partial, count);

/*
 * Scanners are sometimes inheriently slow since they are mechanical
 * in nature.  USB bulk reads tend to timeout while the scanner is
 * positioning, resetting, warming up the lamp, etc if the timeout is
 * set too low.  A very long timeout parameter for bulk reads was used
 * to overcome this limitation, but this sometimes resulted in folks
 * having to wait for the timeout to expire after pressing Ctrl-C from
 * an application. The user was sometimes left with the impression
 * that something had hung or crashed when in fact the USB read was
 * just waiting on data.  So, the below code retains the same long
 * timeout period, but splits it up into smaller parts so that
 * Ctrl-C's are acted upon in a reasonable amount of time.
 */

		if (result == -ETIMEDOUT) { /* NAK */
			if (!partial) { /* No data */
				if (--rd_expire <= 0) {	/* Give it up */
					warn("read_scanner(%d): excessive NAK's received", scn_minor);
					ret = result;
					break;
				} else { /* Keep trying to read data */
					interruptible_sleep_on_timeout(&scn->rd_wait_q, scn->rd_nak_timeout);
					continue;
				}
			} else { /* Timeout w/ some data */
				goto data_recvd;
			}
		}
		
		if (result == -EPIPE) { /* No hope */
			if(usb_clear_halt(dev, scn->bulk_in_ep)) {
				err("read_scanner(%d): Failure to clear endpoint halt condition (%Zd).", scn_minor, ret);
			}
			ret = result;
			break;
		} else if ((result < 0) && (result != USB_ST_DATAUNDERRUN)) {
			warn("read_scanner(%d): funky result:%d. Consult Documentation/usb/scanner.txt.", scn_minor, (int)result);
			ret = -EIO;
			break;
		}

	data_recvd:

#ifdef RD_DATA_DUMP
		if (partial) {
			unsigned char cnt, cnt_max;
			cnt_max = (partial > 24) ? 24 : partial;
			printk(KERN_DEBUG "dump(%d): ", scn_minor);
			for (cnt=0; cnt < cnt_max; cnt++) {
				printk("%X ", ibuf[cnt]);
			}
			printk("\n");
		}
#endif

		if (partial) { /* Data returned */
			if (copy_to_user(buffer, ibuf, partial)) {
				ret = -EFAULT;
				break;
			}
			count -= this_read; /* Compensate for short reads */
			bytes_read += partial; /* Keep tally of what actually was read */
			buffer += partial;
		} else {
			ret = 0;
			break;
		}
	}
	up(&(scn->sem));
	return ret ? ret : bytes_read;
}

static int
ioctl_scanner(struct inode *inode, struct file *file,
	      unsigned int cmd, unsigned long arg)
{
	struct usb_device *dev;

	kdev_t scn_minor;

	scn_minor = USB_SCN_MINOR(inode);

	if (!p_scn_table[scn_minor]) {
		err("ioctl_scanner(%d): invalid scn_minor", scn_minor);
		return -ENODEV;
	}

	dev = p_scn_table[scn_minor]->scn_dev;

	switch (cmd)
	{
	case SCANNER_IOCTL_VENDOR :
		return (put_user(dev->descriptor.idVendor, (unsigned int *) arg));
	case SCANNER_IOCTL_PRODUCT :
		return (put_user(dev->descriptor.idProduct, (unsigned int *) arg));
#ifdef PV8630
	case PV8630_IOCTL_INREQUEST :
	{
		int result;

		struct {
			__u8  data;
			__u8  request;
			__u16 value;
			__u16 index;
		} args;

		if (copy_from_user(&args, (void *)arg, sizeof(args)))
			return -EFAULT;

		result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
					 args.request, USB_TYPE_VENDOR|
					 USB_RECIP_DEVICE|USB_DIR_IN,
					 args.value, args.index, &args.data,
					 1, HZ*5);

		dbg("ioctl_scanner(%d): inreq: args.data:%x args.value:%x args.index:%x args.request:%x\n", scn_minor, args.data, args.value, args.index, args.request);

		if (copy_to_user((void *)arg, &args, sizeof(args)))
			return -EFAULT;

		dbg("ioctl_scanner(%d): inreq: result:%d\n", scn_minor, result);

		return result;
	}
	case PV8630_IOCTL_OUTREQUEST :
	{
		int result;

		struct {
			__u8  request;
			__u16 value;
			__u16 index;
		} args;

		if (copy_from_user(&args, (void *)arg, sizeof(args)))
			return -EFAULT;

		dbg("ioctl_scanner(%d): outreq: args.value:%x args.index:%x args.request:%x\n", scn_minor, args.value, args.index, args.request);

		result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
					 args.request, USB_TYPE_VENDOR|
					 USB_RECIP_DEVICE|USB_DIR_OUT,
					 args.value, args.index, NULL,
					 0, HZ*5);

		dbg("ioctl_scanner(%d): outreq: result:%d\n", scn_minor, result);

		return result;
	}
#endif /* PV8630 */
 	case SCANNER_IOCTL_CTRLMSG:
 	{
 		struct ctrlmsg_ioctl {
 			devrequest	req;
 			void		*data;
 		} cmsg;
 		int pipe, nb, ret;
 		unsigned char buf[64];
 
 		if (copy_from_user(&cmsg, (void *)arg, sizeof(cmsg)))
 			return -EFAULT;

 		nb = le16_to_cpup(&cmsg.req.length);

 		if (nb > sizeof(buf))
 			return -EINVAL;

 		if ((cmsg.req.requesttype & 0x80) == 0) {
 			pipe = usb_sndctrlpipe(dev, 0);
 			if (nb > 0 && copy_from_user(buf, cmsg.data, nb))
 				return -EFAULT;
 		} else {
 			pipe = usb_rcvctrlpipe(dev, 0);
		}

 		ret = usb_control_msg(dev, pipe, cmsg.req.request,
 				      cmsg.req.requesttype,
 				      le16_to_cpup(&cmsg.req.value),
 				      le16_to_cpup(&cmsg.req.index),
 				      buf, nb, HZ);

 		if (ret < 0) {
 			err("ioctl_scanner(%d): control_msg returned %d\n", scn_minor, ret);
 			return -EIO;
 		}

 		if (nb > 0 && (cmsg.req.requesttype & 0x80) && copy_to_user(cmsg.data, buf, nb))
 			return -EFAULT;

 		return 0;
 	}
	default:
		return -ENOTTY;
	}
	return 0;
}

static struct
file_operations usb_scanner_fops = {
	read:		read_scanner,
	write:		write_scanner,
	ioctl:		ioctl_scanner,
	open:		open_scanner,
	release:	close_scanner,
};

static void *
probe_scanner(struct usb_device *dev, unsigned int ifnum,
	      const struct usb_device_id *id)
{
	struct scn_usb_data *scn;
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint;

	int ep_cnt;
	int ix;

	kdev_t scn_minor;

	char valid_device = 0;
	char have_bulk_in, have_bulk_out, have_intr;
	char name[10];

	if (vendor != -1 && product != -1) {
		info("probe_scanner: User specified USB scanner -- Vendor:Product - %x:%x", vendor, product);
	}

	dbg("probe_scanner: USB dev address:%p", dev);
	dbg("probe_scanner: ifnum:%u", ifnum);

/*
 * 1. Check Vendor/Product
 * 2. Determine/Assign Bulk Endpoints
 * 3. Determine/Assign Intr Endpoint
 */

/*
 * There doesn't seem to be an imaging class defined in the USB
 * Spec. (yet).  If there is, HP isn't following it and it doesn't
 * look like anybody else is either.  Therefore, we have to test the
 * Vendor and Product ID's to see what we have.  Also, other scanners
 * may be able to use this driver by specifying both vendor and
 * product ID's as options to the scanner module in conf.modules.
 *
 * NOTE: Just because a product is supported here does not mean that
 * applications exist that support the product.  It's in the hopes
 * that this will allow developers a means to produce applications
 * that will support USB products.
 *
 * Until we detect a device which is pleasing, we silently punt.
 */

	for (ix = 0; ix < sizeof (scanner_device_ids) / sizeof (struct usb_device_id); ix++) {
		if ((dev->descriptor.idVendor == scanner_device_ids [ix].idVendor) &&
		    (dev->descriptor.idProduct == scanner_device_ids [ix].idProduct)) {
			valid_device = 1;
			break;
                }
	}
	if (dev->descriptor.idVendor == vendor &&   /* User specified */
	    dev->descriptor.idProduct == product) { /* User specified */
		valid_device = 1;
	}

        if (!valid_device)
                return NULL;    /* We didn't find anything pleasing */

/*
 * After this point we can be a little noisy about what we are trying to
 *  configure.
 */

	if (dev->descriptor.bNumConfigurations != 1) {
		info("probe_scanner: Only one device configuration is supported.");
		return NULL;
	}

	if (dev->config[0].bNumInterfaces != 1) {
		info("probe_scanner: Only one device interface is supported.");
		return NULL;
	}

	interface = dev->config[0].interface[ifnum].altsetting;
	endpoint = interface[ifnum].endpoint;

/*
 * Start checking for two bulk endpoints OR two bulk endpoints *and* one
 * interrupt endpoint. If we have an interrupt endpoint go ahead and
 * setup the handler. FIXME: This is a future enhancement...
 */

	dbg("probe_scanner: Number of Endpoints:%d", (int) interface->bNumEndpoints);

	if ((interface->bNumEndpoints != 2) && (interface->bNumEndpoints != 3)) {
		info("probe_scanner: Only two or three endpoints supported.");
		return NULL;
	}

	ep_cnt = have_bulk_in = have_bulk_out = have_intr = 0;

	while (ep_cnt < interface->bNumEndpoints) {

		if (!have_bulk_in && IS_EP_BULK_IN(endpoint[ep_cnt])) {
			ep_cnt++;
			have_bulk_in = ep_cnt;
			dbg("probe_scanner: bulk_in_ep:%d", have_bulk_in);
			continue;
		}

		if (!have_bulk_out && IS_EP_BULK_OUT(endpoint[ep_cnt])) {
			ep_cnt++;
			have_bulk_out = ep_cnt;
			dbg("probe_scanner: bulk_out_ep:%d", have_bulk_out);
			continue;
		}

		if (!have_intr && IS_EP_INTR(endpoint[ep_cnt])) {
			ep_cnt++;
			have_intr = ep_cnt;
			dbg("probe_scanner: intr_ep:%d", have_intr);
			continue;
		}
		info("probe_scanner: Undetected endpoint -- consult Documentation/usb/scanner.txt.");
		return NULL;	/* Shouldn't ever get here unless we have something weird */
	}


/*
 * Perform a quick check to make sure that everything worked as it
 * should have.
 */

	switch(interface->bNumEndpoints) {
	case 2:
		if (!have_bulk_in || !have_bulk_out) {
			info("probe_scanner: Two bulk endpoints required.");
			return NULL;
		}
		break;
	case 3:
		if (!have_bulk_in || !have_bulk_out || !have_intr) {
			info("probe_scanner: Two bulk endpoints and one interrupt endpoint required.");
			return NULL;
		}
		break;
	default:
		info("probe_scanner: Endpoint determination failed --  consult Documentation/usb/scanner.txt");
		return NULL;
	}


/*
 * Determine a minor number and initialize the structure associated
 * with it.  The problem with this is that we are counting on the fact
 * that the user will sequentially add device nodes for the scanner
 * devices.  */
	
	down(&scn_mutex);

	for (scn_minor = 0; scn_minor < SCN_MAX_MNR; scn_minor++) {
		if (!p_scn_table[scn_minor])
			break;
	}

/* Check to make sure that the last slot isn't already taken */
	if (p_scn_table[scn_minor]) {
		err("probe_scanner: No more minor devices remaining.");
		return NULL;
	}

	dbg("probe_scanner: Allocated minor:%d", scn_minor);

	if (!(scn = kmalloc (sizeof (struct scn_usb_data), GFP_KERNEL))) {
		err("probe_scanner: Out of memory.");
		return NULL;
	}
	memset (scn, 0, sizeof(struct scn_usb_data));

	init_MUTEX(&(scn->sem)); /* Initializes to unlocked */

	dbg ("probe_scanner(%d): Address of scn:%p", scn_minor, scn);

/* Ok, if we detected an interrupt EP, setup a handler for it */
	if (have_intr) {
		dbg("probe_scanner(%d): Configuring IRQ handler for intr EP:%d", scn_minor, have_intr);
		FILL_INT_URB(&scn->scn_irq, dev,
			     usb_rcvintpipe(dev, have_intr),
			     &scn->button, 1, irq_scanner, scn,
			     // endpoint[(int)have_intr].bInterval);
			     250);

	        if (usb_submit_urb(&scn->scn_irq)) {
			err("probe_scanner(%d): Unable to allocate INT URB.", scn_minor);
                	kfree(scn);
			up(&scn_mutex);
                	return NULL;
        	}
	}


/* Ok, now initialize all the relevant values */
	if (!(scn->obuf = (char *)kmalloc(OBUF_SIZE, GFP_KERNEL))) {
		err("probe_scanner(%d): Not enough memory for the output buffer.", scn_minor);
		kfree(scn);
		up(&scn_mutex);
		return NULL;
	}
	dbg("probe_scanner(%d): obuf address:%p", scn_minor, scn->obuf);

	if (!(scn->ibuf = (char *)kmalloc(IBUF_SIZE, GFP_KERNEL))) {
		err("probe_scanner(%d): Not enough memory for the input buffer.", scn_minor);
		kfree(scn->obuf);
		kfree(scn);
		up(&scn_mutex);
		return NULL;
	}
	dbg("probe_scanner(%d): ibuf address:%p", scn_minor, scn->ibuf);
	

	switch (dev->descriptor.idVendor) { /* Scanner specific read timeout parameters */
	case 0x04b8:		/* Seiko/Epson */
		scn->rd_nak_timeout = HZ * 40;
		break;
	case 0x055f:		/* Mustek */
	case 0x0400:		/* Another Mustek */
	case 0x0ff5:		/* And yet another Mustek */
		scn->rd_nak_timeout = HZ * 1;
	default:
		scn->rd_nak_timeout = RD_NAK_TIMEOUT;
	}


	if (read_timeout > 0) {	/* User specified read timeout overrides everything */
		info("probe_scanner: User specified USB read timeout - %d", read_timeout);
		scn->rd_nak_timeout = read_timeout;
	}


	scn->bulk_in_ep = have_bulk_in;
	scn->bulk_out_ep = have_bulk_out;
	scn->intr_ep = have_intr;
	scn->present = 1;
	scn->scn_dev = dev;
	scn->scn_minor = scn_minor;
	scn->isopen = 0;

	sprintf(name, "scanner%d", scn->scn_minor);
	
	scn->devfs = devfs_register(usb_devfs_handle, name,
				    DEVFS_FL_DEFAULT, USB_MAJOR,
				    SCN_BASE_MNR + scn->scn_minor,
				    S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP |
				    S_IWGRP | S_IROTH | S_IWOTH, &usb_scanner_fops, NULL);
	if (scn->devfs == NULL)
		dbg("scanner%d: device node registration failed", scn_minor);

	up(&scn_mutex);

	return p_scn_table[scn_minor] = scn;
}

static void
disconnect_scanner(struct usb_device *dev, void *ptr)
{
	struct scn_usb_data *scn = (struct scn_usb_data *) ptr;

	down (&scn_mutex);
	down (&(scn->sem));

	if(scn->intr_ep) {
		dbg("disconnect_scanner(%d): Unlinking IRQ URB", scn->scn_minor);
		usb_unlink_urb(&scn->scn_irq);
	}
        usb_driver_release_interface(&scanner_driver,
                &scn->scn_dev->actconfig->interface[scn->ifnum]);

	kfree(scn->ibuf);
	kfree(scn->obuf);

	dbg("disconnect_scanner: De-allocating minor:%d", scn->scn_minor);
	devfs_unregister(scn->devfs);
	p_scn_table[scn->scn_minor] = NULL;
	up (&(scn->sem));
	kfree (scn);
	up (&scn_mutex);
}

static struct
usb_driver scanner_driver = {
	name:		"usbscanner",
	probe:		probe_scanner,
	disconnect:	disconnect_scanner,
	fops:		&usb_scanner_fops,
	minor:		SCN_BASE_MNR,
	id_table:	NULL, /* This would be scanner_device_ids, but we
				 need to check every USB device, in case
				 we match a user defined vendor/product ID. */
};

void __exit
usb_scanner_exit(void)
{
	usb_deregister(&scanner_driver);
}

int __init
usb_scanner_init (void)
{
        if (usb_register(&scanner_driver) < 0)
                return -1;

	info(DRIVER_VERSION ":" DRIVER_DESC);
	return 0;
}

module_init(usb_scanner_init);
module_exit(usb_scanner_exit);
