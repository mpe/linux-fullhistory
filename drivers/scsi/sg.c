/*
 *  History:
 *  Started: Aug 9 by Lawrence Foard (entropy@world.std.com),
 *           to allow user process control of SCSI devices.
 *  Development Sponsored by Killy Corp. NY NY
 *
 * Original driver (sg.c):
 *        Copyright (C) 1992 Lawrence Foard
 * Version 2 and 3 extensions to driver:
 *        Copyright (C) 1998, 1999 Douglas Gilbert
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 */
 static char * sg_version_str = "Version: 3.1.10 (20000123)";
 static int sg_version_num = 30110; /* 2 digits for each component */
/*
 *  D. P. Gilbert (dgilbert@interlog.com, dougg@triode.net.au), notes:
 *      - scsi logging is available via SCSI_LOG_TIMEOUT macros. First
 *        the kernel/module needs to be built with CONFIG_SCSI_LOGGING
 *        (otherwise the macros compile to empty statements).
 *        Then before running the program to be debugged enter:
 *          # echo "scsi log timeout 7" > /proc/scsi/scsi
 *        This will send copious output to the console and the log which
 *        is usually /var/log/messages. To turn off debugging enter:
 *          # echo "scsi log timeout 0" > /proc/scsi/scsi
 *        The 'timeout' token was chosen because it is relatively unused.
 *        The token 'hlcomplete' should be used but that triggers too
 *        much output from the sd device driver. To dump the current
 *        state of the SCSI mid level data structures enter:
 *          # echo "scsi dump 1" > /proc/scsi/scsi
 *        To dump the state of sg's data structures use:
 *          # cat /proc/scsi/sg/debug
 *
 */
#include <linux/config.h>
#include <linux/module.h>

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/mtio.h>
#include <linux/ioctl.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/blk.h>
#include "scsi.h"
#include "hosts.h"
#include <scsi/scsi_ioctl.h>
#include <scsi/sg.h>

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
static int sg_proc_init(void);
static void sg_proc_cleanup(void);
#endif

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif /* LINUX_VERSION_CODE */

/* #define SG_ALLOW_DIO */
#ifdef SG_ALLOW_DIO
#include <linux/iobuf.h>
#endif

int sg_big_buff = SG_DEF_RESERVED_SIZE;
/* N.B. This variable is readable and writeable via
   /proc/scsi/sg/def_reserved_size . Each time sg_open() is called a buffer
   of this size (or less if there is not enough memory) will be reserved
   for use by this file descriptor. [Deprecated usage: this variable is also
   readable via /proc/sys/kernel/sg-big-buff if the sg driver is built into
   the kernel (i.e. it is not a module).] */
static int def_reserved_size = -1;      /* picks up init parameter */

#define SG_SECTOR_SZ 512
#define SG_SECTOR_MSK (SG_SECTOR_SZ - 1)

#define SG_LOW_POOL_THRESHHOLD 30
#define SG_MAX_POOL_SECTORS 320  /* Max. number of pool sectors to take */

static int sg_pool_secs_avail = SG_MAX_POOL_SECTORS;

#define SG_HEAP_PAGE 1  /* heap from kernel via get_free_pages() */
#define SG_HEAP_KMAL 2  /* heap from kernel via kmalloc() */
#define SG_HEAP_POOL 3  /* heap from scsi dma pool (mid-level) */
#define SG_USER_MEM 4   /* memory belongs to user space */


static int sg_init(void);
static int sg_attach(Scsi_Device *);
static void sg_finish(void);
static int sg_detect(Scsi_Device *);
static void sg_detach(Scsi_Device *);

static Scsi_Cmnd * dummy_cmdp = 0;    /* only used for sizeof */


static spinlock_t sg_request_lock = SPIN_LOCK_UNLOCKED;

struct Scsi_Device_Template sg_template =
{
      tag:"sg",
      scsi_type:0xff,
      major:SCSI_GENERIC_MAJOR,
      detect:sg_detect,
      init:sg_init,
      finish:sg_finish,
      attach:sg_attach,
      detach:sg_detach
};

/* Need to add 'rwlock_t sg_rw_lock = RW_LOCK_UNLOCKED;' for list protection */

typedef struct sg_scatter_hold  /* holding area for scsi scatter gather info */
{
    unsigned short k_use_sg;    /* Count of kernel scatter-gather pieces */
    unsigned short sglist_len;  /* size of malloc'd scatter-gather list */
    unsigned bufflen;           /* Size of (aggregate) data buffer */
    unsigned b_malloc_len;      /* actual len malloc'ed in buffer */
    void * buffer;              /* Data buffer or scatter list,12 bytes each*/
    struct kiobuf * kiobp;      /* for direct IO information */
    char mapped;                /* indicates kiobp has locked pages */
    char buffer_mem_src;        /* heap whereabouts of 'buffer' */
    unsigned char cmd_opcode;   /* first byte of command */
} Sg_scatter_hold;    /* 20 bytes long on i386 */

struct sg_device;               /* forward declarations */
struct sg_fd;

typedef struct sg_request  /* SG_MAX_QUEUE requests outstanding per file */
{
    Scsi_Cmnd * my_cmdp;        /* != 0  when request with lower levels */
    struct sg_request * nextrp; /* NULL -> tail request (slist) */
    struct sg_fd * parentfp;    /* NULL -> not in use */
    Sg_scatter_hold data;       /* hold buffer, perhaps scatter list */
    sg_io_hdr_t header;         /* scsi command+info, see <scsi/sg.h> */
    unsigned char sense_b[sizeof(dummy_cmdp->sense_buffer)];
    char res_used;              /* 1 -> using reserve buffer, 0 -> not ... */
    char orphan;                /* 1 -> drop on sight, 0 -> normal */
    char sg_io_owned;           /* 1 -> packet belongs to SG_IO */
    char done;                  /* 1 -> bh handler done, 0 -> prior to bh */
} Sg_request; /* 168 bytes long on i386 */

typedef struct sg_fd /* holds the state of a file descriptor */
{
    struct sg_fd * nextfp; /* NULL when last opened fd on this device */
    struct sg_device * parentdp;     /* owning device */
    wait_queue_head_t read_wait;     /* queue read until command done */
    int timeout;                     /* defaults to SG_DEFAULT_TIMEOUT */
    Sg_scatter_hold reserve;  /* buffer held for this file descriptor */
    unsigned save_scat_len;   /* original length of trunc. scat. element */
    Sg_request * headrp;      /* head of request slist, NULL->empty */
    struct fasync_struct * async_qp; /* used by asynchronous notification */
    Sg_request req_arr[SG_MAX_QUEUE]; /* used as singly-linked list */
    char low_dma;       /* as in parent but possibly overridden to 1 */
    char force_packid;  /* 1 -> pack_id input to read(), 0 -> ignored */
    char closed;        /* 1 -> fd closed but request(s) outstanding */
    char fd_mem_src;    /* heap whereabouts of this Sg_fd object */
    char cmd_q;         /* 1 -> allow command queuing, 0 -> don't */
    char next_cmd_len;  /* 0 -> automatic (def), >0 -> use on next write() */
    char keep_orphan;   /* 0 -> drop orphan (def), 1 -> keep for read() */
} Sg_fd; /* 1212 bytes long on i386 */

typedef struct sg_device /* holds the state of each scsi generic device */
{
    Scsi_Device * device;
    wait_queue_head_t o_excl_wait;   /* queue open() when O_EXCL in use */
    int sg_tablesize;   /* adapter's max scatter-gather table size */
    Sg_fd * headfp;     /* first open fd belonging to this device */
    kdev_t i_rdev;      /* holds device major+minor number */
    char exclude;       /* opened for exclusive access */
    char sgdebug;       /* 0->off, 1->sense, 9->dump dev, 10-> all devs */
} Sg_device; /* 24 bytes long on i386 */


static int sg_fasync(int fd, struct file * filp, int mode);
static void sg_cmd_done_bh(Scsi_Cmnd * SCpnt);
static int sg_start_req(Sg_request * srp);
static void sg_finish_rem_req(Sg_request * srp);
static int sg_build_indi(Sg_scatter_hold * schp, Sg_fd * sfp, int buff_size);
static int sg_build_sgat(Sg_scatter_hold * schp, const Sg_fd * sfp);
static ssize_t sg_new_read(Sg_fd * sfp, char * buf, size_t count,
			   Sg_request * srp);
static ssize_t sg_new_write(Sg_fd * sfp, const char * buf, size_t count,
			int blocking, int read_only, Sg_request ** o_srp);
static int sg_common_write(Sg_fd * sfp, Sg_request * srp,
			   unsigned char * cmnd, int timeout, int blocking);
static int sg_u_iovec(sg_io_hdr_t * hp, int sg_num, int ind,
		      int wr_xf, int * countp, unsigned char ** up);
static int sg_write_xfer(Sg_request * srp);
static int sg_read_xfer(Sg_request * srp);
static void sg_read_oxfer(Sg_request * srp, char * outp, int num_read_xfer);
static void sg_remove_scat(Sg_scatter_hold * schp);
static char * sg_get_sgat_msa(Sg_scatter_hold * schp);
static void sg_build_reserve(Sg_fd * sfp, int req_size);
static void sg_link_reserve(Sg_fd * sfp, Sg_request * srp, int size);
static void sg_unlink_reserve(Sg_fd * sfp, Sg_request * srp);
static char * sg_malloc(const Sg_fd * sfp, int size, int * retSzp,
                        int * mem_srcp);
static void sg_free(char * buff, int size, int mem_src);
static char * sg_low_malloc(int rqSz, int lowDma, int mem_src,
                            int * retSzp);
static void sg_low_free(char * buff, int size, int mem_src);
static Sg_fd * sg_add_sfp(Sg_device * sdp, int dev);
static int sg_remove_sfp(Sg_device * sdp, Sg_fd * sfp);
static Sg_request * sg_get_request(const Sg_fd * sfp, int pack_id);
static Sg_request * sg_add_request(Sg_fd * sfp);
static int sg_remove_request(Sg_fd * sfp, Sg_request * srp);
static int sg_res_in_use(const Sg_fd * sfp);
static int sg_dio_in_use(const Sg_fd * sfp);
static void sg_clr_scpnt(Scsi_Cmnd * SCpnt);
static void sg_shorten_timeout(Scsi_Cmnd * scpnt);
static int sg_ms_to_jif(unsigned int msecs);
static unsigned sg_jif_to_ms(int jifs);
static int sg_allow_access(unsigned char opcode, char dev_type);
static int sg_last_dev(void);
static int sg_build_dir(Sg_request * srp, Sg_fd * sfp, int dxfer_len);
static void sg_unmap_and(Sg_scatter_hold * schp, int free_also);

static Sg_device * sg_dev_arr = NULL;
static const int size_sg_header = sizeof(struct sg_header);
static const int size_sg_io_hdr = sizeof(sg_io_hdr_t);
static const int size_sg_iovec = sizeof(sg_iovec_t);
static const int size_sg_req_info = sizeof(sg_req_info_t);


static int sg_open(struct inode * inode, struct file * filp)
{
    int dev = MINOR(inode->i_rdev);
    int flags = filp->f_flags;
    Sg_device * sdp;
    Sg_fd * sfp;
    int res;

    if ((NULL == sg_dev_arr) || (dev < 0) || (dev >= sg_template.dev_max))
        return -ENXIO;
    sdp = &sg_dev_arr[dev];
    if ((! sdp->device) || (! sdp->device->host))
        return -ENXIO;
    if (sdp->i_rdev != inode->i_rdev)
        printk("sg_open: inode maj=%d, min=%d   sdp maj=%d, min=%d\n",
               MAJOR(inode->i_rdev), MINOR(inode->i_rdev),
               MAJOR(sdp->i_rdev), MINOR(sdp->i_rdev));
    /* If we are in the middle of error recovery, don't let anyone
     * else try and use this device.  Also, if error recovery fails, it
     * may try and take the device offline, in which case all further
     * access to the device is prohibited.  */
    if(! scsi_block_when_processing_errors(sdp->device))
        return -ENXIO;

    SCSI_LOG_TIMEOUT(3, printk("sg_open: dev=%d, flags=0x%x\n", dev, flags));

    if (flags & O_EXCL) {
        if (O_RDONLY == (flags & O_ACCMODE))
            return -EACCES;   /* Can't lock it with read only access */
        if (sdp->headfp && (filp->f_flags & O_NONBLOCK))
            return -EBUSY;
        res = 0;  /* following is a macro that beats race condition */
	__wait_event_interruptible(sdp->o_excl_wait,
	       ((sdp->headfp || sdp->exclude) ? 0 : (sdp->exclude = 1)),
                                   res);
        if (res)
            return res; /* -ERESTARTSYS because signal hit process */
    }
    else if (sdp->exclude) { /* some other fd has an exclusive lock on dev */
        if (filp->f_flags & O_NONBLOCK)
            return -EBUSY;
        res = 0;  /* following is a macro that beats race condition */
        __wait_event_interruptible(sdp->o_excl_wait, (! sdp->exclude), res);
        if (res)
            return res; /* -ERESTARTSYS because signal hit process */
    }
    if (! sdp->headfp) { /* no existing opens on this device */
        sdp->sgdebug = 0;
        sdp->sg_tablesize = sdp->device->host->sg_tablesize;
    }
    if ((sfp = sg_add_sfp(sdp, dev)))
        filp->private_data = sfp;
    else {
        if (flags & O_EXCL) sdp->exclude = 0; /* undo if error */
        return -ENOMEM;
    }

    if (sdp->device->host->hostt->module)
        __MOD_INC_USE_COUNT(sdp->device->host->hostt->module);
    if (sg_template.module)
        __MOD_INC_USE_COUNT(sg_template.module);
    return 0;
}

