/*
 *	CPU Microcode Update interface for Linux
 *
 *	Copyright (C) 2000 Tigran Aivazian
 *
 *	This driver allows to upgrade microcode on Intel processors
 *	belonging to P6 family - PentiumPro, Pentium II, Pentium III etc.
 *
 *	Reference: Section 8.10 of Volume III, Intel Pentium III Manual, 
 *	Order Number 243192 or download from:
 *		
 *	http://developer.intel.com/design/pentiumii/manuals/243192.htm
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	1.0	16 February 2000, Tigran Aivazian <tigran@sco.com>
 *		Initial release.
 *	1.01	18 February 2000, Tigran Aivazian <tigran@sco.com>
 *		Added read() support + cleanups.
 *	1.02	21 February 2000, Tigran Aivazian <tigran@sco.com>
 *		Added 'device trimming' support. open(O_WRONLY) zeroes
 *		and frees the saved copy of applied microcode.
 *	1.03	29 February 2000, Tigran Aivazian <tigran@sco.com>
 *		Made to use devfs (/dev/cpu/microcode) + cleanups.
 *	1.04	06 June 2000, Simon Trimmer <simon@veritas.com>
 *		Added misc device support (now uses both devfs and misc).
 *		Added MICROCODE_IOCFREE ioctl to clear memory.
 *	1.05	09 June 2000, Simon Trimmer <simon@veritas.com>
 *		Messages for error cases (non intel & no suitable microcode).
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/smp_lock.h>
#include <linux/miscdevice.h>
#include <linux/devfs_fs_kernel.h>

#include <asm/msr.h>
#include <asm/uaccess.h>
#include <asm/processor.h>

#define MICROCODE_VERSION 	"1.05"

MODULE_DESCRIPTION("Intel CPU (P6) microcode update driver");
MODULE_AUTHOR("Tigran Aivazian <tigran@veritas.com>");
EXPORT_NO_SYMBOLS;

/* VFS interface */
static int microcode_open(struct inode *, struct file *);
static int microcode_release(struct inode *, struct file *);
static ssize_t microcode_read(struct file *, char *, size_t, loff_t *);
static ssize_t microcode_write(struct file *, const char *, size_t, loff_t *);
static int microcode_ioctl(struct inode *, struct file *, unsigned int, unsigned long);


/* internal helpers to do the work */
static int do_microcode_update(void);
static void do_update_one(void *);

/*
 *  Bits in microcode_status. (31 bits of room for future expansion)
 */
#define MICROCODE_IS_OPEN	0	/* set if device is in use */

static unsigned long microcode_status;

/* the actual array of microcode blocks, each 2048 bytes */
static struct microcode *microcode;
static unsigned int microcode_num;
static char *mc_applied; /* holds an array of applied microcode blocks */
static unsigned int mc_fsize;

static struct file_operations microcode_fops = {
	owner:		THIS_MODULE,
	read:		microcode_read,
	write:		microcode_write,
	ioctl:		microcode_ioctl,
	open:		microcode_open,
	release:	microcode_release,
};

static struct miscdevice microcode_dev = {
	minor: MICROCODE_MINOR,
	name:	"microcode",
	fops:	&microcode_fops,
};

static devfs_handle_t devfs_handle;

static int __init microcode_init(void)
{
	int error = 0;

	if (misc_register(&microcode_dev) < 0) {
		printk(KERN_WARNING 
			"microcode: can't misc_register on minor=%d\n",
			MICROCODE_MINOR);
		error = 1;
	}
	devfs_handle = devfs_register(NULL, "cpu/microcode",
			DEVFS_FL_DEFAULT, 0, 0, S_IFREG | S_IRUSR | S_IWUSR, 
			&microcode_fops, NULL);
	if (devfs_handle == NULL && error) {
		printk(KERN_ERR "microcode: failed to devfs_register()\n");
		return -EINVAL;
	}
	printk(KERN_INFO "P6 Microcode Update Driver v%s registered\n", 
			MICROCODE_VERSION);
	return 0;
}

static void __exit microcode_exit(void)
{
	misc_deregister(&microcode_dev);
	devfs_unregister(devfs_handle);
	if (mc_applied)
		kfree(mc_applied);
	printk(KERN_INFO "P6 Microcode Update Driver v%s unregistered\n", 
			MICROCODE_VERSION);
}

module_init(microcode_init);
module_exit(microcode_exit);

/*
 * We enforce only one user at a time here with open/close.
 */
static int microcode_open(struct inode *inode, struct file *file)
{
	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	/* one at a time, please */
	if (test_and_set_bit(MICROCODE_IS_OPEN, &microcode_status))
		return -EBUSY;

	return 0;
}

static int microcode_release(struct inode *inode, struct file *file)
{
	clear_bit(MICROCODE_IS_OPEN, &microcode_status);
	return 0;
}

/* a pointer to 'struct update_req' is passed to the IPI handler = do_update_one()
 * update_req[cpu].err is set to 1 if update failed on 'cpu', 0 otherwise
 * if err==0, microcode[update_req[cpu].slot] points to applied block of microcode
 */
struct update_req {
	int err;
	int slot;
} update_req[NR_CPUS];

