/*
 *  c 2001 PPC 64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 * /dev/nvram driver for PPC64
 *
 * This perhaps should live in drivers/char
 */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/fcntl.h>
#include <linux/nvram.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include <asm/nvram.h>
#include <asm/rtas.h>
#include <asm/prom.h>
#include <asm/machdep.h>

#define DEBUG_NVRAM

static int nvram_scan_partitions(void);
static int nvram_setup_partition(void);
static int nvram_create_os_partition(void);
static int nvram_remove_os_partition(void);
static unsigned char nvram_checksum(struct nvram_header *p);
static int nvram_write_header(struct nvram_partition * part);

static unsigned int nvram_size;
static unsigned int nvram_fetch, nvram_store;
static char nvram_buf[NVRW_CNT];	/* assume this is in the first 4GB */
static struct nvram_partition * nvram_part;
static long nvram_error_log_index = -1;
static long nvram_error_log_size = 0;
static spinlock_t nvram_lock = SPIN_LOCK_UNLOCKED;

volatile int no_more_logging = 1; /* Until we initialize everything,
				   * make sure we don't try logging
				   * anything */

extern volatile int error_log_cnt;

struct err_log_info {
	int error_type;
	unsigned int seq_num;
};

static loff_t dev_nvram_llseek(struct file *file, loff_t offset, int origin)
{
	switch (origin) {
	case 1:
		offset += file->f_pos;
		break;
	case 2:
		offset += nvram_size;
		break;
	}
	if (offset < 0)
		return -EINVAL;
	file->f_pos = offset;
	return file->f_pos;
}


static ssize_t dev_nvram_read(struct file *file, char *buf,
			  size_t count, loff_t *ppos)
{
	ssize_t len;
	char *tmp_buffer;

	if (verify_area(VERIFY_WRITE, buf, count))
		return -EFAULT;
	if (*ppos >= nvram_size)
		return 0;
	if (count > nvram_size) 
		count = nvram_size;

	tmp_buffer = (char *) kmalloc(count, GFP_KERNEL);
	if (!tmp_buffer) {
		printk(KERN_ERR "dev_read_nvram: kmalloc failed\n");
		return -ENOMEM;
	}

	len = ppc_md.nvram_read(tmp_buffer, count, ppos);
	if ((long)len <= 0) {
		kfree(tmp_buffer);
		return len;
	}

	if (copy_to_user(buf, tmp_buffer, len)) {
		kfree(tmp_buffer);
		return -EFAULT;
	}

	kfree(tmp_buffer);
	return len;

}

static ssize_t dev_nvram_write(struct file *file, const char *buf,
			   size_t count, loff_t *ppos)
{
	ssize_t len;
	char * tmp_buffer;

	if (verify_area(VERIFY_READ, buf, count))
		return -EFAULT;
	if (*ppos >= nvram_size)
		return 0;
	if (count > nvram_size)
		count = nvram_size;

	tmp_buffer = (char *) kmalloc(count, GFP_KERNEL);
	if (!tmp_buffer) {
		printk(KERN_ERR "dev_nvram_write: kmalloc failed\n");
		return -ENOMEM;
	}
	
	if (copy_from_user(tmp_buffer, buf, count)) {
		kfree(tmp_buffer);
		return -EFAULT;
	}

	len = ppc_md.nvram_write(tmp_buffer, count, ppos);
	if ((long)len <= 0) {
		kfree(tmp_buffer);
		return len;
	}

	kfree(tmp_buffer);
	return len;
}

static int dev_nvram_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	return -EINVAL;
}

struct file_operations nvram_fops = {
	.owner =	THIS_MODULE,
	.llseek =	dev_nvram_llseek,
	.read =		dev_nvram_read,
	.write =	dev_nvram_write,
	.ioctl =	dev_nvram_ioctl,
};

static struct miscdevice nvram_dev = {
	NVRAM_MINOR,
	"nvram",
	&nvram_fops
};

ssize_t pSeries_nvram_read(char *buf, size_t count, loff_t *index)
{
	unsigned int i;
	unsigned long len, done;
	unsigned long flags;
	char *p = buf;

	if (*index >= nvram_size)
		return 0;

	i = *index;
	if (i + count > nvram_size)
		count = nvram_size - i;

	spin_lock_irqsave(&nvram_lock, flags);

	for (; count != 0; count -= len) {
		len = count;
		if (len > NVRW_CNT)
			len = NVRW_CNT;
		
		if ((rtas_call(nvram_fetch, 3, 2, &done, i, __pa(nvram_buf),
			       len) != 0) || len != done) {
			spin_unlock_irqrestore(&nvram_lock, flags);
			return -EIO;
		}
		
		memcpy(p, nvram_buf, len);

		p += len;
		i += len;
	}

	spin_unlock_irqrestore(&nvram_lock, flags);
	
	*index = i;
	return p - buf;
}

