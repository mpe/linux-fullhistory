/*
 * linux/ipc/shm.c
 * Copyright (C) 1992, 1993 Krishna Balasubramanian
 *	 Many improvements/fixes by Bruno Haible.
 * Replaced `struct shm_desc' by `struct vm_area_struct', July 1994.
 * Fixed the shm swap deallocation (shm_unuse()), August 1998 Andrea Arcangeli.
 *
 * /proc/sysvipc/shm support (c) 1999 Dragos Acostachioaie <dragos@iname.com>
 * BIGMEM support, Andrea Arcangeli <andrea@suse.de>
 * SMP thread shm, Jean-Luc Boyard <jean-luc.boyard@siemens.fr>
 * HIGHMEM support, Ingo Molnar <mingo@redhat.com>
 * avoid vmalloc and make shmmax, shmall, shmmni sysctl'able,
 *                         Christoph Rohland <hans-christoph.rohland@sap.com>
 * Shared /dev/zero support, Kanoj Sarcar <kanoj@sgi.com>
 * make it a file system,  Christoph Rohland <hans-christoph.rohland@sap.com>
 *
 * The filesystem has the following restrictions/bugs:
 * 1) It only can handle one directory.
 * 2) Private writeable mappings are not supported
 * 3) Read and write are not implemented (should they?)
 * 4) No special nodes are supported
 *
 * There are the following mount options:
 * - nr_blocks (^= shmall) is the number of blocks of size PAGE_SIZE
 *   we are allowed to allocate
 * - nr_inodes (^= shmmni) is the number of files we are allowed to
 *   allocate
 * - mode is the mode for the root directory (default S_IRWXUGO | S_ISVTX)
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/malloc.h>
#include <linux/shm.h>
#include <linux/swap.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/locks.h>
#include <linux/file.h>
#include <linux/mman.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/highmem.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>

#include "util.h"

static struct super_block *shm_read_super(struct super_block *,void *, int);
static void	      shm_put_super  (struct super_block *);
static int	      shm_remount_fs (struct super_block *, int *, char *);
static void	      shm_read_inode (struct inode *);
static void	      shm_write_inode(struct inode *, int);
static int	      shm_statfs   (struct super_block *, struct statfs *);
static int	      shm_create   (struct inode *,struct dentry *,int);
static struct dentry *shm_lookup   (struct inode *,struct dentry *);
static int	      shm_unlink   (struct inode *,struct dentry *);
static int	      shm_setattr  (struct dentry *dent, struct iattr *attr);
static void	      shm_delete   (struct inode *);
static int	      shm_mmap	   (struct file *, struct vm_area_struct *);
static int	      shm_readdir  (struct file *, void *, filldir_t);

#define SHM_NAME_LEN NAME_MAX
#define SHM_FMT ".IPC_%08x"
#define SHM_FMT_LEN 13

/* shm_mode upper byte flags */
/* SHM_DEST and SHM_LOCKED are used in ipcs(8) */
#define PRV_DEST	0010000	/* segment will be destroyed on last detach */
#define PRV_LOCKED	0020000	/* segment will not be swapped */
#define SHM_UNLK	0040000	/* filename is unlinked */
#define SHM_SYSV	0100000	/* It is a SYSV shm segment */

struct shmid_kernel /* private to the kernel */
{	
	struct kern_ipc_perm	shm_perm;
	size_t			shm_segsz;
	unsigned long		shm_nattch;
	unsigned long		shm_npages; /* size of segment (pages) */
	pte_t			**shm_dir;  /* ptr to arr of ptrs to frames */ 
	int			id;
	union permap {
		struct shmem {
			time_t			atime;
			time_t			dtime;
			time_t			ctime;
			pid_t			cpid;
			pid_t			lpid;
			int			nlen;
			char			nm[0];
		} shmem;
		struct zero {
			struct semaphore	sema;
			struct list_head	list;
		} zero;
	} permap;
};

#define shm_atim	permap.shmem.atime
#define shm_dtim	permap.shmem.dtime
#define shm_ctim	permap.shmem.ctime
#define shm_cprid	permap.shmem.cpid
#define shm_lprid	permap.shmem.lpid
#define shm_namelen	permap.shmem.nlen
#define shm_name	permap.shmem.nm
#define shm_flags	shm_perm.mode
#define zsem		permap.zero.sema
#define zero_list	permap.zero.list

static struct ipc_ids shm_ids;

#define shm_lock(id)	((struct shmid_kernel*)ipc_lock(&shm_ids,id))
#define shm_unlock(id)	ipc_unlock(&shm_ids,id)
#define shm_lockall()	ipc_lockall(&shm_ids)
#define shm_unlockall()	ipc_unlockall(&shm_ids)
#define shm_get(id)	((struct shmid_kernel*)ipc_get(&shm_ids,id))
#define shm_buildid(id, seq) \
	ipc_buildid(&shm_ids, id, seq)

static int newseg (key_t key, const char *name, int namelen, int shmflg, size_t size);
static void seg_free(struct shmid_kernel *shp, int doacc);
static void shm_open (struct vm_area_struct *shmd);
static void shm_close (struct vm_area_struct *shmd);
static int shm_remove_name(int id);
static struct page * shm_nopage(struct vm_area_struct *, unsigned long, int);
static int shm_swapout(struct page *, struct file *);
#ifdef CONFIG_PROC_FS
static int sysvipc_shm_read_proc(char *buffer, char **start, off_t offset, int length, int *eof, void *data);
#endif

static void zshm_swap (int prio, int gfp_mask);
static void zmap_unuse(swp_entry_t entry, struct page *page);
static void shmzero_open(struct vm_area_struct *shmd);
static void shmzero_close(struct vm_area_struct *shmd);
static struct page *shmzero_nopage(struct vm_area_struct * shmd, unsigned long address, int no_share);
static int zero_id;
static struct shmid_kernel zshmid_kernel;
static struct dentry *zdent;

#define SHM_FS_MAGIC 0x02011994

static struct super_block * shm_sb;

static DECLARE_FSTYPE(shm_fs_type, "shm", shm_read_super, FS_SINGLE);

static struct super_operations shm_sops = {
	read_inode:	shm_read_inode,
	write_inode:	shm_write_inode,
	delete_inode:	shm_delete,
	put_super:	shm_put_super,
	statfs:		shm_statfs,
	remount_fs:	shm_remount_fs,
};

static struct file_operations shm_root_operations = {
	readdir:	shm_readdir,
};

static struct inode_operations shm_root_inode_operations = {
	create:		shm_create,
	lookup:		shm_lookup,
	unlink:		shm_unlink,
};

static struct file_operations shm_file_operations = {
	mmap:	shm_mmap,
};

static struct inode_operations shm_inode_operations = {
	setattr:	shm_setattr,
};

static struct vm_operations_struct shm_vm_ops = {
	open:	shm_open,	/* callback for a new vm-area open */
	close:	shm_close,	/* callback for when the vm-area is released */
	nopage:	shm_nopage,
	swapout:shm_swapout,
};

size_t shm_ctlmax = SHMMAX;

/* These parameters should be part of the superblock */
static int shm_ctlall;
static int shm_ctlmni;
static int shm_mode;

static int shm_tot; /* total number of shared memory pages */
static int shm_rss; /* number of shared memory pages that are in memory */
static int shm_swp; /* number of shared memory pages that are in swap */

/* locks order:
	pagecache_lock
	shm_lock()/shm_lockall()
	kernel lock
	inode->i_sem
	sem_ids.sem
	mmap_sem

  SMP assumptions:
  - swap_free() never sleeps
  - add_to_swap_cache() never sleeps
  - add_to_swap_cache() doesn't acquire the big kernel lock.
  - shm_unuse() is called with the kernel lock acquired.
 */

