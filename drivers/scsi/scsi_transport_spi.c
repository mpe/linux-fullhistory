/* 
 *  Parallel SCSI (SPI) transport specific attributes exported to sysfs.
 *
 *  Copyright (c) 2003 Silicon Graphics, Inc.  All rights reserved.
 *  Copyright (c) 2004, 2005 James Bottomley <James.Bottomley@SteelEye.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/workqueue.h>
#include <asm/scatterlist.h>
#include <asm/io.h>
#include <scsi/scsi.h>
#include "scsi_priv.h"
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_request.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_spi.h>

#define SPI_PRINTK(x, l, f, a...)	dev_printk(l, &(x)->dev, f , ##a)

#define SPI_NUM_ATTRS 10	/* increase this if you add attributes */
#define SPI_OTHER_ATTRS 1	/* Increase this if you add "always
				 * on" attributes */
#define SPI_HOST_ATTRS	1

#define SPI_MAX_ECHO_BUFFER_SIZE	4096

/* Private data accessors (keep these out of the header file) */
#define spi_dv_pending(x) (((struct spi_transport_attrs *)&(x)->starget_data)->dv_pending)
#define spi_dv_sem(x) (((struct spi_transport_attrs *)&(x)->starget_data)->dv_sem)

struct spi_internal {
	struct scsi_transport_template t;
	struct spi_function_template *f;
	/* The actual attributes */
	struct class_device_attribute private_attrs[SPI_NUM_ATTRS];
	/* The array of null terminated pointers to attributes 
	 * needed by scsi_sysfs.c */
	struct class_device_attribute *attrs[SPI_NUM_ATTRS + SPI_OTHER_ATTRS + 1];
	struct class_device_attribute private_host_attrs[SPI_HOST_ATTRS];
	struct class_device_attribute *host_attrs[SPI_HOST_ATTRS + 1];
};

#define to_spi_internal(tmpl)	container_of(tmpl, struct spi_internal, t)

static const char *const ppr_to_ns[] = {
	/* The PPR values 0-6 are reserved, fill them in when
	 * the committee defines them */
	NULL,			/* 0x00 */
	NULL,			/* 0x01 */
	NULL,			/* 0x02 */
	NULL,			/* 0x03 */
	NULL,			/* 0x04 */
	NULL,			/* 0x05 */
	NULL,			/* 0x06 */
	"3.125",		/* 0x07 */
	"6.25",			/* 0x08 */
	"12.5",			/* 0x09 */
	"25",			/* 0x0a */
	"30.3",			/* 0x0b */
	"50",			/* 0x0c */
};
/* The PPR values at which you calculate the period in ns by multiplying
 * by 4 */
#define SPI_STATIC_PPR	0x0c

static struct {
	enum spi_signal_type	value;
	char			*name;
} signal_types[] = {
	{ SPI_SIGNAL_UNKNOWN, "unknown" },
	{ SPI_SIGNAL_SE, "SE" },
	{ SPI_SIGNAL_LVD, "LVD" },
	{ SPI_SIGNAL_HVD, "HVD" },
};

static inline const char *spi_signal_to_string(enum spi_signal_type type)
{
	int i;

	for (i = 0; i < sizeof(signal_types)/sizeof(signal_types[0]); i++) {
		if (type == signal_types[i].value)
			return signal_types[i].name;
	}
	return NULL;
}
static inline enum spi_signal_type spi_signal_to_value(const char *name)
{
	int i, len;

	for (i = 0; i < sizeof(signal_types)/sizeof(signal_types[0]); i++) {
		len =  strlen(signal_types[i].name);
		if (strncmp(name, signal_types[i].name, len) == 0 &&
		    (name[len] == '\n' || name[len] == '\0'))
			return signal_types[i].value;
	}
	return SPI_SIGNAL_UNKNOWN;
}

static int spi_host_setup(struct device *dev)
{
	struct Scsi_Host *shost = dev_to_shost(dev);

	spi_signalling(shost) = SPI_SIGNAL_UNKNOWN;

	return 0;
}