ssize_t pSeries_nvram_write(char *buf, size_t count, loff_t *index)
{
	unsigned int i;
	unsigned long len, done;
	unsigned long flags;
	const char *p = buf;

	if (*index >= nvram_size)
		return 0;

	i = *index;
	if (i + count > nvram_size)
		count = nvram_size - i;

	spin_lock_irqsave(&nvram_lock, flags);

	for (; count != 0; count -= len) {
		len = count;
		if (len > NVRW_CNT)
			len = NVRW_CNT;

		memcpy(nvram_buf, p, len);

		if ((rtas_call(nvram_store, 3, 2, &done, i, __pa(nvram_buf),
			       len) != 0) || len != done) {
			spin_unlock_irqrestore(&nvram_lock, flags);
			return -EIO;
		}
		
		p += len;
		i += len;
	}
	spin_unlock_irqrestore(&nvram_lock, flags);
	
	*index = i;
	return p - buf;
}
 
int __init nvram_init(void)
{
	struct device_node *nvram;
	unsigned int *nbytes_p, proplen;
	int error;
	int rc;
	
	if ((nvram = of_find_node_by_type(NULL, "nvram")) != NULL) {
		nbytes_p = (unsigned int *)get_property(nvram, "#bytes", &proplen);
		if (nbytes_p && proplen == sizeof(unsigned int)) {
			nvram_size = *nbytes_p;
		} else {
			return -EIO;
		}
	}
	nvram_fetch = rtas_token("nvram-fetch");
	nvram_store = rtas_token("nvram-store");
	printk(KERN_INFO "PPC64 nvram contains %d bytes\n", nvram_size);
	of_node_put(nvram);

  	rc = misc_register(&nvram_dev);
  
  	/* If we don't know how big NVRAM is then we shouldn't touch
  	   the nvram partitions */
  	if (nvram == NULL) {
  		return rc;
  	}
  	
  	/* initialize our anchor for the nvram partition list */
  	nvram_part = (struct nvram_partition *) kmalloc(sizeof(struct nvram_partition), GFP_KERNEL);
  	if (!nvram_part) {
  		printk(KERN_ERR "nvram_init: Failed kmalloc\n");
  		return -ENOMEM;
  	}
  	INIT_LIST_HEAD(&nvram_part->partition);
  
  	/* Get all the NVRAM partitions */
  	error = nvram_scan_partitions();
  	if (error) {
  		printk(KERN_ERR "nvram_init: Failed nvram_scan_partitions\n");
  		return error;
  	}
  		
  	if(nvram_setup_partition()) 
  		printk(KERN_WARNING "nvram_init: Could not find nvram partition"
  		       " for nvram buffered error logging.\n");
  
#ifdef DEBUG_NVRAM
	nvram_print_partitions("NVRAM Partitions");
#endif

  	return rc;
}

void __exit nvram_cleanup(void)
{
        misc_deregister( &nvram_dev );
}

static int nvram_scan_partitions(void)
{
	loff_t cur_index = 0;
	struct nvram_header phead;
	struct nvram_partition * tmp_part;
	unsigned char c_sum;
	char * header;
	long size;
	
	header = (char *) kmalloc(NVRAM_HEADER_LEN, GFP_KERNEL);
	if (!header) {
		printk(KERN_ERR "nvram_scan_partitions: Failed kmalloc\n");
		return -ENOMEM;
	}

	while (cur_index < nvram_size) {

		size = ppc_md.nvram_read(header, NVRAM_HEADER_LEN, &cur_index);
		if (size != NVRAM_HEADER_LEN) {
			printk(KERN_ERR "nvram_scan_partitions: Error parsing "
			       "nvram partitions\n");
			kfree(header);
			return size;
		}

		cur_index -= NVRAM_HEADER_LEN; /* nvram_read will advance us */

		memcpy(&phead, header, NVRAM_HEADER_LEN);

		c_sum = nvram_checksum(&phead);
		if (c_sum != phead.checksum)
			printk(KERN_WARNING "WARNING: nvram partition checksum "
			       "was %02x, should be %02x!\n", phead.checksum, c_sum);
		
		tmp_part = (struct nvram_partition *)
			kmalloc(sizeof(struct nvram_partition), GFP_KERNEL);
		if (!tmp_part) {
			printk(KERN_ERR "nvram_scan_partitions: kmalloc failed\n");
			kfree(header);
			return -ENOMEM;
		}
		
		memcpy(&tmp_part->header, &phead, NVRAM_HEADER_LEN);
		tmp_part->index = cur_index;
		list_add_tail(&tmp_part->partition, &nvram_part->partition);
		
		cur_index += phead.length * NVRAM_BLOCK_LEN;
	}

	kfree(header);
	return 0;
}