/* some statistics */
static ulong swap_attempts;
static ulong swap_successes;
static ulong used_segs;

void __init shm_init (void)
{
	struct vfsmount *res;
	ipc_init_ids(&shm_ids, 1);

	register_filesystem (&shm_fs_type);
	res = kern_mount(&shm_fs_type);
	if (IS_ERR(res)) {
		unregister_filesystem(&shm_fs_type);
		return;
	}
#ifdef CONFIG_PROC_FS
	create_proc_read_entry("sysvipc/shm", 0, 0, sysvipc_shm_read_proc, NULL);
#endif
	zero_id = ipc_addid(&shm_ids, &zshmid_kernel.shm_perm, 1);
	shm_unlock(zero_id);
	INIT_LIST_HEAD(&zshmid_kernel.zero_list);
	zdent = d_alloc_root(get_empty_inode());
	return;
}

static int shm_parse_options(char *options)
{
	int blocks = shm_ctlall;
	int inodes = shm_ctlmni;
	umode_t mode = shm_mode;
	char *this_char, *value;

	this_char = NULL;
	if ( options )
		this_char = strtok(options,",");
	for ( ; this_char; this_char = strtok(NULL,",")) {
		if ((value = strchr(this_char,'=')) != NULL)
			*value++ = 0;
		if (!strcmp(this_char,"nr_blocks")) {
			if (!value || !*value)
				return 1;
			blocks = simple_strtoul(value,&value,0);
			if (*value)
				return 1;
		}
		else if (!strcmp(this_char,"nr_inodes")) {
			if (!value || !*value)
				return 1;
			inodes = simple_strtoul(value,&value,0);
			if (*value)
				return 1;
		}
		else if (!strcmp(this_char,"mode")) {
			if (!value || !*value)
				return 1;
			mode = simple_strtoul(value,&value,8);
			if (*value)
				return 1;
		}
		else
			return 1;
	}
	shm_ctlmni = inodes;
	shm_ctlall = blocks;
	shm_mode   = mode;

	return 0;
}

static struct super_block *shm_read_super(struct super_block *s,void *data, 
					  int silent)
{
	struct inode * root_inode;

	shm_ctlall = SHMALL;
	shm_ctlmni = SHMMNI;
	shm_mode   = S_IRWXUGO | S_ISVTX;
	if (shm_parse_options (data)) {
		printk(KERN_ERR "shm fs invalid option\n");
		goto out_unlock;
	}

	s->s_blocksize = PAGE_SIZE;
	s->s_blocksize_bits = PAGE_SHIFT;
	s->s_magic = SHM_FS_MAGIC;
	s->s_op = &shm_sops;
	root_inode = iget (s, SEQ_MULTIPLIER);
	if (!root_inode)
		goto out_no_root;
	root_inode->i_op = &shm_root_inode_operations;
	root_inode->i_sb = s;
	root_inode->i_nlink = 2;
	root_inode->i_mode = S_IFDIR | shm_mode;
	s->s_root = d_alloc_root(root_inode);
	if (!s->s_root)
		goto out_no_root;
	shm_sb = s;
	return s;

out_no_root:
	printk(KERN_ERR "proc_read_super: get root inode failed\n");
	iput(root_inode);
out_unlock:
	return NULL;
}

static int shm_remount_fs (struct super_block *sb, int *flags, char *data)
{
	if (shm_parse_options (data))
		return -EINVAL;
	return 0;
}

static inline int shm_checkid(struct shmid_kernel *s, int id)
{
	if (!(s->shm_flags & SHM_SYSV))
		return -EINVAL;
	if (ipc_checkid(&shm_ids,&s->shm_perm,id))
		return -EIDRM;
	return 0;
}

static inline struct shmid_kernel *shm_rmid(int id)
{
	return (struct shmid_kernel *)ipc_rmid(&shm_ids,id);
}

static inline int shm_addid(struct shmid_kernel *shp)
{
	return ipc_addid(&shm_ids, &shp->shm_perm, shm_ctlmni+1);
}

static void shm_put_super(struct super_block *sb)
{
	int i;
	struct shmid_kernel *shp;

	down(&shm_ids.sem);
	for(i = 0; i <= shm_ids.max_id; i++) {
		if (i == zero_id)
			continue;
		if (!(shp = shm_lock (i)))
			continue;
		if (shp->shm_nattch)
			printk(KERN_DEBUG "shm_nattch = %ld\n", shp->shm_nattch);
		shp = shm_rmid(i);
		shm_unlock(i);
		seg_free(shp, 1);
	}
	dput (sb->s_root);
	up(&shm_ids.sem);
}

static int shm_statfs(struct super_block *sb, struct statfs *buf)
{
	buf->f_type = SHM_FS_MAGIC;
	buf->f_bsize = PAGE_SIZE;
	buf->f_blocks = shm_ctlall;
	buf->f_bavail = buf->f_bfree = shm_ctlall - shm_tot;
	buf->f_files = shm_ctlmni;
	buf->f_ffree = shm_ctlmni - used_segs;
	buf->f_namelen = SHM_NAME_LEN;
	return 0;
}

static void shm_write_inode(struct inode * inode, int sync)
{
}

static void shm_read_inode(struct inode * inode)
{
	int id;
	struct shmid_kernel *shp;

	id = inode->i_ino;
	inode->i_op = NULL;
	inode->i_mode = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;

	if (id < SEQ_MULTIPLIER) {
		if (!(shp = shm_lock (id)))
			return;
		inode->i_mode = (shp->shm_flags & S_IALLUGO) | S_IFREG;
		inode->i_uid  = shp->shm_perm.uid;
		inode->i_gid  = shp->shm_perm.gid;
		inode->i_size = shp->shm_segsz;
		shm_unlock (id);
		inode->i_op  = &shm_inode_operations;
		inode->i_fop = &shm_file_operations;
		return;
	}
	inode->i_op    = &shm_root_inode_operations;
	inode->i_fop   = &shm_root_operations;
	inode->i_sb    = shm_sb;
	inode->i_nlink = 2;
	inode->i_mode  = S_IFDIR | shm_mode;
	inode->i_uid   = inode->i_gid = 0;

}

static int shm_create (struct inode *dir, struct dentry *dent, int mode)
{
	int id, err;
	struct inode * inode;

	down(&shm_ids.sem);
	err = id = newseg (IPC_PRIVATE, dent->d_name.name, dent->d_name.len, mode, 0);
	if (err < 0)
		goto out;

	err = -ENOMEM;
	inode = iget (shm_sb, id % SEQ_MULTIPLIER);
	if (!inode)
		goto out;

	err = 0;
	down (&inode->i_sem);
	inode->i_mode = mode | S_IFREG;
	inode->i_op   = &shm_inode_operations;
	d_instantiate(dent, inode);
	up (&inode->i_sem);

out:
	up(&shm_ids.sem);
	return err;
}

static int shm_readdir (struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode * inode = filp->f_dentry->d_inode;
	struct shmid_kernel *shp;
	off_t nr;

	nr = filp->f_pos;

	switch(nr)
	{
	case 0:
		if (filldir(dirent, ".", 1, nr, inode->i_ino) < 0)
			return 0;
		filp->f_pos = ++nr;
		/* fall through */
	case 1:
		if (filldir(dirent, "..", 2, nr, inode->i_ino) < 0)
			return 0;
		filp->f_pos = ++nr;
		/* fall through */
	default:
		down(&shm_ids.sem);
		for (; nr-2 <= shm_ids.max_id; nr++ ) {
			if (nr-2 == zero_id)
				continue;
			if (!(shp = shm_get (nr-2))) 
				continue;
			if (shp->shm_flags & SHM_UNLK)
				continue;
			if (filldir(dirent, shp->shm_name, shp->shm_namelen, nr, nr) < 0 )
				break;;
		}
		filp->f_pos = nr;
		up(&shm_ids.sem);
		break;
	}

	UPDATE_ATIME(inode);
	return 0;
}

