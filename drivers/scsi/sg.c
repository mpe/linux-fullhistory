/*
   History:
    Started: Aug 9 by Lawrence Foard (entropy@world.std.com), to allow user 
     process control of SCSI devices.
    Development Sponsored by Killy Corp. NY NY
    
    Borrows code from st driver.
*/

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

#include "../block/blk.h"
#include "scsi.h"
#include "hosts.h"
#include "scsi_ioctl.h"
#include "sg.h"

static void sg_init(void);
static int sg_attach(Scsi_Device *);
static int sg_detect(Scsi_Device *);
static void sg_detach(Scsi_Device *);


struct Scsi_Device_Template sg_template = {NULL, NULL, "sg", 0xff, 
					     SCSI_GENERIC_MAJOR, 0, 0, 0, 0,
					     sg_detect, sg_init,
					     NULL, sg_attach, sg_detach};

#ifdef SG_BIG_BUFF
static char *big_buff;
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
  int dev = MINOR(inode->i_rdev);
  if ((dev<0) || (dev>=sg_template.dev_max))
   return -ENXIO;
  switch(cmd_in)
   {
    case SG_SET_TIMEOUT:
     scsi_generics[dev].timeout=get_fs_long((int *) arg);
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
   while(scsi_generics[dev].exclude)
    {
     if (flags & O_NONBLOCK)
      return -EBUSY;
     interruptible_sleep_on(&scsi_generics[dev].generic_wait);
     if (current->signal & ~current->blocked)
      return -ERESTARTSYS;
    }
  if (!scsi_generics[dev].users && scsi_generics[dev].pending && scsi_generics[dev].complete)
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
  scsi_generics[dev].users++;
  return 0;
 }

static void sg_close(struct inode * inode, struct file * filp)
 {
  int dev=MINOR(inode->i_rdev);
  scsi_generics[dev].users--;
  if (scsi_generics[dev].device->host->hostt->usage_count)
    (*scsi_generics[dev].device->host->hostt->usage_count)--;
  scsi_generics[dev].exclude=0;
  wake_up(&scsi_generics[dev].generic_wait);
 }