/* nvram_setup_partition
 *
 * This will setup the partition we need for buffering the
 * error logs and cleanup partitions if needed.
 *
 * The general strategy is the following:
 * 1.) If there is ppc64,linux partition large enough then use it.
 * 2.) If there is not a ppc64,linux partition large enough, search
 * for a free partition that is large enough.
 * 3.) If there is not a free partition large enough remove 
 * _all_ OS partitions and consolidate the space.
 * 4.) Will first try getting a chunk that will satisfy the maximum
 * error log size (NVRAM_MAX_REQ).
 * 5.) If the max chunk cannot be allocated then try finding a chunk
 * that will satisfy the minum needed (NVRAM_MIN_REQ).
 */
static int nvram_setup_partition(void)
{
	struct list_head * p;
	struct nvram_partition * part;
	int rc;

	/* see if we have an OS partition that meets our needs.
	   will try getting the max we need.  If not we'll delete
	   partitions and try again. */
	list_for_each(p, &nvram_part->partition) {
		part = list_entry(p, struct nvram_partition, partition);
		if (part->header.signature != NVRAM_SIG_OS)
			continue;

		if (strcmp(part->header.name, "ppc64,linux"))
			continue;

		if (part->header.length >= NVRAM_MIN_REQ) {
			/* found our partition */
			nvram_error_log_index = part->index + NVRAM_HEADER_LEN;
			nvram_error_log_size = ((part->header.length - 1) *
						NVRAM_BLOCK_LEN) - sizeof(struct err_log_info);
			return 0;
		}
	}
	
	/* try creating a partition with the free space we have */
	rc = nvram_create_os_partition();
	if (!rc) {
		return 0;
	}
		
	/* need to free up some space */
	rc = nvram_remove_os_partition();
	if (rc) {
		return rc;
	}
	
	/* create a partition in this new space */
	rc = nvram_create_os_partition();
	if (rc) {
		printk(KERN_ERR "nvram_create_os_partition: Could not find a "
		       "NVRAM partition large enough\n");
		return rc;
	}
	
	return 0;
}

static int nvram_remove_os_partition(void)
{
	struct list_head *i;
	struct list_head *j;
	struct nvram_partition * part;
	struct nvram_partition * cur_part;
	int rc;

	list_for_each(i, &nvram_part->partition) {
		part = list_entry(i, struct nvram_partition, partition);
		if (part->header.signature != NVRAM_SIG_OS)
			continue;
		
		/* Make os partition a free partition */
		part->header.signature = NVRAM_SIG_FREE;
		sprintf(part->header.name, "wwwwwwwwwwww");
		part->header.checksum = nvram_checksum(&part->header);

		/* Merge contiguous free partitions backwards */
		list_for_each_prev(j, &part->partition) {
			cur_part = list_entry(j, struct nvram_partition, partition);
			if (cur_part == nvram_part || cur_part->header.signature != NVRAM_SIG_FREE) {
				break;
			}
			
			part->header.length += cur_part->header.length;
			part->header.checksum = nvram_checksum(&part->header);
			part->index = cur_part->index;

			list_del(&cur_part->partition);
			kfree(cur_part);
			j = &part->partition; /* fixup our loop */
		}
		
		/* Merge contiguous free partitions forwards */
		list_for_each(j, &part->partition) {
			cur_part = list_entry(j, struct nvram_partition, partition);
			if (cur_part == nvram_part || cur_part->header.signature != NVRAM_SIG_FREE) {
				break;
			}

			part->header.length += cur_part->header.length;
			part->header.checksum = nvram_checksum(&part->header);

			list_del(&cur_part->partition);
			kfree(cur_part);
			j = &part->partition; /* fixup our loop */
		}
		
		rc = nvram_write_header(part);
		if (rc <= 0) {
			printk(KERN_ERR "nvram_remove_os_partition: nvram_write failed (%d)\n", rc);
			return rc;
		}

	}
	
	return 0;
}