static struct dentry *shm_lookup (struct inode *dir, struct dentry *dent)
{
	int i, err = 0;
	struct shmid_kernel* shp;
	struct inode *inode = NULL;

	if (dent->d_name.len > SHM_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	down(&shm_ids.sem);
	for(i = 0; i <= shm_ids.max_id; i++) {
		if (i == zero_id)
			continue;
		if (!(shp = shm_lock(i)))
			continue;
		if (!(shp->shm_flags & SHM_UNLK) &&
		    dent->d_name.len == shp->shm_namelen &&
		    strncmp(dent->d_name.name, shp->shm_name, shp->shm_namelen) == 0)
			goto found;
		shm_unlock(i);
	}

	/*
	 * prevent the reserved names as negative dentries. 
	 * This also prevents object creation through the filesystem
	 */
	if (dent->d_name.len == SHM_FMT_LEN &&
	    memcmp (SHM_FMT, dent->d_name.name, SHM_FMT_LEN - 8) == 0)
		err = -EINVAL;	/* EINVAL to give IPC_RMID the right error */

	goto out;

found:
	shm_unlock(i);
	inode = iget(dir->i_sb, i);

	if (!inode)
		err = -EACCES;
out:
	if (err == 0)
		d_add (dent, inode);
	up (&shm_ids.sem);
	return ERR_PTR(err);
}

static int shm_unlink (struct inode *dir, struct dentry *dent)
{
	struct inode * inode = dent->d_inode;
	struct shmid_kernel *shp;

	down (&shm_ids.sem);
	if (!(shp = shm_lock (inode->i_ino)))
		BUG();
	shp->shm_flags |= SHM_UNLK | PRV_DEST;
	shp->shm_perm.key = IPC_PRIVATE; /* Do not find it any more */
	shm_unlock (inode->i_ino);
	up (&shm_ids.sem);
	inode->i_nlink -= 1;
	/*
	 * If it's a reserved name we have to drop the dentry instead
	 * of creating a negative dentry
	 */
	if (dent->d_name.len == SHM_FMT_LEN &&
	    memcmp (SHM_FMT, dent->d_name.name, SHM_FMT_LEN - 8) == 0)
		d_drop (dent);
	return 0;
}

/*
 * We cannot use kmalloc for shm_alloc since this restricts the
 * maximum size of the segments.
 *
 * We also cannot use vmalloc, since this uses too much of the vmalloc
 * space and we run out of this on highend machines.
 *
 * So we have to use this complicated indirect scheme to alloc the shm
 * page tables.
 *
 */

#ifdef PTE_INIT
static inline void init_ptes (pte_t *pte, int number) {
	while (number--)
		PTE_INIT (pte++);
}
#else
static inline void init_ptes (pte_t *pte, int number) {
	memset (pte, 0, number*sizeof(*pte));
}
#endif

#define PTES_PER_PAGE (PAGE_SIZE/sizeof(pte_t))
#define SHM_ENTRY(shp, index) (shp)->shm_dir[(index)/PTES_PER_PAGE][(index)%PTES_PER_PAGE]

static pte_t **shm_alloc(unsigned long pages, int doacc)
{
	unsigned short dir  = pages / PTES_PER_PAGE;
	unsigned short last = pages % PTES_PER_PAGE;
	pte_t **ret, **ptr;

	if (pages == 0)
		return NULL;

	ret = kmalloc ((dir+1) * sizeof(pte_t *), GFP_KERNEL);
	if (!ret)
		goto nomem;

	for (ptr = ret; ptr < ret+dir ; ptr++)
	{
		*ptr = (pte_t *)__get_free_page (GFP_KERNEL);
		if (!*ptr)
			goto free;
		init_ptes (*ptr, PTES_PER_PAGE);
	}

	/* The last one is probably not of PAGE_SIZE: we use kmalloc */
	if (last) {
		*ptr = kmalloc (last*sizeof(pte_t), GFP_KERNEL);
		if (!*ptr)
			goto free;
		init_ptes (*ptr, last);
	}
	if (doacc) {
		shm_lockall();
		shm_tot += pages;
		used_segs++;
		shm_unlockall();
	}
	return ret;

free:
	/* The last failed: we decrement first */
	while (--ptr >= ret)
		free_page ((unsigned long)*ptr);

	kfree (ret);
nomem:
	return ERR_PTR(-ENOMEM);
}

static void shm_free(pte_t** dir, unsigned long pages, int doacc)
{
	int i, rss, swp;
	pte_t **ptr = dir+pages/PTES_PER_PAGE;

	if (!dir)
		return;

	for (i = 0, rss = 0, swp = 0; i < pages ; i++) {
		pte_t pte;
		pte = dir[i/PTES_PER_PAGE][i%PTES_PER_PAGE];
		if (pte_none(pte))
			continue;
		if (pte_present(pte)) {
			__free_page (pte_page(pte));
			rss++;
		} else {
			swap_free(pte_to_swp_entry(pte));
			swp++;
		}
	}

	/* first the last page */
	if (pages%PTES_PER_PAGE)
		kfree (*ptr);
	/* now the whole pages */
	while (--ptr >= dir)
		if (*ptr)
			free_page ((unsigned long)*ptr);

	/* Now the indirect block */
	kfree (dir);

	if (doacc) {
		shm_lockall();
		shm_rss -= rss;
		shm_swp -= swp;
		shm_tot -= pages;
		used_segs--;
		shm_unlockall();
	}
}

static 	int shm_setattr (struct dentry *dentry, struct iattr *attr)
{
	int error;
	struct inode *inode = dentry->d_inode;
	struct shmid_kernel *shp;
	unsigned long new_pages, old_pages;
	pte_t **new_dir, **old_dir;

	error = inode_change_ok(inode, attr);
	if (error)
		return error;
	if (!(attr->ia_valid & ATTR_SIZE))
		goto set_attr;
	if (attr->ia_size > shm_ctlmax)
		return -EFBIG;

	/* We set old_pages and old_dir for easier cleanup */
	old_pages = new_pages = (attr->ia_size  + PAGE_SIZE - 1) >> PAGE_SHIFT;
	old_dir = new_dir = shm_alloc(new_pages, 1);
	if (IS_ERR(new_dir))
		return PTR_ERR(new_dir);

	if (!(shp = shm_lock(inode->i_ino)))
		BUG();
	error = -ENOSPC;
	if (shm_tot - shp->shm_npages >= shm_ctlall)
		goto size_out;
	error = 0;
	if (shp->shm_segsz == attr->ia_size)
		goto size_out;
	/* Now we set them to the real values */
	old_dir = shp->shm_dir;
	old_pages = shp->shm_npages;
	if (old_dir){
		pte_t *swap;
		int i,j;
		i = old_pages < new_pages ? old_pages : new_pages;
		j = i % PTES_PER_PAGE;
		i /= PTES_PER_PAGE;
		if (j)
			memcpy (new_dir[i], old_dir[i], j * sizeof (pte_t));
		while (i--) {
			swap = new_dir[i];
			new_dir[i] = old_dir[i];
			old_dir[i] = swap;
		}
	}
	shp->shm_dir = new_dir;
	shp->shm_npages = new_pages;
	shp->shm_segsz = attr->ia_size;
size_out:
	shm_unlock(inode->i_ino);
	shm_free (old_dir, old_pages, 1);

set_attr:
	if (!(shp = shm_lock(inode->i_ino)))
		BUG();
	if (attr->ia_valid & ATTR_MODE)
		shp->shm_perm.mode = attr->ia_mode;
	if (attr->ia_valid & ATTR_UID)
		shp->shm_perm.uid = attr->ia_uid;
	if (attr->ia_valid & ATTR_GID)
		shp->shm_perm.gid = attr->ia_gid;
	shm_unlock (inode->i_ino);

	inode_setattr(inode, attr);
	return error;
}

static struct shmid_kernel *seg_alloc(int numpages, size_t namelen)
{
	struct shmid_kernel *shp;
	pte_t		   **dir;