static char *sg_malloc(int size)
 {
  if (size<=4096)
   return (char *) scsi_malloc(size);
#ifdef SG_BIG_BUFF
  if (size<SG_BIG_BUFF)
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

static int sg_read(struct inode *inode,struct file *filp,char *buf,int count)
 {
  int dev=MINOR(inode->i_rdev);
  int i;
  struct scsi_generic *device=&scsi_generics[dev];
  if ((i=verify_area(VERIFY_WRITE,buf,count)))
   return i;
  while(!device->pending || !device->complete)
   {
    if (filp->f_flags & O_NONBLOCK)
     return -EWOULDBLOCK;
    interruptible_sleep_on(&device->read_wait);
    if (current->signal & ~current->blocked)
     return -ERESTARTSYS;
   }
  device->header.pack_len=device->header.reply_len;
  device->header.result=0;
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
   count=0;
  sg_free(device->buff,device->buff_len);
  device->buff = NULL;
  device->pending=0;
  wake_up(&device->write_wait);
  return count;
 }

static void sg_command_done(Scsi_Cmnd * SCpnt)
 {
  int dev=SCpnt->request.dev;
  struct scsi_generic *device=&scsi_generics[dev];
  if (!device->pending)
   {
    printk("unexpected done for sg %d\n",dev);
    SCpnt->request.dev=-1;
    return;
   }
  memcpy(device->header.sense_buffer, SCpnt->sense_buffer, sizeof(SCpnt->sense_buffer));
  if (SCpnt->sense_buffer[0])
   {
    device->header.result=EIO;
   }
  else
   device->header.result=SCpnt->result;
  device->complete=1;
  SCpnt->request.dev=-1;
  wake_up(&scsi_generics[dev].read_wait);
 }

static int sg_write(struct inode *inode,struct file *filp,char *buf,int count)
 {
  int dev=MINOR(inode->i_rdev);
  Scsi_Cmnd *SCpnt;
  int bsize,size,amt,i;
  unsigned char opcode;
  unsigned char cmnd[MAX_COMMAND_SIZE];
  struct scsi_generic *device=&scsi_generics[dev];

  if ((i=verify_area(VERIFY_READ,buf,count)))
   return i;
  /*
   * The minimum scsi command length is 6 bytes.  If we get anything less than this,
   * it is clearly bogus.
   */
  if (count<(sizeof(struct sg_header) + 6))
   return -EIO;
  /* make sure we can fit */
  while(device->pending)
   {
    if (filp->f_flags & O_NONBLOCK)
     return -EWOULDBLOCK;
#ifdef DEBUG
    printk("sg_write: sleeping on pending request\n");
#endif     
    interruptible_sleep_on(&device->write_wait);
    if (current->signal & ~current->blocked)
     return -ERESTARTSYS;
   }
  device->pending=1;
  device->complete=0;
  memcpy_fromfs(&device->header,buf,sizeof(struct sg_header));
  /* fix input size */
  device->header.pack_len=count;
  buf+=sizeof(struct sg_header);
  bsize=(device->header.pack_len>device->header.reply_len) ? device->header.pack_len : device->header.reply_len;
  bsize-=sizeof(struct sg_header);
  amt=bsize;
  if (!bsize)
   bsize++;
  bsize=(bsize+511) & ~511;
  if ((bsize<0) || !(device->buff=sg_malloc(device->buff_len=bsize)))
   {
    device->pending=0;
    wake_up(&device->write_wait);
    return -ENOMEM;
   }
#ifdef DEBUG
  printk("allocating device\n");
#endif
  if (!(SCpnt=allocate_device(NULL,device->device, !(filp->f_flags & O_NONBLOCK))))
   {
    device->pending=0;
    wake_up(&device->write_wait);
    sg_free(device->buff,device->buff_len);
    device->buff = NULL;
    return -EWOULDBLOCK;
   } 
#ifdef DEBUG
  printk("device allocated\n");
#endif    
  /* now issue command */
  SCpnt->request.dev=dev;
  SCpnt->sense_buffer[0]=0;
  opcode = get_fs_byte(buf);
  size=COMMAND_SIZE(opcode);
  if (opcode >= 0xc0 && device->header.twelve_byte) size = 12;
  SCpnt->cmd_len = size;
  /*
   * Verify that the user has actually passed enough bytes for this command.
   */
  if (count<(sizeof(struct sg_header) + size))
    {
      device->pending=0;
      wake_up(&device->write_wait);
      sg_free(device->buff,device->buff_len);
      device->buff = NULL;
      return -EIO;
    }

  memcpy_fromfs(cmnd,buf,size);
  buf+=size;
  memcpy_fromfs(device->buff,buf,device->header.pack_len-size-sizeof(struct sg_header));
  cmnd[1]=(cmnd[1] & 0x1f) | (device->device->lun<<5);
#ifdef DEBUG
  printk("do cmd\n");
#endif
  scsi_do_cmd (SCpnt,(void *) cmnd,
               (void *) device->buff,amt,
	       sg_command_done,device->timeout,SG_DEFAULT_RETRIES);
#ifdef DEBUG
  printk("done cmd\n");
#endif               
  return count;
 }

static struct file_operations sg_fops = {
   NULL,            /* lseek */
   sg_read,         /* read */
   sg_write,        /* write */
   NULL,            /* readdir */
   NULL,            /* select */
   sg_ioctl,        /* ioctl */
   NULL,            /* mmap */
   sg_open,         /* open */
   sg_close,        /* release */
   NULL		    /* fsync */
};


static int sg_detect(Scsi_Device * SDp){
  ++sg_template.dev_noticed;
  return 1;
}

/* Driver initialization */
static void sg_init()
 {
   static int sg_registered = 0;
   
   if (sg_template.dev_noticed == 0) return;

   if(!sg_registered) {
     if (register_chrdev(SCSI_GENERIC_MAJOR,"sg",&sg_fops)) 
       {
	 printk("Unable to get major %d for generic SCSI device\n",
		SCSI_GENERIC_MAJOR);
	 return;
       }
     sg_registered++;
   }

   /* If we have already been through here, return */
   if(scsi_generics) return;

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
      return;
    }
  return;
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 8
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -8
 * c-argdecl-indent: 8
 * c-label-offset: -8
 * c-continued-statement-offset: 8
 * c-continued-brace-offset: 0
 * End:
 */
