/*
 * Copyright (C) 2001 Jens Axboe <axboe@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public Licens
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-
 *
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/cdrom.h>
#include <linux/slab.h>
#include <linux/bio.h>
#include <linux/times.h>
#include <asm/uaccess.h>

#include <scsi/scsi.h>
#include <scsi/scsi_ioctl.h>


/* Command group 3 is reserved and should never be used.  */
const unsigned char scsi_command_size[8] =
{
	6, 10, 10, 12,
	16, 12, 10, 10
};

#define BLK_DEFAULT_TIMEOUT	(60 * HZ)

/* defined in ../scsi/scsi.h  ... should it be included? */
#ifndef SCSI_SENSE_BUFFERSIZE
#define SCSI_SENSE_BUFFERSIZE 64
#endif

static int blk_do_rq(request_queue_t *q, struct block_device *bdev, 
		     struct request *rq)
{
	char sense[SCSI_SENSE_BUFFERSIZE];
	DECLARE_COMPLETION(wait);
	int err = 0;

	rq->rq_disk = bdev->bd_disk;

	/*
	 * we need an extra reference to the request, so we can look at
	 * it after io completion
	 */
	rq->ref_count++;

	if (!rq->sense) {
		memset(sense, 0, sizeof(sense));
		rq->sense = sense;
		rq->sense_len = 0;
	}

	rq->flags |= REQ_NOMERGE;
	rq->waiting = &wait;
	elv_add_request(q, rq, ELEVATOR_INSERT_BACK, 1);
	generic_unplug_device(q);
	wait_for_completion(&wait);

	if (rq->errors)
		err = -EIO;

	return err;
}

#include <scsi/sg.h>

static int sg_get_version(int *p)
{
	static int sg_version_num = 30527;
	return put_user(sg_version_num, p);
}

static int scsi_get_idlun(request_queue_t *q, int *p)
{
	return put_user(0, p);
}

static int scsi_get_bus(request_queue_t *q, int *p)
{
	return put_user(0, p);
}

static int sg_get_timeout(request_queue_t *q)
{
	return q->sg_timeout / (HZ / USER_HZ);
}

static int sg_set_timeout(request_queue_t *q, int *p)
{
	int timeout, err = get_user(timeout, p);

	if (!err)
		q->sg_timeout = timeout * (HZ / USER_HZ);

	return err;
}

static int sg_get_reserved_size(request_queue_t *q, int *p)
{
	return put_user(q->sg_reserved_size, p);
}

static int sg_set_reserved_size(request_queue_t *q, int *p)
{
	int size, err = get_user(size, p);

	if (err)
		return err;

	if (size < 0)
		return -EINVAL;
	if (size > (q->max_sectors << 9))
		return -EINVAL;

	q->sg_reserved_size = size;
	return 0;
}

/*
 * will always return that we are ATAPI even for a real SCSI drive, I'm not
 * so sure this is worth doing anything about (why would you care??)
 */
static int sg_emulated_host(request_queue_t *q, int *p)
{
	return put_user(1, p);
}

static int sg_io(request_queue_t *q, struct block_device *bdev,
		 struct sg_io_hdr *hdr)
{
	unsigned long start_time;
	int reading, writing;
	struct request *rq;
	struct bio *bio;
	char sense[SCSI_SENSE_BUFFERSIZE];
	void *buffer;

	if (hdr->interface_id != 'S')
		return -EINVAL;
	if (hdr->cmd_len > sizeof(rq->cmd))
		return -EINVAL;

	/*
	 * we'll do that later
	 */
	if (hdr->iovec_count)
		return -EOPNOTSUPP;

	if (hdr->dxfer_len > (q->max_sectors << 9))
		return -EIO;