static int do_microcode_update(void)
{
	int i, error = 0, err;
	struct microcode *m;

	if (smp_call_function(do_update_one, (void *)update_req, 1, 1) != 0)
		panic("do_microcode_update(): timed out waiting for other CPUs\n");
	do_update_one((void *)update_req);

	for (i=0; i<smp_num_cpus; i++) {
		err = update_req[i].err;
		error += err;
		if (!err) {
			m = (struct microcode *)mc_applied + i;
			memcpy(m, &microcode[update_req[i].slot], sizeof(struct microcode));
		}
	}
	return error;
}

static void do_update_one(void *arg)
{
	int cpu_num = smp_processor_id();
	struct cpuinfo_x86 *c = cpu_data + cpu_num;
	struct update_req *req = (struct update_req *)arg + cpu_num;
	unsigned int pf = 0, val[2], rev, sig;
	int i,found=0;

	req->err = 1; /* be pessimistic */

	if (c->x86_vendor != X86_VENDOR_INTEL || c->x86 < 6){
		printk(KERN_ERR "microcode: CPU%d not an Intel P6\n", cpu_num );
		return;
	}

	sig = c->x86_mask + (c->x86_model<<4) + (c->x86<<8);

	if (c->x86_model >= 5) {
		/* get processor flags from BBL_CR_OVRD MSR (0x17) */
		rdmsr(0x17, val[0], val[1]);
		pf = 1 << ((val[1] >> 18) & 7);
	}

	for (i=0; i<microcode_num; i++)
		if (microcode[i].sig == sig && microcode[i].pf == pf &&
		    microcode[i].ldrver == 1 && microcode[i].hdrver == 1) {

			found=1;

			rdmsr(0x8B, val[0], rev);
			if (microcode[i].rev <= rev) {
				printk(KERN_ERR 
					"microcode: CPU%d not 'upgrading' to earlier revision"
					" %d (current=%d)\n", cpu_num, microcode[i].rev, rev);
			} else { 
				int sum = 0;
				struct microcode *m = &microcode[i];
				unsigned int *sump = (unsigned int *)(m+1);

				while (--sump >= (unsigned int *)m)
					sum += *sump;
				if (sum != 0) {
					printk(KERN_ERR "microcode: CPU%d aborting, "
							"bad checksum\n", cpu_num);
					break;
				}

				wrmsr(0x79, (unsigned int)(m->bits), 0);
				__asm__ __volatile__ ("cpuid" : : : "ax", "bx", "cx", "dx", "cc");
				rdmsr(0x8B, val[0], val[1]);

				req->err = 0;
				req->slot = i;
				printk(KERN_ERR "microcode: CPU%d updated from revision "
						"%d to %d, date=%08x\n", 
						cpu_num, rev, val[1], m->date);
			}
			break;
		}

	if(!found)
		printk(KERN_ERR "microcode: found no data for CPU%d (sig=%x, pflags=%d)\n", cpu_num, sig, pf);
}

static ssize_t microcode_read(struct file *file, char *buf, size_t len, loff_t *ppos)
{
	if (*ppos >= mc_fsize)
		return 0;
	if (*ppos + len > mc_fsize)
		len = mc_fsize - *ppos;
	if (copy_to_user(buf, mc_applied + *ppos, len))
		return -EFAULT;
	*ppos += len;
	return len;
}

static ssize_t microcode_write(struct file *file, const char *buf, size_t len, loff_t *ppos)
{
	ssize_t ret;

	if (len % sizeof(struct microcode) != 0) {
		printk(KERN_ERR "microcode: can only write in N*%d bytes units\n", 
			sizeof(struct microcode));
		return -EINVAL;
	}
	if (!mc_applied) {
		mc_applied = kmalloc(smp_num_cpus*sizeof(struct microcode),
				GFP_KERNEL);
		if (!mc_applied) {
			printk(KERN_ERR "microcode: out of memory for saved microcode\n");
			return -ENOMEM;
		}
		memset(mc_applied, 0, mc_fsize);
	}
	
	lock_kernel();
	microcode_num = len/sizeof(struct microcode);
	microcode = vmalloc(len);
	if (!microcode) {
		ret = -ENOMEM;
		goto out_unlock;
	}
	if (copy_from_user(microcode, buf, len)) {
		ret = -EFAULT;
		goto out_fsize;
	}
	if(do_microcode_update()) {
		ret = -EIO;
		goto out_fsize;
	} else {
		mc_fsize = smp_num_cpus * sizeof(struct microcode);
		ret = (ssize_t)len;
	}
out_fsize:
	devfs_set_file_size(devfs_handle, mc_fsize);
	vfree(microcode);
out_unlock:
	unlock_kernel();
	return ret;
}

static int microcode_ioctl(struct inode *inode, struct file *file, 
		unsigned int cmd, unsigned long arg)
{
	switch(cmd) {
		case MICROCODE_IOCFREE:
			if (mc_applied) {
				devfs_set_file_size(devfs_handle, 0);
				memset(mc_applied, 0, mc_fsize);
				kfree(mc_applied);
				mc_applied = NULL;
				printk(KERN_WARNING 
					"microcode: freed %d bytes\n", mc_fsize);
				mc_fsize = 0;
				return 0;
			}
			return -ENODATA;

		default:
			printk(KERN_ERR "microcode: unknown ioctl cmd=%d\n",
					cmd);
			return -EINVAL;
	}
	/* NOT REACHED */
	return -EINVAL;
}