	shp = (struct shmid_kernel *) kmalloc (sizeof (*shp) + namelen, GFP_KERNEL);
	if (!shp)
		return ERR_PTR(-ENOMEM);

	dir = shm_alloc (numpages, namelen);
	if (IS_ERR(dir)) {
		kfree(shp);
		return ERR_PTR(PTR_ERR(dir));
	}
	shp->shm_dir    = dir;
	shp->shm_npages = numpages;
	shp->shm_nattch = 0;
	shp->shm_namelen = namelen;
	return(shp);
}

static void seg_free(struct shmid_kernel *shp, int doacc)
{
	shm_free (shp->shm_dir, shp->shm_npages, doacc);
	kfree(shp);
}

static int newseg (key_t key, const char *name, int namelen,
		   int shmflg, size_t size)
{
	struct shmid_kernel *shp;
	int numpages = (size + PAGE_SIZE -1) >> PAGE_SHIFT;
	int id;

	if (namelen > SHM_NAME_LEN)
		return -ENAMETOOLONG;

	if (size > shm_ctlmax)
		return -EINVAL;
		
	if (shm_tot + numpages >= shm_ctlall)
		return -ENOSPC;

	shp = seg_alloc(numpages, namelen ? namelen : SHM_FMT_LEN + 1);
	if (IS_ERR(shp))
		return PTR_ERR(shp);
	id = shm_addid(shp);
	if(id == -1) {
		seg_free(shp, 1);
		return -ENOSPC;
	}
	shp->shm_perm.key = key;
	shp->shm_flags = (shmflg & S_IRWXUGO);
	shp->shm_segsz = size;
	shp->shm_cprid = current->pid;
	shp->shm_lprid = 0;
	shp->shm_atim = shp->shm_dtim = 0;
	shp->shm_ctim = CURRENT_TIME;
	shp->id = shm_buildid(id,shp->shm_perm.seq);
	if (namelen != 0) {
		shp->shm_namelen = namelen;
		memcpy (shp->shm_name, name, namelen);		  
	} else {
		shp->shm_flags |= SHM_SYSV;
		shp->shm_namelen = sprintf (shp->shm_name, SHM_FMT, shp->id);
	}
	shm_unlock(id);
	
	return shp->id;
}

asmlinkage long sys_shmget (key_t key, size_t size, int shmflg)
{
	struct shmid_kernel *shp;
	int err, id = 0;

	if (size < SHMMIN)
		return -EINVAL;

	down(&shm_ids.sem);
	if (key == IPC_PRIVATE) {
		err = newseg(key, NULL, 0, shmflg, size);
	} else if ((id = ipc_findkey(&shm_ids,key)) == -1) {
		if (!(shmflg & IPC_CREAT))
			err = -ENOENT;
		else
			err = newseg(key, NULL, 0, shmflg, size);
	} else if ((shmflg & IPC_CREAT) && (shmflg & IPC_EXCL)) {
		err = -EEXIST;
	} else {
		shp = shm_lock(id);
		if(shp==NULL)
			BUG();
		if (shp->shm_segsz < size)
			err = -EINVAL;
		else if (ipcperms(&shp->shm_perm, shmflg))
			err = -EACCES;
		else
			err = shm_buildid(id, shp->shm_perm.seq);
		shm_unlock(id);
	}
	up(&shm_ids.sem);
	return err;
}

static void shm_delete (struct inode *ino)
{
	int shmid = ino->i_ino;
	struct shmid_kernel *shp;

	down(&shm_ids.sem);
	shp = shm_lock(shmid);
	if(shp==NULL) {
		BUG();
	}
	shp = shm_rmid(shmid);
	shm_unlock(shmid);
	up(&shm_ids.sem);
	seg_free(shp, 1);
	clear_inode(ino);
}

static inline unsigned long copy_shmid_to_user(void *buf, struct shmid64_ds *in, int version)
{
	switch(version) {
	case IPC_64:
		return copy_to_user(buf, in, sizeof(*in));
	case IPC_OLD:
	    {
		struct shmid_ds out;

		ipc64_perm_to_ipc_perm(&in->shm_perm, &out.shm_perm);
		out.shm_segsz	= in->shm_segsz;
		out.shm_atime	= in->shm_atime;
		out.shm_dtime	= in->shm_dtime;
		out.shm_ctime	= in->shm_ctime;
		out.shm_cpid	= in->shm_cpid;
		out.shm_lpid	= in->shm_lpid;
		out.shm_nattch	= in->shm_nattch;

		return copy_to_user(buf, &out, sizeof(out));
	    }
	default:
		return -EINVAL;
	}
}

struct shm_setbuf {
	uid_t	uid;
	gid_t	gid;
	mode_t	mode;
};	

static inline unsigned long copy_shmid_from_user(struct shm_setbuf *out, void *buf, int version)
{
	switch(version) {
	case IPC_64:
	    {
		struct shmid64_ds tbuf;

		if (copy_from_user(&tbuf, buf, sizeof(tbuf)))
			return -EFAULT;

		out->uid	= tbuf.shm_perm.uid;
		out->gid	= tbuf.shm_perm.gid;
		out->mode	= tbuf.shm_flags;

		return 0;
	    }
	case IPC_OLD:
	    {
		struct shmid_ds tbuf_old;

		if (copy_from_user(&tbuf_old, buf, sizeof(tbuf_old)))
			return -EFAULT;

		out->uid	= tbuf_old.shm_perm.uid;
		out->gid	= tbuf_old.shm_perm.gid;
		out->mode	= tbuf_old.shm_flags;

		return 0;
	    }
	default:
		return -EINVAL;
	}
}

static inline unsigned long copy_shminfo_to_user(void *buf, struct shminfo64 *in, int version)
{
	switch(version) {
	case IPC_64:
		return copy_to_user(buf, in, sizeof(*in));
	case IPC_OLD:
	    {
		struct shminfo out;

		if(in->shmmax > INT_MAX)
			out.shmmax = INT_MAX;
		else
			out.shmmax = (int)in->shmmax;

		out.shmmin	= in->shmmin;
		out.shmmni	= in->shmmni;
		out.shmseg	= in->shmseg;
		out.shmall	= in->shmall; 

		return copy_to_user(buf, &out, sizeof(out));
	    }
	default:
		return -EINVAL;
	}
}

asmlinkage long sys_shmctl (int shmid, int cmd, struct shmid_ds *buf)
{
	struct shm_setbuf setbuf;
	struct shmid_kernel *shp;
	int err, version;

	if (cmd < 0 || shmid < 0)
		return -EINVAL;

	version = ipc_parse_version(&cmd);

	switch (cmd) { /* replace with proc interface ? */
	case IPC_INFO:
	{
		struct shminfo64 shminfo;

		memset(&shminfo,0,sizeof(shminfo));
		shminfo.shmmni = shminfo.shmseg = shm_ctlmni;
		shminfo.shmmax = shm_ctlmax;
		shminfo.shmall = shm_ctlall;

		shminfo.shmmin = SHMMIN;
		if(copy_shminfo_to_user (buf, &shminfo, version))
			return -EFAULT;
		/* reading a integer is always atomic */
		err= shm_ids.max_id;
		if(err<0)
			err = 0;
		return err;
	}
	case SHM_INFO:
	{
		struct shm_info shm_info;

		memset(&shm_info,0,sizeof(shm_info));
		shm_lockall();
		shm_info.used_ids = shm_ids.in_use;
		shm_info.shm_rss = shm_rss;
		shm_info.shm_tot = shm_tot;
		shm_info.shm_swp = shm_swp;
		shm_info.swap_attempts = swap_attempts;
		shm_info.swap_successes = swap_successes;
		err = shm_ids.max_id;
		shm_unlockall();
		if(copy_to_user (buf, &shm_info, sizeof(shm_info)))
			return -EFAULT;

		return err < 0 ? 0 : err;
	}
	case SHM_STAT:
	case IPC_STAT:
	{
		struct shmid64_ds tbuf;
		int result;
		if ((shmid % SEQ_MULTIPLIER) == zero_id)
			return -EINVAL;
		memset(&tbuf, 0, sizeof(tbuf));
		shp = shm_lock(shmid);
		if(shp==NULL)
			return -EINVAL;
		if(cmd==SHM_STAT) {
			err = -EINVAL;
			if (!(shp->shm_flags & SHM_SYSV) ||
			    shmid > shm_ids.max_id)
				goto out_unlock;
			result = shm_buildid(shmid, shp->shm_perm.seq);
		} else {
			err = shm_checkid(shp,shmid);
			if(err)
				goto out_unlock;
			result = 0;
		}
		err=-EACCES;
		if (ipcperms (&shp->shm_perm, S_IRUGO))
			goto out_unlock;
		kernel_to_ipc64_perm(&shp->shm_perm, &tbuf.shm_perm);
		/* ugly hack to keep binary compatibility for ipcs */
		tbuf.shm_flags &= PRV_DEST | PRV_LOCKED | S_IRWXUGO;
		if (tbuf.shm_flags & PRV_DEST)
			tbuf.shm_flags |= SHM_DEST;
		if (tbuf.shm_flags & PRV_LOCKED)
			tbuf.shm_flags |= SHM_LOCKED;
		tbuf.shm_flags &= SHM_DEST | SHM_LOCKED | S_IRWXUGO;
		tbuf.shm_segsz	= shp->shm_segsz;
		tbuf.shm_atime	= shp->shm_atim;
		tbuf.shm_dtime	= shp->shm_dtim;
		tbuf.shm_ctime	= shp->shm_ctim;
		tbuf.shm_cpid	= shp->shm_cprid;
		tbuf.shm_lpid	= shp->shm_lprid;
		tbuf.shm_nattch	= shp->shm_nattch;
		shm_unlock(shmid);
		if(copy_shmid_to_user (buf, &tbuf, version))
			return -EFAULT;
		return result;
	}
	case SHM_LOCK:
	case SHM_UNLOCK:
	{
/* Allow superuser to lock segment in memory */
/* Should the pages be faulted in here or leave it to user? */
/* need to determine interaction with current->swappable */
		if ((shmid % SEQ_MULTIPLIER)== zero_id)
			return -EINVAL;
		if (!capable(CAP_IPC_LOCK))
			return -EPERM;

		shp = shm_lock(shmid);
		if(shp==NULL)
			return -EINVAL;
		err = shm_checkid(shp,shmid);
		if(err)
			goto out_unlock;
		if(cmd==SHM_LOCK)
			shp->shm_flags |= PRV_LOCKED;
		else
			shp->shm_flags &= ~PRV_LOCKED;
		shm_unlock(shmid);
		return err;
	}
	case IPC_RMID:
	{
		/*
		 *	We cannot simply remove the file. The SVID states
		 *	that the block remains until the last person
		 *	detaches from it, then is deleted. A shmat() on
		 *	an RMID segment is legal in older Linux and if 
		 *	we change it apps break...
		 *
		 *	Instead we set a destroyed flag, and then blow
		 *	the name away when the usage hits zero.
		 */
		if ((shmid % SEQ_MULTIPLIER) == zero_id)
			return -EINVAL;
		down(&shm_ids.sem);
		shp = shm_lock(shmid);
		if (shp == NULL) {
			up(&shm_ids.sem);
			return -EINVAL;
		}
		err = shm_checkid(shp, shmid);
		if (err == 0) {
			if (shp->shm_nattch == 0 && 
			    !(shp->shm_flags & SHM_UNLK)) {
				int id=shp->id;
				shm_unlock(shmid);
				up(&shm_ids.sem);
				/*
				 * We can't hold shm_lock here else we
				 * will deadlock in shm_lookup when we
				 * try to recursively grab it.
				 */
				return shm_remove_name(id);
			}
			shp->shm_flags |= PRV_DEST;
			/* Do not find it any more */
			shp->shm_perm.key = IPC_PRIVATE;
		}
		/* Unlock */
		shm_unlock(shmid);
		up(&shm_ids.sem);
		return err;
	}

	case IPC_SET:
	{
		struct dentry * dentry;
		char name[SHM_FMT_LEN+1];

		if ((shmid % SEQ_MULTIPLIER)== zero_id)
			return -EINVAL;

		if(copy_shmid_from_user (&setbuf, buf, version))
			return -EFAULT;
		down(&shm_ids.sem);
		shp = shm_lock(shmid);
		err=-EINVAL;
		if(shp==NULL)
			goto out_up;
		err = shm_checkid(shp,shmid);
		if(err)
			goto out_unlock_up;
		err=-EPERM;
		if (current->euid != shp->shm_perm.uid &&
		    current->euid != shp->shm_perm.cuid && 
		    !capable(CAP_SYS_ADMIN)) {
			goto out_unlock_up;
		}

		shp->shm_perm.uid = setbuf.uid;
		shp->shm_perm.gid = setbuf.gid;
		shp->shm_flags = (shp->shm_flags & ~S_IRWXUGO)
			| (setbuf.mode & S_IRWXUGO);
		shp->shm_ctim = CURRENT_TIME;
		shm_unlock(shmid);
		up(&shm_ids.sem);

		sprintf (name, SHM_FMT, shmid);
		lock_kernel();
		dentry = lookup_one(name, lock_parent(shm_sb->s_root));
		unlock_dir(shm_sb->s_root);
		err = PTR_ERR(dentry);
		if (IS_ERR(dentry))
			goto bad_dentry;
		err = -ENOENT;
		if (dentry->d_inode) {
			struct inode *ino = dentry->d_inode;
			ino->i_uid   = setbuf.uid;
			ino->i_gid   = setbuf.gid;
			ino->i_mode  = (setbuf.mode & S_IRWXUGO) | (ino->i_mode & ~S_IALLUGO);;
			ino->i_atime = ino->i_mtime = ino->i_ctime = CURRENT_TIME;
			err = 0;
		}
		dput (dentry);
	bad_dentry:
		unlock_kernel();
		return err;
	}

	default:
		return -EINVAL;
	}

	err = 0;
out_unlock_up:
	shm_unlock(shmid);
out_up:
	up(&shm_ids.sem);
	return err;
out_unlock:
	shm_unlock(shmid);
	return err;
}

static inline void shm_inc (int id) {
	struct shmid_kernel *shp;

	if(!(shp = shm_lock(id)))
		BUG();
	shp->shm_atim = CURRENT_TIME;
	shp->shm_lprid = current->pid;
	shp->shm_nattch++;
	shm_unlock(id);
}

static int shm_mmap(struct file * file, struct vm_area_struct * vma)
{
	if ((vma->vm_flags & VM_WRITE) && !(vma->vm_flags & VM_SHARED))
		return -EINVAL; /* we cannot do private writable mappings */
	UPDATE_ATIME(file->f_dentry->d_inode);
	vma->vm_ops = &shm_vm_ops;
	shm_inc(file->f_dentry->d_inode->i_ino);
	return 0;
}

/*
 * Fix shmaddr, allocate descriptor, map shm, add attach descriptor to lists.
 */
asmlinkage long sys_shmat (int shmid, char *shmaddr, int shmflg, ulong *raddr)
{
	struct shmid_kernel *shp;
	unsigned long addr;
	struct file * file;
	int    err;
	unsigned long flags;
	unsigned long prot;
	unsigned long o_flags;
	int acc_mode;
	struct dentry *dentry;
	char   name[SHM_FMT_LEN+1];

	if (!shm_sb || (shmid % SEQ_MULTIPLIER) == zero_id)
		return -EINVAL;

	if ((addr = (ulong)shmaddr)) {
		if (addr & (SHMLBA-1)) {
			if (shmflg & SHM_RND)
				addr &= ~(SHMLBA-1);	   /* round down */
			else
				return -EINVAL;
		}
		flags = MAP_SHARED | MAP_FIXED;
	} else
		flags = MAP_SHARED;

	if (shmflg & SHM_RDONLY) {
		prot = PROT_READ;
		o_flags = O_RDONLY;
		acc_mode = S_IRUGO;
	} else {
		prot = PROT_READ | PROT_WRITE;
		o_flags = O_RDWR;
		acc_mode = S_IRUGO | S_IWUGO;
	}

	/*
	 * We cannot rely on the fs check since SYSV IPC does have an
	 * aditional creator id...
	 */
	shp = shm_lock(shmid);
	if(shp==NULL)
		return -EINVAL;
	err = ipcperms(&shp->shm_perm, acc_mode);
	shm_unlock(shmid);
	if (err)
		return -EACCES;

	sprintf (name, SHM_FMT, shmid);

	lock_kernel();
	mntget(shm_fs_type.kern_mnt);
	dentry = lookup_one(name, lock_parent(shm_sb->s_root));
	unlock_dir(shm_sb->s_root);
	err = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto bad_file;
	err = -ENOENT;
	if (!dentry->d_inode)
		goto bad_file;
	file = dentry_open(dentry, shm_fs_type.kern_mnt, o_flags);
	err = PTR_ERR(file);
	if (IS_ERR (file))
		goto bad_file1;
	down(&current->mm->mmap_sem);
	*raddr = do_mmap (file, addr, file->f_dentry->d_inode->i_size,
			  prot, flags, 0);
	up(&current->mm->mmap_sem);
	unlock_kernel();
	if (IS_ERR(*raddr))
		err = PTR_ERR(*raddr);
	else
		err = 0;
	fput (file);
	return err;

bad_file1:
	dput(dentry);
bad_file:
	mntput(shm_fs_type.kern_mnt);
	unlock_kernel();
	if (err == -ENOENT)
		return -EINVAL;
	return err;
}

/* This is called by fork, once for every shm attach. */
static void shm_open (struct vm_area_struct *shmd)
{
	shm_inc (shmd->vm_file->f_dentry->d_inode->i_ino);
}

/*
 *	Remove a name.
 */
 
static int shm_remove_name(int id)
{
	struct dentry *dir;
	struct dentry *dentry;
	int error;
	char name[SHM_FMT_LEN+1];

	sprintf (name, SHM_FMT, id);
	lock_kernel();
	dir = lock_parent(shm_sb->s_root);
	dentry = lookup_one(name, dir);
	error = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		/*
		 * We have to do our own unlink to prevent the vfs
		 * permission check. The SYSV IPC layer has already
		 * checked the permissions which do not comply to the
		 * vfs rules.
		 */
		struct inode *inode = dir->d_inode;
		down(&inode->i_zombie);
		error = shm_unlink(inode, dentry);
		if (!error)
			d_delete(dentry);
		up(&inode->i_zombie);
		dput(dentry);
	}
	unlock_dir(dir);
	unlock_kernel();
	return error;
}