/* Following function was formerly called 'sg_close' */
static int sg_release(struct inode * inode, struct file * filp)
{
    Sg_device * sdp;
    Sg_fd * sfp;

    if ((! (sfp = (Sg_fd *)filp->private_data)) || (! (sdp = sfp->parentdp)))
        return -ENXIO;
    SCSI_LOG_TIMEOUT(3, printk("sg_release: dev=%d\n", MINOR(sdp->i_rdev)));
    sg_fasync(-1, filp, 0);   /* remove filp from async notification list */
    sg_remove_sfp(sdp, sfp);
    if (! sdp->headfp)
        filp->private_data = NULL;

    if (sdp->device->host->hostt->module)
        __MOD_DEC_USE_COUNT(sdp->device->host->hostt->module);
    if(sg_template.module)
        __MOD_DEC_USE_COUNT(sg_template.module);
    sdp->exclude = 0;
    wake_up_interruptible(&sdp->o_excl_wait);
    return 0;
}

static ssize_t sg_read(struct file * filp, char * buf,
                       size_t count, loff_t *ppos)
{
    int k, res;
    Sg_device * sdp;
    Sg_fd * sfp;
    Sg_request * srp;
    int req_pack_id = -1;
    struct sg_header old_hdr;
    sg_io_hdr_t new_hdr;
    sg_io_hdr_t * hp;

    if ((! (sfp = (Sg_fd *)filp->private_data)) || (! (sdp = sfp->parentdp)))
        return -ENXIO;
    SCSI_LOG_TIMEOUT(3, printk("sg_read: dev=%d, count=%d\n",
                               MINOR(sdp->i_rdev), (int)count));

    if(! scsi_block_when_processing_errors(sdp->device))
        return -ENXIO;
    if (ppos != &filp->f_pos)
        ; /* FIXME: Hmm.  Seek to the right place, or fail?  */
    if ((k = verify_area(VERIFY_WRITE, buf, count)))
        return k;
    if (sfp->force_packid && (count >= size_sg_header)) {
	__copy_from_user(&old_hdr, buf, size_sg_header);
	if (old_hdr.reply_len < 0) {
	    if (count >= size_sg_io_hdr) {
		__copy_from_user(&new_hdr, buf, size_sg_io_hdr);
		req_pack_id = new_hdr.pack_id;
	    }
	}
	else
	    req_pack_id = old_hdr.pack_id;
    }
    srp = sg_get_request(sfp, req_pack_id);
    if (! srp) { /* now wait on packet to arrive */
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN;
	while (1) {
	    int dio = sg_dio_in_use(sfp);
	    res = 0;  /* following is a macro that beats race condition */
	    __wait_event_interruptible(sfp->read_wait,
                                   (srp = sg_get_request(sfp, req_pack_id)),
                                   res);
	    if (0 == res)
		break;
	    else if (! dio)     /* only let signal out if no dio */
		return res; /* -ERESTARTSYS because signal hit process */
	}
    }
    if (srp->header.interface_id != '\0')
	return sg_new_read(sfp, buf, count, srp);

    hp = &srp->header;
    memset(&old_hdr, 0, size_sg_header);
    old_hdr.reply_len = (int)hp->timeout;
    old_hdr.pack_len = old_hdr.reply_len;   /* very old, strange behaviour */
    old_hdr.pack_id = hp->pack_id;
    old_hdr.twelve_byte =
	    ((srp->data.cmd_opcode >= 0xc0) && (12 == hp->cmd_len)) ? 1 : 0;
    old_hdr.target_status = hp->masked_status;
    old_hdr.host_status = hp->host_status;
    old_hdr.driver_status = hp->driver_status;
    if ((CHECK_CONDITION & hp->masked_status) ||
	(DRIVER_SENSE & hp->driver_status))
	memcpy(old_hdr.sense_buffer, srp->sense_b,
	       sizeof(old_hdr.sense_buffer));
    switch (hp->host_status)
    { /* This setup of 'result' is for backward compatibility and is best
	 ignored by the user who should use target, host + driver status */
	case DID_OK:
	case DID_PASSTHROUGH:
	case DID_SOFT_ERROR:
	    old_hdr.result = 0;
	    break;
	case DID_NO_CONNECT:
	case DID_BUS_BUSY:
	case DID_TIME_OUT:
	    old_hdr.result = EBUSY;
	    break;
	case DID_BAD_TARGET:
	case DID_ABORT:
	case DID_PARITY:
	case DID_RESET:
	case DID_BAD_INTR:
	    old_hdr.result = EIO;
	    break;
	case DID_ERROR:
	    old_hdr.result =
	      (srp->sense_b[0] == 0 && hp->masked_status == GOOD) ? 0 : EIO;
	    break;
	default:
	    old_hdr.result = EIO;
	    break;
    }

    /* Now copy the result back to the user buffer.  */
    if (count >= size_sg_header) {
	__copy_to_user(buf, &old_hdr, size_sg_header);
        buf += size_sg_header;
	if (count > old_hdr.reply_len)
	    count = old_hdr.reply_len;
	if (count > size_sg_header)
	    sg_read_oxfer(srp, buf, count - size_sg_header);
    }
    else
	count = (old_hdr.result == 0) ? 0 : -EIO;
    sg_finish_rem_req(srp);
    return count;
}

static ssize_t sg_new_read(Sg_fd * sfp, char * buf, size_t count,
			   Sg_request * srp)
{
    Sg_device           * sdp = sfp->parentdp;
    sg_io_hdr_t         * hp = &srp->header;
    int                   k, len;

    if(! scsi_block_when_processing_errors(sdp->device) )
	return -ENXIO;
    if (count < size_sg_io_hdr)
	return -EINVAL;

    hp->sb_len_wr = 0;
    if ((hp->mx_sb_len > 0) && hp->sbp) {
	if ((CHECK_CONDITION & hp->masked_status) ||
	    (DRIVER_SENSE & hp->driver_status)) {
	    int sb_len = sizeof(dummy_cmdp->sense_buffer);
	    sb_len = (hp->mx_sb_len > sb_len) ? sb_len : hp->mx_sb_len;
	    len = 8 + (int)srp->sense_b[7]; /* Additional sense length field */
	    len = (len > sb_len) ? sb_len : len;
	    if ((k = verify_area(VERIFY_WRITE, hp->sbp, len)))
		return k;
	    __copy_to_user(hp->sbp, srp->sense_b, len);
	    hp->sb_len_wr = len;
	}
    }
    if (hp->masked_status || hp->host_status || hp->driver_status)
	hp->info |= SG_INFO_CHECK;
    copy_to_user(buf, hp, size_sg_io_hdr);

    k = sg_read_xfer(srp);
    if (k) return k; /* probably -EFAULT, bad addr in dxferp or iovec list */
    sg_finish_rem_req(srp);
    return count;
}


static ssize_t sg_write(struct file * filp, const char * buf,
                        size_t count, loff_t *ppos)
{
    int                   mxsize, cmd_size, k;
    int                   input_size, blocking;
    unsigned char         opcode;
    Sg_device           * sdp;
    Sg_fd               * sfp;
    Sg_request          * srp;
    struct sg_header      old_hdr;
    sg_io_hdr_t         * hp;
    unsigned char         cmnd[sizeof(dummy_cmdp->cmnd)];

    if ((! (sfp = (Sg_fd *)filp->private_data)) || (! (sdp = sfp->parentdp)))
        return -ENXIO;
    SCSI_LOG_TIMEOUT(3, printk("sg_write: dev=%d, count=%d\n",
                               MINOR(sdp->i_rdev), (int)count));

    if(! scsi_block_when_processing_errors(sdp->device) )
        return -ENXIO;
    if (ppos != &filp->f_pos)
        ; /* FIXME: Hmm.  Seek to the right place, or fail?  */

    if ((k = verify_area(VERIFY_READ, buf, count)))
        return k;  /* protects following copy_from_user()s + get_user()s */
    if (count < size_sg_header)
	return -EIO;
    __copy_from_user(&old_hdr, buf, size_sg_header);
    blocking = !(filp->f_flags & O_NONBLOCK);
    if (old_hdr.reply_len < 0)
	return sg_new_write(sfp, buf, count, blocking, 0, NULL);
    if (count < (size_sg_header + 6))
	return -EIO;   /* The minimum scsi command length is 6 bytes. */

    if (! (srp = sg_add_request(sfp))) {
	SCSI_LOG_TIMEOUT(1, printk("sg_write: queue full\n"));
	return -EDOM;
    }
    buf += size_sg_header;
    __get_user(opcode, buf);
    if (sfp->next_cmd_len > 0) {
        if (sfp->next_cmd_len > MAX_COMMAND_SIZE) {
            SCSI_LOG_TIMEOUT(1, printk("sg_write: command length too long\n"));
            sfp->next_cmd_len = 0;
	    sg_remove_request(sfp, srp);
            return -EIO;
        }
        cmd_size = sfp->next_cmd_len;
        sfp->next_cmd_len = 0; /* reset so only this write() effected */
    }
    else {
        cmd_size = COMMAND_SIZE(opcode); /* based on SCSI command group */
	if ((opcode >= 0xc0) && old_hdr.twelve_byte)
            cmd_size = 12;
    }
    SCSI_LOG_TIMEOUT(4, printk("sg_write:   scsi opcode=0x%02x, cmd_size=%d\n",
                               (int)opcode, cmd_size));
/* Determine buffer size.  */
    input_size = count - cmd_size;
    mxsize = (input_size > old_hdr.reply_len) ? input_size :
						old_hdr.reply_len;
    mxsize -= size_sg_header;
    input_size -= size_sg_header;
    if (input_size < 0) {
        sg_remove_request(sfp, srp);
        return -EIO; /* User did not pass enough bytes for this command. */
    }
    hp = &srp->header;
    hp->interface_id = '\0'; /* indicator of old interface tunnelled */
    hp->cmd_len = (unsigned char)cmd_size;
    hp->iovec_count = 0;
    hp->mx_sb_len = 0;
    if (input_size > 0)
	hp->dxfer_direction = ((old_hdr.reply_len - size_sg_header) > 0) ?
			      SG_DXFER_TO_FROM_DEV : SG_DXFER_TO_DEV;
    else
	hp->dxfer_direction = (mxsize > 0) ? SG_DXFER_FROM_DEV :
					     SG_DXFER_NONE;
    hp->dxfer_len = mxsize;
    hp->dxferp = (unsigned char *)buf + cmd_size;
    hp->sbp = NULL;
    hp->timeout = old_hdr.reply_len;    /* structure abuse ... */
    hp->flags = input_size;             /* structure abuse ... */
    hp->pack_id = old_hdr.pack_id;
    hp->usr_ptr = NULL;
    __copy_from_user(cmnd, buf, cmd_size);
    k = sg_common_write(sfp, srp, cmnd, sfp->timeout, blocking);
    return (k < 0) ? k : count;
}

static ssize_t sg_new_write(Sg_fd * sfp, const char * buf, size_t count,
			    int blocking, int read_only, Sg_request ** o_srp)
{
    int                   k;
    Sg_request          * srp;
    sg_io_hdr_t         * hp;
    unsigned char         cmnd[sizeof(dummy_cmdp->cmnd)];
    int                   timeout;

    if (count < size_sg_io_hdr)
	return -EINVAL;
    if ((k = verify_area(VERIFY_READ, buf, count)))
	return k;  /* protects following copy_from_user()s + get_user()s */

    sfp->cmd_q = 1;  /* when sg_io_hdr seen, set command queuing on */
    if (! (srp = sg_add_request(sfp))) {
	SCSI_LOG_TIMEOUT(1, printk("sg_new_write: queue full\n"));
	return -EDOM;
    }
    hp = &srp->header;
    __copy_from_user(hp, buf, size_sg_io_hdr);
    if (hp->interface_id != 'S') {
	sg_remove_request(sfp, srp);
	return -ENOSYS;
    }
    timeout = sg_ms_to_jif(srp->header.timeout);
    if ((! hp->cmdp) || (hp->cmd_len < 6) || (hp->cmd_len > sizeof(cmnd))) {
	sg_remove_request(sfp, srp);
	return -EMSGSIZE;
    }
    if ((k = verify_area(VERIFY_READ, hp->cmdp, hp->cmd_len))) {
	sg_remove_request(sfp, srp);
	return k;  /* protects following copy_from_user()s + get_user()s */
    }
    __copy_from_user(cmnd, hp->cmdp, hp->cmd_len);
    if (read_only &&
	(! sg_allow_access(cmnd[0], sfp->parentdp->device->type))) {
	sg_remove_request(sfp, srp);
	return -EACCES;
    }
    k = sg_common_write(sfp, srp, cmnd, timeout, blocking);
    if (k < 0) return k;
    if (o_srp) *o_srp = srp;
    return count;
}

