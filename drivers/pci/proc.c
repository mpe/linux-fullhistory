/*
 *	$Id: proc.c,v 1.13 1998/05/12 07:36:07 mj Exp $
 *
 *	Procfs interface for the PCI bus.
 *
 *	Copyright (c) 1997, 1998 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>

#define PCI_CFG_SPACE_SIZE 256

static loff_t
proc_bus_pci_lseek(struct file *file, loff_t off, int whence)
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
		new = PCI_CFG_SPACE_SIZE + off;
		break;
	default:
		return -EINVAL;
	}
	if (new < 0 || new > PCI_CFG_SPACE_SIZE)
		return -EINVAL;
	return (file->f_pos = new);
}

static ssize_t
proc_bus_pci_read(struct file *file, char *buf, size_t nbytes, loff_t *ppos)
{
	struct inode *ino = file->f_dentry->d_inode;
	struct proc_dir_entry *dp = ino->u.generic_ip;
	struct pci_dev *dev = dp->data;
	int pos = *ppos;
	unsigned char bus = dev->bus->number;
	unsigned char dfn = dev->devfn;
	int cnt, size;

	/*
	 * Normal users can read only the standardized portion of the
	 * configuration space as several chips lock up when trying to read
	 * undefined locations (think of Intel PIIX4 as a typical example).
	 */

	if (capable(CAP_SYS_ADMIN))
		size = PCI_CFG_SPACE_SIZE;
	else if (dev->hdr_type == PCI_HEADER_TYPE_CARDBUS)
		size = 128;
	else
		size = 64;

	if (pos >= size)
		return 0;
	if (nbytes >= size)
		nbytes = size;
	if (pos + nbytes > size)
		nbytes = size - pos;
	cnt = nbytes;

	if (!access_ok(VERIFY_WRITE, buf, cnt))
		return -EINVAL;

	if ((pos & 1) && cnt) {
		unsigned char val;
		pcibios_read_config_byte(bus, dfn, pos, &val);
		__put_user(val, buf);
		buf++;
		pos++;
		cnt--;
	}

	if ((pos & 3) && cnt > 2) {
		unsigned short val;
		pcibios_read_config_word(bus, dfn, pos, &val);
		__put_user(cpu_to_le16(val), (unsigned short *) buf);
		buf += 2;
		pos += 2;
		cnt -= 2;
	}

	while (cnt >= 4) {
		unsigned int val;
		pcibios_read_config_dword(bus, dfn, pos, &val);
		__put_user(cpu_to_le32(val), (unsigned int *) buf);
		buf += 4;
		pos += 4;
		cnt -= 4;
	}

	if (cnt >= 2) {
		unsigned short val;
		pcibios_read_config_word(bus, dfn, pos, &val);
		__put_user(cpu_to_le16(val), (unsigned short *) buf);
		buf += 2;
		pos += 2;
		cnt -= 2;
	}

	if (cnt) {
		unsigned char val;
		pcibios_read_config_byte(bus, dfn, pos, &val);
		__put_user(val, buf);
		buf++;
		pos++;
		cnt--;
	}

	*ppos = pos;
	return nbytes;
}

static ssize_t
proc_bus_pci_write(struct file *file, const char *buf, size_t nbytes, loff_t *ppos)
{
	struct inode *ino = file->f_dentry->d_inode;
	struct proc_dir_entry *dp = ino->u.generic_ip;
	struct pci_dev *dev = dp->data;
	int pos = *ppos;
	unsigned char bus = dev->bus->number;
	unsigned char dfn = dev->devfn;
	int cnt;

	if (pos >= PCI_CFG_SPACE_SIZE)
		return 0;
	if (nbytes >= PCI_CFG_SPACE_SIZE)
		nbytes = PCI_CFG_SPACE_SIZE;
	if (pos + nbytes > PCI_CFG_SPACE_SIZE)
		nbytes = PCI_CFG_SPACE_SIZE - pos;
	cnt = nbytes;

	if (!access_ok(VERIFY_READ, buf, cnt))
		return -EINVAL;

	if ((pos & 1) && cnt) {
		unsigned char val;
		__get_user(val, buf);
		pcibios_write_config_byte(bus, dfn, pos, val);
		buf++;
		pos++;
		cnt--;
	}

	if ((pos & 3) && cnt > 2) {
		unsigned short val;
		__get_user(val, (unsigned short *) buf);
		pcibios_write_config_word(bus, dfn, pos, le16_to_cpu(val));
		buf += 2;
		pos += 2;
		cnt -= 2;
	}

	while (cnt >= 4) {
		unsigned int val;
		__get_user(val, (unsigned int *) buf);
		pcibios_write_config_dword(bus, dfn, pos, le32_to_cpu(val));
		buf += 4;
		pos += 4;
		cnt -= 4;
	}

	if (cnt >= 2) {
		unsigned short val;
		__get_user(val, (unsigned short *) buf);
		pcibios_write_config_word(bus, dfn, pos, le16_to_cpu(val));
		buf += 2;
		pos += 2;
		cnt -= 2;
	}

	if (cnt) {
		unsigned char val;
		__get_user(val, buf);
		pcibios_write_config_byte(bus, dfn, pos, val);
		buf++;
		pos++;
		cnt--;
	}

	*ppos = pos;
	return nbytes;
}

