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

#define MICROCODE_VERSION 	"1.02"

MODULE_DESCRIPTION("CPU (P6) microcode update driver");
MODULE_AUTHOR("Tigran Aivazian <tigran@ocston.org>");
EXPORT_NO_SYMBOLS;

/* VFS interface */
static int microcode_open(struct inode *, struct file *);
static int microcode_release(struct inode *, struct file *);
static ssize_t microcode_read(struct file *, char *, size_t, loff_t *);
static ssize_t microcode_write(struct file *, const char *, size_t, loff_t *);


/* internal helpers to do the work */
static int do_microcode_update(void);
static void do_update_one(void *);

/*
 *  Bits in microcode_status. (31 bits of room for future expansion)
 */
#define MICROCODE_IS_OPEN	0	/* set if device is in use */
static unsigned long microcode_status = 0;

/* the actual array of microcode blocks, each 2048 bytes */
static struct microcode * microcode = NULL;
static unsigned int microcode_num = 0;
static char *mc_applied = NULL; /* holds an array of applied microcode blocks */

static struct file_operations microcode_fops = {
	read:		microcode_read,
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
	proc_microcode = create_proc_entry("microcode", S_IWUSR|S_IRUSR, proc_root_driver);
	if (!proc_microcode) {
		printk(KERN_ERR "microcode: can't create /proc/driver/microcode\n");
		return -ENOMEM;
	}
	proc_microcode->ops = &microcode_inops;
	printk(KERN_INFO "P6 Microcode Update Driver v%s registered\n", MICROCODE_VERSION);
	return 0;
}

static void __exit microcode_exit(void)
{
	remove_proc_entry("microcode", proc_root_driver);
	if (mc_applied)
		kfree(mc_applied);
	printk(KERN_INFO "P6 Microcode Update Driver v%s unregistered\n", MICROCODE_VERSION);
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

	if ((file->f_flags & O_ACCMODE) == O_WRONLY) {
		proc_microcode->size = 0;
		if (mc_applied) {
			memset(mc_applied, 0, smp_num_cpus * sizeof(struct microcode));
			kfree(mc_applied);
			mc_applied = NULL;
		}
	}

	MOD_INC_USE_COUNT;

	return 0;
}

static int microcode_release(struct inode *inode, struct file *file)
{
	MOD_DEC_USE_COUNT;

	clear_bit(MICROCODE_IS_OPEN, &microcode_status);
	return 0;
}

/* a pointer to 'struct update_req' is passed to the IPI hanlder = do_update_one()
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
	return error ? -EIO : 0;
}

static void do_update_one(void *arg)
{
	struct update_req *req;
	struct cpuinfo_x86 * c;
	unsigned int pf = 0, val[2], rev, sig;
	int i, cpu_num;

	cpu_num = smp_processor_id();
	c = cpu_data + cpu_num;
	req = (struct update_req *)arg + cpu_num;
	req->err = 1; /* be pessimistic */

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
				__asm__ __volatile__ ("cpuid");
				rdmsr(0x8B, val[0], val[1]);

				req->err = 0;
				req->slot = i;
				printk(KERN_ERR "microcode: CPU%d microcode updated "
						"from revision %d to %d, date=%08x\n", 
						cpu_num, rev, val[1], m->date);
			}
			break;
		}
}

static ssize_t microcode_read(struct file *file, char *buf, size_t len, loff_t *ppos)
{
	size_t fsize = smp_num_cpus * sizeof(struct microcode);

	if (!proc_microcode->size || *ppos >= fsize)
		return 0; /* EOF */
	if (*ppos + len > fsize)
		len = fsize - *ppos;
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
		int size = smp_num_cpus * sizeof(struct microcode);
		mc_applied = kmalloc(size, GFP_KERNEL);
		if (!mc_applied) {
			printk(KERN_ERR "microcode: can't allocate memory for saved microcode\n");
			return -ENOMEM;
		}
		memset(mc_applied, 0, size);
	}
	
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
	ret = do_microcode_update();
	if (!ret) {
		proc_microcode->size = smp_num_cpus * sizeof(struct microcode);
		ret = (ssize_t)len;
	}
	vfree(microcode);
	unlock_kernel();
	return ret;
}