static int sg_common_write(Sg_fd * sfp, Sg_request * srp,
			   unsigned char * cmnd, int timeout, int blocking)
{
    int                   k;
    Scsi_Cmnd           * SCpnt;
    Sg_device           * sdp = sfp->parentdp;
    sg_io_hdr_t         * hp = &srp->header;

    srp->data.cmd_opcode = cmnd[0];  /* hold opcode of command */
    hp->status = 0;
    hp->masked_status = 0;
    hp->msg_status = 0;
    hp->info = 0;
    hp->host_status = 0;
    hp->driver_status = 0;
    hp->resid = 0;
    SCSI_LOG_TIMEOUT(4,
	printk("sg_common_write:  scsi opcode=0x%02x, cmd_size=%d\n",
	       (int)cmnd[0], (int)hp->cmd_len));

    if ((k = sg_start_req(srp))) {
	SCSI_LOG_TIMEOUT(1, printk("sg_write: start_req err=%d\n", k));
	sg_finish_rem_req(srp);
        return k;    /* probably out of space --> ENOMEM */
    }
    if ((k = sg_write_xfer(srp))) {
	SCSI_LOG_TIMEOUT(1, printk("sg_write: write_xfer, bad address\n"));
	sg_finish_rem_req(srp);
	return k;
    }
/*  SCSI_LOG_TIMEOUT(7, printk("sg_write: allocating device\n")); */
    SCpnt = scsi_allocate_device(sdp->device, blocking, TRUE);
    if (! SCpnt) {
	sg_finish_rem_req(srp);
	return (signal_pending(current)) ? -EINTR : -EAGAIN;
	/* No available command blocks, or, interrupted while waiting */
    }
/*  SCSI_LOG_TIMEOUT(7, printk("sg_write: device allocated\n")); */
    srp->my_cmdp = SCpnt;
    SCpnt->request.rq_dev = sdp->i_rdev;
    SCpnt->request.rq_status = RQ_ACTIVE;
    SCpnt->sense_buffer[0] = 0;
    SCpnt->cmd_len = hp->cmd_len;
/* Set the LUN field in the command structure, overriding user input  */
    if (! (hp->flags & SG_FLAG_LUN_INHIBIT))
	cmnd[1] = (cmnd[1] & 0x1f) | (sdp->device->lun << 5);

/*  SCSI_LOG_TIMEOUT(7, printk("sg_write: do cmd\n")); */
    SCpnt->use_sg = srp->data.k_use_sg;
    SCpnt->sglist_len = srp->data.sglist_len;
    SCpnt->bufflen = srp->data.bufflen;
    SCpnt->underflow = 0;
    SCpnt->buffer = srp->data.buffer;
    srp->data.k_use_sg = 0;
    srp->data.sglist_len = 0;
    srp->data.bufflen = 0;
    srp->data.buffer = NULL;
    hp->duration = jiffies;
/* Now send everything of to mid-level. The next time we hear about this
   packet is when sg_cmd_done_bh() is called (i.e. a callback). */
    scsi_do_cmd(SCpnt, (void *)cmnd,
		(void *)SCpnt->buffer, hp->dxfer_len,
		sg_cmd_done_bh, timeout, SG_DEFAULT_RETRIES);
    /* dxfer_len overwrites SCpnt->bufflen, hence need for b_malloc_len */
    return 0;
}

static int sg_ioctl(struct inode * inode, struct file * filp,
                    unsigned int cmd_in, unsigned long arg)
{
    int result, val, read_only;
    Sg_device * sdp;
    Sg_fd * sfp;
    Sg_request * srp;

    if ((! (sfp = (Sg_fd *)filp->private_data)) || (! (sdp = sfp->parentdp)))
        return -ENXIO;
    SCSI_LOG_TIMEOUT(3, printk("sg_ioctl: dev=%d, cmd=0x%x\n",
                               MINOR(sdp->i_rdev), (int)cmd_in));
    if(! scsi_block_when_processing_errors(sdp->device) )
        return -ENXIO;
    read_only = (O_RDWR != (filp->f_flags & O_ACCMODE));

    switch(cmd_in)
    {
    case SG_IO:
	{
	    int blocking = 1;   /* ignore O_NONBLOCK flag */

	    if(! scsi_block_when_processing_errors(sdp->device) )
		return -ENXIO;
	    result = verify_area(VERIFY_WRITE, (void *)arg, size_sg_io_hdr);
	    if (result) return result;
	    result = sg_new_write(sfp, (const char *)arg, size_sg_io_hdr,
				  blocking, read_only, &srp);
	    if (result < 0) return result;
	    srp->sg_io_owned = 1;
	    while (1) {
		int dio = sg_dio_in_use(sfp);
		result = 0;  /* following macro to beat race condition */
		__wait_event_interruptible(sfp->read_wait,
				   (sfp->closed || srp->done), result);
		if (sfp->closed)
		    return 0;       /* request packet dropped already */
		if (0 == result)
		    break;
		else if (! dio) {       /* only let signal out if no dio */
		    srp->orphan = 1;
		    return result; /* -ERESTARTSYS because signal hit process */
		}
	    }
	    result = sg_new_read(sfp, (char *)arg, size_sg_io_hdr, srp);
	    return (result < 0) ? result : 0;
	}
    case SG_SET_TIMEOUT:
        result =  get_user(val, (int *)arg);
        if (result) return result;
        if (val < 0)
            return -EIO;
        sfp->timeout = val;
        return 0;
    case SG_GET_TIMEOUT:  /* N.B. User receives timeout as return value */
        return sfp->timeout; /* strange ..., for backward compatibility */
    case SG_SET_FORCE_LOW_DMA:
        result = get_user(val, (int *)arg);
        if (result) return result;
        if (val) {
            sfp->low_dma = 1;
            if ((0 == sfp->low_dma) && (0 == sg_res_in_use(sfp))) {
                val = (int)sfp->reserve.bufflen;
                sg_remove_scat(&sfp->reserve);
                sg_build_reserve(sfp, val);
            }
        }
        else
            sfp->low_dma = sdp->device->host->unchecked_isa_dma;
        return 0;
    case SG_GET_LOW_DMA:
        return put_user((int)sfp->low_dma, (int *)arg);
    case SG_GET_SCSI_ID:
	result = verify_area(VERIFY_WRITE, (void *)arg, sizeof(sg_scsi_id_t));
        if (result) return result;
        else {
	    sg_scsi_id_t * sg_idp = (sg_scsi_id_t *)arg;
            __put_user((int)sdp->device->host->host_no, &sg_idp->host_no);
            __put_user((int)sdp->device->channel, &sg_idp->channel);
            __put_user((int)sdp->device->id, &sg_idp->scsi_id);
            __put_user((int)sdp->device->lun, &sg_idp->lun);
            __put_user((int)sdp->device->type, &sg_idp->scsi_type);
	    __put_user((short)sdp->device->host->cmd_per_lun,
                       &sg_idp->h_cmd_per_lun);
	    __put_user((short)sdp->device->queue_depth,
                       &sg_idp->d_queue_depth);
	    __put_user(0, &sg_idp->unused[0]);
	    __put_user(0, &sg_idp->unused[1]);
            return 0;
        }
    case SG_SET_FORCE_PACK_ID:
        result = get_user(val, (int *)arg);
        if (result) return result;
        sfp->force_packid = val ? 1 : 0;
        return 0;
    case SG_GET_PACK_ID:
        result = verify_area(VERIFY_WRITE, (void *) arg, sizeof(int));
        if (result) return result;
        srp = sfp->headrp;
        while (srp) {
	    if (srp->done && (! srp->sg_io_owned)) {
                __put_user(srp->header.pack_id, (int *)arg);
                return 0;
            }
            srp = srp->nextrp;
        }
        __put_user(-1, (int *)arg);
        return 0;
    case SG_GET_NUM_WAITING:
        srp = sfp->headrp;
        val = 0;
        while (srp) {
	    if (srp->done && (! srp->sg_io_owned))
                ++val;
            srp = srp->nextrp;
        }
        return put_user(val, (int *)arg);
    case SG_GET_SG_TABLESIZE:
        return put_user(sdp->sg_tablesize, (int *)arg);
    case SG_SET_RESERVED_SIZE:
        result = get_user(val, (int *)arg);
        if (result) return result;
        if (val != sfp->reserve.bufflen) {
            if (sg_res_in_use(sfp))
                return -EBUSY;
            sg_remove_scat(&sfp->reserve);
            sg_build_reserve(sfp, val);
        }
        return 0;
    case SG_GET_RESERVED_SIZE:
        val = (int)sfp->reserve.bufflen;
        return put_user(val, (int *)arg);
    case SG_SET_COMMAND_Q:
        result = get_user(val, (int *)arg);
        if (result) return result;
        sfp->cmd_q = val ? 1 : 0;
        return 0;
    case SG_GET_COMMAND_Q:
        return put_user((int)sfp->cmd_q, (int *)arg);
    case SG_SET_KEEP_ORPHAN:
        result = get_user(val, (int *)arg);
        if (result) return result;
	sfp->keep_orphan = val;
        return 0;
    case SG_GET_KEEP_ORPHAN:
	return put_user((int)sfp->keep_orphan, (int *)arg);
    case SG_NEXT_CMD_LEN:
        result = get_user(val, (int *)arg);
        if (result) return result;
        sfp->next_cmd_len = (val > 0) ? val : 0;
        return 0;
    case SG_GET_VERSION_NUM:
        return put_user(sg_version_num, (int *)arg);
    case SG_GET_REQUEST_TABLE:
	result = verify_area(VERIFY_WRITE, (void *) arg,
			     size_sg_req_info * SG_MAX_QUEUE);
	if (result) return result;
	else {
	    sg_req_info_t rinfo[SG_MAX_QUEUE];
	    Sg_request * srp = sfp->headrp;
	    for (val = 0; val < SG_MAX_QUEUE;
		 ++val, srp = srp ? srp->nextrp : srp) {
		memset(&rinfo[val], 0, size_sg_req_info);
		if (srp) {
		    rinfo[val].req_state = srp->done ? 2 : 1;
		    rinfo[val].problem = srp->header.masked_status &
			srp->header.host_status & srp->header.driver_status;
		    rinfo[val].duration = srp->done ?
			    sg_jif_to_ms(srp->header.duration) :
			    sg_jif_to_ms(jiffies - srp->header.duration);
		    rinfo[val].orphan = srp->orphan;
		    rinfo[val].sg_io_owned = srp->sg_io_owned;
		    rinfo[val].pack_id = srp->header.pack_id;
		    rinfo[val].usr_ptr = srp->header.usr_ptr;
		}
	    }
	    __copy_to_user((void *)arg, rinfo, size_sg_req_info * SG_MAX_QUEUE);
	    return 0;
	}
    case SG_EMULATED_HOST:
        return put_user(sdp->device->host->hostt->emulated, (int *)arg);
    case SG_SCSI_RESET:
        if (! scsi_block_when_processing_errors(sdp->device))
            return -EBUSY;
        result = get_user(val, (int *)arg);
        if (result) return result;
	/* Don't do anything till scsi mid level visibility */
        return 0;
    case SCSI_IOCTL_SEND_COMMAND:
	if (read_only) {
	    unsigned char opcode = WRITE_6;
	    Scsi_Ioctl_Command * siocp = (void *)arg;

	    copy_from_user(&opcode, siocp->data, 1);
	    if (! sg_allow_access(opcode, sdp->device->type))
		return -EACCES;
	}
        return scsi_ioctl_send_command(sdp->device, (void *)arg);
    case SG_SET_DEBUG:
        result = get_user(val, (int *)arg);
        if (result) return result;
        sdp->sgdebug = (char)val;
        return 0;
    case SCSI_IOCTL_GET_IDLUN:
    case SCSI_IOCTL_GET_BUS_NUMBER:
    case SCSI_IOCTL_PROBE_HOST:
    case SG_GET_TRANSFORM:
        return scsi_ioctl(sdp->device, cmd_in, (void *)arg);
    default:
	if (read_only)
            return -EACCES; /* don't know so take safe approach */
        return scsi_ioctl(sdp->device, cmd_in, (void *)arg);
    }
}

