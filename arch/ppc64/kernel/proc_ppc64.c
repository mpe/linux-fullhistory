/*
 * arch/ppc64/kernel/proc_ppc64.c
 *
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen IBM Corporation
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */


/*
 * Change Activity:
 * 2001       : mikec    : Created
 * 2001/06/05 : engebret : Software event count support.
 * 2003/02/13 : bergner  : Move PMC code to pmc.c
 * End Change Activity 
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/kernel.h>

#include <asm/proc_fs.h>
#include <asm/naca.h>
#include <asm/paca.h>
#include <asm/systemcfg.h>
#include <asm/rtas.h>
#include <asm/uaccess.h>
#include <asm/prom.h>

struct proc_ppc64_t proc_ppc64;

void proc_ppc64_create_paca(int num);

static loff_t  page_map_seek( struct file *file, loff_t off, int whence);
static ssize_t page_map_read( struct file *file, char *buf, size_t nbytes, loff_t *ppos);
static int     page_map_mmap( struct file *file, struct vm_area_struct *vma );

static struct file_operations page_map_fops = {
	.llseek	= page_map_seek,
	.read	= page_map_read,
	.mmap	= page_map_mmap
};

#ifdef CONFIG_PPC_PSERIES
/* routines for /proc/ppc64/ofdt */
static ssize_t ofdt_write(struct file *, const char __user *, size_t, loff_t *);
static void proc_ppc64_create_ofdt(struct proc_dir_entry *);
static int do_remove_node(char *);
static int do_add_node(char *, size_t);
static void release_prop_list(const struct property *);
static struct property *new_property(const char *, const int, const unsigned char *, struct property *);
static char * parse_next_property(char *, char *, char **, int *, unsigned char**);
static struct file_operations ofdt_fops = {
	.write = ofdt_write
};
#endif

int __init proc_ppc64_init(void)
{


	if (proc_ppc64.root == NULL) {
		printk(KERN_INFO "proc_ppc64: Creating /proc/ppc64/\n");
		proc_ppc64.root = proc_mkdir("ppc64", 0);
		if (!proc_ppc64.root)
			return 0;
	} else {
		return 0;
	}

	proc_ppc64.naca = create_proc_entry("naca", S_IRUSR, proc_ppc64.root);
	if ( proc_ppc64.naca ) {
		proc_ppc64.naca->nlink = 1;
		proc_ppc64.naca->data = naca;
		proc_ppc64.naca->size = 4096;
		proc_ppc64.naca->proc_fops = &page_map_fops;
	}
	
	proc_ppc64.systemcfg = create_proc_entry("systemcfg", S_IFREG|S_IRUGO, proc_ppc64.root);
	if ( proc_ppc64.systemcfg ) {
		proc_ppc64.systemcfg->nlink = 1;
		proc_ppc64.systemcfg->data = systemcfg;
		proc_ppc64.systemcfg->size = 4096;
		proc_ppc64.systemcfg->proc_fops = &page_map_fops;
	}

	/* /proc/ppc64/paca/XX -- raw paca contents.  Only readable to root */
	proc_ppc64.paca = proc_mkdir("paca", proc_ppc64.root);
	if (proc_ppc64.paca) {
		unsigned long i;

		for (i = 0; i < NR_CPUS; i++) {
			if (!cpu_online(i))
				continue;
			proc_ppc64_create_paca(i);
		}
	}

#ifdef CONFIG_PPC_PSERIES
	/* Placeholder for rtas interfaces. */
	if (proc_ppc64.rtas == NULL)
		proc_ppc64.rtas = proc_mkdir("rtas", proc_ppc64.root);

	if (proc_ppc64.rtas)
		proc_symlink("rtas", 0, "ppc64/rtas");

	proc_ppc64_create_ofdt(proc_ppc64.root);
#endif

	return 0;
}


/*
 * NOTE: since paca data is always in flux the values will never be a consistant set.
 * In theory it could be made consistent if we made the corresponding cpu
 * copy the page for us (via an IPI).  Probably not worth it.
 *
 */
