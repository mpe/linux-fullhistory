/*
 *  History:
 *  Started: Aug 9 by Lawrence Foard (entropy@world.std.com),
 *           to allow user process control of SCSI devices.
 *  Development Sponsored by Killy Corp. NY NY
 *
 * Original driver (sg.c):
 *        Copyright (C) 1992 Lawrence Foard
 * 2.x extensions to driver:
 *        Copyright (C) 1998, 1999 Douglas Gilbert
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 *  Borrows code from st driver. Thanks to Alessandro Rubini's "dd" book.
 */
 static char * sg_version_str = "Version: 2.3.35 (990708)";
 static int sg_version_num = 20335; /* 2 digits for each component */
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
 *        To dump the state of sg's data structures get the 'sg_debug'
 *        program from the utilities and enter:
 *          # sg_debug /dev/sga 
 *        or any valid sg device name. The state of _all_ sg devices
 *        will be sent to the console and the log.
 *
 *      - The 'alt_address' field in the scatter_list structure and the
 *        related 'mem_src' indicate the source of the heap allocation.
 *
 */
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
#include <linux/poll.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/blk.h>
#include "scsi.h"
#include "hosts.h"
#include <scsi/scsi_ioctl.h>
#include <scsi/sg.h>

static spinlock_t sg_request_lock = SPIN_LOCK_UNLOCKED;

int sg_big_buff = SG_DEF_RESERVED_SIZE; /* sg_big_buff is ro through sysctl */
/* N.B. This global is here to keep existing software happy. It now holds
   the size of the reserve buffer of the most recent sucessful sg_open(). 
   Only available when 'sg' compiled into kernel (rather than a module). 
   This is deprecated (use SG_GET_RESERVED_SIZE ioctl() instead). */

#define SG_SECTOR_SZ 512
#define SG_SECTOR_MSK (SG_SECTOR_SZ - 1)

#define SG_LOW_POOL_THRESHHOLD 30
#define SG_MAX_POOL_SECTORS 320  /* Max. number of pool sectors to take */

static int sg_pool_secs_avail = SG_MAX_POOL_SECTORS;

/* #define SG_DEBUG */  /* for counting varieties of allocations */

#ifdef SG_DEBUG
static int sg_num_kmal = 0;
static int sg_num_pool = 0;
static int sg_num_page = 0;
#endif

#define SG_HEAP_PAGE 1  /* heap from kernel via get_free_pages() */
#define SG_HEAP_KMAL 2  /* heap from kernel via kmalloc() */
#define SG_HEAP_POOL 3  /* heap from scsi dma pool (mid-level) */


static int sg_init(void);
static int sg_attach(Scsi_Device *);
static void sg_finish(void);
static int sg_detect(Scsi_Device *);
static void sg_detach(Scsi_Device *);


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


typedef struct sg_scatter_hold  /* holding area for scsi scatter gather info */
{
    unsigned short use_sg;      /* Number of pieces of scatter-gather */
    unsigned short sglist_len;  /* size of malloc'd scatter-gather list */
    unsigned bufflen;           /* Size of (aggregate) data buffer */
    unsigned b_malloc_len;      /* actual len malloc'ed in buffer */
    void * buffer;              /* Data buffer or scatter list,12 bytes each*/
    char mem_src;               /* heap whereabouts of 'buffer' */
} Sg_scatter_hold;    /* 20 bytes long on i386 */

struct sg_device;               /* forward declarations */
struct sg_fd;

typedef struct sg_request  /* SG_MAX_QUEUE requests outstanding per file */
{
    Scsi_Cmnd * my_cmdp;        /* NULL -> ready to read, else id */
    struct sg_request * nextrp; /* NULL -> tail request (slist) */
    struct sg_fd * parentfp;    /* NULL -> not in use */
    Sg_scatter_hold data;       /* hold buffer, perhaps scatter list */
    struct sg_header header;    /* scsi command+info, see <scsi/sg.h> */
    char res_used;              /* 1 -> using reserve buffer, 0 -> not ... */
} Sg_request; /* 72 bytes long on i386 */

typedef struct sg_fd /* holds the state of a file descriptor */
{
    struct sg_fd * nextfp; /* NULL when last opened fd on this device */
    struct sg_device * parentdp;     /* owning device */
    wait_queue_head_t read_wait;     /* queue read until command done */
    wait_queue_head_t write_wait;    /* write waits on pending read */
    int timeout;                     /* defaults to SG_DEFAULT_TIMEOUT */
    Sg_scatter_hold reserve;  /* buffer held for this file descriptor */
    unsigned save_scat_len;   /* original length of trunc. scat. element */
    Sg_request * headrp;      /* head of request slist, NULL->empty */
    struct fasync_struct * async_qp; /* used by asynchronous notification */
    Sg_request req_arr[SG_MAX_QUEUE]; /* used as singly-linked list */
    char low_dma;       /* as in parent but possibly overridden to 1 */
    char force_packid;  /* 1 -> pack_id input to read(), 0 -> ignored */
    char closed;        /* 1 -> fd closed but request(s) outstanding */
    char my_mem_src;    /* heap whereabouts of this Sg_fd object */
    char cmd_q;         /* 1 -> allow command queuing, 0 -> don't */
    char underrun_flag; /* 1 -> flag underruns, 0 -> don't, 2 -> test */
    char next_cmd_len;  /* 0 -> automatic (def), >0 -> use on next write() */
} Sg_fd; /* 1212 bytes long on i386 */