static unsigned int sg_poll(struct file * filp, poll_table * wait)
{
    unsigned int res = 0;
    Sg_device * sdp;
    Sg_fd * sfp;
    Sg_request * srp;
    int count = 0;

    if ((! (sfp = (Sg_fd *)filp->private_data)) || (! (sdp = sfp->parentdp)))
        return POLLERR;
    poll_wait(filp, &sfp->read_wait, wait);
    srp = sfp->headrp;
    while (srp) {   /* if any read waiting, flag it */
	if ((0 == res) && srp->done && (! srp->sg_io_owned))
            res = POLLIN | POLLRDNORM;
        ++count;
        srp = srp->nextrp;
    }
    if (! sfp->cmd_q) {
        if (0 == count)
            res |= POLLOUT | POLLWRNORM;
    }
    else if (count < SG_MAX_QUEUE)
        res |= POLLOUT | POLLWRNORM;
    SCSI_LOG_TIMEOUT(3, printk("sg_poll: dev=%d, res=0x%x\n",
                        MINOR(sdp->i_rdev), (int)res));
    return res;
}

static int sg_fasync(int fd, struct file * filp, int mode)
{
    int retval;
    Sg_device * sdp;
    Sg_fd * sfp;

    if ((! (sfp = (Sg_fd *)filp->private_data)) || (! (sdp = sfp->parentdp)))
        return -ENXIO;
    SCSI_LOG_TIMEOUT(3, printk("sg_fasync: dev=%d, mode=%d\n",
                               MINOR(sdp->i_rdev), mode));

    retval = fasync_helper(fd, filp, mode, &sfp->async_qp);
    return (retval < 0) ? retval : 0;
}

/* This function is a "bottom half" handler that is called by the
 * mid level when a command is completed (or has failed). */
static void sg_cmd_done_bh(Scsi_Cmnd * SCpnt)
{
    int dev = MINOR(SCpnt->request.rq_dev);
    Sg_device * sdp;
    Sg_fd * sfp;
    Sg_request * srp = NULL;

    if ((NULL == sg_dev_arr) || (dev < 0) || (dev >= sg_template.dev_max)
	|| (NULL == (sdp = &sg_dev_arr[dev]))) {
	SCSI_LOG_TIMEOUT(1, printk("sg...bh: bad args dev=%d\n", dev));
        scsi_release_command(SCpnt);
        SCpnt = NULL;
        return;
    }
    sfp = sdp->headfp;
    while (sfp) {
        srp = sfp->headrp;
        while (srp) {
            if (SCpnt == srp->my_cmdp)
                break;
            srp = srp->nextrp;
        }
        if (srp)
            break;
        sfp = sfp->nextfp;
    }
    if (! srp) {
	SCSI_LOG_TIMEOUT(1, printk("sg...bh: req missing, dev=%d\n", dev));
        scsi_release_command(SCpnt);
        SCpnt = NULL;
        return;
    }
    /* First transfer ownership of data buffers to sg_device object. */
    srp->data.k_use_sg = SCpnt->use_sg;
    srp->data.sglist_len = SCpnt->sglist_len;
    srp->data.bufflen = SCpnt->bufflen;
    srp->data.buffer = SCpnt->buffer;
    sg_clr_scpnt(SCpnt);
    srp->my_cmdp = NULL;
    srp->done = 1;

    SCSI_LOG_TIMEOUT(4, printk("sg...bh: dev=%d, pack_id=%d, res=0x%x\n",
		     dev, srp->header.pack_id, (int)SCpnt->result));
    srp->header.resid = SCpnt->resid;
    /* sg_unmap_and(&srp->data, 0); */     /* unmap locked pages a.s.a.p. */
    srp->header.duration = sg_jif_to_ms(jiffies - (int)srp->header.duration);
    if (0 != SCpnt->result) {
	memcpy(srp->sense_b, SCpnt->sense_buffer, sizeof(srp->sense_b));
	srp->header.status = 0xff & SCpnt->result;
	srp->header.masked_status  = status_byte(SCpnt->result);
	srp->header.msg_status  = msg_byte(SCpnt->result);
	srp->header.host_status = host_byte(SCpnt->result);
	srp->header.driver_status = driver_byte(SCpnt->result);
	if ((sdp->sgdebug > 0) &&
	    ((CHECK_CONDITION == srp->header.masked_status) ||
	     (COMMAND_TERMINATED == srp->header.masked_status)))
	    print_sense("sg_cmd_done_bh", SCpnt);

	/* Following if statement is a patch supplied by Eric Youngdale */
	if (driver_byte(SCpnt->result) != 0
	    && (SCpnt->sense_buffer[0] & 0x7f) == 0x70
	    && (SCpnt->sense_buffer[2] & 0xf) == UNIT_ATTENTION
	    && sdp->device->removable) {
	    /* Detected disc change. Set the bit - this may be used if */
	    /* there are filesystems using this device. */
	    sdp->device->changed = 1;
	}
    }
    /* Rely on write phase to clean out srp status values, so no "else" */

    scsi_release_command(SCpnt);
    SCpnt = NULL;
    if (sfp->closed) { /* whoops this fd already released, cleanup */
        SCSI_LOG_TIMEOUT(1,
	       printk("sg...bh: already closed, freeing ...\n"));
	/* should check if module is unloaded <<<<<<< */
	sg_finish_rem_req(srp);
	srp = NULL;
	if (NULL == sfp->headrp) {
            SCSI_LOG_TIMEOUT(1,
		printk("sg...bh: already closed, final cleanup\n"));
            sg_remove_sfp(sdp, sfp);
	    sfp = NULL;
        }
    }
    else if (srp && srp->orphan) {
	if (sfp->keep_orphan)
	    srp->sg_io_owned = 0;
	else {
	    sg_finish_rem_req(srp);
	    srp = NULL;
        }
    }
    if (sfp && srp) {
	/* Now wake up any sg_read() that is waiting for this packet. */
	wake_up_interruptible(&sfp->read_wait);
	if (sfp->async_qp)
	    kill_fasync(sfp->async_qp, SIGPOLL, POLL_IN);
    }
}

static struct file_operations sg_fops = {
	 read:		sg_read,
	 write:		sg_write,
	 poll:		sg_poll,
	 ioctl:		sg_ioctl,
	 open:		sg_open,
	 release:	sg_release,
	 fasync:	sg_fasync,
};


static int sg_detect(Scsi_Device * scsidp)
{
    switch (scsidp->type) {
        case TYPE_DISK:
        case TYPE_MOD:
        case TYPE_ROM:
        case TYPE_WORM:
        case TYPE_TAPE: break;
        default:
        printk("Detected scsi generic sg%c at scsi%d,"
                " channel %d, id %d, lun %d\n",
               'a'+sg_template.dev_noticed,
	       scsidp->host->host_no, scsidp->channel,
               scsidp->id, scsidp->lun);
    }
    sg_template.dev_noticed++;
    return 1;
}

/* Driver initialization */
static int sg_init()
{
    static int sg_registered = 0;
    int size;

    if (sg_template.dev_noticed == 0) return 0;

    if(!sg_registered) {
        if (register_chrdev(SCSI_GENERIC_MAJOR,"sg",&sg_fops))
        {
            printk("Unable to get major %d for generic SCSI device\n",
                   SCSI_GENERIC_MAJOR);
            return 1;
        }
        sg_registered++;
    }

    /* If we have already been through here, return */
    if(sg_dev_arr) return 0;

    SCSI_LOG_TIMEOUT(3, printk("sg_init\n"));
    size = sizeof(Sg_device) *
	   (sg_template.dev_noticed + SG_EXTRA_DEVS);
    sg_dev_arr = (Sg_device *)kmalloc(size, GFP_ATOMIC);
    memset(sg_dev_arr, 0, size);
    if (NULL == sg_dev_arr) {
        printk("sg_init: no space for sg_dev_arr\n");
        return 1;
    }
#ifdef CONFIG_PROC_FS
    sg_proc_init();
#endif  /* CONFIG_PROC_FS */
    sg_template.dev_max = sg_template.dev_noticed + SG_EXTRA_DEVS;
    return 0;
}

#ifndef MODULE
static int __init sg_def_reserved_size_setup(char *str)
{
    int tmp;

    if (get_option(&str, &tmp) == 1) {
	def_reserved_size = tmp;
	if (tmp >= 0)
	    sg_big_buff = tmp;
	return 1;
    } else {
	printk("sg_def_reserved_size : usage sg_def_reserved_size=n "
	       "(n could be 65536, 131072 or 262144)\n");
	return 0;
    }
}

__setup("sg_def_reserved_size=", sg_def_reserved_size_setup);
#endif


static int sg_attach(Scsi_Device * scsidp)
{
    Sg_device * sdp = sg_dev_arr;
    int k;

    if ((sg_template.nr_dev >= sg_template.dev_max) || (! sdp))
    {
        scsidp->attached--;
	printk("sg_attach: rejected since exceeds dev_max=%d\n",
	       sg_template.dev_max);
        return 1;
    }

    for(k = 0; k < sg_template.dev_max; k++, sdp++)
        if(! sdp->device) break;

    if(k >= sg_template.dev_max) panic ("scsi_devices corrupt (sg)");

    SCSI_LOG_TIMEOUT(3, printk("sg_attach: dev=%d \n", k));
    sdp->device = scsidp;
    init_waitqueue_head(&sdp->o_excl_wait);
    sdp->headfp= NULL;
    sdp->exclude = 0;
    sdp->sgdebug = 0;
    sdp->sg_tablesize = scsidp->host ? scsidp->host->sg_tablesize : 0;
    sdp->i_rdev = MKDEV(SCSI_GENERIC_MAJOR, k);
    sg_template.nr_dev++;
    return 0;
}

/* Called at 'finish' of init process, after all attaches */
static void sg_finish(void)
{
    SCSI_LOG_TIMEOUT(3, printk("sg_finish: dma_free_sectors=%u\n",
                     scsi_dma_free_sectors));
}

static void sg_detach(Scsi_Device * scsidp)
{
    Sg_device * sdp = sg_dev_arr;
    unsigned long flags = 0;
    Sg_fd * sfp;
    Sg_request * srp;
    int k;

    if (NULL == sdp) return; /* all is not well ... */
    for (k = 0; k < sg_template.dev_max; k++, sdp++) {
        if(sdp->device != scsidp)
            continue;   /* dirty but lowers nesting */
        if (sdp->headfp) {
/* Need to stop sg_cmd_done_bh() playing with this list during this loop */
            spin_lock_irqsave(&sg_request_lock, flags);
            sfp = sdp->headfp;
            while (sfp) {
                srp = sfp->headrp;
                while (srp) {
		    if (! srp->done)
                        sg_shorten_timeout(srp->my_cmdp);
                    srp = srp->nextrp;
                }
                sfp = sfp->nextfp;
            }
            spin_unlock_irqrestore(&sg_request_lock, flags);
    SCSI_LOG_TIMEOUT(3, printk("sg_detach: dev=%d, dirty, sleep(3)\n", k));
            scsi_sleep(3); /* sleep 3 jiffies, hoping for timeout to go off */
        }
        else {
            SCSI_LOG_TIMEOUT(3, printk("sg_detach: dev=%d\n", k));
	    sdp->device = NULL;
        }
        scsidp->attached--;
        sg_template.nr_dev--;
/* avoid associated device /dev/sg? being incremented
 * each time module is inserted/removed , <dan@lectra.fr> */
        sg_template.dev_noticed--;
        return;
    }
    return;
}

#ifdef MODULE

MODULE_PARM(def_reserved_size, "i");
MODULE_PARM_DESC(def_reserved_size, "size of buffer reserved for each fd");

int init_module(void) {
    if (def_reserved_size >= 0)
	sg_big_buff = def_reserved_size;
    sg_template.module = &__this_module;
    return scsi_register_module(MODULE_SCSI_DEV, &sg_template);
}

void cleanup_module( void)
{
    scsi_unregister_module(MODULE_SCSI_DEV, &sg_template);
    unregister_chrdev(SCSI_GENERIC_MAJOR, "sg");

#ifdef CONFIG_PROC_FS
    sg_proc_cleanup();
#endif  /* CONFIG_PROC_FS */
    if(sg_dev_arr != NULL) {
/* Really worrying situation of writes still pending and get here */
/* Strategy: shorten timeout on release + wait on detach ... */
	kfree((char *)sg_dev_arr);
        sg_dev_arr = NULL;
    }
    sg_template.dev_max = 0;
}
#endif /* MODULE */


#if 0
extern void scsi_times_out (Scsi_Cmnd * SCpnt);
extern void scsi_old_times_out (Scsi_Cmnd * SCpnt);
#endif

/* Can't see clean way to abort a command so shorten timeout to 1 jiffy */
static void sg_shorten_timeout(Scsi_Cmnd * scpnt)
{
#if 0 /* scsi_syms.c is very miserly about exported functions */
    scsi_delete_timer(scpnt);
    if (! scpnt)
        return;
    scpnt->timeout_per_command = 1; /* try 1 jiffy (perhaps 0 jiffies) */
    if (scpnt->host->hostt->use_new_eh_code)
        scsi_add_timer(scpnt, scpnt->timeout_per_command, scsi_times_out);
    else
        scsi_add_timer(scpnt, scpnt->timeout_per_command,
                       scsi_old_times_out);
#else
    unsigned long flags = 0;
    spin_lock_irqsave(&sg_request_lock, flags);
    scsi_sleep(HZ); /* just sleep 1 second and hope ... */
    spin_unlock_irqrestore(&sg_request_lock, flags);
#endif
}

