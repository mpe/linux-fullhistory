/*
 *  History:
 *  Started: Aug 9 by Lawrence Foard (entropy@world.std.com), 
 *           to allow user process control of SCSI devices.
 *  Development Sponsored by Killy Corp. NY NY
 *   
 *  Borrows code from st driver.
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
#include <asm/io.h>
#include <asm/segment.h>
#include <asm/system.h>

#include <linux/blk.h>
#include "scsi.h"
#include "hosts.h"
#include <scsi/scsi_ioctl.h>
#include <scsi/sg.h>

static int sg_init(void);
static int sg_attach(Scsi_Device *);
static int sg_detect(Scsi_Device *);
static void sg_detach(Scsi_Device *);


struct Scsi_Device_Template sg_template = {NULL, NULL, "sg", NULL, 0xff, 
					       SCSI_GENERIC_MAJOR, 0, 0, 0, 0,
					       sg_detect, sg_init,
					       NULL, sg_attach, sg_detach};

#ifdef SG_BIG_BUFF
static char *big_buff = NULL;
static struct wait_queue *big_wait;   /* wait for buffer available */
static int big_inuse=0;
#endif

struct scsi_generic
{
    Scsi_Device *device;
    int users;   /* how many people have it open? */
    struct wait_queue *generic_wait; /* wait for device to be available */
    struct wait_queue *read_wait;    /* wait for response */
    struct wait_queue *write_wait;   /* wait for free buffer */
    int timeout; /* current default value for device */
    int buff_len; /* length of current buffer */
    char *buff;   /* the buffer */
    struct sg_header header; /* header of pending command */
    char exclude; /* opened for exclusive access */
    char pending;  /* don't accept writes now */
    char complete; /* command complete allow a read */
};

static struct scsi_generic *scsi_generics=NULL;
static void sg_free(char *buff,int size);

static int sg_ioctl(struct inode * inode,struct file * file,
		    unsigned int cmd_in, unsigned long arg)
{
    int result;
    int dev = MINOR(inode->i_rdev);
    if ((dev<0) || (dev>=sg_template.dev_max))
	return -ENXIO;
    switch(cmd_in)
    {
    case SG_SET_TIMEOUT:
        result = verify_area(VERIFY_READ, (const void *)arg, sizeof(long));
        if (result) return result;

	scsi_generics[dev].timeout=get_user((int *) arg);
	return 0;
    case SG_GET_TIMEOUT:
	return scsi_generics[dev].timeout;
    default:
	return scsi_ioctl(scsi_generics[dev].device, cmd_in, (void *) arg);
    }
}

static int sg_open(struct inode * inode, struct file * filp)
{
    int dev=MINOR(inode->i_rdev);
    int flags=filp->f_flags;
    if (dev>=sg_template.dev_max || !scsi_generics[dev].device)
	return -ENXIO;
    if (O_RDWR!=(flags & O_ACCMODE))
	return -EACCES;

  /*
   * If we want exclusive access, then wait until the device is not
   * busy, and then set the flag to prevent anyone else from using it.
   */
    if (flags & O_EXCL)
    {
	while(scsi_generics[dev].users)
	{
	    if (flags & O_NONBLOCK)
		return -EBUSY;
	    interruptible_sleep_on(&scsi_generics[dev].generic_wait);
	    if (current->signal & ~current->blocked)
		return -ERESTARTSYS;
	}
	scsi_generics[dev].exclude=1;
    }
    else
        /*
         * Wait until nobody has an exclusive open on
         * this device.
         */
	while(scsi_generics[dev].exclude)
	{
	    if (flags & O_NONBLOCK)
		return -EBUSY;
	    interruptible_sleep_on(&scsi_generics[dev].generic_wait);
	    if (current->signal & ~current->blocked)
		return -ERESTARTSYS;
	}

    /*
     * OK, we should have grabbed the device.  Mark the thing so
     * that other processes know that we have it, and initialize the
     * state variables to known values.
     */
    if (!scsi_generics[dev].users 
        && scsi_generics[dev].pending
        && scsi_generics[dev].complete)
    {
	if (scsi_generics[dev].buff != NULL)
	    sg_free(scsi_generics[dev].buff,scsi_generics[dev].buff_len);
	scsi_generics[dev].buff=NULL;
	scsi_generics[dev].pending=0;
    }
    if (!scsi_generics[dev].users)
	scsi_generics[dev].timeout=SG_DEFAULT_TIMEOUT;
    if (scsi_generics[dev].device->host->hostt->usage_count)
	(*scsi_generics[dev].device->host->hostt->usage_count)++;
    if(sg_template.usage_count) (*sg_template.usage_count)++;
    scsi_generics[dev].users++;
    return 0;
}