/*
 * remove the attach descriptor shmd.
 * free memory for segment if it is marked destroyed.
 * The descriptor has already been removed from the current->mm->mmap list
 * and will later be kfree()d.
 */
static void shm_close (struct vm_area_struct *shmd)
{
	int id = shmd->vm_file->f_dentry->d_inode->i_ino;
	struct shmid_kernel *shp;

	/* remove from the list of attaches of the shm segment */
	if(!(shp = shm_lock(id)))
		BUG();
	shp->shm_lprid = current->pid;
	shp->shm_dtim = CURRENT_TIME;
	shp->shm_nattch--;
	if(shp->shm_nattch == 0 && 
	   shp->shm_flags & PRV_DEST &&
	   !(shp->shm_flags & SHM_UNLK)) {
		int pid=shp->id;
		int err;
		shm_unlock(id);

		/* The kernel lock prevents new attaches from
		 * being happening.  We can't hold shm_lock here
		 * else we will deadlock in shm_lookup when we
		 * try to recursively grab it.
		 */
		err = shm_remove_name(pid);
		if(err && err != -EINVAL && err != -ENOENT) 
			printk(KERN_ERR "Unlink of SHM id %d failed (%d).\n", pid, err);

	} else {
		shm_unlock(id);
	}
}

/*
 * detach and kill segment if marked destroyed.
 * The work is done in shm_close.
 */
