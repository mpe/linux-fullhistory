/*
 * IA32 Architecture-specific ioctl shim code
 *
 * Copyright (C) 2000 VA Linux Co
 * Copyright (C) 2000 Don Dugger <n0ano@valinux.com>
 */

#include <linux/types.h>
#include <linux/dirent.h>
#include <linux/msdos_fs.h>
#include <linux/mtio.h>
#include <linux/ncp_fs.h>
#include <linux/capi.h>
#include <linux/videodev.h>
#include <linux/synclink.h>
#include <linux/atmdev.h>
#include <linux/atm_eni.h>
#include <linux/atm_nicstar.h>
#include <linux/atm_zatm.h>
#include <linux/atm_idt77105.h>
#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>
#include <linux/ixjuser.h>
#include <linux/i2o-dev.h>

asmlinkage long sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg);

asmlinkage long ia32_ioctl(unsigned int fd, unsigned int cmd, unsigned int arg)
{

	switch (cmd) {

	case VFAT_IOCTL_READDIR_BOTH:
	case VFAT_IOCTL_READDIR_SHORT:
	case MTIOCGET:
	case MTIOCPOS:
	case MTIOCGETCONFIG:
	case MTIOCSETCONFIG:
	case PPPIOCSCOMPRESS:
	case PPPIOCGIDLE:
	case NCP_IOC_GET_FS_INFO_V2:
	case NCP_IOC_GETOBJECTNAME:
	case NCP_IOC_SETOBJECTNAME:
	case NCP_IOC_GETPRIVATEDATA:
	case NCP_IOC_SETPRIVATEDATA:
	case NCP_IOC_GETMOUNTUID2:
	case CAPI_MANUFACTURER_CMD:
	case VIDIOCGTUNER:
	case VIDIOCSTUNER:
	case VIDIOCGWIN:
	case VIDIOCSWIN:
	case VIDIOCGFBUF:
	case VIDIOCSFBUF:
	case MGSL_IOCSPARAMS:
	case MGSL_IOCGPARAMS:
	case ATM_GETNAMES:
	case ATM_GETLINKRATE:
	case ATM_GETTYPE:
	case ATM_GETESI:
	case ATM_GETADDR:
	case ATM_RSTADDR:
	case ATM_ADDADDR:
	case ATM_DELADDR:
	case ATM_GETCIRANGE:
	case ATM_SETCIRANGE:
	case ATM_SETESI:
	case ATM_SETESIF:
	case ATM_GETSTAT:
	case ATM_GETSTATZ:
	case ATM_GETLOOP:
	case ATM_SETLOOP:
	case ATM_QUERYLOOP:
	case ENI_SETMULT:
	case NS_GETPSTAT:
	/* case NS_SETBUFLEV: This is a duplicate case with ZATM_GETPOOLZ */
	case ZATM_GETPOOLZ:
	case ZATM_GETPOOL:
	case ZATM_SETPOOL:
	case ZATM_GETTHIST:
	case IDT77105_GETSTAT:
	case IDT77105_GETSTATZ:
	case IXJCTL_TONE_CADENCE:
	case IXJCTL_FRAMES_READ:
	case IXJCTL_FRAMES_WRITTEN:
	case IXJCTL_READ_WAIT:
	case IXJCTL_WRITE_WAIT:
	case IXJCTL_DRYBUFFER_READ:
	case I2OHRTGET:
	case I2OLCTGET:
	case I2OPARMSET:
	case I2OPARMGET:
	case I2OSWDL:
	case I2OSWUL:
	case I2OSWDEL:
	case I2OHTML:
		printk("%x:unimplemented IA32 ioctl system call\n", cmd);
		return(-EINVAL);
	default:
		return(sys_ioctl(fd, cmd, (unsigned long)arg));

	}
}
