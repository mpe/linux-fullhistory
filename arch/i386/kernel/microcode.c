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
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/smp_lock.h>
#include <linux/proc_fs.h>

#include <asm/msr.h>
#include <asm/uaccess.h>
#include <asm/processor.h>

#define MICROCODE_VERSION 	"1.0"

MODULE_DESCRIPTION("CPU (P6) microcode update driver");
MODULE_AUTHOR("Tigran Aivazian <tigran@ocston.org>");
EXPORT_NO_SYMBOLS;

/* VFS interface */
static int microcode_open(struct inode *, struct file *);
static int microcode_release(struct inode *, struct file *);
static ssize_t microcode_write(struct file *, const char *, size_t, loff_t *);


/* internal helpers to do the work */
static void do_microcode_update(void);
static void do_update_one(void *);

/*
 *  Bits in microcode_status. (31 bits of room for future expansion)
 */
#define MICROCODE_IS_OPEN	0	/* set if /dev/microcode is in use */
static unsigned long microcode_status = 0;

/* the actual array of microcode blocks, each 2048 bytes */
static struct microcode * microcode = NULL;
static unsigned int microcode_num = 0;

static struct file_operations microcode_fops = {
	write:		microcode_write,
	open:		microcode_open,
	release:	microcode_release,
};

static struct inode_operations microcode_inops = {
	default_file_ops:	&microcode_fops,
};

static struct proc_dir_entry *proc_microcode;

static int __init microcode_init(void)
{
	/* write-only /proc/driver/microcode file, one day may become read-write.. */
	proc_microcode = create_proc_entry("microcode", S_IWUSR, proc_root_driver);
	if (!proc_microcode) {
		printk(KERN_ERR "microcode: can't create /proc/driver/microcode entry\n");
		return -ENOMEM;
	}
	proc_microcode->ops = &microcode_inops;
	printk(KERN_ERR "P6 Microcode Update Driver v%s registered\n", MICROCODE_VERSION);
	return 0;
}

static void __exit microcode_exit(void)
{
	remove_proc_entry("microcode", proc_root_driver);
	printk(KERN_ERR "P6 Microcode Update Driver v%s unregistered\n", MICROCODE_VERSION);
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

	MOD_INC_USE_COUNT;

	return 0;
}

static int microcode_release(struct inode *inode, struct file *file)
{
	MOD_DEC_USE_COUNT;

	clear_bit(MICROCODE_IS_OPEN, &microcode_status);
	return 0;
}


static void do_microcode_update(void)
{
	if (smp_call_function(do_update_one, NULL, 1, 0) != 0)
		panic("do_microcode_update(): timed out waiting for other CPUs\n");
	do_update_one(NULL);
}

static void do_update_one(void *unused)
{
	struct cpuinfo_x86 * c;
	unsigned int pf = 0, val[2], rev, sig;
	int i, id;

	id = smp_processor_id();
	c = cpu_data + id;


	if (c->x86_vendor != X86_VENDOR_INTEL || c->x86 < 6)
		return;

	sig = c->x86_mask + (c->x86_model<<4) + (c->x86<<8);

	if (c->x86_model >= 5) {
		/* get processor flags from BBL_CR_OVRD MSR (0x17) */
		rdmsr(0x17, val[0], val[1]);
		pf = 1 << ((val[1] >> 18) & 7);
	}

	for (i=0; i<microcode_num; i++)
		if (microcode[i].sig == sig && microcode[i].pf == pf &&
		    microcode[i].ldrver == 1 && microcode[i].hdrver == 1) {

			rdmsr(0x8B, val[0], rev);
			if (microcode[i].rev <= rev) {
				printk(KERN_ERR 
					"microcode: CPU%d not 'upgrading' to earlier revision"
					" %d (current=%d)\n", id, microcode[i].rev, rev);
			} else { 
				int sum = 0;
				struct microcode *m = &microcode[i];
				unsigned int *sump = (unsigned int *)(m+1);

				while (--sump >= (unsigned int *)m)
					sum += *sump;
				if (sum != 0) {
					printk(KERN_ERR "microcode: CPU%d aborting, "
							"bad checksum\n", id);
					break;
				}
				wrmsr(0x79, (unsigned int)(m->bits), 0);
				__asm__ __volatile__ ("cpuid");
				rdmsr(0x8B, val[0], val[1]);
				printk(KERN_ERR "microcode: CPU%d microcode updated "
						"from revision %d to %d\n", id, rev, val[1]);
			}
			break;
		}
}

static ssize_t microcode_write(struct file *file, const char *buf, size_t len, loff_t *ppos)
{
	if (len % sizeof(struct microcode) != 0) {
		printk(KERN_ERR "microcode: can only write in N*%d bytes units\n", 
			sizeof(struct microcode));
		return -EINVAL;
		return -EINVAL;
	}
	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	lock_kernel();
	microcode_num = len/sizeof(struct microcode);
	microcode = vmalloc(len);
	if (!microcode) {
		unlock_kernel();
		return -ENOMEM;
	}
	if (copy_from_user(microcode, buf, len)) {
		vfree(microcode);
		unlock_kernel();
		return -EFAULT;
	}
	do_microcode_update();
	vfree(microcode);
	unlock_kernel();
	return len;
}