void proc_ppc64_create_paca(int num)
{
	struct proc_dir_entry *ent;
	struct paca_struct *lpaca = paca + num;
	char buf[16];

	sprintf(buf, "%02x", num);
	ent = create_proc_entry(buf, S_IRUSR, proc_ppc64.paca);
	if ( ent ) {
		ent->nlink = 1;
		ent->data = lpaca;
		ent->size = 4096;
		ent->proc_fops = &page_map_fops;
	}
}


static loff_t page_map_seek( struct file *file, loff_t off, int whence)
{
	loff_t new;
	struct proc_dir_entry *dp = PDE(file->f_dentry->d_inode);

	switch(whence) {
	case 0:
		new = off;
		break;
	case 1:
		new = file->f_pos + off;
		break;
	case 2:
		new = dp->size + off;
		break;
	default:
		return -EINVAL;
	}
	if ( new < 0 || new > dp->size )
		return -EINVAL;
	return (file->f_pos = new);
}

static ssize_t page_map_read( struct file *file, char *buf, size_t nbytes, loff_t *ppos)
{
	unsigned pos = *ppos;
	struct proc_dir_entry *dp = PDE(file->f_dentry->d_inode);

	if ( pos >= dp->size )
		return 0;
	if ( nbytes >= dp->size )
		nbytes = dp->size;
	if ( pos + nbytes > dp->size )
		nbytes = dp->size - pos;

	copy_to_user( buf, (char *)dp->data + pos, nbytes );
	*ppos = pos + nbytes;
	return nbytes;
}

static int page_map_mmap( struct file *file, struct vm_area_struct *vma )
{
	struct proc_dir_entry *dp = PDE(file->f_dentry->d_inode);

	vma->vm_flags |= VM_SHM | VM_LOCKED;

	if ((vma->vm_end - vma->vm_start) > dp->size)
		return -EINVAL;

	remap_page_range( vma, vma->vm_start, __pa(dp->data), dp->size, vma->vm_page_prot );
	return 0;
}

#ifdef CONFIG_PPC_PSERIES
/* create /proc/ppc64/ofdt write-only by root */
static void proc_ppc64_create_ofdt(struct proc_dir_entry *parent)
{
	struct proc_dir_entry *ent;

	ent = create_proc_entry("ofdt", S_IWUSR, parent);
	if (ent) {
		ent->nlink = 1;
		ent->data = NULL;
		ent->size = 0;
		ent->proc_fops = &ofdt_fops;
	}
}

/**
 * ofdt_write - perform operations on the Open Firmware device tree
 *
 * @file: not used
 * @buf: command and arguments
 * @count: size of the command buffer
 * @off: not used
 *
 * Operations supported at this time are addition and removal of
 * whole nodes along with their properties.  Operations on individual
 * properties are not implemented (yet).
 */
static ssize_t ofdt_write(struct file *file, const char __user *buf, size_t count, loff_t *off)
{
	int rv = 0;
	char *kbuf;
	char *tmp;

	if (!(kbuf = kmalloc(count + 1, GFP_KERNEL))) {
		rv = -ENOMEM;
		goto out;
	}
	if (copy_from_user(kbuf, buf, count)) {
		rv = -EFAULT;
		goto out;
	}

	kbuf[count] = '\0';

	tmp = strchr(kbuf, ' ');
	if (!tmp) {
		rv = -EINVAL;
		goto out;
	}
	*tmp = '\0';
	tmp++;

	if (!strcmp(kbuf, "add_node"))
		rv = do_add_node(tmp, count - (tmp - kbuf));
	else if (!strcmp(kbuf, "remove_node"))
		rv = do_remove_node(tmp);
	else
		rv = -EINVAL;
out:
	kfree(kbuf);
	return rv ? rv : count;
}

static int do_remove_node(char *buf)
{
	struct device_node *node;
	int rv = 0;

	if ((node = of_find_node_by_path(buf)))
		of_remove_node(node);
	else
		rv = -ENODEV;

	of_node_put(node);
	return rv;
}