asmlinkage long sys_shmdt (char *shmaddr)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *shmd, *shmdnext;

	down(&mm->mmap_sem);
	for (shmd = mm->mmap; shmd; shmd = shmdnext) {
		shmdnext = shmd->vm_next;
		if (shmd->vm_ops == &shm_vm_ops
		    && shmd->vm_start - (shmd->vm_pgoff << PAGE_SHIFT) == (ulong) shmaddr)
			do_munmap(mm, shmd->vm_start, shmd->vm_end - shmd->vm_start);
	}
	up(&mm->mmap_sem);
	return 0;
}

/*
 * Enter the shm page into the SHM data structures.
 *
 * The way "nopage" is done, we don't actually have to
 * do anything here: nopage will have filled in the shm
 * data structures already, and shm_swap_out() will just
 * work off them..
 */
static int shm_swapout(struct page * page, struct file *file)
{
	return 0;
}

/*
 * page not present ... go through shm_dir
 */
static struct page * shm_nopage_core(struct shmid_kernel *shp, unsigned int idx, int *swp, int *rss, unsigned long address)
{
	pte_t pte;
	struct page * page;

	if (idx >= shp->shm_npages)
		return NOPAGE_SIGBUS;

	pte = SHM_ENTRY(shp,idx);
	if (!pte_present(pte)) {
		/* page not present so shm_swap can't race with us
		   and the semaphore protects us by other tasks that
		   could potentially fault on our pte under us */
		if (pte_none(pte)) {
			shm_unlock(shp->id);
			page = page_cache_alloc();
			if (!page)
				goto oom;
			clear_user_highpage(page, address);
			if ((shp != shm_lock(shp->id)) && (shp->id != zero_id))
				BUG();
		} else {
			swp_entry_t entry = pte_to_swp_entry(pte);

			shm_unlock(shp->id);
			page = lookup_swap_cache(entry);
			if (!page) {
				lock_kernel();
				swapin_readahead(entry);
				page = read_swap_cache(entry);
				unlock_kernel();
				if (!page)
					goto oom;
			}
			delete_from_swap_cache(page);
			page = replace_with_highmem(page);
			swap_free(entry);
			if ((shp != shm_lock(shp->id)) && (shp->id != zero_id))
				BUG();
			(*swp)--;
		}
		(*rss)++;
		pte = pte_mkdirty(mk_pte(page, PAGE_SHARED));
		SHM_ENTRY(shp, idx) = pte;
	}