static int sg_start_req(Sg_request * srp)
{
    int res;
    Sg_fd * sfp = srp->parentfp;
    sg_io_hdr_t * hp = &srp->header;
    int dxfer_len = (int)hp->dxfer_len;
    Sg_scatter_hold * req_schp = &srp->data;
    Sg_scatter_hold * rsv_schp = &sfp->reserve;

    SCSI_LOG_TIMEOUT(4, printk("sg_start_req: dxfer_len=%d\n", dxfer_len));
    if ((hp->flags & SG_FLAG_DIRECT_IO) && (dxfer_len > 0) &&
	(hp->dxfer_direction != SG_DXFER_NONE) && (0 == hp->iovec_count) &&
	(! sfp->parentdp->device->host->unchecked_isa_dma)) {
	res = sg_build_dir(srp, sfp, dxfer_len);
	if (res <= 0)   /* -ve -> error, 0 -> done, 1 -> try indirect */
	    return res;
    }
    if ((! sg_res_in_use(sfp)) && (dxfer_len <= rsv_schp->bufflen)) {
	sg_link_reserve(sfp, srp, dxfer_len);
    }
    else {
	res = sg_build_indi(req_schp, sfp, dxfer_len);
        if (res) {
            sg_remove_scat(req_schp);
            return res;
        }
    }
    return 0;
}

static void sg_finish_rem_req(Sg_request * srp)
{
    Sg_fd * sfp = srp->parentfp;
    Sg_scatter_hold * req_schp = &srp->data;

    SCSI_LOG_TIMEOUT(4, printk("sg_finish_rem_req: res_used=%d\n",
			       (int)srp->res_used));
    sg_unmap_and(&srp->data, 1);
    if (srp->res_used)
        sg_unlink_reserve(sfp, srp);
    else
        sg_remove_scat(req_schp);
    sg_remove_request(sfp, srp);
}

static int sg_build_sgat(Sg_scatter_hold * schp, const Sg_fd * sfp)
{
    int mem_src, ret_sz;
    int sg_bufflen = PAGE_SIZE;
    int elem_sz = sizeof(struct scatterlist) + sizeof(char);
    int mx_sc_elems = (sg_bufflen / elem_sz) - 1;

    mem_src = SG_HEAP_KMAL;
    schp->buffer = (struct scatterlist *)sg_malloc(sfp, sg_bufflen,
						   &ret_sz, &mem_src);
    schp->buffer_mem_src = (char)mem_src;
    if (! schp->buffer)
	return -ENOMEM;
    else if (ret_sz != sg_bufflen) {
	sg_bufflen = ret_sz;
	mx_sc_elems = (sg_bufflen / elem_sz) - 1;
    }
    schp->sglist_len = sg_bufflen;
    memset(schp->buffer, 0, sg_bufflen);
    return mx_sc_elems; /* number of scat_gath elements allocated */
}

static void sg_unmap_and(Sg_scatter_hold * schp, int free_also)
{
#ifdef SG_ALLOW_DIO
    if (schp && schp->kiobp) {
	if (schp->mapped) {
	    unmap_kiobuf(schp->kiobp);
	    schp->mapped = 0;
	}
	if (free_also) {
	    free_kiovec(1, &schp->kiobp);
	    schp->kiobp = NULL;
	}
    }
#endif
}

static int sg_build_dir(Sg_request * srp, Sg_fd * sfp, int dxfer_len)
{
#ifdef SG_ALLOW_DIO
    int res, k, split, offset, num, mx_sc_elems, rem_sz;
    struct kiobuf * kp;
    char * mem_src_arr;
    struct scatterlist * sclp;
    unsigned long addr, prev_addr;
    sg_io_hdr_t * hp = &srp->header;
    Sg_scatter_hold * schp = &srp->data;
    int sg_tablesize = sfp->parentdp->sg_tablesize;

    res = alloc_kiovec(1, &schp->kiobp);
    if (0 != res) {
	SCSI_LOG_TIMEOUT(5, printk("sg_build_dir: alloc_kiovec res=%d\n", res));
	return 1;
    }
    res = map_user_kiobuf((SG_DXFER_TO_DEV == hp->dxfer_direction) ? 1 : 0,
			  schp->kiobp, (unsigned long)hp->dxferp, dxfer_len);
    if (0 != res) {
	SCSI_LOG_TIMEOUT(5,
		printk("sg_build_dir: map_user_kiobuf res=%d\n", res));
	sg_unmap_and(schp, 1);
	return 1;
    }
    schp->mapped = 1;
    kp = schp->kiobp;
    prev_addr = page_address(kp->maplist[0]);
    for (k = 1, split = 0; k < kp->nr_pages; ++k, prev_addr = addr) {
	addr = page_address(kp->maplist[k]);
	if ((prev_addr + PAGE_SIZE) != addr) {
	    split = k;
	    break;
	}
    }
    if (! split) {
	schp->k_use_sg = 0;
	schp->buffer = (void *)(page_address(kp->maplist[0]) + kp->offset);
	schp->bufflen = dxfer_len;
	schp->buffer_mem_src = SG_USER_MEM;
	schp->b_malloc_len = dxfer_len;
	hp->info |= SG_INFO_DIRECT_IO;
	return 0;
    }
    mx_sc_elems = sg_build_sgat(schp, sfp);
    if (mx_sc_elems <= 1) {
	sg_unmap_and(schp, 1);
	sg_remove_scat(schp);
	return 1;
    }
    mem_src_arr = schp->buffer + (mx_sc_elems * sizeof(struct scatterlist));
    for (k = 0, sclp = schp->buffer, rem_sz = dxfer_len;
	 (k < sg_tablesize) && (rem_sz > 0) && (k < mx_sc_elems);
	 ++k, ++sclp) {
	offset = (0 == k) ? kp->offset : 0;
	num = (rem_sz > (PAGE_SIZE - offset)) ? (PAGE_SIZE - offset) :
						rem_sz;
	sclp->address = (void *)(page_address(kp->maplist[k]) + offset);
	sclp->length = num;
	mem_src_arr[k] = SG_USER_MEM;
	rem_sz -= num;
	SCSI_LOG_TIMEOUT(5,
	    printk("sg_build_dir: k=%d, a=0x%p, len=%d, ms=%d\n",
	    k, sclp->address, num, mem_src_arr[k]));
    }
    schp->k_use_sg = k;
    SCSI_LOG_TIMEOUT(5,
	printk("sg_build_dir: k_use_sg=%d, rem_sz=%d\n", k, rem_sz));
    schp->bufflen = dxfer_len;
    if (rem_sz > 0) {   /* must have failed */
	sg_unmap_and(schp, 1);
	sg_remove_scat(schp);
	return 1;   /* out of scatter gather elements, try indirect */
    }
    hp->info |= SG_INFO_DIRECT_IO;
    return 0;
#else
    return 1;
#endif /* SG_ALLOW_DIO */
}

static int sg_build_indi(Sg_scatter_hold * schp, Sg_fd * sfp, int buff_size)
{
    int ret_sz, mem_src;
    int blk_size = buff_size;
    char * p = NULL;

    if ((blk_size < 0) || (! sfp))
        return -EFAULT;
    if (0 == blk_size)
        ++blk_size;             /* don't know why */
/* round request up to next highest SG_SECTOR_SZ byte boundary */
    blk_size = (blk_size + SG_SECTOR_MSK) & (~SG_SECTOR_MSK);
    SCSI_LOG_TIMEOUT(4, printk("sg_build_indi: buff_size=%d, blk_size=%d\n",
                               buff_size, blk_size));
    if (blk_size <= SG_SCATTER_SZ) {
        mem_src = SG_HEAP_PAGE;
        p = sg_malloc(sfp, blk_size, &ret_sz, &mem_src);
        if (! p)
            return -ENOMEM;
        if (blk_size == ret_sz) { /* got it on the first attempt */
	    schp->k_use_sg = 0;
            schp->buffer = p;
            schp->bufflen = blk_size;
	    schp->buffer_mem_src = (char)mem_src;
            schp->b_malloc_len = blk_size;
            return 0;
        }
    }
    else {
        mem_src = SG_HEAP_PAGE;
        p = sg_malloc(sfp, SG_SCATTER_SZ, &ret_sz, &mem_src);
        if (! p)
            return -ENOMEM;
    }
/* Want some local declarations, so start new block ... */
    {   /* lets try and build a scatter gather list */
        struct scatterlist * sclp;
	int k, rem_sz, num;
	int mx_sc_elems;
        int sg_tablesize = sfp->parentdp->sg_tablesize;
        int first = 1;
	char * mem_src_arr;

        /* N.B. ret_sz and mem_src carried into this block ... */
	mx_sc_elems = sg_build_sgat(schp, sfp);
	if (mx_sc_elems < 0)
	    return mx_sc_elems; /* most likely -ENOMEM */
	mem_src_arr = schp->buffer +
		      (mx_sc_elems * sizeof(struct scatterlist));

	for (k = 0, sclp = schp->buffer, rem_sz = blk_size;
	     (k < sg_tablesize) && (rem_sz > 0) && (k < mx_sc_elems);
             ++k, rem_sz -= ret_sz, ++sclp) {
	    if (first)
                first = 0;
            else {
                num = (rem_sz > SG_SCATTER_SZ) ? SG_SCATTER_SZ : rem_sz;
                mem_src = SG_HEAP_PAGE;
                p = sg_malloc(sfp, num, &ret_sz, &mem_src);
                if (! p)
                    break;
            }
            sclp->address = p;
            sclp->length = ret_sz;
	    mem_src_arr[k] = mem_src;

	    SCSI_LOG_TIMEOUT(5,
		printk("sg_build_build: k=%d, a=0x%p, len=%d, ms=%d\n",
                k, sclp->address, ret_sz, mem_src));
        } /* end of for loop */
	schp->k_use_sg = k;
	SCSI_LOG_TIMEOUT(5,
	    printk("sg_build_indi: k_use_sg=%d, rem_sz=%d\n", k, rem_sz));
        schp->bufflen = blk_size;
        if (rem_sz > 0)   /* must have failed */
            return -ENOMEM;
    }
    return 0;
}

static int sg_write_xfer(Sg_request * srp)
{
    sg_io_hdr_t * hp = &srp->header;
    Sg_scatter_hold * schp = &srp->data;
    int num_xfer = 0;
    int j, k, onum, usglen, ksglen, res, ok;
    int iovec_count = (int)hp->iovec_count;
    unsigned char * p;
    unsigned char * up;
    int new_interface = ('\0' == hp->interface_id) ? 0 : 1;

    if ((SG_DXFER_TO_DEV == hp->dxfer_direction) ||
	(SG_DXFER_TO_FROM_DEV == hp->dxfer_direction)) {
	num_xfer = (int)(new_interface ?  hp->dxfer_len : hp->flags);
	if (schp->bufflen < num_xfer)
	    num_xfer = schp->bufflen;
    }
    if ((num_xfer <= 0) || (new_interface && (SG_FLAG_NO_DXFER & hp->flags)))
	return 0;

    SCSI_LOG_TIMEOUT(4,
	 printk("sg_write_xfer: num_xfer=%d, iovec_count=%d, k_use_sg=%d\n",
		num_xfer, iovec_count, schp->k_use_sg));
    if (iovec_count) {
	onum = iovec_count;
	if ((k = verify_area(VERIFY_READ, hp->dxferp,
			     size_sg_iovec * onum)))
	    return k;
    }
    else
	onum = 1;

    if (0 == schp->k_use_sg) {  /* kernel has single buffer */
	if (SG_USER_MEM != schp->buffer_mem_src) { /* else nothing to do */

	    for (j = 0, p = schp->buffer; j < onum; ++j) {
		res = sg_u_iovec(hp, iovec_count, j, 1, &usglen, &up);
		if (res) return res;
		usglen = (num_xfer > usglen) ? usglen : num_xfer;
		__copy_from_user(p, up, usglen);
		p += usglen;
		num_xfer -= usglen;
		if (num_xfer <= 0)
		    return 0;
            }
	}
    }
    else {      /* kernel using scatter gather list */
	struct scatterlist * sclp = (struct scatterlist *)schp->buffer;
	char * mem_src_arr = sg_get_sgat_msa(schp);
	ksglen = (int)sclp->length;
	p = sclp->address;

	for (j = 0, k = 0; j < onum; ++j) {
	    res = sg_u_iovec(hp, iovec_count, j, 1, &usglen, &up);
	    if (res) return res;

	    for (; (k < schp->k_use_sg) && p;
		 ++k, ++sclp, ksglen = (int)sclp->length, p = sclp->address) {
		ok = (SG_USER_MEM != mem_src_arr[k]);
		if (usglen <= 0)
		    break;
		if (ksglen > usglen) {
		    if (usglen >= num_xfer) {
			if (ok) __copy_from_user(p, up, num_xfer);
			return 0;
		    }
		    if (ok) __copy_from_user(p, up, usglen);
		    p += usglen;
		    ksglen -= usglen;
                    break;
		}
		else {
		    if (ksglen >= num_xfer) {
			if (ok) __copy_from_user(p, up, num_xfer);
			return 0;
		    }
		    if (ok) __copy_from_user(p, up, ksglen);
		    up += ksglen;
		    usglen -= ksglen;
		}
            }
        }
    }
    return 0;
}