	reading = writing = 0;
	buffer = NULL;
	bio = NULL;
	if (hdr->dxfer_len) {
		unsigned int bytes = (hdr->dxfer_len + 511) & ~511;

		switch (hdr->dxfer_direction) {
		default:
			return -EINVAL;
		case SG_DXFER_TO_FROM_DEV:
			reading = 1;
			/* fall through */
		case SG_DXFER_TO_DEV:
			writing = 1;
			break;
		case SG_DXFER_FROM_DEV:
			reading = 1;
			break;
		}

		/*
		 * first try to map it into a bio. reading from device will
		 * be a write to vm.
		 */
		bio = bio_map_user(bdev, (unsigned long) hdr->dxferp,
				   hdr->dxfer_len, reading);

		/*
		 * if bio setup failed, fall back to slow approach
		 */
		if (!bio) {
			buffer = kmalloc(bytes, q->bounce_gfp | GFP_USER);
			if (!buffer)
				return -ENOMEM;

			if (writing) {
				if (copy_from_user(buffer, hdr->dxferp,
						   hdr->dxfer_len))
					goto out_buffer;
			} else
				memset(buffer, 0, hdr->dxfer_len);
		}
	}

	rq = blk_get_request(q, writing ? WRITE : READ, __GFP_WAIT);

	/*
	 * fill in request structure
	 */
	rq->cmd_len = hdr->cmd_len;
	memcpy(rq->cmd, hdr->cmdp, hdr->cmd_len);
	if (sizeof(rq->cmd) != hdr->cmd_len)
		memset(rq->cmd + hdr->cmd_len, 0, sizeof(rq->cmd) - hdr->cmd_len);

	memset(sense, 0, sizeof(sense));
	rq->sense = sense;
	rq->sense_len = 0;

	rq->flags |= REQ_BLOCK_PC;

	rq->bio = rq->biotail = NULL;

	if (bio)
		blk_rq_bio_prep(q, rq, bio);

	rq->data = buffer;
	rq->data_len = hdr->dxfer_len;

	rq->timeout = (hdr->timeout * HZ) / 1000;
	if (!rq->timeout)
		rq->timeout = q->sg_timeout;
	if (!rq->timeout)
		rq->timeout = BLK_DEFAULT_TIMEOUT;

	start_time = jiffies;

	/* ignore return value. All information is passed back to caller
	 * (if he doesn't check that is his problem).
	 * N.B. a non-zero SCSI status is _not_ necessarily an error.
	 */
	blk_do_rq(q, bdev, rq);

	if (bio)
		bio_unmap_user(bio, reading);

	/* write to all output members */
	hdr->status = rq->errors;	
	hdr->masked_status = (hdr->status >> 1) & 0x1f;
	hdr->msg_status = 0;
	hdr->host_status = 0;
	hdr->driver_status = 0;
	hdr->info = 0;
	if (hdr->masked_status || hdr->host_status || hdr->driver_status)
		hdr->info |= SG_INFO_CHECK;
	hdr->resid = rq->data_len;
	hdr->duration = ((jiffies - start_time) * 1000) / HZ;
	hdr->sb_len_wr = 0;

	if (rq->sense_len && hdr->sbp) {
		int len = min((unsigned int) hdr->mx_sb_len, rq->sense_len);

		if (!copy_to_user(hdr->sbp, rq->sense, len))
			hdr->sb_len_wr = len;
	}

	blk_put_request(rq);

	if (buffer) {
		if (reading)
			if (copy_to_user(hdr->dxferp, buffer, hdr->dxfer_len))
				goto out_buffer;

		kfree(buffer);
	}

	/* may not have succeeded, but output values written to control
	 * structure (struct sg_io_hdr).  */
	return 0;
out_buffer:
	kfree(buffer);
	return -EFAULT;
}

#define FORMAT_UNIT_TIMEOUT		(2 * 60 * 60 * HZ)
#define START_STOP_TIMEOUT		(60 * HZ)
#define MOVE_MEDIUM_TIMEOUT		(5 * 60 * HZ)
#define READ_ELEMENT_STATUS_TIMEOUT	(5 * 60 * HZ)
#define READ_DEFECT_DATA_TIMEOUT	(60 * HZ )
#define OMAX_SB_LEN 16          /* For backward compatibility */