	/* pte_val(pte) == SHM_ENTRY (shp, idx) */
	page_cache_get(pte_page(pte));
	return pte_page(pte);

oom:
	shm_lock(shp->id);
	return NOPAGE_OOM;
}

static struct page * shm_nopage(struct vm_area_struct * shmd, unsigned long address, int no_share)
{
	struct page * page;
	struct shmid_kernel *shp;
	unsigned int idx;
	struct inode * inode = shmd->vm_file->f_dentry->d_inode;

	idx = (address - shmd->vm_start) >> PAGE_SHIFT;
	idx += shmd->vm_pgoff;

	down(&inode->i_sem);
	if(!(shp = shm_lock(inode->i_ino)))
		BUG();
	page = shm_nopage_core(shp, idx, &shm_swp, &shm_rss, address);
	shm_unlock(inode->i_ino);
	up(&inode->i_sem);
	return(page);
}

#define OKAY	0
#define RETRY	1
#define FAILED	2

static int shm_swap_core(struct shmid_kernel *shp, unsigned long idx, swp_entry_t swap_entry, int *counter, struct page **outpage)
{
	pte_t page;
	struct page *page_map;

	page = SHM_ENTRY(shp, idx);
	if (!pte_present(page))
		return RETRY;
	page_map = pte_page(page);
	if (page_map->zone->free_pages > page_map->zone->pages_high)
		return RETRY;
	if (shp->id != zero_id) swap_attempts++;

	if (--*counter < 0) /* failed */
		return FAILED;
	if (page_count(page_map) != 1)
		return RETRY;

	lock_page(page_map);
	if (!(page_map = prepare_highmem_swapout(page_map)))
		return FAILED;
	SHM_ENTRY (shp, idx) = swp_entry_to_pte(swap_entry);

	/* add the locked page to the swap cache before allowing
	   the swapin path to run lookup_swap_cache(). This avoids
	   reading a not yet uptodate block from disk.
	   NOTE: we just accounted the swap space reference for this
	   swap cache page at __get_swap_page() time. */
	add_to_swap_cache(*outpage = page_map, swap_entry);
	return OKAY;
}

static void shm_swap_postop(struct page *page)
{
	lock_kernel();
	rw_swap_page(WRITE, page, 0);
	unlock_kernel();
	page_cache_release(page);
}

static int shm_swap_preop(swp_entry_t *swap_entry)
{
	lock_kernel();
	/* subtle: preload the swap count for the swap cache. We can't
	   increase the count inside the critical section as we can't release
	   the shm_lock there. And we can't acquire the big lock with the
	   shm_lock held (otherwise we would deadlock too easily). */
	*swap_entry = __get_swap_page(2);
	if (!(*swap_entry).val) {
		unlock_kernel();
		return 1;
	}
	unlock_kernel();
	return 0;
}

/*
 * Goes through counter = (shm_rss / (prio + 1)) present shm pages.
 */
static unsigned long swap_id; /* currently being swapped */
static unsigned long swap_idx; /* next to swap */

int shm_swap (int prio, int gfp_mask)
{
	struct shmid_kernel *shp;
	swp_entry_t swap_entry;
	unsigned long id, idx;
	int loop = 0;
	int counter;
	struct page * page_map;

	zshm_swap(prio, gfp_mask);
	counter = shm_rss / (prio + 1);
	if (!counter)
		return 0;
	if (shm_swap_preop(&swap_entry))
		return 0;

	shm_lockall();
check_id:
	shp = shm_get(swap_id);
	if(shp==NULL || shp->shm_flags & PRV_LOCKED) {
next_id:
		swap_idx = 0;
		if (++swap_id > shm_ids.max_id) {
			swap_id = 0;
			if (loop) {
failed:
				shm_unlockall();
				__swap_free(swap_entry, 2);
				return 0;
			}
			loop = 1;
		}
		goto check_id;
	}
	id = swap_id;

check_table:
	idx = swap_idx++;
	if (idx >= shp->shm_npages)
		goto next_id;

	switch (shm_swap_core(shp, idx, swap_entry, &counter, &page_map)) {
		case RETRY: goto check_table;
		case FAILED: goto failed;
	}
	swap_successes++;
	shm_swp++;
	shm_rss--;
	shm_unlockall();

	shm_swap_postop(page_map);
	return 1;
}

/*
 * Free the swap entry and set the new pte for the shm page.
 */
static void shm_unuse_page(struct shmid_kernel *shp, unsigned long idx,
			   swp_entry_t entry, struct page *page)
{
	pte_t pte;

	pte = pte_mkdirty(mk_pte(page, PAGE_SHARED));
	SHM_ENTRY(shp, idx) = pte;
	page_cache_get(page);
	shm_rss++;

	shm_swp--;

	swap_free(entry);
}

static int shm_unuse_core(struct shmid_kernel *shp, swp_entry_t entry, struct page *page)
{
	int n;

	for (n = 0; n < shp->shm_npages; n++) {
		if (pte_none(SHM_ENTRY(shp,n)))
			continue;
		if (pte_present(SHM_ENTRY(shp,n)))
			continue;
		if (pte_to_swp_entry(SHM_ENTRY(shp,n)).val == entry.val) {
			shm_unuse_page(shp, n, entry, page);
			return 1;
		}
	}
	return 0;
}

/*
 * unuse_shm() search for an eventually swapped out shm page.
 */
void shm_unuse(swp_entry_t entry, struct page *page)
{
	int i;

	shm_lockall();
	for (i = 0; i <= shm_ids.max_id; i++) {
		struct shmid_kernel *shp = shm_get(i);
		if(shp==NULL)
			continue;
		if (shm_unuse_core(shp, entry, page))
			goto out;
	}
out:
	shm_unlockall();
	zmap_unuse(entry, page);
}

#ifdef CONFIG_PROC_FS
static int sysvipc_shm_read_proc(char *buffer, char **start, off_t offset, int length, int *eof, void *data)
{
	off_t pos = 0;
	off_t begin = 0;
	int i, len = 0;

	down(&shm_ids.sem);
	len += sprintf(buffer, "       key      shmid perms       size  cpid  lpid nattch   uid   gid  cuid  cgid      atime      dtime      ctime name\n");

	for(i = 0; i <= shm_ids.max_id; i++) {
		struct shmid_kernel* shp;

		if (i == zero_id)
			continue;
		shp = shm_lock(i);
		if(shp!=NULL) {
#define SMALL_STRING "%10d %10d  %4o %10u %5u %5u  %5d %5u %5u %5u %5u %10lu %10lu %10lu %.*s%s\n"
#define BIG_STRING   "%10d %10d  %4o %21u %5u %5u  %5d %5u %5u %5u %5u %10lu %10lu %10lu %.*s%s\n"
			char *format;

			if (sizeof(size_t) <= sizeof(int))
				format = SMALL_STRING;
			else
				format = BIG_STRING;
			len += sprintf(buffer + len, format,
				shp->shm_perm.key,
				shm_buildid(i, shp->shm_perm.seq),
				shp->shm_flags,
				shp->shm_segsz,
				shp->shm_cprid,
				shp->shm_lprid,
				shp->shm_nattch,
				shp->shm_perm.uid,
				shp->shm_perm.gid,
				shp->shm_perm.cuid,
				shp->shm_perm.cgid,
				shp->shm_atim,
				shp->shm_dtim,
				shp->shm_ctim,
				shp->shm_namelen,
				shp->shm_name,
				shp->shm_flags & SHM_UNLK ? " (deleted)" : "");
			shm_unlock(i);

			pos += len;
			if(pos < offset) {
				len = 0;
				begin = pos;
			}
			if(pos > offset + length)
				goto done;
		}
	}
	*eof = 1;
done:
	up(&shm_ids.sem);
	*start = buffer + (offset - begin);
	len -= (offset - begin);
	if(len > length)
		len = length;
	if(len < 0)
		len = 0;
	return len;
}
#endif