typedef struct sg_device /* holds the state of each scsi generic device */
{
    Scsi_Device * device;
    wait_queue_head_t o_excl_wait; /* queue open() when O_EXCL in use */
    int sg_tablesize;   /* adapter's max scatter-gather table size */
    Sg_fd * headfp;     /* first open fd belonging to this device */
    kdev_t i_rdev;      /* holds device major+minor number */
    char exclude;       /* opened for exclusive access */
    char sgdebug;       /* 0->off, 1->sense, 9->dump dev, 10-> all devs */
    unsigned char merge_fd; /* 0->sequencing per fd, else fd count */
} Sg_device; /* 24 bytes long on i386 */


static int sg_fasync(int fd, struct file * filp, int mode);
static void sg_command_done(Scsi_Cmnd * SCpnt);
static int sg_start_req(Sg_request * srp, int max_buff_size,
                        const char * inp, int num_write_xfer);
static void sg_finish_rem_req(Sg_request * srp, char * outp,
                              int num_read_xfer);
static int sg_build_scat(Sg_scatter_hold * schp, int buff_size, 
                         const Sg_fd * sfp);
static void sg_write_xfer(Sg_scatter_hold * schp, const char * inp, 
                          int num_write_xfer);
static void sg_remove_scat(Sg_scatter_hold * schp);
static void sg_read_xfer(Sg_scatter_hold * schp, char * outp,
                         int num_read_xfer);
static void sg_build_reserve(Sg_fd * sfp, int req_size);
static void sg_link_reserve(Sg_fd * sfp, Sg_request * srp, int size);
static void sg_unlink_reserve(Sg_fd * sfp, Sg_request * srp);
static char * sg_malloc(const Sg_fd * sfp, int size, int * retSzp, 
                        int * mem_srcp);
static void sg_free(char * buff, int size, int mem_src);
static char * sg_low_malloc(int rqSz, int lowDma, int mem_src, 
                            int * retSzp);
static void sg_low_free(char * buff, int size, int mem_src);
static Sg_fd * sg_add_sfp(Sg_device * sdp, int dev, int get_reserved);
static int sg_remove_sfp(Sg_device * sdp, Sg_fd * sfp);
static Sg_request * sg_get_request(const Sg_fd * sfp, int pack_id);
static Sg_request * sg_add_request(Sg_fd * sfp);
static int sg_remove_request(Sg_fd * sfp, const Sg_request * srp);
static int sg_res_in_use(const Sg_fd * sfp);
static void sg_clr_scpnt(Scsi_Cmnd * SCpnt);
static void sg_shorten_timeout(Scsi_Cmnd * scpnt);
static void sg_debug(const Sg_device * sdp, const Sg_fd * sfp, int part_of);
static void sg_debug_all(const Sg_fd * sfp);

static Sg_device * sg_dev_arr = NULL;
static const int size_sg_header = sizeof(struct sg_header);


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
        sdp->merge_fd = 0;   /* A little tricky if SG_DEF_MERGE_FD set */
    }
    if ((sfp = sg_add_sfp(sdp, dev, O_RDWR == (flags & O_ACCMODE)))) {
        filp->private_data = sfp;
#if SG_DEF_MERGE_FD
        if (0 == sdp->merge_fd)
            sdp->merge_fd = 1;
#endif
    }
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
    if (! sdp->headfp) {
        filp->private_data = NULL;
        sdp->merge_fd = 0;
    }

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
    struct sg_header * shp = (struct sg_header *)buf;

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
    if (sfp->force_packid && (count >= size_sg_header))
        req_pack_id = shp->pack_id;
    srp = sg_get_request(sfp, req_pack_id);
    if (! srp) { /* now wait on packet to arrive */
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN;
        res = 0;  /* following is a macro that beats race condition */
        __wait_event_interruptible(sfp->read_wait, 
                                   (srp = sg_get_request(sfp, req_pack_id)),
                                   res);
        if (res)
            return res; /* -ERESTARTSYS because signal hit process */
    }
    if (2 != sfp->underrun_flag)
        srp->header.pack_len = srp->header.reply_len;   /* Why ????? */

    /* Now copy the result back to the user buffer.  */
    if (count >= size_sg_header) {
        __copy_to_user(buf, &srp->header, size_sg_header);
        buf += size_sg_header;
        if (count > srp->header.reply_len)
            count = srp->header.reply_len;
        if (count > size_sg_header) /* release does copy_to_user */
            sg_finish_rem_req(srp, buf, count - size_sg_header);
        else
            sg_finish_rem_req(srp, NULL, 0);
    }
    else {
        count = (srp->header.result == 0) ? 0 : -EIO;
        sg_finish_rem_req(srp, NULL, 0);
    }
    if (! sfp->cmd_q)
        wake_up_interruptible(&sfp->write_wait);
    return count;
}

