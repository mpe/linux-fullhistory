#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <asm/segment.h>
#include <linux/errno.h>

#include "../blk.h"
#include "scsi.h"
#include "sr.h"
#include "scsi_ioctl.h"

#include <linux/cdrom.h>

#define IOCTL_RETRIES 3
/* The CDROM is fairly slow, so we need a little extra time */
#define IOCTL_TIMEOUT 200

extern int scsi_ioctl (Scsi_Device *dev, int cmd, void *arg);

static void sr_ioctl_done(Scsi_Cmnd * SCpnt)
{
  struct request * req;
  struct task_struct * p;
  
  req = &SCpnt->request;
  req->dev = 0xfffe; /* Busy, but indicate request done */
  
  if ((p = req->waiting) != NULL) {
    req->waiting = NULL;
    p->state = TASK_RUNNING;
    if (p->counter > current->counter)
      need_resched = 1;
  }
}

/* We do our own retries because we want to know what the specific
   error code is.  Normally the UNIT_ATTENTION code will automatically
   clear after one error */

static int do_ioctl(int target, unsigned char * sr_cmd)
{
	Scsi_Cmnd * SCpnt;
	int result;

	SCpnt = allocate_device(NULL, scsi_CDs[target].device->index, 1);
	scsi_do_cmd(SCpnt,
		    (void *) sr_cmd, NULL, 255, sr_ioctl_done, 
		    IOCTL_TIMEOUT, IOCTL_RETRIES);


	if (SCpnt->request.dev != 0xfffe){
	  SCpnt->request.waiting = current;
	  current->state = TASK_UNINTERRUPTIBLE;
	  while (SCpnt->request.dev != 0xfffe) schedule();
	};

	result = SCpnt->result;

/* Minimal error checking.  Ignore cases we know about, and report the rest. */
	if(driver_byte(result) != 0)
	  switch(SCpnt->sense_buffer[2] & 0xf) {
	  case UNIT_ATTENTION:
	    scsi_CDs[target].device->changed = 1;
	    printk("Disc change detected.\n");
	    break;
	  case NOT_READY: /* This happens if there is no disc in drive */
	    printk("CDROM not ready.  Make sure there is a disc in the drive.\n");
	    break;
	  case ILLEGAL_REQUEST:
	    printk("CDROM (ioctl) reports ILLEGAL REQUEST.\n");
	    break;
	  default:
	    printk("SCSI CD error: host %d id %d lun %d return code = %03x\n", 
		   scsi_CDs[target].device->host_no, 
		   scsi_CDs[target].device->id,
		   scsi_CDs[target].device->lun,
		   result);
	    printk("\tSense class %x, sense error %x, extended sense %x\n",
		   sense_class(SCpnt->sense_buffer[0]), 
		   sense_error(SCpnt->sense_buffer[0]),
		   SCpnt->sense_buffer[2] & 0xf);
	    
	};

	result = SCpnt->result;
	SCpnt->request.dev = -1; /* Deallocate */
	wake_up(&scsi_devices[SCpnt->index].device_wait);
	/* Wake up a process waiting for device*/
      	return result;
}

int sr_ioctl(struct inode * inode, struct file * file, unsigned long cmd, unsigned long arg)
{
        u_char 	sr_cmd[10];

	int dev = inode->i_rdev;
	int result, target;

	target = MINOR(dev);

	switch (cmd) 
		{
		/* Sun-compatible */
		case CDROMPAUSE:

			sr_cmd[0] = SCMD_PAUSE_RESUME;
			sr_cmd[1] = scsi_CDs[target].device->lun << 5;
			sr_cmd[2] = sr_cmd[3] = sr_cmd[4] = 0;
			sr_cmd[5] = sr_cmd[6] = sr_cmd[7] = 0;
			sr_cmd[8] = 1;
			sr_cmd[9] = 0;

			result = do_ioctl(target, sr_cmd);
			return result;

		case CDROMRESUME:

			sr_cmd[0] = SCMD_PAUSE_RESUME;
			sr_cmd[1] = scsi_CDs[target].device->lun << 5;
			sr_cmd[2] = sr_cmd[3] = sr_cmd[4] = 0;
			sr_cmd[5] = sr_cmd[6] = sr_cmd[7] = 0;
			sr_cmd[8] = 0;
			sr_cmd[9] = 0;

			result = do_ioctl(target, sr_cmd);

			return result;

		case CDROMPLAYMSF:
			{
			struct cdrom_msf msf;
			memcpy_fromfs(&msf, (void *) arg, sizeof(msf));

			sr_cmd[0] = SCMD_PLAYAUDIO_MSF;
			sr_cmd[1] = scsi_CDs[target].device->lun << 5;
			sr_cmd[2] = 0;
			sr_cmd[3] = msf.cdmsf_min0;
			sr_cmd[4] = msf.cdmsf_sec0;
			sr_cmd[5] = msf.cdmsf_frame0;
			sr_cmd[6] = msf.cdmsf_min1;
			sr_cmd[7] = msf.cdmsf_sec1;
			sr_cmd[8] = msf.cdmsf_frame1;
			sr_cmd[9] = 0;

			result = do_ioctl(target, sr_cmd);
			return result;
			}

		case CDROMPLAYTRKIND:
			{
			struct cdrom_ti ti;
			memcpy_fromfs(&ti, (void *) arg, sizeof(ti));

			sr_cmd[0] = SCMD_PLAYAUDIO_TI;
			sr_cmd[1] = scsi_CDs[target].device->lun << 5;
			sr_cmd[2] = 0;
			sr_cmd[3] = 0;
			sr_cmd[4] = ti.cdti_trk0;
			sr_cmd[5] = ti.cdti_ind0;
			sr_cmd[6] = 0;
			sr_cmd[7] = ti.cdti_trk1;
			sr_cmd[8] = ti.cdti_ind1;
			sr_cmd[9] = 0;

			result = do_ioctl(target, sr_cmd);

			return result;
			}

		case CDROMREADTOCHDR:
			return -EINVAL;
		case CDROMREADTOCENTRY:
			return -EINVAL;

		case CDROMSTOP:
		        sr_cmd[0] = START_STOP;
			sr_cmd[1] = ((scsi_CDs[target].device->lun) << 5) | 1;
			sr_cmd[2] = sr_cmd[3] = sr_cmd[5] = 0;
			sr_cmd[4] = 0;

			result = do_ioctl(target, sr_cmd);
			return result;
			
		case CDROMSTART:
		        sr_cmd[0] = START_STOP;
			sr_cmd[1] = ((scsi_CDs[target].device->lun) << 5) | 1;
			sr_cmd[2] = sr_cmd[3] = sr_cmd[5] = 0;
			sr_cmd[4] = 1;

			result = do_ioctl(target, sr_cmd);
			return result;

		case CDROMEJECT:
		        sr_cmd[0] = START_STOP;
			sr_cmd[1] = ((scsi_CDs[target].device->lun) << 5) | 1;
			sr_cmd[2] = sr_cmd[3] = sr_cmd[5] = 0;
			sr_cmd[4] = 0x02;

			result = do_ioctl(target, sr_cmd);
			return result;

		case CDROMVOLCTRL:
			return -EINVAL;
		case CDROMSUBCHNL:
			return -EINVAL;
		case CDROMREADMODE2:
			return -EINVAL;
		case CDROMREADMODE1:
			return -EINVAL;

		RO_IOCTLS(dev,arg);
		default:
			return scsi_ioctl(scsi_CDs[target].device,cmd,(void *) arg);
		}
}
