/*
 * proc_devtree.c - handles /proc/device-tree
 *
 * Copyright 1997 Paul Mackerras
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <asm/prom.h>
#include <asm/uaccess.h>

static struct proc_dir_entry *proc_device_tree;

/*
 * Supply data on a read from /proc/device-tree/node/property.
 */
static int property_read_proc(char *page, char **start, off_t off,
			      int count, int *eof, void *data)
{
	struct property *pp = data;
	int n;

	if (off >= pp->length) {
		*eof = 1;
		return 0;
	}
	n = pp->length - off;
	if (n > count)
		n = count;
	else
		*eof = 1;
	memcpy(page, pp->value + off, n);
	*start = page;
	return n;
}

/*
 * For a node with a name like "gc@10", we make symlinks called "gc"
 * and "@10" to it.
 */

static int devtree_readlink(struct dentry *, char *, int);
static struct dentry *devtree_follow_link(struct dentry *, struct dentry *, unsigned int);

struct inode_operations devtree_symlink_inode_operations = {
	NULL,			/* no file-operations */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	devtree_readlink,	/* readlink */
	devtree_follow_link,	/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};

static struct dentry *devtree_follow_link(struct dentry *dentry,
					  struct dentry *base,
					  unsigned int follow)
{
	struct inode *inode = dentry->d_inode;
	struct proc_dir_entry * de;
	char *link;

	de = (struct proc_dir_entry *) inode->u.generic_ip;
	link = (char *) de->data;
	return lookup_dentry(link, base, follow);
}

static int devtree_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	struct inode *inode = dentry->d_inode;
	struct proc_dir_entry * de;
	char *link;
	int linklen;

	de = (struct proc_dir_entry *) inode->u.generic_ip;
	link = (char *) de->data;
	linklen = strlen(link);
	if (linklen > buflen)
		linklen = buflen;
	if (copy_to_user(buffer, link, linklen))
		return -EFAULT;
	return linklen;
}

/*
 * Process a node, adding entries for its children and its properties.
 */
static void add_node(struct device_node *np, struct proc_dir_entry *de)
{
	struct property *pp;
	struct proc_dir_entry *ent;
	struct device_node *child, *sib;
	const char *p, *at;
	int l;
	struct proc_dir_entry *list, **lastp, *al;

	lastp = &list;
	for (pp = np->properties; pp != 0; pp = pp->next) {
		/*
		 * Unfortunately proc_register puts each new entry
		 * at the beginning of the list.  So we rearrange them.
		 */
		ent = kmalloc(sizeof(struct proc_dir_entry), GFP_KERNEL);
		if (ent == 0)
			break;
		memset(ent, 0, sizeof(struct proc_dir_entry));
		ent->name = pp->name;
		ent->namelen = strlen(pp->name);
		ent->mode = S_IFREG | S_IRUGO;
		ent->nlink = 1;
		ent->data = pp;
		ent->read_proc = property_read_proc;
		ent->size = pp->length;
		proc_register(de, ent);
		*lastp = ent;
		lastp = &ent->next;
	}
	for (child = np->child; child != 0; child = child->sibling) {
		p = strrchr(child->full_name, '/');
		if (p == 0)
			p = child->full_name;
		else
			++p;
		/* chop off '@0' if the name ends with that */
		l = strlen(p);
		if (l > 2 && p[l-2] == '@' && p[l-1] == '0')
			l -= 2;
		ent = kmalloc(sizeof(struct proc_dir_entry), GFP_KERNEL);
		if (ent == 0)
			break;
		memset(ent, 0, sizeof(struct proc_dir_entry));
		ent->name = p;
		ent->namelen = l;
		ent->mode = S_IFDIR | S_IRUGO | S_IXUGO;
		ent->nlink = 2;
		proc_register(de, ent);
		*lastp = ent;
		lastp = &ent->next;
		add_node(child, ent);

		/*
		 * If we left the address part on the name, consider
		 * adding symlinks from the name and address parts.
		 */
		if (p[l] != 0 || (at = strchr(p, '@')) == 0)
			continue;

		/*
		 * If this is the first node with a given name property,
		 * add a symlink with the name property as its name.
		 */
		for (sib = np->child; sib != child; sib = sib->sibling)
			if (strcmp(sib->name, child->name) == 0)
				break;
		if (sib == child && strncmp(p, child->name, l) != 0) {
			al = kmalloc(sizeof(struct proc_dir_entry),
				     GFP_KERNEL);
			if (al == 0)
				break;
			memset(al, 0, sizeof(struct proc_dir_entry));
			al->name = child->name;
			al->namelen = strlen(child->name);
			al->mode = S_IFLNK | S_IRUGO | S_IXUGO;
			al->nlink = 1;
			al->data = (void *) ent->name;
			al->ops = &devtree_symlink_inode_operations;
			proc_register(de, al);
			*lastp = al;
			lastp = &al->next;
		}

		/*
		 * Add another directory with the @address part as its name.
		 */
		al = kmalloc(sizeof(struct proc_dir_entry), GFP_KERNEL);
		if (al == 0)
			break;
		memset(al, 0, sizeof(struct proc_dir_entry));
		al->name = at;
		al->namelen = strlen(at);
		al->mode = S_IFLNK | S_IRUGO | S_IXUGO;
		al->nlink = 1;
		al->data = (void *) ent->name;
		al->ops = &devtree_symlink_inode_operations;
		proc_register(de, al);
		*lastp = al;
		lastp = &al->next;
	}
	*lastp = 0;
	de->subdir = list;
}

/*
 * Called on initialization to set up the /proc/device-tree subtree
 */
void proc_device_tree_init(void)
{
	struct device_node *root;
	if ( !have_of )
		return;
	proc_device_tree = create_proc_entry("device-tree", S_IFDIR, 0);
	if (proc_device_tree == 0)
		return;
	root = find_path_device("/");
	if (root == 0) {
		printk(KERN_ERR "/proc/device-tree: can't find root\n");
		return;
	}
	add_node(root, proc_device_tree);
}