/* nvram_create_os_partition
 *
 * Create a OS linux partition to buffer error logs.
 * Will create a partition starting at the first free
 * space found if space has enough room.
 */
static int nvram_create_os_partition(void)
{
	struct list_head * p;
	struct nvram_partition * part;
	struct nvram_partition * new_part = NULL;
	struct nvram_partition * free_part;
	int seq_init[2] = { 0, 0 };
	loff_t tmp_index;
	long size = 0;
	int rc;
	
	/* Find a free partition that will give us the maximum needed size 
	   If can't find one that will give us the minimum size needed */
	list_for_each(p, &nvram_part->partition) {
		part = list_entry(p, struct nvram_partition, partition);
		if (part->header.signature != NVRAM_SIG_FREE)
			continue;

		if (part->header.length >= NVRAM_MAX_REQ) {
			size = NVRAM_MAX_REQ;
			free_part = part;
			break;
		}
		if (!size && part->header.length >= NVRAM_MIN_REQ) {
			size = NVRAM_MIN_REQ;
			free_part = part;
		}
	}
	if (!size) {
		return -ENOSPC;
	}
	
	/* Create our OS partition */
	new_part = (struct nvram_partition *)
		kmalloc(sizeof(struct nvram_partition), GFP_KERNEL);
	if (!new_part) {
		printk(KERN_ERR "nvram_create_os_partition: kmalloc failed\n");
		return -ENOMEM;
	}

	new_part->index = free_part->index;
	new_part->header.signature = NVRAM_SIG_OS;
	new_part->header.length = size;
	sprintf(new_part->header.name, "ppc64,linux");
	new_part->header.checksum = nvram_checksum(&new_part->header);

	rc = nvram_write_header(new_part);
	if (rc <= 0) {
		printk(KERN_ERR "nvram_create_os_partition: nvram_write_header \
				failed (%d)\n", rc);
		return rc;
	}

	/* make sure and initialize to zero the sequence number and the error
	   type logged */
	tmp_index = new_part->index + NVRAM_HEADER_LEN;
	rc = ppc_md.nvram_write((char *)&seq_init, sizeof(seq_init), &tmp_index);
	if (rc <= 0) {
		printk(KERN_ERR "nvram_create_os_partition: nvram_write failed (%d)\n", rc);
		return rc;
	}
	
	nvram_error_log_index = new_part->index + NVRAM_HEADER_LEN;
	nvram_error_log_size = ((part->header.length - 1) *
				NVRAM_BLOCK_LEN) - sizeof(struct err_log_info);
	
	list_add_tail(&new_part->partition, &free_part->partition);

	if (free_part->header.length <= size) {
		list_del(&free_part->partition);
		kfree(free_part);
		return 0;
	} 

	/* Adjust the partition we stole the space from */
	free_part->index += size * NVRAM_BLOCK_LEN;
	free_part->header.length -= size;
	free_part->header.checksum = nvram_checksum(&free_part->header);
	
	rc = nvram_write_header(free_part);
	if (rc <= 0) {
		printk(KERN_ERR "nvram_create_os_partition: nvram_write_header "
		       "failed (%d)\n", rc);
		return rc;
	}

	return 0;
}


void nvram_print_partitions(char * label)
{
	struct list_head * p;
	struct nvram_partition * tmp_part;
	
	printk(KERN_WARNING "--------%s---------\n", label);
	printk(KERN_WARNING "indx\t\tsig\tchks\tlen\tname\n");
	list_for_each(p, &nvram_part->partition) {
		tmp_part = list_entry(p, struct nvram_partition, partition);
		printk(KERN_WARNING "%d    \t%02x\t%02x\t%d\t%s\n",
		       tmp_part->index, tmp_part->header.signature,
		       tmp_part->header.checksum, tmp_part->header.length,
		       tmp_part->header.name);
	}
}