static ssize_t sg_write(struct file * filp, const char * buf, 
                        size_t count, loff_t *ppos)
{
    int                   mxsize, cmd_size, k;
    unsigned char         cmnd[MAX_COMMAND_SIZE];
    int                   input_size;
    unsigned char         opcode;
    Scsi_Cmnd           * SCpnt;
    Sg_device           * sdp;
    Sg_fd               * sfp;
    Sg_request          * srp;

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
    if (count < (size_sg_header + 6))
        return -EIO;   /* The minimum scsi command length is 6 bytes. */ 

    if (! (srp = sg_add_request(sfp))) {
        if (sfp->cmd_q) {
            SCSI_LOG_TIMEOUT(1, printk("sg_write: queue full\n"));
            return -EDOM;
        }
        else { /* old semantics: wait for pending read() to finish */
            if (filp->f_flags & O_NONBLOCK)
                return -EAGAIN;
            k = 0;
            __wait_event_interruptible(sfp->write_wait, 
                                   (srp = sg_add_request(sfp)),
                                   k);
            if (k)
                return k; /* -ERESTARTSYS because signal hit process */
        }
    }
    __copy_from_user(&srp->header, buf, size_sg_header); 
    buf += size_sg_header;
    srp->header.pack_len = count;
    __get_user(opcode, buf);
    if (sfp->next_cmd_len > 0) {
        if (sfp->next_cmd_len > MAX_COMMAND_SIZE) {
            SCSI_LOG_TIMEOUT(1, printk("sg_write: command length too long\n"));
            sfp->next_cmd_len = 0;
            return -EIO;
        }
        cmd_size = sfp->next_cmd_len;
        sfp->next_cmd_len = 0; /* reset so only this write() effected */
    }
    else {
        cmd_size = COMMAND_SIZE(opcode); /* based on SCSI command group */
        if ((opcode >= 0xc0) && srp->header.twelve_byte) 
            cmd_size = 12;
    }
    SCSI_LOG_TIMEOUT(4, printk("sg_write:   scsi opcode=0x%02x, cmd_size=%d\n", 
                               (int)opcode, cmd_size));
/* Determine buffer size.  */
    input_size = count - cmd_size;
    mxsize = (input_size > srp->header.reply_len) ? input_size :
                                                    srp->header.reply_len;
    mxsize -= size_sg_header;
    input_size -= size_sg_header;
    if (input_size < 0) {
        sg_remove_request(sfp, srp);
        return -EIO; /* User did not pass enough bytes for this command. */
    }
    if ((k = sg_start_req(srp, mxsize, buf + cmd_size, input_size))) {
        SCSI_LOG_TIMEOUT(1, printk("sg_write: build err=%d\n", k));
        sg_finish_rem_req(srp, NULL, 0);
        return k;    /* probably out of space --> ENOMEM */
    }
/*  SCSI_LOG_TIMEOUT(7, printk("sg_write: allocating device\n")); */
    if (! (SCpnt = scsi_allocate_device(sdp->device, 
                                        !(filp->f_flags & O_NONBLOCK), 
					TRUE))) {
        sg_finish_rem_req(srp, NULL, 0);
        if( signal_pending(current) )
        {
                return -EINTR;
        }
        return -EAGAIN;   /* No available command blocks at the moment */
    }
/*  SCSI_LOG_TIMEOUT(7, printk("sg_write: device allocated\n")); */
    srp->my_cmdp = SCpnt;
    SCpnt->request.rq_dev = sdp->i_rdev;
    SCpnt->request.rq_status = RQ_ACTIVE;
    SCpnt->sense_buffer[0] = 0;
    SCpnt->cmd_len = cmd_size;
    __copy_from_user(cmnd, buf, cmd_size);
/* Set the LUN field in the command structure, overriding user input  */
    cmnd[1]= (cmnd[1] & 0x1f) | (sdp->device->lun << 5);

/*  SCSI_LOG_TIMEOUT(7, printk("sg_write: do cmd\n")); */
    SCpnt->use_sg = srp->data.use_sg;
    SCpnt->sglist_len = srp->data.sglist_len;
    SCpnt->bufflen = srp->data.bufflen;
    if (1 == sfp->underrun_flag)
        SCpnt->underflow = srp->data.bufflen;
    else
        SCpnt->underflow = 0;
    SCpnt->buffer = srp->data.buffer;
    srp->data.use_sg = 0;
    srp->data.sglist_len = 0;
    srp->data.bufflen = 0;
    srp->data.buffer = NULL;
/* Now send everything of to mid-level. The next time we hear about this
   packet is when sg_command_done() is called (ie a callback). */
    scsi_do_cmd(SCpnt, (void *)cmnd,
                (void *)SCpnt->buffer, mxsize,
                sg_command_done, sfp->timeout, SG_DEFAULT_RETRIES);
    /* 'mxsize' overwrites SCpnt->bufflen, hence need for b_malloc_len */
/*  SCSI_LOG_TIMEOUT(6, printk("sg_write: sent scsi cmd to mid-level\n")); */
    return count;
}