static void sg_close(struct inode * inode, struct file * filp)
{
    int dev=MINOR(inode->i_rdev);
    scsi_generics[dev].users--;
    if (scsi_generics[dev].device->host->hostt->usage_count)
	(*scsi_generics[dev].device->host->hostt->usage_count)--;
    if(sg_template.usage_count) (*sg_template.usage_count)--;
    scsi_generics[dev].exclude=0;
    wake_up(&scsi_generics[dev].generic_wait);
}

static char *sg_malloc(int size)
{
    if (size<=4096)
	return (char *) scsi_malloc(size);
#ifdef SG_BIG_BUFF
    if (size<=SG_BIG_BUFF)
    {
	while(big_inuse)
	{
	    interruptible_sleep_on(&big_wait);
	    if (current->signal & ~current->blocked)
		return NULL;
	}
	big_inuse=1;
	return big_buff;
    }
#endif   
    return NULL;
}

static void sg_free(char *buff,int size) 
{
#ifdef SG_BIG_BUFF
    if (buff==big_buff)
    {
	big_inuse=0;
	wake_up(&big_wait);
	return;
    }
#endif
    scsi_free(buff,size);
}

/*
 * Read back the results of a previous command.  We use the pending and
 * complete semaphores to tell us whether the buffer is available for us
 * and whether the command is actually done.
 */
static int sg_read(struct inode *inode,struct file *filp,char *buf,int count)
{
    int dev=MINOR(inode->i_rdev);
    int i;
    unsigned long flags;
    struct scsi_generic *device=&scsi_generics[dev];
    if ((i=verify_area(VERIFY_WRITE,buf,count)))
	return i;

    /*
     * Wait until the command is actually done.
     */
    save_flags(flags);
    cli();
    while(!device->pending || !device->complete)
    {
	if (filp->f_flags & O_NONBLOCK)
	{
	    restore_flags(flags);
	    return -EAGAIN;
	}
	interruptible_sleep_on(&device->read_wait);
	if (current->signal & ~current->blocked)
	{
	    restore_flags(flags);
	    return -ERESTARTSYS;
	}
    }
    restore_flags(flags);

    /*
     * Now copy the result back to the user buffer.
     */
    device->header.pack_len=device->header.reply_len;

    if (count>=sizeof(struct sg_header))
    {
	memcpy_tofs(buf,&device->header,sizeof(struct sg_header));
	buf+=sizeof(struct sg_header);
	if (count>device->header.pack_len)
	    count=device->header.pack_len;
	if (count > sizeof(struct sg_header)) {
	    memcpy_tofs(buf,device->buff,count-sizeof(struct sg_header));
	}
    }
    else
	count= device->header.result==0 ? 0 : -EIO;
    
    /*
     * Clean up, and release the device so that we can send another
     * command.
     */
    sg_free(device->buff,device->buff_len);
    device->buff = NULL;
    device->pending=0;
    wake_up(&device->write_wait);
    return count;
}

/*
 * This function is called by the interrupt handler when we
 * actually have a command that is complete.  Change the
 * flags to indicate that we have a result.
 */
static void sg_command_done(Scsi_Cmnd * SCpnt)
{
    int dev = MINOR(SCpnt->request.rq_dev);
    struct scsi_generic *device = &scsi_generics[dev];
    if (!device->pending)
    {
	printk("unexpected done for sg %d\n",dev);
	SCpnt->request.rq_status = RQ_INACTIVE;
	return;
    }

    /*
     * See if the command completed normally, or whether something went
     * wrong.
     */
    memcpy(device->header.sense_buffer, SCpnt->sense_buffer, sizeof(SCpnt->sense_buffer));
    device->header.result=SCpnt->result;

    /*
     * Now wake up the process that is waiting for the
     * result.
     */
    device->complete=1;
    SCpnt->request.rq_status = RQ_INACTIVE;
    wake_up(&scsi_generics[dev].read_wait);
}