static int do_add_node(char *buf, size_t bufsize)
{
	char *path, *end, *name;
	struct device_node *np;
	struct property *prop = NULL;
	unsigned char* value;
	int length, rv = 0;

	end = buf + bufsize;
	path = buf;
	buf = strchr(buf, ' ');
	if (!buf)
		return -EINVAL;
	*buf = '\0';
	buf++;

	if ((np = of_find_node_by_path(path))) {
		of_node_put(np);
		return -EINVAL;
	}

	/* rv = build_prop_list(tmp, bufsize - (tmp - buf), &proplist); */
	while (buf < end &&
	       (buf = parse_next_property(buf, end, &name, &length, &value))) {
		struct property *last = prop;

		prop = new_property(name, length, value, last);
		if (!prop) {
			rv = -ENOMEM;
			prop = last;
			goto out;
		}
	}
	if (!buf) {
		rv = -EINVAL;
		goto out;
	}

	rv = of_add_node(path, prop);

out:
	if (rv)
		release_prop_list(prop);
	return rv;
}

static struct property *new_property(const char *name, const int length, const unsigned char *value, struct property *last)
{
	struct property *new = kmalloc(sizeof(*new), GFP_KERNEL);

	if (!new)
		return NULL;
	memset(new, 0, sizeof(*new));

	if (!(new->name = kmalloc(strlen(name) + 1, GFP_KERNEL)))
		goto cleanup;
	if (!(new->value = kmalloc(length + 1, GFP_KERNEL)))
		goto cleanup;

	strcpy(new->name, name);
	memcpy(new->value, value, length);
	*(((char *)new->value) + length) = 0;
	new->length = length;
	new->next = last;
	return new;

cleanup:
	if (new->name)
		kfree(new->name);
	if (new->value)
		kfree(new->value);
	kfree(new);
	return NULL;
}

/**
 * parse_next_property - process the next property from raw input buffer
 * @buf: input buffer, must be nul-terminated
 * @end: end of the input buffer + 1, for validation
 * @name: return value; set to property name in buf
 * @length: return value; set to length of value
 * @value: return value; set to the property value in buf
 *
 * Note that the caller must make copies of the name and value returned,
 * this function does no allocation or copying of the data.  Return value
 * is set to the next name in buf, or NULL on error.
 */
static char * parse_next_property(char *buf, char *end, char **name, int *length, unsigned char **value)
{
	char *tmp;

	*name = buf;

	tmp = strchr(buf, ' ');
	if (!tmp) {
		printk(KERN_ERR "property parse failed in %s at line %d\n", __FUNCTION__, __LINE__);
		return NULL;
	}
	*tmp = '\0';

	if (++tmp >= end) {
		printk(KERN_ERR "property parse failed in %s at line %d\n", __FUNCTION__, __LINE__);
		return NULL;
	}

	/* now we're on the length */
	*length = -1;
	*length = simple_strtoul(tmp, &tmp, 10);
	if (*length == -1) {
		printk(KERN_ERR "property parse failed in %s at line %d\n", __FUNCTION__, __LINE__);
		return NULL;
	}
	if (*tmp != ' ' || ++tmp >= end) {
		printk(KERN_ERR "property parse failed in %s at line %d\n", __FUNCTION__, __LINE__);
		return NULL;
	}

	/* now we're on the value */
	*value = tmp;
	tmp += *length;
	if (tmp > end) {
		printk(KERN_ERR "property parse failed in %s at line %d\n", __FUNCTION__, __LINE__);
		return NULL;
	}
	else if (tmp < end && *tmp != ' ' && *tmp != '\0') {
		printk(KERN_ERR "property parse failed in %s at line %d\n", __FUNCTION__, __LINE__);
		return NULL;
	}
	tmp++;

	/* and now we should be on the next name, or the end */
	return tmp;
}

static void release_prop_list(const struct property *prop)
{
	struct property *next;
	for (; prop; prop = next) {
		next = prop->next;
		kfree(prop->name);
		kfree(prop->value);
		kfree(prop);
	}

}
#endif	/* defined(CONFIG_PPC_PSERIES) */

fs_initcall(proc_ppc64_init);