static int sg_ioctl(struct inode * inode, struct file * filp,
                    unsigned int cmd_in, unsigned long arg)
{
    int result, val;
    Sg_device * sdp;
    Sg_fd * sfp;
    Sg_request * srp;

    if ((! (sfp = (Sg_fd *)filp->private_data)) || (! (sdp = sfp->parentdp)))
        return -ENXIO;
    SCSI_LOG_TIMEOUT(3, printk("sg_ioctl: dev=%d, cmd=0x%x\n", 
                               MINOR(sdp->i_rdev), (int)cmd_in));
    if(! scsi_block_when_processing_errors(sdp->device) )
        return -ENXIO;

    switch(cmd_in)
    {
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
        result = verify_area(VERIFY_WRITE, (void *)arg, sizeof(Sg_scsi_id));
        if (result) return result;
        else {
            Sg_scsi_id * sg_idp = (Sg_scsi_id *)arg;
            __put_user((int)sdp->device->host->host_no, &sg_idp->host_no);
            __put_user((int)sdp->device->channel, &sg_idp->channel);
            __put_user((int)sdp->device->id, &sg_idp->scsi_id);
            __put_user((int)sdp->device->lun, &sg_idp->lun);
            __put_user((int)sdp->device->type, &sg_idp->scsi_type);
            __put_user((short)sdp->device->host->cmd_per_lun, 
                       &sg_idp->h_cmd_per_lun);
            __put_user((short)sdp->device->queue_depth, 
                       &sg_idp->d_queue_depth);
            __put_user(0, &sg_idp->unused1);
            __put_user(0, &sg_idp->unused2);
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
            if (! srp->my_cmdp) {
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
            if (! srp->my_cmdp)
                ++val;
            srp = srp->nextrp;
        }
        return put_user(val, (int *)arg);
    case SG_GET_SG_TABLESIZE:
        return put_user(sdp->sg_tablesize, (int *)arg);
    case SG_SET_RESERVED_SIZE:
        if (O_RDWR != (filp->f_flags & O_ACCMODE))
            return -EACCES;
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
    case SG_GET_MERGE_FD:
        return put_user((int)sdp->merge_fd, (int *)arg);
    case SG_SET_MERGE_FD:
        if (O_RDWR != (filp->f_flags & O_ACCMODE))
            return -EACCES; /* require write access since effect wider
                               then just this fd */
        result = get_user(val, (int *)arg);
        if (result) return result;
        val = val ? 1 : 0;
        if ((val ^ (0 != sdp->merge_fd)) && 
            sdp->headfp && sdp->headfp->nextfp)
            return -EBUSY;   /* too much work if multiple fds already */
        sdp->merge_fd = val;
        return 0;
    case SG_SET_COMMAND_Q:
        result = get_user(val, (int *)arg);
        if (result) return result;
        sfp->cmd_q = val ? 1 : 0;
        return 0;
    case SG_GET_COMMAND_Q:
        return put_user((int)sfp->cmd_q, (int *)arg);
    case SG_SET_UNDERRUN_FLAG:
        result = get_user(val, (int *)arg);
        if (result) return result;
        sfp->underrun_flag = val;
        return 0;
    case SG_GET_UNDERRUN_FLAG:
        return put_user((int)sfp->underrun_flag, (int *)arg);
    case SG_NEXT_CMD_LEN:
        result = get_user(val, (int *)arg);
        if (result) return result;
        sfp->next_cmd_len = (val > 0) ? val : 0;
        return 0;
    case SG_GET_VERSION_NUM:
        return put_user(sg_version_num, (int *)arg);
    case SG_EMULATED_HOST:
        return put_user(sdp->device->host->hostt->emulated, (int *)arg);
    case SG_SCSI_RESET:
        if (! scsi_block_when_processing_errors(sdp->device))
            return -EBUSY;
        result = get_user(val, (int *)arg);
        if (result) return result;
        /* Don't do anything till scsi mod level visibility */
        return 0;
    case SCSI_IOCTL_SEND_COMMAND:
        /* Allow SCSI_IOCTL_SEND_COMMAND without checking suser() since the
           user already has read/write access to the generic device and so
           can execute arbitrary SCSI commands.  */
        if (O_RDWR != (filp->f_flags & O_ACCMODE))
            return -EACCES; /* very dangerous things can be done here */
        return scsi_ioctl_send_command(sdp->device, (void *)arg);
    case SG_SET_DEBUG:
        result = get_user(val, (int *)arg);
        if (result) return result;
        sdp->sgdebug = (char)val;
        if (9 == sdp->sgdebug)
            sg_debug(sdp, sfp, 0);
        else if (sdp->sgdebug > 9)
            sg_debug_all(sfp);
        return 0;
    case SCSI_IOCTL_GET_IDLUN:
    case SCSI_IOCTL_GET_BUS_NUMBER:
    case SCSI_IOCTL_PROBE_HOST:
    case SG_GET_TRANSFORM:
        return scsi_ioctl(sdp->device, cmd_in, (void *)arg);
    default:
        if (O_RDWR != (filp->f_flags & O_ACCMODE))
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
        if (! (res || srp->my_cmdp))
            res = POLLIN | POLLRDNORM;
        ++count;
        srp = srp->nextrp;
    }
    if (0 == sfp->cmd_q) {
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

/* This function is called by the interrupt handler when we
 * actually have a command that is complete. */
static void sg_command_done(Scsi_Cmnd * SCpnt)
{
    int dev = MINOR(SCpnt->request.rq_dev);
    Sg_device * sdp;
    Sg_fd * sfp;
    Sg_request * srp = NULL;
    int closed = 0;
    static const int min_sb_len = 
                SG_MAX_SENSE > sizeof(SCpnt->sense_buffer) ? 
                        sizeof(SCpnt->sense_buffer) : SG_MAX_SENSE;

    if ((NULL == sg_dev_arr) || (dev < 0) || (dev >= sg_template.dev_max)) {
        SCSI_LOG_TIMEOUT(1, printk("sg__done: bad args dev=%d\n", dev));
        scsi_release_command(SCpnt);
        SCpnt = NULL;
        return;
    }
    sdp = &sg_dev_arr[dev];
    if (NULL == sdp->device)
        return; /* Get out of here quick ... */

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
        SCSI_LOG_TIMEOUT(1, printk("sg__done: req missing, dev=%d\n", dev));
        scsi_release_command(SCpnt);
        SCpnt = NULL;
        return;
    }
/* First transfer ownership of data buffers to sg_device object. */
    srp->data.use_sg = SCpnt->use_sg;
    srp->data.sglist_len = SCpnt->sglist_len;
    srp->data.bufflen = SCpnt->bufflen;
    srp->data.buffer = SCpnt->buffer;
    if (2 == sfp->underrun_flag)
        srp->header.pack_len = SCpnt->underflow;
    sg_clr_scpnt(SCpnt);
    srp->my_cmdp = NULL;

    SCSI_LOG_TIMEOUT(4, printk("sg__done: dev=%d, scsi_stat=%d, res=0x%x\n", 
                dev, (int)status_byte(SCpnt->result), (int)SCpnt->result));
    memcpy(srp->header.sense_buffer, SCpnt->sense_buffer, min_sb_len);
    switch (host_byte(SCpnt->result)) 
    { /* This setup of 'result' is for backward compatibility and is best
         ignored by the user who should use target, host + driver status */
    case DID_OK:
    case DID_PASSTHROUGH: 
    case DID_SOFT_ERROR:
      srp->header.result = 0;
      break;
    case DID_NO_CONNECT:
    case DID_BUS_BUSY:
    case DID_TIME_OUT:
      srp->header.result = EBUSY;
      break;
    case DID_BAD_TARGET:
    case DID_ABORT:
    case DID_PARITY:
    case DID_RESET:
    case DID_BAD_INTR:
      srp->header.result = EIO;
      break;
    case DID_ERROR:
      if (SCpnt->sense_buffer[0] == 0 &&
          status_byte(SCpnt->result) == GOOD)
          srp->header.result = 0;
      else 
          srp->header.result = EIO;
      break;
    default:
      SCSI_LOG_TIMEOUT(1, printk(
                "sg: unexpected host_byte=%d, dev=%d in 'done'\n", 
                host_byte(SCpnt->result), dev));
      srp->header.result = EIO;
      break;
    }

/* Following if statement is a patch supplied by Eric Youngdale */
    if (driver_byte(SCpnt->result) != 0
        && (SCpnt->sense_buffer[0] & 0x7f) == 0x70
        && (SCpnt->sense_buffer[2] & 0xf) == UNIT_ATTENTION
        && sdp->device->removable) {
/* Detected disc change. Set the bit - this may be used if there are */
/* filesystems using this device. */
        sdp->device->changed = 1;
    }
    srp->header.target_status = status_byte(SCpnt->result);
    if ((sdp->sgdebug > 0) && 
        ((CHECK_CONDITION == srp->header.target_status) ||
         (COMMAND_TERMINATED == srp->header.target_status)))
        print_sense("sg_command_done", SCpnt);
    srp->header.host_status = host_byte(SCpnt->result);
    srp->header.driver_status = driver_byte(SCpnt->result);

    scsi_release_command(SCpnt);
    SCpnt = NULL;
    if (sfp->closed) { /* whoops this fd already released, cleanup */
        closed = 1;
        SCSI_LOG_TIMEOUT(1,
               printk("sg__done: already closed, freeing ...\n"));
/* should check if module is unloaded <<<<<<< */
        sg_finish_rem_req(srp, NULL, 0);
        if (NULL == sfp->headrp) { 
            SCSI_LOG_TIMEOUT(1,
                printk("sg__done: already closed, final cleanup\n"));
            sg_remove_sfp(sdp, sfp);
        }
    }
/* Now wake up any sg_read() that is waiting for this packet. */
    wake_up_interruptible(&sfp->read_wait);
    if ((sfp->async_qp) && (! closed))
        kill_fasync(sfp->async_qp, SIGPOLL, POLL_IN);
}

static void sg_debug_all(const Sg_fd * sfp)
{
    const Sg_device * sdp = sg_dev_arr;
    int k;
   
    if (NULL == sg_dev_arr) {
        printk("sg_debug_all: sg_dev_arr NULL, death is imminent\n"); 
        return;
    }
    if (! sfp)
        printk("sg_debug_all: sfp (file descriptor pointer) NULL\n"); 
    
    printk("sg_debug_all: dev_max=%d, %s\n", 
           sg_template.dev_max, sg_version_str);
    printk(" scsi_dma_free_sectors=%u, sg_pool_secs_aval=%d\n",
           scsi_dma_free_sectors, sg_pool_secs_avail);
    printk(" sg_big_buff=%d\n", sg_big_buff);
#ifdef SG_DEBUG
    printk(" malloc counts, kmallocs=%d, dma_pool=%d, pages=%d\n",
           sg_num_kmal, sg_num_pool, sg_num_page);
#endif
    for (k = 0; k < sg_template.dev_max; ++k, ++sdp) {
        if (sdp->headfp) {
            if (! sfp)
                sfp = sdp->headfp;      /* just to keep things going */
            else if (sdp == sfp->parentdp)
        printk("  ***** Invoking device follows *****\n");
            sg_debug(sdp, sfp, 1);
        }
    }
}

static void sg_debug(const Sg_device * sdp, const Sg_fd * sfp, int part_of)
{
    Sg_fd * fp;
    Sg_request * srp;
    int dev;
    int k;
   
    if (! sfp)
        printk("sg_debug: sfp (file descriptor pointer) NULL\n"); 
    if (! sdp) {
        printk("sg_debug: sdp pointer (to device) NULL\n"); 
        return;
    }
    else if (! sdp->device) {
        printk("sg_debug: device detached ??\n"); 
        return;
    }
    dev = MINOR(sdp->i_rdev);

    if (part_of)
        printk(" >>> device=%d(sg%c), ", dev, 'a' + dev);
    else
        printk("sg_debug: device=%d(sg%c), ", dev, 'a' + dev);
    printk("scsi%d chan=%d id=%d lun=%d  em=%d\n", sdp->device->host->host_no,
           sdp->device->channel, sdp->device->id, sdp->device->lun,
           sdp->device->host->hostt->emulated);
    printk(" sg_tablesize=%d, excl=%d, sgdebug=%d, merge_fd=%d\n",
           sdp->sg_tablesize, sdp->exclude, sdp->sgdebug, sdp->merge_fd);
    if (! part_of) {
        printk(" scsi_dma_free_sectors=%u, sg_pool_secs_aval=%d\n",
               scsi_dma_free_sectors, sg_pool_secs_avail);
#ifdef SG_DEBUG
        printk(" mallocs: kmallocs=%d, dma_pool=%d, pages=%d\n",
               sg_num_kmal, sg_num_pool, sg_num_page);
#endif
    }

    fp = sdp->headfp;
    for (k = 1; fp; fp = fp->nextfp, ++k) {
        if (sfp == fp)
            printk("  *** Following data belongs to invoking FD ***\n");
        else if (! fp->parentdp)
            printk(">> Following FD has NULL parent pointer ???\n");
        printk("   FD(%d): timeout=%d, bufflen=%d, use_sg=%d\n",
               k, fp->timeout, fp->reserve.bufflen, (int)fp->reserve.use_sg);
        printk("   low_dma=%d, cmd_q=%d, s_sc_len=%d, f_packid=%d\n",
               (int)fp->low_dma, (int)fp->cmd_q, (int)fp->save_scat_len,
               (int)fp->force_packid);
        printk("   urun_flag=%d, next_cmd_len=%d, closed=%d\n",
               (int)fp->underrun_flag, (int)fp->next_cmd_len, 
               (int)fp->closed);
        srp = fp->headrp;
        if (NULL == srp)
            printk("     No requests active\n");
        while (srp) {
            if (srp->res_used)
                printk("reserved buff >> ");
            else
                printk("     ");
            if (srp->my_cmdp)
                printk("written: pack_id=%d, bufflen=%d, use_sg=%d\n",
                       srp->header.pack_id, srp->my_cmdp->bufflen, 
                       srp->my_cmdp->use_sg);
            else
                printk("to_read: pack_id=%d, bufflen=%d, use_sg=%d\n",
                   srp->header.pack_id, srp->data.bufflen, srp->data.use_sg);
            if (! srp->parentfp)
                printk(">> request has NULL parent pointer ???\n");
            srp = srp->nextrp;
        }
    }
}

static struct file_operations sg_fops = {
    NULL,            /* lseek */
    sg_read,         /* read */
    sg_write,        /* write */
    NULL,            /* readdir */
    sg_poll,         /* poll */
    sg_ioctl,        /* ioctl */
    NULL,            /* mmap */
    sg_open,         /* open */
    NULL,            /* flush */
    sg_release,      /* release, was formerly sg_close */
    NULL,            /* fsync */
    sg_fasync,       /* fasync */
    NULL,            /* lock */
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
    sg_dev_arr = (Sg_device *)
	kmalloc((sg_template.dev_noticed + SG_EXTRA_DEVS)
		* sizeof(Sg_device), GFP_ATOMIC);
    memset(sg_dev_arr, 0, (sg_template.dev_noticed + SG_EXTRA_DEVS)
		* sizeof(Sg_device));
    if (NULL == sg_dev_arr) {
        printk("sg_init: no space for sg_dev_arr\n");
        return 1;
    }
    sg_template.dev_max = sg_template.dev_noticed + SG_EXTRA_DEVS;
    return 0;
}

static int sg_attach(Scsi_Device * scsidp)
{
    Sg_device * sdp = sg_dev_arr;
    int k;

    if ((sg_template.nr_dev >= sg_template.dev_max) || (! sdp))
    {
        scsidp->attached--;
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
    sdp->merge_fd = 0;  /* Cope with SG_DEF_MERGE_FD on open */
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
/* Need to stop sg_command_done() playing with this list during this loop */
            spin_lock_irqsave(&sg_request_lock, flags);
            sfp = sdp->headfp;
            while (sfp) {
                srp = sfp->headrp;
                while (srp) {
                    if (srp->my_cmdp)
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

int init_module(void) {
    sg_template.module = &__this_module;
    return scsi_register_module(MODULE_SCSI_DEV, &sg_template);
}

void cleanup_module( void)
{
    scsi_unregister_module(MODULE_SCSI_DEV, &sg_template);
    unregister_chrdev(SCSI_GENERIC_MAJOR, "sg");

    if(sg_dev_arr != NULL) {
/* Really worrying situation of writes still pending and get here */
/* Strategy: shorten timeout on release + wait on detach ... */
	kfree((char *) sg_dev_arr);
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
    spin_unlock_irq(&sg_request_lock);
    scsi_sleep(HZ); /* just sleep 1 second and hope ... */
    spin_lock_irq(&sg_request_lock);
#endif
}

static int sg_start_req(Sg_request * srp, int max_buff_size,
                        const char * inp, int num_write_xfer)
{
    int res;
    Sg_fd * sfp = srp->parentfp;
    Sg_scatter_hold * req_schp = &srp->data;
    Sg_scatter_hold * rsv_schp = &sfp->reserve;

    SCSI_LOG_TIMEOUT(4, printk("sg_start_req: max_buff_size=%d\n", 
                               max_buff_size)); 
    if ((! sg_res_in_use(sfp)) && (max_buff_size <= rsv_schp->bufflen)) {
        sg_link_reserve(sfp, srp, max_buff_size);
        sg_write_xfer(req_schp, inp, num_write_xfer);
    }
    else {
        res = sg_build_scat(req_schp, max_buff_size, sfp);
        if (res) {
            sg_remove_scat(req_schp);
            return res;
        }
        sg_write_xfer(req_schp, inp, num_write_xfer);
    }
    return 0;
}

static void sg_finish_rem_req(Sg_request * srp, char * outp, 
                              int num_read_xfer)
{
    Sg_fd * sfp = srp->parentfp;
    Sg_scatter_hold * req_schp = &srp->data;

    SCSI_LOG_TIMEOUT(4, printk("sg_finish_rem_req: res_used=%d\n",
                               (int)srp->res_used)); 
    if (num_read_xfer > 0)
        sg_read_xfer(req_schp, outp, num_read_xfer);
    if (srp->res_used)
        sg_unlink_reserve(sfp, srp);
    else 
        sg_remove_scat(req_schp);
    sg_remove_request(sfp, srp);
}

static int sg_build_scat(Sg_scatter_hold * schp, int buff_size, 
                         const Sg_fd * sfp)
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
    SCSI_LOG_TIMEOUT(4, printk("sg_build_scat: buff_size=%d, blk_size=%d\n",
                               buff_size, blk_size));
    if (blk_size <= SG_SCATTER_SZ) {
        mem_src = SG_HEAP_PAGE;
        p = sg_malloc(sfp, blk_size, &ret_sz, &mem_src);
        if (! p)
            return -ENOMEM;
        if (blk_size == ret_sz) { /* got it on the first attempt */
            schp->use_sg = 0;
            schp->buffer = p;
            schp->bufflen = blk_size;
            schp->mem_src = mem_src;
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
        int k, rem_sz, num, nxt;
        int sc_bufflen = PAGE_SIZE;
        int mx_sc_elems = (sc_bufflen / sizeof(struct scatterlist)) - 1;
        int sg_tablesize = sfp->parentdp->sg_tablesize;
        int first = 1;

        k = SG_HEAP_KMAL;  /* want to protect mem_src, use k as scratch */
        schp->buffer = (struct scatterlist *)sg_malloc(sfp, 
                                sc_bufflen, &num, &k);
        schp->mem_src = (char)k;
        /* N.B. ret_sz and mem_src carried into this block ... */
        if (! schp->buffer)
            return -ENOMEM;
        else if (num != sc_bufflen) {
            sc_bufflen = num;
            mx_sc_elems = (sc_bufflen / sizeof(struct scatterlist)) - 1;
        }
        schp->sglist_len = sc_bufflen;
        memset(schp->buffer, 0, sc_bufflen);
        for (k = 0, sclp = schp->buffer, rem_sz = blk_size, nxt =0; 
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
            sclp->alt_address = (char *)(long)mem_src;
            
            SCSI_LOG_TIMEOUT(5, 
                printk("sg_build_build: k=%d, a=0x%p, len=%d, ms=%d\n", 
                k, sclp->address, ret_sz, mem_src));
        } /* end of for loop */
        schp->use_sg = k;
        SCSI_LOG_TIMEOUT(5, 
            printk("sg_build_scat: use_sg=%d, rem_sz=%d\n", k, rem_sz));
        schp->bufflen = blk_size;
        if (rem_sz > 0)   /* must have failed */
            return -ENOMEM;
    }
    return 0;
}

static void sg_write_xfer(Sg_scatter_hold * schp, const char * inp, 
                          int num_write_xfer)
{
    SCSI_LOG_TIMEOUT(4, printk("sg_write_xfer: num_write_xfer=%d, use_sg=%d\n", 
                               num_write_xfer, schp->use_sg)); 
    if ((! inp) || (num_write_xfer <= 0))
        return;
    if (schp->use_sg > 0) {
        int k, num;
        struct scatterlist * sclp = (struct scatterlist *)schp->buffer;

        for (k = 0; (k < schp->use_sg) && sclp->address; ++k, ++sclp) { 
            num = (int)sclp->length;
            if (num > num_write_xfer) {
                __copy_from_user(sclp->address, inp, num_write_xfer);
                break;
            }
            else {
                __copy_from_user(sclp->address, inp, num);
                num_write_xfer -= num;
                if (num_write_xfer <= 0)
                    break;
                inp += num;
            }
        }
    }
    else
        __copy_from_user(schp->buffer, inp, num_write_xfer);
}

static void sg_remove_scat(Sg_scatter_hold * schp)
{
    SCSI_LOG_TIMEOUT(4, printk("sg_remove_scat: use_sg=%d\n", schp->use_sg)); 
    if(schp->use_sg > 0) {
        int k, mem_src;
        struct scatterlist * sclp = (struct scatterlist *)schp->buffer;

        for (k = 0; (k < schp->use_sg) && sclp->address; ++k, ++sclp) {
            mem_src = (int)(long)sclp->alt_address;
            SCSI_LOG_TIMEOUT(5, 
                printk("sg_remove_scat: k=%d, a=0x%p, len=%d, ms=%d\n", 
                       k, sclp->address, sclp->length, mem_src));
            sg_free(sclp->address, sclp->length, mem_src);
            sclp->address = NULL;
            sclp->length = 0;
        }
        sg_free(schp->buffer, schp->sglist_len, schp->mem_src);
    }
    else if (schp->buffer)
        sg_free(schp->buffer, schp->b_malloc_len, schp->mem_src);
    schp->buffer = NULL;
    schp->bufflen = 0;
    schp->use_sg = 0;
    schp->sglist_len = 0;
}

static void sg_read_xfer(Sg_scatter_hold * schp, char * outp,
                         int num_read_xfer)
{
    SCSI_LOG_TIMEOUT(4, printk("sg_read_xfer: num_read_xfer=%d\n", 
                               num_read_xfer)); 
    if ((! outp) || (num_read_xfer <= 0))
        return;
    if(schp->use_sg > 0) {
        int k, num;
        struct scatterlist * sclp = (struct scatterlist *)schp->buffer;

        for (k = 0; (k < schp->use_sg) && sclp->address; ++k, ++sclp) {
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
        if (0 == sg_build_scat(schp, req_size, sfp))
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
    if (rsv_schp->use_sg > 0) {
        int k, num;
        int rem = size;
        struct scatterlist * sclp = (struct scatterlist *)rsv_schp->buffer;

        for (k = 0; k < rsv_schp->use_sg; ++k, ++sclp) {
            num = (int)sclp->length;
            if (rem <= num) {
                sfp->save_scat_len = num;
                sclp->length = (unsigned)rem;
                break;
            }
            else
                rem -= num;
        }
        if (k < rsv_schp->use_sg) {
            req_schp->use_sg = k + 1;   /* adjust scatter list length */
            req_schp->bufflen = size;
            req_schp->sglist_len = rsv_schp->sglist_len;
            req_schp->buffer = rsv_schp->buffer;
            req_schp->mem_src = rsv_schp->mem_src;
            req_schp->b_malloc_len = rsv_schp->b_malloc_len;
        }
        else
            SCSI_LOG_TIMEOUT(1, printk("sg_link_reserve: BAD size\n")); 
    }
    else {
        req_schp->use_sg = 0;
        req_schp->bufflen = size;
        req_schp->buffer = rsv_schp->buffer;
        req_schp->mem_src = rsv_schp->mem_src;
        req_schp->use_sg = rsv_schp->use_sg;
        req_schp->b_malloc_len = rsv_schp->b_malloc_len;
    }
    srp->res_used = 1;
}

static void sg_unlink_reserve(Sg_fd * sfp, Sg_request * srp)
{
    Sg_scatter_hold * req_schp = &srp->data;
    Sg_scatter_hold * rsv_schp = &sfp->reserve;

    SCSI_LOG_TIMEOUT(4, printk("sg_unlink_reserve: req->use_sg=%d\n",
                               (int)req_schp->use_sg)); 
    if (rsv_schp->use_sg > 0) {
        struct scatterlist * sclp = (struct scatterlist *)rsv_schp->buffer;

        if (sfp->save_scat_len > 0) 
            (sclp + (req_schp->use_sg - 1))->length = 
                                        (unsigned)sfp->save_scat_len;
        else
            SCSI_LOG_TIMEOUT(1, printk(
                        "sg_unlink_reserve: BAD save_scat_len\n")); 
    }
    req_schp->use_sg = 0;
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
    while (resp) {
        if ((! resp->my_cmdp) && 
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
        memset(&resp->data, 0, sizeof(Sg_scatter_hold));
        memset(&resp->header, 0, sizeof(struct sg_header));
        resp->my_cmdp = NULL;
    }
    return resp;
}

/* Return of 1 for found; 0 for not found */
static int sg_remove_request(Sg_fd * sfp, const Sg_request * srp)
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

static Sg_fd * sg_add_sfp(Sg_device * sdp, int dev, int get_reserved)
{
    Sg_fd * sfp;

    if (sdp->merge_fd) {
        ++sdp->merge_fd;
        return sdp->headfp;
    }
    sfp = (Sg_fd *)sg_low_malloc(sizeof(Sg_fd), 0, SG_HEAP_KMAL, 0);
    if (sfp) {
        memset(sfp, 0, sizeof(Sg_fd));
        sfp->my_mem_src = SG_HEAP_KMAL;
        init_waitqueue_head(&sfp->read_wait);
        init_waitqueue_head(&sfp->write_wait);
    }
    else
        return NULL;
        
    sfp->timeout = SG_DEFAULT_TIMEOUT;
    sfp->force_packid = SG_DEF_FORCE_PACK_ID;
    sfp->low_dma = (SG_DEF_FORCE_LOW_DMA == 0) ?
                   sdp->device->host->unchecked_isa_dma : 1;
    sfp->cmd_q = SG_DEF_COMMAND_Q;
    sfp->underrun_flag = SG_DEF_UNDERRUN_FLAG;
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
                               sfp, (int)sfp->my_mem_src));
    if (get_reserved) {
        sg_build_reserve(sfp, SG_DEF_RESERVED_SIZE);
        sg_big_buff = sfp->reserve.bufflen; /* sysctl shows most recent size */
        SCSI_LOG_TIMEOUT(3, printk("sg_add_sfp:   bufflen=%d, use_sg=%d\n",
                               sfp->reserve.bufflen, sfp->reserve.use_sg));
    }
    return sfp;
}

static int sg_remove_sfp(Sg_device * sdp, Sg_fd * sfp)
{
    Sg_request * srp;
    Sg_request * tsrp;
    int dirty = 0;
    int res = 0;

    if (sdp->merge_fd) {
        if (--sdp->merge_fd)
            return 0;   /* if merge_fd then dec merge_fd counter */
    }
    srp = sfp->headrp;
    if (srp) {
/* Need to stop sg_command_done() playing with this list during this loop */
        while (srp) {
            tsrp = srp->nextrp;
            if (! srp->my_cmdp)
                sg_finish_rem_req(srp, NULL, 0);
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
SCSI_LOG_TIMEOUT(6, printk("sg_remove_sfp:    bufflen=%d, use_sg=%d\n",
                 (int)sfp->reserve.bufflen, (int)sfp->reserve.use_sg));
            sg_remove_scat(&sfp->reserve);
        }
        sfp->parentdp = NULL;
    SCSI_LOG_TIMEOUT(6, printk("sg_remove_sfp:    sfp=0x%p\n", sfp));
        sg_low_free((char *)sfp, sizeof(Sg_fd), sfp->my_mem_src);
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
#ifdef SG_DEBUG
        if (resp) ++sg_num_kmal;
#endif
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
#ifdef SG_DEBUG
                    ++sg_num_pool;
#endif
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
#ifdef SG_DEBUG
        if (resp) ++sg_num_page;
#endif
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
    if (! buff)
        return;
    if (SG_HEAP_POOL == mem_src) {
        int num_sect = size / SG_SECTOR_SZ;
        scsi_free(buff, size);
        sg_pool_secs_avail += num_sect;
    }
    else if (SG_HEAP_KMAL == mem_src)
        kfree(buff);    /* size not used */
    else if (SG_HEAP_PAGE == mem_src) {
        int order, a_size;

        for (order = 0, a_size = PAGE_SIZE;
             a_size < size; order++, a_size <<= 1)
            ;
        free_pages((unsigned long)buff, order);
    }
    else
        printk("sg_low_free: bad mem_src=%d, buff=0x%p, rqSz=%df\n", 
               mem_src, buff, size);
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