static int sg_u_iovec(sg_io_hdr_t * hp, int sg_num, int ind,
		      int wr_xf, int * countp, unsigned char ** up)
{
    int num_xfer = (int)hp->dxfer_len;
    unsigned char * p;
    int count, k;
    sg_iovec_t u_iovec;

    if (0 == sg_num) {
	p = (unsigned char *)hp->dxferp;
	if (wr_xf && ('\0' == hp->interface_id))
	    count = (int)hp->flags; /* holds "old" input_size */
	else
	    count = num_xfer;
    }
    else {
	__copy_from_user(&u_iovec,
			 (unsigned char *)hp->dxferp + (ind * size_sg_iovec),
			 size_sg_iovec);
	p = (unsigned char *)u_iovec.iov_base;
	count = (int)u_iovec.iov_len;
    }
    if ((k = verify_area(wr_xf ? VERIFY_READ : VERIFY_WRITE, p, count)))
	return k;
    if (up) *up = p;
    if (countp) *countp = count;
    return 0;
}

static char * sg_get_sgat_msa(Sg_scatter_hold * schp)
{
    int elem_sz = sizeof(struct scatterlist) + sizeof(char);
    int mx_sc_elems = (schp->sglist_len / elem_sz) - 1;
    return schp->buffer + (sizeof(struct scatterlist) * mx_sc_elems);
}

static void sg_remove_scat(Sg_scatter_hold * schp)
{
    SCSI_LOG_TIMEOUT(4, printk("sg_remove_scat: k_use_sg=%d\n",
			       schp->k_use_sg));
    if (schp->buffer && schp->sglist_len) {
        int k, mem_src;
        struct scatterlist * sclp = (struct scatterlist *)schp->buffer;
	char * mem_src_arr = sg_get_sgat_msa(schp);

	for (k = 0; (k < schp->k_use_sg) && sclp->address; ++k, ++sclp) {
	    mem_src = mem_src_arr[k];
	    SCSI_LOG_TIMEOUT(5,
		printk("sg_remove_scat: k=%d, a=0x%p, len=%d, ms=%d\n",
                       k, sclp->address, sclp->length, mem_src));
            sg_free(sclp->address, sclp->length, mem_src);
            sclp->address = NULL;
            sclp->length = 0;
        }
	sg_free(schp->buffer, schp->sglist_len, schp->buffer_mem_src);
    }
    else if (schp->buffer)
	sg_free(schp->buffer, schp->b_malloc_len, schp->buffer_mem_src);
    memset(schp, 0, sizeof(*schp));
}

static int sg_read_xfer(Sg_request * srp)
{
    sg_io_hdr_t * hp = &srp->header;
    Sg_scatter_hold * schp = &srp->data;
    int num_xfer = 0;
    int j, k, onum, usglen, ksglen, res, ok;
    int iovec_count = (int)hp->iovec_count;
    unsigned char * p;
    unsigned char * up;
    int new_interface = ('\0' == hp->interface_id) ? 0 : 1;

    if ((SG_DXFER_FROM_DEV == hp->dxfer_direction) ||
	(SG_DXFER_TO_FROM_DEV == hp->dxfer_direction)) {
	num_xfer =  hp->dxfer_len;
	if (schp->bufflen < num_xfer)
	    num_xfer = schp->bufflen;
    }
    if ((num_xfer <= 0) || (new_interface && (SG_FLAG_NO_DXFER & hp->flags)))
	return 0;

    SCSI_LOG_TIMEOUT(4,
	 printk("sg_read_xfer: num_xfer=%d, iovec_count=%d, k_use_sg=%d\n",
		num_xfer, iovec_count, schp->k_use_sg));
    if (iovec_count) {
	onum = iovec_count;
	if ((k = verify_area(VERIFY_READ, hp->dxferp,
			     size_sg_iovec * onum)))
	    return k;
    }
    else
	onum = 1;

    if (0 == schp->k_use_sg) {  /* kernel has single buffer */
	if (SG_USER_MEM != schp->buffer_mem_src) { /* else nothing to do */

	    for (j = 0, p = schp->buffer; j < onum; ++j) {
		res = sg_u_iovec(hp, iovec_count, j, 0, &usglen, &up);
		if (res) return res;
		usglen = (num_xfer > usglen) ? usglen : num_xfer;
		__copy_to_user(up, p, usglen);
		p += usglen;
		num_xfer -= usglen;
		if (num_xfer <= 0)
		    return 0;
	    }
	}
    }
    else {      /* kernel using scatter gather list */
	struct scatterlist * sclp = (struct scatterlist *)schp->buffer;
	char * mem_src_arr = sg_get_sgat_msa(schp);
	ksglen = (int)sclp->length;
	p = sclp->address;

	for (j = 0, k = 0; j < onum; ++j) {
	    res = sg_u_iovec(hp, iovec_count, j, 0, &usglen, &up);
	    if (res) return res;

	    for (; (k < schp->k_use_sg) && p;
		 ++k, ++sclp, ksglen = (int)sclp->length, p = sclp->address) {
		ok = (SG_USER_MEM != mem_src_arr[k]);
		if (usglen <= 0)
		    break;
		if (ksglen > usglen) {
		    if (usglen >= num_xfer) {
			if (ok) __copy_to_user(up, p, num_xfer);
			return 0;
		    }
		    if (ok) __copy_to_user(up, p, usglen);
		    p += usglen;
		    ksglen -= usglen;
		    break;
		}
		else {
		    if (ksglen >= num_xfer) {
			if (ok) __copy_to_user(up, p, num_xfer);
			return 0;
		    }
		    if (ok) __copy_to_user(up, p, ksglen);
		    up += ksglen;
		    usglen -= ksglen;
		}
	    }
	}
    }
    return 0;
}

static void sg_read_oxfer(Sg_request * srp, char * outp, int num_read_xfer)
{
    Sg_scatter_hold * schp = &srp->data;

    SCSI_LOG_TIMEOUT(4, printk("sg_read_oxfer: num_read_xfer=%d\n",
			       num_read_xfer));
    if ((! outp) || (num_read_xfer <= 0))
        return;
    if(schp->k_use_sg > 0) {
        int k, num;
        struct scatterlist * sclp = (struct scatterlist *)schp->buffer;

	for (k = 0; (k < schp->k_use_sg) && sclp->address; ++k, ++sclp) {
            num = (int)sclp->length;
            if (num > num_read_xfer) {
                __copy_to_user(outp, sclp->address, num_read_xfer);
                break;
            }
            else {
                __copy_to_user(outp, sclp->address, num);
                num_read_xfer -= num;
                if (num_read_xfer <= 0)
                    break;
                outp += num;
            }
        }
    }
    else
        __copy_to_user(outp, schp->buffer, num_read_xfer);
}

static void sg_build_reserve(Sg_fd * sfp, int req_size)
{
    Sg_scatter_hold * schp = &sfp->reserve;

    SCSI_LOG_TIMEOUT(4, printk("sg_build_reserve: req_size=%d\n", req_size));
    do {
        if (req_size < PAGE_SIZE)
            req_size = PAGE_SIZE;
	if (0 == sg_build_indi(schp, sfp, req_size))
            return;
        else
            sg_remove_scat(schp);
        req_size >>= 1; /* divide by 2 */
    } while (req_size >  (PAGE_SIZE / 2));
}

static void sg_link_reserve(Sg_fd * sfp, Sg_request * srp, int size)
{
    Sg_scatter_hold * req_schp = &srp->data;
    Sg_scatter_hold * rsv_schp = &sfp->reserve;

    SCSI_LOG_TIMEOUT(4, printk("sg_link_reserve: size=%d\n", size));
    if (rsv_schp->k_use_sg > 0) {
        int k, num;
        int rem = size;
        struct scatterlist * sclp = (struct scatterlist *)rsv_schp->buffer;

	for (k = 0; k < rsv_schp->k_use_sg; ++k, ++sclp) {
            num = (int)sclp->length;
            if (rem <= num) {
                sfp->save_scat_len = num;
                sclp->length = (unsigned)rem;
                break;
            }
            else
                rem -= num;
        }
	if (k < rsv_schp->k_use_sg) {
	    req_schp->k_use_sg = k + 1;   /* adjust scatter list length */
            req_schp->bufflen = size;
            req_schp->sglist_len = rsv_schp->sglist_len;
            req_schp->buffer = rsv_schp->buffer;
	    req_schp->buffer_mem_src = rsv_schp->buffer_mem_src;
            req_schp->b_malloc_len = rsv_schp->b_malloc_len;
        }
        else
	    SCSI_LOG_TIMEOUT(1, printk("sg_link_reserve: BAD size\n"));
    }
    else {
	req_schp->k_use_sg = 0;
        req_schp->bufflen = size;
        req_schp->buffer = rsv_schp->buffer;
	req_schp->buffer_mem_src = rsv_schp->buffer_mem_src;
	req_schp->k_use_sg = rsv_schp->k_use_sg;
        req_schp->b_malloc_len = rsv_schp->b_malloc_len;
    }
    srp->res_used = 1;
}

static void sg_unlink_reserve(Sg_fd * sfp, Sg_request * srp)
{
    Sg_scatter_hold * req_schp = &srp->data;
    Sg_scatter_hold * rsv_schp = &sfp->reserve;

    SCSI_LOG_TIMEOUT(4, printk("sg_unlink_reserve: req->k_use_sg=%d\n",
			       (int)req_schp->k_use_sg));
    if (rsv_schp->k_use_sg > 0) {
        struct scatterlist * sclp = (struct scatterlist *)rsv_schp->buffer;

	if (sfp->save_scat_len > 0)
	    (sclp + (req_schp->k_use_sg - 1))->length =
                                        (unsigned)sfp->save_scat_len;
        else
            SCSI_LOG_TIMEOUT(1, printk(
			"sg_unlink_reserve: BAD save_scat_len\n"));
    }
    req_schp->k_use_sg = 0;
    req_schp->bufflen = 0;
    req_schp->buffer = NULL;
    req_schp->sglist_len = 0;
    sfp->save_scat_len = 0;
    srp->res_used = 0;
}

static Sg_request * sg_get_request(const Sg_fd * sfp, int pack_id)
{
    Sg_request * resp = NULL;

    resp = sfp->headrp;
    while (resp) { /* look for requests that are ready + not SG_IO owned */
	if (resp->done && (! resp->sg_io_owned) &&
            ((-1 == pack_id) || (resp->header.pack_id == pack_id)))
            return resp;
        resp = resp->nextrp;
    }
    return resp;
}

/* always adds to end of list */
static Sg_request * sg_add_request(Sg_fd * sfp)
{
    int k;
    Sg_request * resp = NULL;
    Sg_request * rp;

    resp = sfp->headrp;
    rp = sfp->req_arr;
    if (! resp) {
        resp = rp;
        sfp->headrp = resp;
    }
    else {
        if (0 == sfp->cmd_q)
            resp = NULL;   /* command queuing disallowed */
        else {
            for (k = 0, rp; k < SG_MAX_QUEUE; ++k, ++rp) {
                if (! rp->parentfp)
                    break;
            }
            if (k < SG_MAX_QUEUE) {
                while (resp->nextrp) resp = resp->nextrp;
                resp->nextrp = rp;
                resp = rp;
            }
            else
                resp = NULL;
        }
    }
    if (resp) {
        resp->parentfp = sfp;
        resp->nextrp = NULL;
        resp->res_used = 0;
	resp->orphan = 0;
	resp->sg_io_owned = 0;
	resp->done = 0;
        memset(&resp->data, 0, sizeof(Sg_scatter_hold));
	memset(&resp->header, 0, size_sg_io_hdr);
	resp->header.duration = jiffies;
        resp->my_cmdp = NULL;
	resp->data.kiobp = NULL;
	resp->data.mapped = 0;
    }
    return resp;
}

/* Return of 1 for found; 0 for not found */
static int sg_remove_request(Sg_fd * sfp, Sg_request * srp)
{
    Sg_request * prev_rp;
    Sg_request * rp;

    if ((! sfp) || (! srp) || (! sfp->headrp))
        return 0;
    prev_rp = sfp->headrp;
    if (srp == prev_rp) {
        prev_rp->parentfp = NULL;
        sfp->headrp = prev_rp->nextrp;
        return 1;
    }
    while ((rp = prev_rp->nextrp)) {
        if (srp == rp) {
            rp->parentfp = NULL;
            prev_rp->nextrp = rp->nextrp;
            return 1;
        }
        prev_rp = rp;
    }
    return 0;
}