static DECLARE_TRANSPORT_CLASS(spi_host_class,
			       "spi_host",
			       spi_host_setup,
			       NULL,
			       NULL);

static int spi_host_match(struct attribute_container *cont,
			  struct device *dev)
{
	struct Scsi_Host *shost;
	struct spi_internal *i;

	if (!scsi_is_host_device(dev))
		return 0;

	shost = dev_to_shost(dev);
	if (!shost->transportt  || shost->transportt->host_attrs.class
	    != &spi_host_class.class)
		return 0;

	i = to_spi_internal(shost->transportt);
	
	return &i->t.host_attrs == cont;
}

static int spi_device_configure(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct scsi_target *starget = sdev->sdev_target;

	/* Populate the target capability fields with the values
	 * gleaned from the device inquiry */

	spi_support_sync(starget) = scsi_device_sync(sdev);
	spi_support_wide(starget) = scsi_device_wide(sdev);
	spi_support_dt(starget) = scsi_device_dt(sdev);
	spi_support_dt_only(starget) = scsi_device_dt_only(sdev);
	spi_support_ius(starget) = scsi_device_ius(sdev);
	spi_support_qas(starget) = scsi_device_qas(sdev);

	return 0;
}

static int spi_setup_transport_attrs(struct device *dev)
{
	struct scsi_target *starget = to_scsi_target(dev);

	spi_period(starget) = -1;	/* illegal value */
	spi_offset(starget) = 0;	/* async */
	spi_width(starget) = 0;	/* narrow */
	spi_iu(starget) = 0;	/* no IU */
	spi_dt(starget) = 0;	/* ST */
	spi_qas(starget) = 0;
	spi_wr_flow(starget) = 0;
	spi_rd_strm(starget) = 0;
	spi_rti(starget) = 0;
	spi_pcomp_en(starget) = 0;
	spi_dv_pending(starget) = 0;
	spi_initial_dv(starget) = 0;
	init_MUTEX(&spi_dv_sem(starget));

	return 0;
}

#define spi_transport_show_function(field, format_string)		\
									\
static ssize_t								\
show_spi_transport_##field(struct class_device *cdev, char *buf)	\
{									\
	struct scsi_target *starget = transport_class_to_starget(cdev);	\
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);	\
	struct spi_transport_attrs *tp;					\
	struct spi_internal *i = to_spi_internal(shost->transportt);	\
	tp = (struct spi_transport_attrs *)&starget->starget_data;	\
	if (i->f->get_##field)						\
		i->f->get_##field(starget);				\
	return snprintf(buf, 20, format_string, tp->field);		\
}

#define spi_transport_store_function(field, format_string)		\
static ssize_t								\
store_spi_transport_##field(struct class_device *cdev, const char *buf, \
			    size_t count)				\
{									\
	int val;							\
	struct scsi_target *starget = transport_class_to_starget(cdev);	\
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);	\
	struct spi_internal *i = to_spi_internal(shost->transportt);	\
									\
	val = simple_strtoul(buf, NULL, 0);				\
	i->f->set_##field(starget, val);				\
	return count;							\
}

#define spi_transport_rd_attr(field, format_string)			\
	spi_transport_show_function(field, format_string)		\
	spi_transport_store_function(field, format_string)		\