#define VMA_TO_SHP(vma)		((vma)->vm_file->private_data)

static spinlock_t zmap_list_lock = SPIN_LOCK_UNLOCKED;
static unsigned long zswap_idx; /* next to swap */
static struct shmid_kernel *zswap_shp = &zshmid_kernel;
static int zshm_rss;

static struct vm_operations_struct shmzero_vm_ops = {
	open:		shmzero_open,
	close:		shmzero_close,
	nopage:		shmzero_nopage,
	swapout:	shm_swapout,
};

/*
 * In this implementation, the "unuse" and "swapout" interfaces are
 * interlocked out via the kernel_lock, as well as shm_lock(zero_id).
 * "unuse" and "nopage/swapin", as well as "swapout" and "nopage/swapin"
 * interlock via shm_lock(zero_id). All these interlocks can be based
 * on a per mapping lock instead of being a global lock.
 */
/*
 * Reference (existance) counting on the file/dentry/inode is done
 * by generic vm_file code. The zero code does not hold any reference 
 * on the pseudo-file. This is possible because the open/close calls
 * are bracketed by the file count update calls.
 */
static struct file *file_setup(struct file *fzero, struct shmid_kernel *shp)
{
	struct file *filp;
	struct inode *inp;

	if ((filp = get_empty_filp()) == 0)
		return(filp);
	if ((inp = get_empty_inode()) == 0) {
		put_filp(filp);
		return(0);
	}
	if ((filp->f_dentry = d_alloc(zdent, &(const struct qstr) { "dev/zero", 
				8, 0 })) == 0) {
		iput(inp);
		put_filp(filp);
		return(0);
	}
	filp->f_vfsmnt = mntget(shm_fs_type.kern_mnt);
	d_instantiate(filp->f_dentry, inp);

	/*
	 * Copy over dev/ino for benefit of procfs. Use
	 * ino to indicate seperate mappings.
	 */
	filp->f_dentry->d_inode->i_dev = shm_fs_type.kern_mnt->mnt_sb->s_dev;
	filp->f_dentry->d_inode->i_ino = (unsigned long)shp;
	if (fzero)
		fput(fzero);	/* release /dev/zero file */
	return(filp);
}

int map_zero_setup(struct vm_area_struct *vma)
{
	extern int vm_enough_memory(long pages);
	struct shmid_kernel *shp;
	struct file *filp;

	if (!vm_enough_memory((vma->vm_end - vma->vm_start) >> PAGE_SHIFT))
		return -ENOMEM;
	if (IS_ERR(shp = seg_alloc((vma->vm_end - vma->vm_start) / PAGE_SIZE, 0)))
		return PTR_ERR(shp);
	if ((filp = file_setup(vma->vm_file, shp)) == 0) {
		seg_free(shp, 0);
		return -ENOMEM;
	}
	vma->vm_file = filp;
	VMA_TO_SHP(vma) = (void *)shp;
	shp->id = zero_id;
	init_MUTEX(&shp->zsem);
	vma->vm_ops = &shmzero_vm_ops;
	shmzero_open(vma);
	spin_lock(&zmap_list_lock);
	list_add(&shp->zero_list, &zshmid_kernel.zero_list);
	spin_unlock(&zmap_list_lock);
	return 0;
}

static void shmzero_open(struct vm_area_struct *shmd)
{
	struct shmid_kernel *shp;

	shp = VMA_TO_SHP(shmd);
	down(&shp->zsem);
	shp->shm_nattch++;
	up(&shp->zsem);
}

static void shmzero_close(struct vm_area_struct *shmd)
{
	int done = 0;
	struct shmid_kernel *shp;

	shp = VMA_TO_SHP(shmd);
	down(&shp->zsem);
	if (--shp->shm_nattch == 0)
		done = 1;
	up(&shp->zsem);
	if (done) {
		spin_lock(&zmap_list_lock);
		if (shp == zswap_shp)
			zswap_shp = list_entry(zswap_shp->zero_list.next, 
						struct shmid_kernel, zero_list);
		list_del(&shp->zero_list);
		spin_unlock(&zmap_list_lock);
		seg_free(shp, 0);
	}
}

static struct page * shmzero_nopage(struct vm_area_struct * shmd, unsigned long address, int no_share)
{
	struct page *page;
	struct shmid_kernel *shp;
	unsigned int idx;
	int dummy;

	idx = (address - shmd->vm_start) >> PAGE_SHIFT;
	idx += shmd->vm_pgoff;

	shp = VMA_TO_SHP(shmd);
	down(&shp->zsem);
	shm_lock(zero_id);
	page = shm_nopage_core(shp, idx, &dummy, &zshm_rss, address);
	shm_unlock(zero_id);
	up(&shp->zsem);
	return(page);
}

static void zmap_unuse(swp_entry_t entry, struct page *page)
{
	struct shmid_kernel *shp;

	spin_lock(&zmap_list_lock);
	shm_lock(zero_id);
	for (shp = list_entry(zshmid_kernel.zero_list.next, struct shmid_kernel, 
			zero_list); shp != &zshmid_kernel;
			shp = list_entry(shp->zero_list.next, struct shmid_kernel,
								zero_list)) {
		if (shm_unuse_core(shp, entry, page))
			break;
	}
	shm_unlock(zero_id);
	spin_unlock(&zmap_list_lock);
}

static void zshm_swap (int prio, int gfp_mask)
{
	struct shmid_kernel *shp;
	swp_entry_t swap_entry;
	unsigned long idx;
	int loop = 0;
	int counter;
	struct page * page_map;

	counter = zshm_rss / (prio + 1);
	if (!counter)
		return;
next:
	if (shm_swap_preop(&swap_entry))
		return;

	spin_lock(&zmap_list_lock);
	shm_lock(zero_id);
	if (zshmid_kernel.zero_list.next == 0)
		goto failed;
next_id:
	if (zswap_shp == &zshmid_kernel) {
		if (loop) {
failed:
			shm_unlock(zero_id);
			spin_unlock(&zmap_list_lock);
			__swap_free(swap_entry, 2);
			return;
		}
		zswap_shp = list_entry(zshmid_kernel.zero_list.next, 
					struct shmid_kernel, zero_list);
		zswap_idx = 0;
		loop = 1;
	}
	shp = zswap_shp;

check_table:
	idx = zswap_idx++;
	if (idx >= shp->shm_npages) {
		zswap_shp = list_entry(zswap_shp->zero_list.next, 
					struct shmid_kernel, zero_list);
		zswap_idx = 0;
		goto next_id;
	}

	switch (shm_swap_core(shp, idx, swap_entry, &counter, &page_map)) {
		case RETRY: goto check_table;
		case FAILED: goto failed;
	}
	shm_unlock(zero_id);
	spin_unlock(&zmap_list_lock);

	shm_swap_postop(page_map);
	if (counter)
		goto next;
	return;
}

