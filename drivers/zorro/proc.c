/*
 *	$Id: proc.c,v 1.1.2.1 1998/06/07 23:21:01 geert Exp $
 *
 *	Procfs interface for the Zorro bus.
 *
 *	Copyright (C) 1998 Geert Uytterhoeven
 *
 *	Heavily based on the procfs interface for the PCI bus, which is
 *
 *	Copyright (C) 1997, 1998 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 */

#include <linux/types.h>
#include <linux/zorro.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/amigahw.h>
#include <asm/setup.h>

static loff_t
proc_bus_zorro_lseek(struct file *file, loff_t off, int whence)
{
	loff_t new;

	switch (whence) {
	case 0:
		new = off;
		break;
	case 1:
		new = file->f_pos + off;
		break;
	case 2:
		new = sizeof(struct ConfigDev) + off;
		break;
	default:
		return -EINVAL;
	}
	if (new < 0 || new > sizeof(struct ConfigDev))
		return -EINVAL;
	return (file->f_pos = new);
}

static ssize_t
proc_bus_zorro_read(struct file *file, char *buf, size_t nbytes, loff_t *ppos)
{
	struct inode *ino = file->f_dentry->d_inode;
	struct proc_dir_entry *dp = ino->u.generic_ip;
	struct ConfigDev *cd = dp->data;
	int pos = *ppos;

	if (pos >= sizeof(struct ConfigDev))
		return 0;
	if (nbytes >= sizeof(struct ConfigDev))
		nbytes = sizeof(struct ConfigDev);
	if (pos + nbytes > sizeof(struct ConfigDev))
		nbytes = sizeof(struct ConfigDev) - pos;
	if (copy_to_user(buf, cd, nbytes))
		return -EFAULT;
	*ppos += nbytes;

	return nbytes;
}

static struct file_operations proc_bus_zorro_operations = {
	proc_bus_zorro_lseek,
	proc_bus_zorro_read,
	NULL,		/* write */
	NULL,		/* readdir */
	NULL,		/* poll */
	NULL,		/* ioctl */
	NULL,		/* mmap */
	NULL,		/* no special open code */
	NULL,		/* flush */
	NULL,		/* no special release code */
	NULL		/* can't fsync */
};

static struct inode_operations proc_bus_zorro_inode_operations = {
	&proc_bus_zorro_operations, /* default base directory file-ops */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

int
get_zorro_dev_info(char *buf, char **start, off_t pos, int count, int wr)
{
	u_int slot;
	off_t at = 0;
	int len, cnt;

	for (slot = cnt = 0; slot < zorro_num_autocon && count > cnt; slot++) {
		struct ConfigDev *cd = &zorro_autocon[slot];
		u16 manuf = cd->cd_Rom.er_Manufacturer;
		u8 prod = cd->cd_Rom.er_Product;
		u8 epc;
		if (manuf == ZORRO_MANUF(ZORRO_PROD_GVP_EPC_BASE) &&
		    prod == ZORRO_PROD(ZORRO_PROD_GVP_EPC_BASE)) {
		    /* GVP quirk */
		    u32 addr = (u32)cd->cd_BoardAddr;
		    epc = (*(u16 *)ZTWO_VADDR(addr+0x8000)) & GVP_PRODMASK;
		} else
		    epc = 0;
		len = sprintf(buf, "%02x\t%04x%02x%02x\t%08x\t%08x\t%02x\n",
			      slot, manuf, prod, epc, (u32)cd->cd_BoardAddr,
			      cd->cd_BoardSize, cd->cd_Rom.er_Type);
		at += len;
		if (at >= pos) {
			if (!*start) {
				*start = buf + (pos - (at - len));
				cnt = at - pos;
			} else
				cnt += len;
			buf += len;
		}
	}
	return (count > cnt) ? cnt : count;
}

static struct proc_dir_entry proc_zorro_devices = {
	PROC_BUS_ZORRO_DEVICES, 7, "devices",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations,
	get_zorro_dev_info
};

static struct proc_dir_entry *proc_bus_zorro_dir;

__initfunc(static int zorro_proc_attach_device(u_int slot))
{
	struct proc_dir_entry *entry;
	char name[4];

	sprintf(name, "%02x", slot);
	entry = create_proc_entry(name, S_IFREG | S_IRUGO, proc_bus_zorro_dir);
	if (!entry)
		return -ENOMEM;
	entry->ops = &proc_bus_zorro_inode_operations;
	entry->data = &zorro_autocon[slot];
	entry->size = sizeof(struct ConfigDev);
	return 0;
}

__initfunc(void zorro_proc_init(void))
{
	u_int slot;

	if (!MACH_IS_AMIGA || !AMIGAHW_PRESENT(ZORRO))
		return;
	proc_bus_zorro_dir = create_proc_entry("zorro", S_IFDIR, proc_bus);
	proc_register(proc_bus_zorro_dir, &proc_zorro_devices);
	for (slot = 0; slot < zorro_num_autocon; slot++)
	    zorro_proc_attach_device(slot);
}