static int sg_scsi_ioctl(request_queue_t *q, struct block_device *bdev,
			 Scsi_Ioctl_Command *sic)
{
	struct request *rq;
	int err, in_len, out_len, bytes, opcode, cmdlen;
	char *buffer = NULL, sense[SCSI_SENSE_BUFFERSIZE];

	/*
	 * get in an out lengths, verify they don't exceed a page worth of data
	 */
	if (get_user(in_len, &sic->inlen))
		return -EFAULT;
	if (get_user(out_len, &sic->outlen))
		return -EFAULT;
	if (in_len > PAGE_SIZE || out_len > PAGE_SIZE)
		return -EINVAL;
	if (get_user(opcode, sic->data))
		return -EFAULT;

	bytes = max(in_len, out_len);
	if (bytes) {
		buffer = kmalloc(bytes, q->bounce_gfp | GFP_USER);
		if (!buffer)
			return -ENOMEM;

		memset(buffer, 0, bytes);
	}

	rq = blk_get_request(q, in_len ? WRITE : READ, __GFP_WAIT);

	cmdlen = COMMAND_SIZE(opcode);

	/*
	 * get command and data to send to device, if any
	 */
	err = -EFAULT;
	rq->cmd_len = cmdlen;
	if (copy_from_user(rq->cmd, sic->data, cmdlen))
		goto error;

	if (copy_from_user(buffer, sic->data + cmdlen, in_len))
		goto error;

	switch (opcode) {
		case SEND_DIAGNOSTIC:
		case FORMAT_UNIT:
			rq->timeout = FORMAT_UNIT_TIMEOUT;
			break;
		case START_STOP:
			rq->timeout = START_STOP_TIMEOUT;
			break;
		case MOVE_MEDIUM:
			rq->timeout = MOVE_MEDIUM_TIMEOUT;
			break;
		case READ_ELEMENT_STATUS:
			rq->timeout = READ_ELEMENT_STATUS_TIMEOUT;
			break;
		case READ_DEFECT_DATA:
			rq->timeout = READ_DEFECT_DATA_TIMEOUT;
			break;
		default:
			rq->timeout = BLK_DEFAULT_TIMEOUT;
			break;
	}

	memset(sense, 0, sizeof(sense));
	rq->sense = sense;
	rq->sense_len = 0;

	rq->data = buffer;
	rq->data_len = bytes;
	rq->flags |= REQ_BLOCK_PC;

	blk_do_rq(q, bdev, rq);
	err = rq->errors & 0xff;	/* only 8 bit SCSI status */
	if (err) {
		if (rq->sense_len && rq->sense) {
			bytes = (OMAX_SB_LEN > rq->sense_len) ?
				rq->sense_len : OMAX_SB_LEN;
			if (copy_to_user(sic->data, rq->sense, bytes))
				err = -EFAULT;
		}
	} else {
		if (copy_to_user(sic->data, buffer, out_len))
			err = -EFAULT;
	}
	
error:
	kfree(buffer);
	blk_put_request(rq);
	return err;
}