static Sg_fd * sg_add_sfp(Sg_device * sdp, int dev)
{
    Sg_fd * sfp;

    sfp = (Sg_fd *)sg_low_malloc(sizeof(Sg_fd), 0, SG_HEAP_KMAL, 0);
    if (! sfp)
        return NULL;
    memset(sfp, 0, sizeof(Sg_fd));
    sfp->fd_mem_src = SG_HEAP_KMAL;
    init_waitqueue_head(&sfp->read_wait);

    sfp->timeout = SG_DEFAULT_TIMEOUT;
    sfp->force_packid = SG_DEF_FORCE_PACK_ID;
    sfp->low_dma = (SG_DEF_FORCE_LOW_DMA == 0) ?
                   sdp->device->host->unchecked_isa_dma : 1;
    sfp->cmd_q = SG_DEF_COMMAND_Q;
    sfp->keep_orphan = SG_DEF_KEEP_ORPHAN;
    sfp->parentdp = sdp;
    if (! sdp->headfp)
        sdp->headfp = sfp;
    else {    /* add to tail of existing list */
        Sg_fd * pfp = sdp->headfp;
        while (pfp->nextfp)
           pfp = pfp->nextfp;
        pfp->nextfp = sfp;
    }
    SCSI_LOG_TIMEOUT(3, printk("sg_add_sfp: sfp=0x%p, m_s=%d\n",
			       sfp, (int)sfp->fd_mem_src));
    sg_build_reserve(sfp, sg_big_buff);
    SCSI_LOG_TIMEOUT(3, printk("sg_add_sfp:   bufflen=%d, k_use_sg=%d\n",
			   sfp->reserve.bufflen, sfp->reserve.k_use_sg));
    return sfp;
}

static int sg_remove_sfp(Sg_device * sdp, Sg_fd * sfp)
{
    Sg_request * srp;
    Sg_request * tsrp;
    int dirty = 0;
    int res = 0;

    srp = sfp->headrp;
    if (srp) {
/* Need to stop sg_cmd_done_bh() playing with this list during this loop */
        while (srp) {
            tsrp = srp->nextrp;
	    if (srp->done)
		sg_finish_rem_req(srp);
            else
                ++dirty;
            srp = tsrp;
        }
    }
    if (0 == dirty) {
        Sg_fd * fp;
        Sg_fd * prev_fp =  sdp->headfp;

        if (sfp == prev_fp)
            sdp->headfp = prev_fp->nextfp;
        else {
            while ((fp = prev_fp->nextfp)) {
                if (sfp == fp) {
                    prev_fp->nextfp = fp->nextfp;
                    break;
                }
                prev_fp = fp;
            }
        }
        if (sfp->reserve.bufflen > 0) {
SCSI_LOG_TIMEOUT(6, printk("sg_remove_sfp:    bufflen=%d, k_use_sg=%d\n",
		 (int)sfp->reserve.bufflen, (int)sfp->reserve.k_use_sg));
            sg_remove_scat(&sfp->reserve);
        }
        sfp->parentdp = NULL;
	SCSI_LOG_TIMEOUT(6, printk("sg_remove_sfp:    sfp=0x%p\n", sfp));
	sg_low_free((char *)sfp, sizeof(Sg_fd), sfp->fd_mem_src);
        res = 1;
    }
    else {
        sfp->closed = 1; /* flag dirty state on this fd */
        SCSI_LOG_TIMEOUT(1, printk(
          "sg_remove_sfp: worrisome, %d writes pending\n", dirty));
    }
    return res;
}

static int sg_res_in_use(const Sg_fd * sfp)
{
    const Sg_request * srp = sfp->headrp;

    while (srp) {
        if (srp->res_used)
            return 1;
        srp = srp->nextrp;
    }
    return 0;
}

static int sg_dio_in_use(const Sg_fd * sfp)
{
    const Sg_request * srp = sfp->headrp;

    while (srp) {
	if ((! srp->done) && srp->data.kiobp)
	    return 1;
	srp = srp->nextrp;
    }
    return 0;
}

/* If retSzp==NULL want exact size or fail */
/* sg_low_malloc() should always be called from a process context allowing
   GFP_KERNEL to be used instead of GFP_ATOMIC */
static char * sg_low_malloc(int rqSz, int lowDma, int mem_src, int * retSzp)
{
    char * resp = NULL;
    int page_mask = lowDma ? (GFP_KERNEL | GFP_DMA) : GFP_KERNEL;

    if (rqSz <= 0)
        return resp;
    if (SG_HEAP_KMAL == mem_src) {
        page_mask = lowDma ? (GFP_ATOMIC | GFP_DMA) : GFP_ATOMIC;
        /* Seen kmalloc(..,GFP_KERNEL) hang for 40 secs! */
        resp = kmalloc(rqSz, page_mask);
        if (resp && retSzp) *retSzp = rqSz;
        return resp;
    }
    if (SG_HEAP_POOL == mem_src) {
        int num_sect = rqSz / SG_SECTOR_SZ;

        if (0 != (rqSz & SG_SECTOR_MSK)) {
            if (! retSzp)
                return resp;
            ++num_sect;
            rqSz = num_sect * SG_SECTOR_SZ;
        }
        while (num_sect > 0) {
            if ((num_sect <= sg_pool_secs_avail) &&
                (scsi_dma_free_sectors > (SG_LOW_POOL_THRESHHOLD + num_sect))) {
                resp = scsi_malloc(rqSz);
                if (resp) {
                    if (retSzp) *retSzp = rqSz;
                    sg_pool_secs_avail -= num_sect;
                    return resp;
                }
            }
            if (! retSzp)
                return resp;
            num_sect /= 2;      /* try half as many */
            rqSz = num_sect * SG_SECTOR_SZ;
        }
    }
    else if (SG_HEAP_PAGE == mem_src) {
        int order, a_size;
        int resSz = rqSz;

        for (order = 0, a_size = PAGE_SIZE;
             a_size < rqSz; order++, a_size <<= 1)
            ;
        resp = (char *)__get_free_pages(page_mask, order);
        while ((! resp) && order && retSzp) {
            --order;
            a_size >>= 1;   /* divide by 2, until PAGE_SIZE */
            resp = (char *)__get_free_pages(page_mask, order); /* try half */
            resSz = a_size;
        }
        if (retSzp) *retSzp = resSz;
    }
    else
        printk("sg_low_malloc: bad mem_src=%d, rqSz=%df\n", mem_src, rqSz);
    return resp;
}

static char * sg_malloc(const Sg_fd * sfp, int size, int * retSzp,
                        int * mem_srcp)
{
    char * resp = NULL;

    if (retSzp) *retSzp = size;
    if (size <= 0)
        ;
    else {
        int low_dma = sfp->low_dma;
        int l_ms = -1;  /* invalid value */

	switch (*mem_srcp)
        {
        case SG_HEAP_PAGE:
            l_ms = (size < PAGE_SIZE) ? SG_HEAP_POOL : SG_HEAP_PAGE;
            resp = sg_low_malloc(size, low_dma, l_ms, 0);
            if (resp)
                break;
            resp = sg_low_malloc(size, low_dma, l_ms, &size);
            if (! resp) {
                l_ms = (SG_HEAP_POOL == l_ms) ? SG_HEAP_PAGE : SG_HEAP_POOL;
                resp = sg_low_malloc(size, low_dma, l_ms, &size);
                if (! resp) {
                    l_ms = SG_HEAP_KMAL;
                    resp = sg_low_malloc(size, low_dma, l_ms, &size);
                }
            }
            if (resp && retSzp) *retSzp = size;
            break;
        case SG_HEAP_KMAL:
            l_ms = SG_HEAP_PAGE;
            resp = sg_low_malloc(size, low_dma, l_ms, 0);
            if (resp)
                break;
            l_ms = SG_HEAP_POOL;
            resp = sg_low_malloc(size, low_dma, l_ms, &size);
            if (resp && retSzp) *retSzp = size;
            break;
        default:
            SCSI_LOG_TIMEOUT(1, printk("sg_malloc: bad ms=%d\n", *mem_srcp));
            break;
        }
        if (resp) *mem_srcp = l_ms;
    }
    SCSI_LOG_TIMEOUT(6, printk("sg_malloc: size=%d, ms=%d, ret=0x%p\n",
                               size, *mem_srcp, resp));
    return resp;
}

static void sg_low_free(char * buff, int size, int mem_src)
{
    if (! buff) return;
    switch (mem_src) {
    case SG_HEAP_POOL:
	{
	    int num_sect = size / SG_SECTOR_SZ;

	    scsi_free(buff, size);
	    sg_pool_secs_avail += num_sect;
	}
	break;
    case SG_HEAP_KMAL:
	kfree(buff);    /* size not used */
	break;
    case SG_HEAP_PAGE:
	{
	    int order, a_size;
	    for (order = 0, a_size = PAGE_SIZE;
		 a_size < size; order++, a_size <<= 1)
		;
	    free_pages((unsigned long)buff, order);
	}
	break;
    case SG_USER_MEM:
	break; /* nothing to do */
    default:
	printk("sg_low_free: bad mem_src=%d, buff=0x%p, rqSz=%df\n",
               mem_src, buff, size);
	break;
    }
}

static void sg_free(char * buff, int size, int mem_src)
{
    SCSI_LOG_TIMEOUT(6,
        printk("sg_free: buff=0x%p, size=%d\n", buff, size));
    if ((! buff) || (size <= 0))
        ;
    else
        sg_low_free(buff, size, mem_src);
}

static void sg_clr_scpnt(Scsi_Cmnd * SCpnt)
{
    SCpnt->use_sg = 0;
    SCpnt->sglist_len = 0;
    SCpnt->bufflen = 0;
    SCpnt->buffer = NULL;
    SCpnt->underflow = 0;
    SCpnt->request.rq_dev = MKDEV(0, 0);  /* "sg" _disowns_ command blk */
}

static int sg_ms_to_jif(unsigned int msecs)
{
    if ((UINT_MAX / 2U) < msecs)
	return INT_MAX;      /* special case, set largest possible */
    else
	return ((int)msecs < (INT_MAX / 1000)) ? (((int)msecs * HZ) / 1000)
					       : (((int)msecs / 1000) * HZ);
}

static unsigned sg_jif_to_ms(int jifs)
{
    if (jifs <= 0)
	return 0U;
    else {
	unsigned int j = (unsigned int)jifs;
	return (j < (UINT_MAX / 1000)) ? ((j * 1000) / HZ) : ((j / HZ) * 1000);
    }
}

static unsigned char allow_ops[] = {TEST_UNIT_READY, INQUIRY,
READ_CAPACITY, READ_BUFFER, READ_6, READ_10, READ_12};

static int sg_allow_access(unsigned char opcode, char dev_type)
{
    int k;

    if (TYPE_SCANNER == dev_type) /* TYPE_ROM maybe burner */
	return 1;
    for (k = 0; k < sizeof(allow_ops); ++k) {
	if (opcode == allow_ops[k])
	    return 1;
    }
    return 0;
}


static int sg_last_dev()
{
    int k;
    for (k = sg_template.dev_max - 1; k >= 0; --k) {
	if (sg_dev_arr[k].device)
	    return k + 1;
    }
    return 0;   /* origin 1 */
}

#ifdef CONFIG_PROC_FS

static struct proc_dir_entry * sg_proc_sgp = NULL;

static const char * sg_proc_sg_dirname = "sg";
static const char * sg_proc_leaf_names[] = {"def_reserved_size", "debug",
			    "devices", "device_hdr", "device_strs",
			    "hosts", "host_hdr", "host_strs", "version"};

static int sg_proc_dressz_read(char * buffer, char ** start, off_t offset,
			       int size, int * eof, void * data);
static int sg_proc_dressz_info(char * buffer, int * len, off_t * begin,
			       off_t offset, int size);
static int sg_proc_dressz_write(struct file * filp, const char * buffer,
				unsigned long count, void * data);
static int sg_proc_debug_read(char * buffer, char ** start, off_t offset,
			      int size, int * eof, void * data);
static int sg_proc_debug_info(char * buffer, int * len, off_t * begin,
			      off_t offset, int size);
static int sg_proc_dev_read(char * buffer, char ** start, off_t offset,
			    int size, int * eof, void * data);
static int sg_proc_dev_info(char * buffer, int * len, off_t * begin,
			    off_t offset, int size);
static int sg_proc_devhdr_read(char * buffer, char ** start, off_t offset,
			       int size, int * eof, void * data);
static int sg_proc_devhdr_info(char * buffer, int * len, off_t * begin,
			       off_t offset, int size);
static int sg_proc_devstrs_read(char * buffer, char ** start, off_t offset,
				int size, int * eof, void * data);
static int sg_proc_devstrs_info(char * buffer, int * len, off_t * begin,
				off_t offset, int size);