static struct file_operations proc_bus_pci_operations = {
	proc_bus_pci_lseek,
	proc_bus_pci_read,
	proc_bus_pci_write,
	NULL,		/* readdir */
	NULL,		/* poll */
	NULL,		/* ioctl */
	NULL,		/* mmap */
	NULL,		/* no special open code */
	NULL,		/* flush */
	NULL,		/* no special release code */
	NULL		/* can't fsync */
};

static struct inode_operations proc_bus_pci_inode_operations = {
	&proc_bus_pci_operations, /* default base directory file-ops */
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
get_pci_dev_info(char *buf, char **start, off_t pos, int count, int wr)
{
	struct pci_dev *dev = pci_devices;
	off_t at = 0;
	int len, i, cnt;

	cnt = 0;
	while (dev && count > cnt) {
		len = sprintf(buf, "%02x%02x\t%04x%04x\t%x",
			dev->bus->number,
			dev->devfn,
			dev->vendor,
			dev->device,
			dev->irq);
		for(i=0; i<6; i++)
			len += sprintf(buf+len,
#if BITS_PER_LONG == 32
						"\t%08lx",
#else
						"\t%016lx",
#endif
					dev->base_address[i]);
		len += sprintf(buf+len,
#if BITS_PER_LONG == 32
					"\t%08lx",
#else
					"\t%016lx",
#endif
			       dev->rom_address);
		buf[len++] = '\n';
		at += len;
		if (at >= pos) {
			if (!*start) {
				*start = buf + (pos - (at - len));
				cnt = at - pos;
			} else
				cnt += len;
			buf += len;
		}
		dev = dev->next;
	}
	return (count > cnt) ? cnt : count;
}

static struct proc_dir_entry proc_pci_devices = {
	PROC_BUS_PCI_DEVICES, 7, "devices",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations,
	get_pci_dev_info
};

static struct proc_dir_entry *proc_bus_pci_dir;

int pci_proc_attach_device(struct pci_dev *dev)
{
	struct pci_bus *bus = dev->bus;
	struct proc_dir_entry *de, *e;
	char name[16];

	if (!(de = bus->procdir)) {
		sprintf(name, "%02x", bus->number);
		de = bus->procdir = create_proc_entry(name, S_IFDIR, proc_bus_pci_dir);
		if (!de)
			return -ENOMEM;
	}
	sprintf(name, "%02x.%x", PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
	e = dev->procent = create_proc_entry(name, S_IFREG | S_IRUGO | S_IWUSR, de);
	if (!e)
		return -ENOMEM;
	e->ops = &proc_bus_pci_inode_operations;
	e->data = dev;
	e->size = PCI_CFG_SPACE_SIZE;
	return 0;
}

int pci_proc_detach_device(struct pci_dev *dev)
{
	struct proc_dir_entry *e;

	if ((e = dev->procent)) {
		if (e->count)
			return -EBUSY;
		remove_proc_entry(e->name, dev->bus->procdir);
		dev->procent = NULL;
	}
	return 0;
}

__initfunc(void proc_bus_pci_add(struct pci_bus *bus))
{
	while (bus) {
		struct pci_dev *dev;

		for(dev = bus->devices; dev; dev = dev->sibling)
			pci_proc_attach_device(dev);
		if (bus->children)
			proc_bus_pci_add(bus->children);
		bus = bus->next;
	}
}

__initfunc(void pci_proc_init(void))
{
	if (!pci_present())
		return;
	proc_bus_pci_dir = create_proc_entry("pci", S_IFDIR, proc_bus);
	proc_register(proc_bus_pci_dir, &proc_pci_devices);
	proc_bus_pci_add(&pci_root);

#ifdef CONFIG_PCI_OLD_PROC
	proc_old_pci_init();
#endif
}