/* nvram_write_error_log
 *
 * We need to buffer the error logs into nvram to ensure that we have
 * the failure information to decode.  If we have a severe error there
 * is no way to guarantee that the OS or the machine is in a state to
 * get back to user land and write the error to disk.  For example if
 * the SCSI device driver causes a Machine Check by writing to a bad
 * IO address, there is no way of guaranteeing that the device driver
 * is in any state that is would also be able to write the error data
 * captured to disk, thus we buffer it in NVRAM for analysis on the
 * next boot.
 *
 * In NVRAM the partition containing the error log buffer will looks like:
 * Header (in bytes):
 * +-----------+----------+--------+------------+------------------+
 * | signature | checksum | length | name       | data             |
 * |0          |1         |2      3|4         15|16        length-1|
 * +-----------+----------+--------+------------+------------------+
 *
 * The 'data' section would look like (in bytes):
 * +--------------+------------+-----------------------------------+
 * | event_logged | sequence # | error log                         |
 * |0            3|4          7|8            nvram_error_log_size-1|
 * +--------------+------------+-----------------------------------+
 *
 * event_logged: 0 if event has not been logged to syslog, 1 if it has
 * sequence #: The unique sequence # for each event. (until it wraps)
 * error log: The error log from event_scan
 */
int nvram_write_error_log(char * buff, int length, unsigned int err_type)
{
	int rc;
	loff_t tmp_index;
	struct err_log_info info;
	
	if (no_more_logging) {
		return -EPERM;
	}

	if (nvram_error_log_index == -1) {
		return -ESPIPE;
	}

	if (length > nvram_error_log_size) {
		length = nvram_error_log_size;
	}

	info.error_type = err_type;
	info.seq_num = error_log_cnt;

	tmp_index = nvram_error_log_index;

	rc = ppc_md.nvram_write((char *)&info, sizeof(struct err_log_info), &tmp_index);
	if (rc <= 0) {
		printk(KERN_ERR "nvram_write_error_log: Failed nvram_write (%d)\n", rc);
		return rc;
	}

	rc = ppc_md.nvram_write(buff, length, &tmp_index);
	if (rc <= 0) {
		printk(KERN_ERR "nvram_write_error_log: Failed nvram_write (%d)\n", rc);
		return rc;
	}
	
	return 0;
}

/* nvram_read_error_log
 *
 * Reads nvram for error log for at most 'length'
 */
int nvram_read_error_log(char * buff, int length, unsigned int * err_type)
{
	int rc;
	loff_t tmp_index;
	struct err_log_info info;
	
	if (nvram_error_log_index == -1)
		return -1;

	if (length > nvram_error_log_size)
		length = nvram_error_log_size;

	tmp_index = nvram_error_log_index;

	rc = ppc_md.nvram_read((char *)&info, sizeof(struct err_log_info), &tmp_index);
	if (rc <= 0) {
		printk(KERN_ERR "nvram_read_error_log: Failed nvram_read (%d)\n", rc);
		return rc;
	}

	rc = ppc_md.nvram_read(buff, length, &tmp_index);
	if (rc <= 0) {
		printk(KERN_ERR "nvram_read_error_log: Failed nvram_read (%d)\n", rc);
		return rc;
	}

	error_log_cnt = info.seq_num;
	*err_type = info.error_type;

	return 0;
}

/* This doesn't actually zero anything, but it sets the event_logged
 * word to tell that this event is safely in syslog.
 */
int nvram_clear_error_log()
{
	loff_t tmp_index;
	int clear_word = ERR_FLAG_ALREADY_LOGGED;
	int rc;

	tmp_index = nvram_error_log_index;
	
	rc = ppc_md.nvram_write((char *)&clear_word, sizeof(int), &tmp_index);
	if (rc <= 0) {
		printk(KERN_ERR "nvram_clear_error_log: Failed nvram_write (%d)\n", rc);
		return rc;
	}

	return 0;
}

static int nvram_write_header(struct nvram_partition * part)
{
	loff_t tmp_index;
	int rc;
	
	tmp_index = part->index;
	rc = ppc_md.nvram_write((char *)&part->header, NVRAM_HEADER_LEN, &tmp_index); 

	return rc;
}

static unsigned char nvram_checksum(struct nvram_header *p)
{
	unsigned int c_sum, c_sum2;
	unsigned short *sp = (unsigned short *)p->name; /* assume 6 shorts */
	c_sum = p->signature + p->length + sp[0] + sp[1] + sp[2] + sp[3] + sp[4] + sp[5];

	/* The sum may have spilled into the 3rd byte.  Fold it back. */
	c_sum = ((c_sum & 0xffff) + (c_sum >> 16)) & 0xffff;
	/* The sum cannot exceed 2 bytes.  Fold it into a checksum */
	c_sum2 = (c_sum >> 8) + (c_sum << 8);
	c_sum = ((c_sum + c_sum2) >> 8) & 0xff;
	return c_sum;
}

module_init(nvram_init);
module_exit(nvram_cleanup);
MODULE_LICENSE("GPL");