static CLASS_DEVICE_ATTR(field, S_IRUGO | S_IWUSR,			\
			 show_spi_transport_##field,			\
			 store_spi_transport_##field);

/* The Parallel SCSI Tranport Attributes: */
spi_transport_rd_attr(offset, "%d\n");
spi_transport_rd_attr(width, "%d\n");
spi_transport_rd_attr(iu, "%d\n");
spi_transport_rd_attr(dt, "%d\n");
spi_transport_rd_attr(qas, "%d\n");
spi_transport_rd_attr(wr_flow, "%d\n");
spi_transport_rd_attr(rd_strm, "%d\n");
spi_transport_rd_attr(rti, "%d\n");
spi_transport_rd_attr(pcomp_en, "%d\n");

static ssize_t
store_spi_revalidate(struct class_device *cdev, const char *buf, size_t count)
{
	struct scsi_target *starget = transport_class_to_starget(cdev);

	/* FIXME: we're relying on an awful lot of device internals
	 * here.  We really need a function to get the first available
	 * child */
	struct device *dev = container_of(starget->dev.children.next, struct device, node);
	struct scsi_device *sdev = to_scsi_device(dev);
	spi_dv_device(sdev);
	return count;
}
static CLASS_DEVICE_ATTR(revalidate, S_IWUSR, NULL, store_spi_revalidate);

/* Translate the period into ns according to the current spec
 * for SDTR/PPR messages */
static ssize_t show_spi_transport_period(struct class_device *cdev, char *buf)

{
	struct scsi_target *starget = transport_class_to_starget(cdev);
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct spi_transport_attrs *tp;
	const char *str;
	struct spi_internal *i = to_spi_internal(shost->transportt);

	tp = (struct spi_transport_attrs *)&starget->starget_data;

	if (i->f->get_period)
		i->f->get_period(starget);

	switch(tp->period) {

	case 0x07 ... SPI_STATIC_PPR:
		str = ppr_to_ns[tp->period];
		if(!str)
			str = "reserved";
		break;


	case (SPI_STATIC_PPR+1) ... 0xff:
		return sprintf(buf, "%d\n", tp->period * 4);

	default:
		str = "unknown";
	}
	return sprintf(buf, "%s\n", str);
}

static ssize_t
store_spi_transport_period(struct class_device *cdev, const char *buf,
			    size_t count)
{
	struct scsi_target *starget = transport_class_to_starget(cdev);
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct spi_internal *i = to_spi_internal(shost->transportt);
	int j, period = -1;

	for (j = 0; j < SPI_STATIC_PPR; j++) {
		int len;

		if(ppr_to_ns[j] == NULL)
			continue;

		len = strlen(ppr_to_ns[j]);

		if(strncmp(ppr_to_ns[j], buf, len) != 0)
			continue;

		if(buf[len] != '\n')
			continue;
		
		period = j;
		break;
	}

	if (period == -1) {
		int val = simple_strtoul(buf, NULL, 0);


		/* Should probably check limits here, but this
		 * gets reasonably close to OK for most things */
		period = val/4;
	}

	if (period > 0xff)
		period = 0xff;

	i->f->set_period(starget, period);

	return count;
}
	
static CLASS_DEVICE_ATTR(period, S_IRUGO | S_IWUSR, 
			 show_spi_transport_period,
			 store_spi_transport_period);

static ssize_t show_spi_host_signalling(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *shost = transport_class_to_shost(cdev);
	struct spi_internal *i = to_spi_internal(shost->transportt);

	if (i->f->get_signalling)
		i->f->get_signalling(shost);

	return sprintf(buf, "%s\n", spi_signal_to_string(spi_signalling(shost)));
}
static ssize_t store_spi_host_signalling(struct class_device *cdev,
					 const char *buf, size_t count)
{
	struct Scsi_Host *shost = transport_class_to_shost(cdev);
	struct spi_internal *i = to_spi_internal(shost->transportt);
	enum spi_signal_type type = spi_signal_to_value(buf);

	if (type != SPI_SIGNAL_UNKNOWN)
		i->f->set_signalling(shost, type);

	return count;
}
static CLASS_DEVICE_ATTR(signalling, S_IRUGO | S_IWUSR,
			 show_spi_host_signalling,
			 store_spi_host_signalling);

#define DV_SET(x, y)			\
	if(i->f->set_##x)		\
		i->f->set_##x(sdev->sdev_target, y)

#define DV_LOOPS	3
#define DV_TIMEOUT	(10*HZ)
#define DV_RETRIES	3	/* should only need at most 
				 * two cc/ua clears */

enum spi_compare_returns {
	SPI_COMPARE_SUCCESS,
	SPI_COMPARE_FAILURE,
	SPI_COMPARE_SKIP_TEST,
};


/* This is for read/write Domain Validation:  If the device supports
 * an echo buffer, we do read/write tests to it */
static enum spi_compare_returns
spi_dv_device_echo_buffer(struct scsi_request *sreq, u8 *buffer,
			  u8 *ptr, const int retries)
{
	struct scsi_device *sdev = sreq->sr_device;
	int len = ptr - buffer;
	int j, k, r;
	unsigned int pattern = 0x0000ffff;

	const char spi_write_buffer[] = {
		WRITE_BUFFER, 0x0a, 0, 0, 0, 0, 0, len >> 8, len & 0xff, 0
	};
	const char spi_read_buffer[] = {
		READ_BUFFER, 0x0a, 0, 0, 0, 0, 0, len >> 8, len & 0xff, 0
	};

	/* set up the pattern buffer.  Doesn't matter if we spill
	 * slightly beyond since that's where the read buffer is */
	for (j = 0; j < len; ) {

		/* fill the buffer with counting (test a) */
		for ( ; j < min(len, 32); j++)
			buffer[j] = j;
		k = j;
		/* fill the buffer with alternating words of 0x0 and
		 * 0xffff (test b) */
		for ( ; j < min(len, k + 32); j += 2) {
			u16 *word = (u16 *)&buffer[j];
			
			*word = (j & 0x02) ? 0x0000 : 0xffff;
		}
		k = j;
		/* fill with crosstalk (alternating 0x5555 0xaaa)
                 * (test c) */
		for ( ; j < min(len, k + 32); j += 2) {
			u16 *word = (u16 *)&buffer[j];

			*word = (j & 0x02) ? 0x5555 : 0xaaaa;
		}
		k = j;
		/* fill with shifting bits (test d) */
		for ( ; j < min(len, k + 32); j += 4) {
			u32 *word = (unsigned int *)&buffer[j];
			u32 roll = (pattern & 0x80000000) ? 1 : 0;
			
			*word = pattern;
			pattern = (pattern << 1) | roll;
		}
		/* don't bother with random data (test e) */
	}

	for (r = 0; r < retries; r++) {
		sreq->sr_cmd_len = 0;	/* wait_req to fill in */
		sreq->sr_data_direction = DMA_TO_DEVICE;
		scsi_wait_req(sreq, spi_write_buffer, buffer, len,
			      DV_TIMEOUT, DV_RETRIES);
		if(sreq->sr_result || !scsi_device_online(sdev)) {
			struct scsi_sense_hdr sshdr;

			scsi_device_set_state(sdev, SDEV_QUIESCE);
			if (scsi_request_normalize_sense(sreq, &sshdr)
			    && sshdr.sense_key == ILLEGAL_REQUEST
			    /* INVALID FIELD IN CDB */
			    && sshdr.asc == 0x24 && sshdr.ascq == 0x00)
				/* This would mean that the drive lied
				 * to us about supporting an echo
				 * buffer (unfortunately some Western
				 * Digital drives do precisely this)
				 */
				return SPI_COMPARE_SKIP_TEST;


			SPI_PRINTK(sdev->sdev_target, KERN_ERR, "Write Buffer failure %x\n", sreq->sr_result);
			return SPI_COMPARE_FAILURE;
		}

		memset(ptr, 0, len);
		sreq->sr_cmd_len = 0;	/* wait_req to fill in */
		sreq->sr_data_direction = DMA_FROM_DEVICE;
		scsi_wait_req(sreq, spi_read_buffer, ptr, len,
			      DV_TIMEOUT, DV_RETRIES);
		scsi_device_set_state(sdev, SDEV_QUIESCE);

		if (memcmp(buffer, ptr, len) != 0)
			return SPI_COMPARE_FAILURE;
	}
	return SPI_COMPARE_SUCCESS;
}

/* This is for the simplest form of Domain Validation: a read test
 * on the inquiry data from the device */
static enum spi_compare_returns
spi_dv_device_compare_inquiry(struct scsi_request *sreq, u8 *buffer,
			      u8 *ptr, const int retries)
{
	int r;
	const int len = sreq->sr_device->inquiry_len;
	struct scsi_device *sdev = sreq->sr_device;
	const char spi_inquiry[] = {
		INQUIRY, 0, 0, 0, len, 0
	};

	for (r = 0; r < retries; r++) {
		sreq->sr_cmd_len = 0;	/* wait_req to fill in */
		sreq->sr_data_direction = DMA_FROM_DEVICE;

		memset(ptr, 0, len);

		scsi_wait_req(sreq, spi_inquiry, ptr, len,
			      DV_TIMEOUT, DV_RETRIES);
		
		if(sreq->sr_result || !scsi_device_online(sdev)) {
			scsi_device_set_state(sdev, SDEV_QUIESCE);
			return SPI_COMPARE_FAILURE;
		}

		/* If we don't have the inquiry data already, the
		 * first read gets it */
		if (ptr == buffer) {
			ptr += len;
			--r;
			continue;
		}

		if (memcmp(buffer, ptr, len) != 0)
			/* failure */
			return SPI_COMPARE_FAILURE;
	}
	return SPI_COMPARE_SUCCESS;
}

static enum spi_compare_returns
spi_dv_retrain(struct scsi_request *sreq, u8 *buffer, u8 *ptr,
	       enum spi_compare_returns 
	       (*compare_fn)(struct scsi_request *, u8 *, u8 *, int))
{
	struct spi_internal *i = to_spi_internal(sreq->sr_host->transportt);
	struct scsi_device *sdev = sreq->sr_device;
	int period = 0, prevperiod = 0; 
	enum spi_compare_returns retval;


	for (;;) {
		int newperiod;
		retval = compare_fn(sreq, buffer, ptr, DV_LOOPS);

		if (retval == SPI_COMPARE_SUCCESS
		    || retval == SPI_COMPARE_SKIP_TEST)
			break;

		/* OK, retrain, fallback */
		if (i->f->get_period)
			i->f->get_period(sdev->sdev_target);
		newperiod = spi_period(sdev->sdev_target);
		period = newperiod > period ? newperiod : period;
		if (period < 0x0d)
			period++;
		else
			period += period >> 1;

		if (unlikely(period > 0xff || period == prevperiod)) {
			/* Total failure; set to async and return */
			SPI_PRINTK(sdev->sdev_target, KERN_ERR, "Domain Validation Failure, dropping back to Asynchronous\n");
			DV_SET(offset, 0);
			return SPI_COMPARE_FAILURE;
		}
		SPI_PRINTK(sdev->sdev_target, KERN_ERR, "Domain Validation detected failure, dropping back\n");
		DV_SET(period, period);
		prevperiod = period;
	}
	return retval;
}

static int
spi_dv_device_get_echo_buffer(struct scsi_request *sreq, u8 *buffer)
{
	int l;

	/* first off do a test unit ready.  This can error out 
	 * because of reservations or some other reason.  If it
	 * fails, the device won't let us write to the echo buffer
	 * so just return failure */
	
	const char spi_test_unit_ready[] = {
		TEST_UNIT_READY, 0, 0, 0, 0, 0
	};

	const char spi_read_buffer_descriptor[] = {
		READ_BUFFER, 0x0b, 0, 0, 0, 0, 0, 0, 4, 0
	};

	
	sreq->sr_cmd_len = 0;
	sreq->sr_data_direction = DMA_NONE;

	/* We send a set of three TURs to clear any outstanding 
	 * unit attention conditions if they exist (Otherwise the
	 * buffer tests won't be happy).  If the TUR still fails
	 * (reservation conflict, device not ready, etc) just
	 * skip the write tests */
	for (l = 0; ; l++) {
		scsi_wait_req(sreq, spi_test_unit_ready, NULL, 0,
			      DV_TIMEOUT, DV_RETRIES);

		if(sreq->sr_result) {
			if(l >= 3)
				return 0;
		} else {
			/* TUR succeeded */
			break;
		}
	}

	sreq->sr_cmd_len = 0;
	sreq->sr_data_direction = DMA_FROM_DEVICE;

	scsi_wait_req(sreq, spi_read_buffer_descriptor, buffer, 4,
		      DV_TIMEOUT, DV_RETRIES);

	if (sreq->sr_result)
		/* Device has no echo buffer */
		return 0;

	return buffer[3] + ((buffer[2] & 0x1f) << 8);
}

static void
spi_dv_device_internal(struct scsi_request *sreq, u8 *buffer)
{
	struct spi_internal *i = to_spi_internal(sreq->sr_host->transportt);
	struct scsi_device *sdev = sreq->sr_device;
	int len = sdev->inquiry_len;
	/* first set us up for narrow async */
	DV_SET(offset, 0);
	DV_SET(width, 0);
	
	if (spi_dv_device_compare_inquiry(sreq, buffer, buffer, DV_LOOPS)
	    != SPI_COMPARE_SUCCESS) {
		SPI_PRINTK(sdev->sdev_target, KERN_ERR, "Domain Validation Initial Inquiry Failed\n");
		/* FIXME: should probably offline the device here? */
		return;
	}

	/* test width */
	if (i->f->set_width && sdev->wdtr) {
		i->f->set_width(sdev->sdev_target, 1);

		if (spi_dv_device_compare_inquiry(sreq, buffer,
						   buffer + len,
						   DV_LOOPS)
		    != SPI_COMPARE_SUCCESS) {
			SPI_PRINTK(sdev->sdev_target, KERN_ERR, "Wide Transfers Fail\n");
			i->f->set_width(sdev->sdev_target, 0);
		}
	}

	if (!i->f->set_period)
		return;

	/* device can't handle synchronous */
	if(!sdev->ppr && !sdev->sdtr)
		return;

	/* see if the device has an echo buffer.  If it does we can
	 * do the SPI pattern write tests */

	len = 0;
	if (sdev->ppr)
		len = spi_dv_device_get_echo_buffer(sreq, buffer);

 retry:

	/* now set up to the maximum */
	DV_SET(offset, 255);
	DV_SET(period, 1);

	if (len == 0) {
		SPI_PRINTK(sdev->sdev_target, KERN_INFO, "Domain Validation skipping write tests\n");
		spi_dv_retrain(sreq, buffer, buffer + len,
			       spi_dv_device_compare_inquiry);
		return;
	}

	if (len > SPI_MAX_ECHO_BUFFER_SIZE) {
		SPI_PRINTK(sdev->sdev_target, KERN_WARNING, "Echo buffer size %d is too big, trimming to %d\n", len, SPI_MAX_ECHO_BUFFER_SIZE);
		len = SPI_MAX_ECHO_BUFFER_SIZE;
	}

	if (spi_dv_retrain(sreq, buffer, buffer + len,
			   spi_dv_device_echo_buffer)
	    == SPI_COMPARE_SKIP_TEST) {
		/* OK, the stupid drive can't do a write echo buffer
		 * test after all, fall back to the read tests */
		len = 0;
		goto retry;
	}
}


/**	spi_dv_device - Do Domain Validation on the device
 *	@sdev:		scsi device to validate
 *
 *	Performs the domain validation on the given device in the
 *	current execution thread.  Since DV operations may sleep,
 *	the current thread must have user context.  Also no SCSI
 *	related locks that would deadlock I/O issued by the DV may
 *	be held.
 */
void
spi_dv_device(struct scsi_device *sdev)
{
	struct scsi_request *sreq = scsi_allocate_request(sdev, GFP_KERNEL);
	struct scsi_target *starget = sdev->sdev_target;
	u8 *buffer;
	const int len = SPI_MAX_ECHO_BUFFER_SIZE*2;

	if (unlikely(!sreq))
		return;

	if (unlikely(scsi_device_get(sdev)))
		goto out_free_req;

	buffer = kmalloc(len, GFP_KERNEL);

	if (unlikely(!buffer))
		goto out_put;

	memset(buffer, 0, len);

	/* We need to verify that the actual device will quiesce; the
	 * later target quiesce is just a nice to have */
	if (unlikely(scsi_device_quiesce(sdev)))
		goto out_free;

	scsi_target_quiesce(starget);

	spi_dv_pending(starget) = 1;
	down(&spi_dv_sem(starget));

	SPI_PRINTK(starget, KERN_INFO, "Beginning Domain Validation\n");

	spi_dv_device_internal(sreq, buffer);

	SPI_PRINTK(starget, KERN_INFO, "Ending Domain Validation\n");

	up(&spi_dv_sem(starget));
	spi_dv_pending(starget) = 0;

	scsi_target_resume(starget);

	spi_initial_dv(starget) = 1;

 out_free:
	kfree(buffer);
 out_put:
	scsi_device_put(sdev);
 out_free_req:
	scsi_release_request(sreq);
}
EXPORT_SYMBOL(spi_dv_device);

struct work_queue_wrapper {
	struct work_struct	work;
	struct scsi_device	*sdev;
};

static void
spi_dv_device_work_wrapper(void *data)
{
	struct work_queue_wrapper *wqw = (struct work_queue_wrapper *)data;
	struct scsi_device *sdev = wqw->sdev;

	kfree(wqw);
	spi_dv_device(sdev);
	spi_dv_pending(sdev->sdev_target) = 0;
	scsi_device_put(sdev);
}


/**
 *	spi_schedule_dv_device - schedule domain validation to occur on the device
 *	@sdev:	The device to validate
 *
 *	Identical to spi_dv_device() above, except that the DV will be
 *	scheduled to occur in a workqueue later.  All memory allocations
 *	are atomic, so may be called from any context including those holding
 *	SCSI locks.
 */
void
spi_schedule_dv_device(struct scsi_device *sdev)
{
	struct work_queue_wrapper *wqw =
		kmalloc(sizeof(struct work_queue_wrapper), GFP_ATOMIC);

	if (unlikely(!wqw))
		return;

	if (unlikely(spi_dv_pending(sdev->sdev_target))) {
		kfree(wqw);
		return;
	}
	/* Set pending early (dv_device doesn't check it, only sets it) */
	spi_dv_pending(sdev->sdev_target) = 1;
	if (unlikely(scsi_device_get(sdev))) {
		kfree(wqw);
		spi_dv_pending(sdev->sdev_target) = 0;
		return;
	}

	INIT_WORK(&wqw->work, spi_dv_device_work_wrapper, wqw);
	wqw->sdev = sdev;

	schedule_work(&wqw->work);
}
EXPORT_SYMBOL(spi_schedule_dv_device);

#define SETUP_ATTRIBUTE(field)						\
	i->private_attrs[count] = class_device_attr_##field;		\
	if (!i->f->set_##field) {					\
		i->private_attrs[count].attr.mode = S_IRUGO;		\
		i->private_attrs[count].store = NULL;			\
	}								\
	i->attrs[count] = &i->private_attrs[count];			\
	if (i->f->show_##field)						\
		count++

#define SETUP_HOST_ATTRIBUTE(field)					\
	i->private_host_attrs[count] = class_device_attr_##field;	\
	if (!i->f->set_##field) {					\
		i->private_host_attrs[count].attr.mode = S_IRUGO;	\
		i->private_host_attrs[count].store = NULL;		\
	}								\
	i->host_attrs[count] = &i->private_host_attrs[count];		\
	count++

static int spi_device_match(struct attribute_container *cont,
			    struct device *dev)
{
	struct scsi_device *sdev;
	struct Scsi_Host *shost;

	if (!scsi_is_sdev_device(dev))
		return 0;

	sdev = to_scsi_device(dev);
	shost = sdev->host;
	if (!shost->transportt  || shost->transportt->host_attrs.class
	    != &spi_host_class.class)
		return 0;
	/* Note: this class has no device attributes, so it has
	 * no per-HBA allocation and thus we don't need to distinguish
	 * the attribute containers for the device */
	return 1;
}

static int spi_target_match(struct attribute_container *cont,
			    struct device *dev)
{
	struct Scsi_Host *shost;
	struct spi_internal *i;

	if (!scsi_is_target_device(dev))
		return 0;

	shost = dev_to_shost(dev->parent);
	if (!shost->transportt  || shost->transportt->host_attrs.class
	    != &spi_host_class.class)
		return 0;

	i = to_spi_internal(shost->transportt);
	
	return &i->t.target_attrs == cont;
}

static DECLARE_TRANSPORT_CLASS(spi_transport_class,
			       "spi_transport",
			       spi_setup_transport_attrs,
			       NULL,
			       NULL);

static DECLARE_ANON_TRANSPORT_CLASS(spi_device_class,
				    spi_device_match,
				    spi_device_configure);

struct scsi_transport_template *
spi_attach_transport(struct spi_function_template *ft)
{
	struct spi_internal *i = kmalloc(sizeof(struct spi_internal),
					 GFP_KERNEL);
	int count = 0;
	if (unlikely(!i))
		return NULL;

	memset(i, 0, sizeof(struct spi_internal));


	i->t.target_attrs.class = &spi_transport_class.class;
	i->t.target_attrs.attrs = &i->attrs[0];
	i->t.target_attrs.match = spi_target_match;
	attribute_container_register(&i->t.target_attrs);
	i->t.target_size = sizeof(struct spi_transport_attrs);
	i->t.host_attrs.class = &spi_host_class.class;
	i->t.host_attrs.attrs = &i->host_attrs[0];
	i->t.host_attrs.match = spi_host_match;
	attribute_container_register(&i->t.host_attrs);
	i->t.host_size = sizeof(struct spi_host_attrs);
	i->f = ft;

	SETUP_ATTRIBUTE(period);
	SETUP_ATTRIBUTE(offset);
	SETUP_ATTRIBUTE(width);
	SETUP_ATTRIBUTE(iu);
	SETUP_ATTRIBUTE(dt);
	SETUP_ATTRIBUTE(qas);
	SETUP_ATTRIBUTE(wr_flow);
	SETUP_ATTRIBUTE(rd_strm);
	SETUP_ATTRIBUTE(rti);
	SETUP_ATTRIBUTE(pcomp_en);

	/* if you add an attribute but forget to increase SPI_NUM_ATTRS
	 * this bug will trigger */
	BUG_ON(count > SPI_NUM_ATTRS);

	i->attrs[count++] = &class_device_attr_revalidate;

	i->attrs[count] = NULL;

	count = 0;
	SETUP_HOST_ATTRIBUTE(signalling);

	BUG_ON(count > SPI_HOST_ATTRS);

	i->host_attrs[count] = NULL;

	return &i->t;
}
EXPORT_SYMBOL(spi_attach_transport);

void spi_release_transport(struct scsi_transport_template *t)
{
	struct spi_internal *i = to_spi_internal(t);

	attribute_container_unregister(&i->t.target_attrs);
	attribute_container_unregister(&i->t.host_attrs);

	kfree(i);
}
EXPORT_SYMBOL(spi_release_transport);

static __init int spi_transport_init(void)
{
	int error = transport_class_register(&spi_transport_class);
	if (error)
		return error;
	error = anon_transport_class_register(&spi_device_class);
	return transport_class_register(&spi_host_class);
}

static void __exit spi_transport_exit(void)
{
	transport_class_unregister(&spi_transport_class);
	anon_transport_class_unregister(&spi_device_class);
	transport_class_unregister(&spi_host_class);
}

MODULE_AUTHOR("Martin Hicks");
MODULE_DESCRIPTION("SPI Transport Attributes");
MODULE_LICENSE("GPL");

module_init(spi_transport_init);
module_exit(spi_transport_exit);