static int sg_write(struct inode *inode,struct file *filp,const char *buf,int count)
{
    int			  bsize,size,amt,i;
    unsigned char	  cmnd[MAX_COMMAND_SIZE];
    kdev_t		  devt = inode->i_rdev;
    int			  dev = MINOR(devt);
    struct scsi_generic   * device=&scsi_generics[dev];
    int			  input_size;
    unsigned char	  opcode;
    Scsi_Cmnd		* SCpnt;
    
    if ((i=verify_area(VERIFY_READ,buf,count)))
	return i;
    /*
     * The minimum scsi command length is 6 bytes.  If we get anything
     * less than this, it is clearly bogus.  
     */
    if (count<(sizeof(struct sg_header) + 6))
	return -EIO;

    /*
     * If we still have a result pending from a previous command,
     * wait until the result has been read by the user before sending
     * another command.
     */
    while(device->pending)
    {
	if (filp->f_flags & O_NONBLOCK)
	    return -EAGAIN;
#ifdef DEBUG
	printk("sg_write: sleeping on pending request\n");
#endif     
	interruptible_sleep_on(&device->write_wait);
	if (current->signal & ~current->blocked)
	    return -ERESTARTSYS;
    }

    /*
     * Mark the device flags for the new state.
     */
    device->pending=1;
    device->complete=0;
    memcpy_fromfs(&device->header,buf,sizeof(struct sg_header));

    device->header.pack_len=count;
    buf+=sizeof(struct sg_header);

    /*
     * Now we need to grab the command itself from the user's buffer.
     */
    opcode = get_user(buf);
    size=COMMAND_SIZE(opcode);
    if (opcode >= 0xc0 && device->header.twelve_byte) size = 12;

    /*
     * Determine buffer size.
     */
    input_size = device->header.pack_len - size;
    if( input_size > device->header.reply_len)
    {
        bsize = input_size;
    } else {
        bsize = device->header.reply_len;
    }
    
    /*
     * Don't include the command header itself in the size.
     */
    bsize-=sizeof(struct sg_header);
    input_size-=sizeof(struct sg_header);

    /*
     * Verify that the user has actually passed enough bytes for this command.
     */
    if( input_size < 0 )
    {
	device->pending=0;
        wake_up( &device->write_wait );
	return -EIO;
    }
    
    /*
     * Allocate a buffer that is large enough to hold the data
     * that has been requested.  Round up to an even number of sectors,
     * since scsi_malloc allocates in chunks of 512 bytes.
     */
    amt=bsize;
    if (!bsize)
	bsize++;
    bsize=(bsize+511) & ~511;

    /*
     * If we cannot allocate the buffer, report an error.
     */
    if ((bsize<0) || !(device->buff=sg_malloc(device->buff_len=bsize)))
    {
	device->pending=0;
	wake_up(&device->write_wait);
	return -ENOMEM;
    }

#ifdef DEBUG
    printk("allocating device\n");
#endif

    /*
     * Grab a device pointer for the device we want to talk to.  If we
     * don't want to block, just return with the appropriate message.
     */
    if (!(SCpnt=allocate_device(NULL,device->device, !(filp->f_flags & O_NONBLOCK))))
    {
	device->pending=0;
	wake_up(&device->write_wait);
	sg_free(device->buff,device->buff_len);
	device->buff = NULL;
	return -EAGAIN;
    } 
#ifdef DEBUG
    printk("device allocated\n");
#endif    

    SCpnt->request.rq_dev = devt;
    SCpnt->request.rq_status = RQ_ACTIVE;
    SCpnt->sense_buffer[0]=0;
    SCpnt->cmd_len = size;

    /*
     * Now copy the SCSI command from the user's address space.
     */
    memcpy_fromfs(cmnd,buf,size);
    buf+=size;

    /*
     * If we are writing data, copy the data we are writing.  The pack_len
     * field also includes the length of the header and the command,
     * so we need to subtract these off.
     */
    if (input_size > 0) memcpy_fromfs(device->buff, buf, input_size);
    
    /*
     * Set the LUN field in the command structure.
     */
    cmnd[1]= (cmnd[1] & 0x1f) | (device->device->lun<<5);

#ifdef DEBUG
    printk("do cmd\n");
#endif

    /*
     * Now pass the actual command down to the low-level driver.  We
     * do not do any more here - when the interrupt arrives, we will
     * then do the post-processing.
     */
    scsi_do_cmd (SCpnt,(void *) cmnd,
		 (void *) device->buff,amt,
		 sg_command_done,device->timeout,SG_DEFAULT_RETRIES);

#ifdef DEBUG
    printk("done cmd\n");
#endif               

    return count;
}