static int sg_proc_host_read(char * buffer, char ** start, off_t offset,
			     int size, int * eof, void * data);
static int sg_proc_host_info(char * buffer, int * len, off_t * begin,
			     off_t offset, int size);
static int sg_proc_hosthdr_read(char * buffer, char ** start, off_t offset,
				int size, int * eof, void * data);
static int sg_proc_hosthdr_info(char * buffer, int * len, off_t * begin,
				off_t offset, int size);
static int sg_proc_hoststrs_read(char * buffer, char ** start, off_t offset,
				 int size, int * eof, void * data);
static int sg_proc_hoststrs_info(char * buffer, int * len, off_t * begin,
				 off_t offset, int size);
static int sg_proc_version_read(char * buffer, char ** start, off_t offset,
				int size, int * eof, void * data);
static int sg_proc_version_info(char * buffer, int * len, off_t * begin,
				off_t offset, int size);
static read_proc_t * sg_proc_leaf_reads[] = {
	     sg_proc_dressz_read, sg_proc_debug_read,
	     sg_proc_dev_read, sg_proc_devhdr_read, sg_proc_devstrs_read,
	     sg_proc_host_read, sg_proc_hosthdr_read, sg_proc_hoststrs_read,
	     sg_proc_version_read};
static write_proc_t * sg_proc_leaf_writes[] = {
	     sg_proc_dressz_write, 0, 0, 0, 0, 0, 0, 0, 0};

#define PRINT_PROC(fmt,args...)                                 \
    do {                                                        \
	*len += sprintf(buffer + *len, fmt, ##args);            \
	if (*begin + *len > offset + size)                      \
	    return 0;                                           \
	if (*begin + *len < offset) {                           \
	    *begin += *len;                                     \
	    *len = 0;                                           \
	}                                                       \
    } while(0)

#define SG_PROC_READ_FN(infofp)                                 \
    do {                                                        \
	int len = 0;                                            \
	off_t begin = 0;                                        \
	*eof = infofp(buffer, &len, &begin, offset, size);      \
	if (offset >= (begin + len))                            \
	    return 0;                                           \
	*start = buffer + ((begin > offset) ?                   \
			(begin - offset) : (offset - begin));   \
	return (size < (begin + len - offset)) ?                \
				size : begin + len - offset;    \
    } while(0)


static int sg_proc_init()
{
    int k, mask;
    int leaves = sizeof(sg_proc_leaf_names) / sizeof(sg_proc_leaf_names[0]);
    struct proc_dir_entry * pdep;

    if (! proc_scsi)
	return 1;
    sg_proc_sgp = create_proc_entry(sg_proc_sg_dirname,
				    S_IFDIR | S_IRUGO | S_IXUGO, proc_scsi);
    if (! sg_proc_sgp)
	return 1;
    for (k = 0; k < leaves; ++k) {
	mask = sg_proc_leaf_writes[k] ? S_IRUGO | S_IWUSR : S_IRUGO;
	pdep = create_proc_entry(sg_proc_leaf_names[k], mask, sg_proc_sgp);
	if (pdep) {
	    pdep->read_proc = sg_proc_leaf_reads[k];
	    if (sg_proc_leaf_writes[k])
		pdep->write_proc = sg_proc_leaf_writes[k];
	}
    }
    return 0;
}

static void sg_proc_cleanup()
{
    int k;
    int leaves = sizeof(sg_proc_leaf_names) / sizeof(sg_proc_leaf_names[0]);

    if ((! proc_scsi) || (! sg_proc_sgp))
	return;
    for (k = 0; k < leaves; ++k)
	remove_proc_entry(sg_proc_leaf_names[k], sg_proc_sgp);
    remove_proc_entry(sg_proc_sg_dirname, proc_scsi);
}

static int sg_proc_dressz_read(char * buffer, char ** start, off_t offset,
			       int size, int * eof, void * data)
{ SG_PROC_READ_FN(sg_proc_dressz_info); }

static int sg_proc_dressz_info(char * buffer, int * len, off_t * begin,
			       off_t offset, int size)
{
    PRINT_PROC("%d\n", sg_big_buff);
    return 1;
}

static int sg_proc_dressz_write(struct file * filp, const char * buffer,
				unsigned long count, void * data)
{
    int num;
    unsigned long k = ULONG_MAX;
    char buff[11];

    if (! capable(CAP_SYS_ADMIN))
	return -EACCES;
    num = (count < 10) ? count : 10;
    copy_from_user(buff, buffer, num);
    buff[count] = '\0';
    k = simple_strtoul(buff, 0, 10);
    if (k <= 1048576) {
	sg_big_buff = k;
	return count;
    }
    return -ERANGE;
}

static int sg_proc_debug_read(char * buffer, char ** start, off_t offset,
			      int size, int * eof, void * data)
{ SG_PROC_READ_FN(sg_proc_debug_info); }

static int sg_proc_debug_info(char * buffer, int * len, off_t * begin,
			      off_t offset, int size)
{
    const Sg_device * sdp = sg_dev_arr;
    const sg_io_hdr_t * hp;
    int j, max_dev;

    if (NULL == sg_dev_arr) {
	PRINT_PROC("sg_dev_arr NULL, death is imminent\n");
	return 1;
    }
    max_dev = sg_last_dev();
    PRINT_PROC("dev_max=%d max_active_device=%d (origin 1)\n",
	       sg_template.dev_max, max_dev);
    PRINT_PROC(" scsi_dma_free_sectors=%u sg_pool_secs_aval=%d "
	       "def_reserved_size=%d\n",
	       scsi_dma_free_sectors, sg_pool_secs_avail, sg_big_buff);
    max_dev = sg_last_dev();
    for (j = 0; j < max_dev; ++j, ++sdp) {
	if (sdp) {
	    Sg_fd * fp;
	    Sg_request * srp;
	    struct scsi_device * scsidp;
	    int dev, k, blen, usg, crep;

	    if (! (scsidp = sdp->device)) {
		PRINT_PROC("device %d detached ??\n", j);
		continue;
	    }
	    dev = MINOR(sdp->i_rdev);
	    crep = 'a' + dev;

	    PRINT_PROC(" >>> device=%d(sg%c) ", dev, crep > 126 ? '?' : crep);
	    PRINT_PROC("scsi%d chan=%d id=%d lun=%d   em=%d sg_tablesize=%d"
		       " excl=%d\n", scsidp->host->host_no, scsidp->channel,
		       scsidp->id, scsidp->lun, scsidp->host->hostt->emulated,
		       sdp->sg_tablesize, sdp->exclude);
	    fp = sdp->headfp;
	    for (k = 1; fp; fp = fp->nextfp, ++k) {
		PRINT_PROC("   FD(%d): timeout=%d bufflen=%d "
			   "(res)sgat=%d low_dma=%d\n",
			   k, fp->timeout, fp->reserve.bufflen,
			   (int)fp->reserve.k_use_sg, (int)fp->low_dma);
		PRINT_PROC("   cmd_q=%d f_packid=%d k_orphan=%d closed=%d\n",
			   (int)fp->cmd_q, (int)fp->force_packid,
			   (int)fp->keep_orphan, (int)fp->closed);
		srp = fp->headrp;
		if (NULL == srp)
		    PRINT_PROC("     No requests active\n");
		while (srp) {
		    hp = &srp->header;
/* stop indenting so far ... */
	PRINT_PROC(srp->res_used ? "     reserved_buff>> " :
	    ((SG_INFO_DIRECT_IO_MASK & hp->info) ? "     dio>> " : "     "));
	blen = srp->my_cmdp ? srp->my_cmdp->bufflen : srp->data.bufflen;
	usg = srp->my_cmdp ? srp->my_cmdp->use_sg : srp->data.k_use_sg;
	PRINT_PROC(srp->done ? "rcv: id=%d" : (srp->my_cmdp ? "act: id=%d" :
		    "prior: id=%d"), srp->header.pack_id);
	if (! srp->res_used) PRINT_PROC(" blen=%d", blen);
	if (srp->done)
	    PRINT_PROC(" dur=%d", sg_jif_to_ms(hp->duration));
	else
	    PRINT_PROC(" t_o/elap=%d/%d", ((hp->interface_id == '\0') ?
			sg_jif_to_ms(fp->timeout) : hp->timeout),
		  sg_jif_to_ms(hp->duration ? (jiffies - hp->duration) : 0));
	PRINT_PROC(" sgat=%d op=0x%02x\n", usg, (int)srp->data.cmd_opcode);
	srp = srp->nextrp;
/* reset indenting */
		}
	    }
	}
    }
    return 1;
}

static int sg_proc_dev_read(char * buffer, char ** start, off_t offset,
			    int size, int * eof, void * data)
{ SG_PROC_READ_FN(sg_proc_dev_info); }

static int sg_proc_dev_info(char * buffer, int * len, off_t * begin,
			    off_t offset, int size)
{
    const Sg_device * sdp = sg_dev_arr;
    int j, max_dev;
    struct scsi_device * scsidp;

    max_dev = sg_last_dev();
    for (j = 0; j < max_dev; ++j, ++sdp) {
	if (sdp) {
	    if (! (scsidp = sdp->device)) {
		PRINT_PROC("-1\t-1\t-1\t-1\t-1\t-1\t-1\t-1\n");
		continue;
	    }
	    PRINT_PROC("%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
	       scsidp->host->host_no, scsidp->channel, scsidp->id,
	       scsidp->lun, (int)scsidp->type, (int)scsidp->disconnect,
	       (int)scsidp->queue_depth, (int)scsidp->tagged_queue);
	}
    }
    return 1;
}

static int sg_proc_devhdr_read(char * buffer, char ** start, off_t offset,
			       int size, int * eof, void * data)
{ SG_PROC_READ_FN(sg_proc_devhdr_info); }

static int sg_proc_devhdr_info(char * buffer, int * len, off_t * begin,
			       off_t offset, int size)
{
    PRINT_PROC("host\tchan\tid\tlun\ttype\tdiscon\tqdepth\ttq\n");
    return 1;
}

static int sg_proc_devstrs_read(char * buffer, char ** start, off_t offset,
				int size, int * eof, void * data)
{ SG_PROC_READ_FN(sg_proc_devstrs_info); }

static int sg_proc_devstrs_info(char * buffer, int * len, off_t * begin,
				off_t offset, int size)
{
    const Sg_device * sdp = sg_dev_arr;
    int j, max_dev;
    struct scsi_device * scsidp;

    max_dev = sg_last_dev();
    for (j = 0; j < max_dev; ++j, ++sdp) {
	if (sdp) {
	    if ((scsidp = sdp->device))
		PRINT_PROC("%8.8s\t%16.16s\t%4.4s\n",
			   scsidp->vendor, scsidp->model, scsidp->rev);
	    else
		PRINT_PROC("<no active device>\n");
	}
    }
    return 1;
}

static int sg_proc_host_read(char * buffer, char ** start, off_t offset,
			     int size, int * eof, void * data)
{ SG_PROC_READ_FN(sg_proc_host_info); }

static int sg_proc_host_info(char * buffer, int * len, off_t * begin,
			     off_t offset, int size)
{
    struct Scsi_Host * shp;

    for (shp = scsi_hostlist; shp; shp = shp->next)
	PRINT_PROC("%u\t%hu\t%hd\t%hu\t%d\t%d\n",
		   shp->unique_id, shp->host_busy, shp->cmd_per_lun,
		   shp->sg_tablesize, (int)shp->unchecked_isa_dma,
		   (int)shp->hostt->emulated);
    return 1;
}

static int sg_proc_hosthdr_read(char * buffer, char ** start, off_t offset,
				int size, int * eof, void * data)
{ SG_PROC_READ_FN(sg_proc_hosthdr_info); }

static int sg_proc_hosthdr_info(char * buffer, int * len, off_t * begin,
				off_t offset, int size)
{
    PRINT_PROC("uid\tbusy\tcpl\tscatg\tisa\temul\n");
    return 1;
}

static int sg_proc_hoststrs_read(char * buffer, char ** start, off_t offset,
				 int size, int * eof, void * data)
{ SG_PROC_READ_FN(sg_proc_hoststrs_info); }

static int sg_proc_hoststrs_info(char * buffer, int * len, off_t * begin,
				 off_t offset, int size)
{
    struct Scsi_Host * shp;

    for (shp = scsi_hostlist; shp; shp = shp->next)
	PRINT_PROC("%s\n", shp->hostt->info ? shp->hostt->info(shp) :
		    (shp->hostt->name ? shp->hostt->name : "<no name>"));
    return 1;
}

static int sg_proc_version_read(char * buffer, char ** start, off_t offset,
				int size, int * eof, void * data)
{ SG_PROC_READ_FN(sg_proc_version_info); }

static int sg_proc_version_info(char * buffer, int * len, off_t * begin,
				off_t offset, int size)
{
    PRINT_PROC("%d\t%s\n", sg_version_num, sg_version_str);
    return 1;
}
#endif  /* CONFIG_PROC_FS */