int scsi_cmd_ioctl(struct block_device *bdev, unsigned int cmd, unsigned long arg)
{
	request_queue_t *q;
	struct request *rq;
	int close = 0, err;

	q = bdev_get_queue(bdev);
	if (!q)
		return -ENXIO;

	if (blk_get_queue(q))
		return -ENXIO;

	switch (cmd) {
		/*
		 * new sgv3 interface
		 */
		case SG_GET_VERSION_NUM:
			err = sg_get_version((int *) arg);
			break;
		case SCSI_IOCTL_GET_IDLUN:
			err = scsi_get_idlun(q, (int *) arg);
			break;
		case SCSI_IOCTL_GET_BUS_NUMBER:
			err = scsi_get_bus(q, (int *) arg);
			break;
		case SG_SET_TIMEOUT:
			err = sg_set_timeout(q, (int *) arg);
			break;
		case SG_GET_TIMEOUT:
			err = sg_get_timeout(q);
			break;
		case SG_GET_RESERVED_SIZE:
			err = sg_get_reserved_size(q, (int *) arg);
			break;
		case SG_SET_RESERVED_SIZE:
			err = sg_set_reserved_size(q, (int *) arg);
			break;
		case SG_EMULATED_HOST:
			err = sg_emulated_host(q, (int *) arg);
			break;
		case SG_IO: {
			struct sg_io_hdr hdr;
			unsigned char cdb[BLK_MAX_CDB], *old_cdb;

			err = -EFAULT;
			if (copy_from_user(&hdr, (struct sg_io_hdr *) arg, sizeof(hdr)))
				break;
			err = -EINVAL;
			if (hdr.cmd_len > sizeof(rq->cmd))
				break;
			err = -EFAULT;
			if (copy_from_user(cdb, hdr.cmdp, hdr.cmd_len))
				break;

			old_cdb = hdr.cmdp;
			hdr.cmdp = cdb;
			err = sg_io(q, bdev, &hdr);

			hdr.cmdp = old_cdb;
			if (copy_to_user((struct sg_io_hdr *) arg, &hdr, sizeof(hdr)))
				err = -EFAULT;
			break;
		}
		case CDROM_SEND_PACKET: {
			struct cdrom_generic_command cgc;
			struct sg_io_hdr hdr;

			if (copy_from_user(&cgc, (struct cdrom_generic_command *) arg, sizeof(cgc))) {
				err = -EFAULT;
				break;
			}
			cgc.timeout = clock_t_to_jiffies(cgc.timeout);
			memset(&hdr, 0, sizeof(hdr));
			hdr.interface_id = 'S';
			hdr.cmd_len = sizeof(cgc.cmd);
			hdr.dxfer_len = cgc.buflen;
			err = 0;
			switch (cgc.data_direction) {
				case CGC_DATA_UNKNOWN:
					hdr.dxfer_direction = SG_DXFER_UNKNOWN;
					break;
				case CGC_DATA_WRITE:
					hdr.dxfer_direction = SG_DXFER_TO_DEV;
					break;
				case CGC_DATA_READ:
					hdr.dxfer_direction = SG_DXFER_FROM_DEV;
					break;
				case CGC_DATA_NONE:
					hdr.dxfer_direction = SG_DXFER_NONE;
					break;
				default:
					err = -EINVAL;
			}
			if (err)
				break;

			hdr.dxferp = cgc.buffer;
			hdr.sbp = (char *) cgc.sense;
			if (hdr.sbp)
				hdr.mx_sb_len = sizeof(struct request_sense);
			hdr.timeout = cgc.timeout;
			hdr.cmdp = cgc.cmd;
			hdr.cmd_len = sizeof(cgc.cmd);
			err = sg_io(q, bdev, &hdr);

			if (hdr.status)
				err = -EIO;

			cgc.stat = err;
			cgc.buflen = hdr.resid;
			if (copy_to_user((struct cdrom_generic_command *) arg, &cgc, sizeof(cgc)))
				err = -EFAULT;

			break;
		}

		/*
		 * old junk scsi send command ioctl
		 */
		case SCSI_IOCTL_SEND_COMMAND:
			err = -EINVAL;
			if (!arg)
				break;

			err = sg_scsi_ioctl(q, bdev, (Scsi_Ioctl_Command *)arg);
			break;
		case CDROMCLOSETRAY:
			close = 1;
		case CDROMEJECT:
			rq = blk_get_request(q, WRITE, __GFP_WAIT);
			rq->flags |= REQ_BLOCK_PC;
			rq->data = NULL;
			rq->data_len = 0;
			rq->timeout = BLK_DEFAULT_TIMEOUT;
			memset(rq->cmd, 0, sizeof(rq->cmd));
			rq->cmd[0] = GPCMD_START_STOP_UNIT;
			rq->cmd[4] = 0x02 + (close != 0);
			rq->cmd_len = 6;
			err = blk_do_rq(q, bdev, rq);
			blk_put_request(rq);
			break;
		default:
			err = -ENOTTY;
	}

	blk_put_queue(q);
	return err;
}

EXPORT_SYMBOL(scsi_cmd_ioctl);
EXPORT_SYMBOL(scsi_command_size);