static int sg_select(struct inode *inode, struct file *file, int sel_type, select_table * wait)
{
    int dev=MINOR(inode->i_rdev);
    int r = 0;
    struct scsi_generic *device=&scsi_generics[dev];

    if (sel_type == SEL_IN) {
        if(device->pending && device->complete)
        {
            r = 1;
    	} else {
	    select_wait(&scsi_generics[dev].read_wait, wait);
    	}
    }
    if (sel_type == SEL_OUT) {
        if(!device->pending){
            r = 1;
        }
        else
        {
	    select_wait(&scsi_generics[dev].write_wait, wait);
        }
    }

    return(r);
}

static struct file_operations sg_fops = {
    NULL,            /* lseek */
    sg_read,         /* read */
    sg_write,        /* write */
    NULL,            /* readdir */
    sg_select,       /* select */
    sg_ioctl,        /* ioctl */
    NULL,            /* mmap */
    sg_open,         /* open */
    sg_close,        /* release */
    NULL             /* fsync */
};


static int sg_detect(Scsi_Device * SDp){

    switch (SDp->type) {
	case TYPE_DISK:
	case TYPE_MOD:
	case TYPE_ROM:
	case TYPE_WORM:
	case TYPE_TAPE: break;
	default: 
	printk("Detected scsi generic sg%c at scsi%d, channel %d, id %d, lun %d\n",
           'a'+sg_template.dev_noticed,
           SDp->host->host_no, SDp->channel, SDp->id, SDp->lun);
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
    if(scsi_generics) return 0;
    
#ifdef DEBUG
    printk("sg: Init generic device.\n");
#endif
    
#ifdef SG_BIG_BUFF
    big_buff= (char *) scsi_init_malloc(SG_BIG_BUFF, GFP_ATOMIC | GFP_DMA);
#endif
    
    scsi_generics = (struct scsi_generic *) 
	scsi_init_malloc((sg_template.dev_noticed + SG_EXTRA_DEVS) 
			 * sizeof(struct scsi_generic), GFP_ATOMIC);
    memset(scsi_generics, 0, (sg_template.dev_noticed + SG_EXTRA_DEVS)
	   * sizeof(struct scsi_generic));
    
    sg_template.dev_max = sg_template.dev_noticed + SG_EXTRA_DEVS;
    return 0;
}

static int sg_attach(Scsi_Device * SDp)
{
    struct scsi_generic * gpnt;
    int i;
    
    if(sg_template.nr_dev >= sg_template.dev_max) 
    {
	SDp->attached--;
	return 1;
    }
    
    for(gpnt = scsi_generics, i=0; i<sg_template.dev_max; i++, gpnt++) 
	if(!gpnt->device) break;
    
    if(i >= sg_template.dev_max) panic ("scsi_devices corrupt (sg)");
    
    scsi_generics[i].device=SDp;
    scsi_generics[i].users=0;
    scsi_generics[i].generic_wait=NULL;
    scsi_generics[i].read_wait=NULL;
    scsi_generics[i].write_wait=NULL;
    scsi_generics[i].buff=NULL;
    scsi_generics[i].exclude=0;
    scsi_generics[i].pending=0;
    scsi_generics[i].timeout=SG_DEFAULT_TIMEOUT;
    sg_template.nr_dev++;
    return 0;
};



static void sg_detach(Scsi_Device * SDp)
{
    struct scsi_generic * gpnt;
    int i;
    
    for(gpnt = scsi_generics, i=0; i<sg_template.dev_max; i++, gpnt++) 
	if(gpnt->device == SDp) {
	    gpnt->device = NULL;
	    SDp->attached--;
	    sg_template.nr_dev--;
            /* 
             * avoid associated device /dev/sg? bying incremented 
             * each time module is inserted/removed , <dan@lectra.fr>
             */
            sg_template.dev_noticed--;
	    return;
	}
    return;
}

#ifdef MODULE

int init_module(void) {
    sg_template.usage_count = &mod_use_count_;
    return scsi_register_module(MODULE_SCSI_DEV, &sg_template);
}

void cleanup_module( void) 
{
    scsi_unregister_module(MODULE_SCSI_DEV, &sg_template);
    unregister_chrdev(SCSI_GENERIC_MAJOR, "sg");
    
    if(scsi_generics != NULL) {
	scsi_init_free((char *) scsi_generics,
		       (sg_template.dev_noticed + SG_EXTRA_DEVS) 
		       * sizeof(struct scsi_generic));
    }
    sg_template.dev_max = 0;
#ifdef SG_BIG_BUFF
    if(big_buff != NULL)
	scsi_init_free(big_buff, SG_BIG_BUFF);
#endif
}
#endif /* MODULE */

/*
 * Overrides for Emacs so that we almost follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
