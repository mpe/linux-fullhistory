/*
 *  dir.c
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *  Modified for big endian by J.F. Chadima and David S. Miller
 *  Modified 1997 Peter Waltenberg, Bill Hawes, David Woodhouse for 2.1 dcache
 *  Modified 1998 Wolfram Pienkoss for NLS
 *  Modified 1999 Wolfram Pienkoss for directory caching
 *
 */

#include <linux/config.h>

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <linux/locks.h>

#include <linux/ncp_fs.h>

#include "ncplib_kernel.h"

static void ncp_read_volume_list(struct file *, void *, filldir_t,
				struct ncp_cache_control *);
static void ncp_do_readdir(struct file *, void *, filldir_t,
				struct ncp_cache_control *);

static ssize_t ncp_dir_read(struct file *, char *, size_t, loff_t *);
static int ncp_readdir(struct file *, void *, filldir_t);

static int ncp_create(struct inode *, struct dentry *, int);
static struct dentry *ncp_lookup(struct inode *, struct dentry *);
static int ncp_unlink(struct inode *, struct dentry *);
static int ncp_mkdir(struct inode *, struct dentry *, int);
static int ncp_rmdir(struct inode *, struct dentry *);
static int ncp_rename(struct inode *, struct dentry *,
	  	      struct inode *, struct dentry *);
#ifdef CONFIG_NCPFS_EXTRAS
extern int ncp_symlink(struct inode *, struct dentry *, const char *);
#endif
		      
static struct file_operations ncp_dir_operations =
{
	NULL,			/* lseek - default */
	ncp_dir_read,		/* read - bad */
	NULL,			/* write - bad */
	ncp_readdir,		/* readdir */
	NULL,			/* poll - default */
	ncp_ioctl,		/* ioctl */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* flush */
	NULL,			/* no special release code */
	NULL			/* fsync */
};

struct inode_operations ncp_dir_inode_operations =
{
	&ncp_dir_operations,	/* default directory file ops */
	ncp_create,		/* create */
	ncp_lookup,		/* lookup */
	NULL,			/* link */
	ncp_unlink,		/* unlink */
#ifdef CONFIG_NCPFS_EXTRAS
	ncp_symlink,		/* symlink */
#else
	NULL,			/* symlink */
#endif
	ncp_mkdir,		/* mkdir */
	ncp_rmdir,		/* rmdir */
	NULL,			/* mknod */
	ncp_rename,		/* rename */
	NULL,			/* readlink */
	NULL,			/* follow link */
	NULL,			/* get_block */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* flushpage */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	NULL,			/* revalidate */
};

static ssize_t 
ncp_dir_read(struct file *filp, char *buf, size_t count, loff_t *ppos)
{
	return -EISDIR;
}

/*
 * Dentry operations routines
 */
static int ncp_lookup_validate(struct dentry *, int);
static int ncp_hash_dentry(struct dentry *, struct qstr *);
static int ncp_compare_dentry (struct dentry *, struct qstr *, struct qstr *);
static void ncp_delete_dentry(struct dentry *);

struct dentry_operations ncp_dentry_operations =
{
	ncp_lookup_validate,	/* d_revalidate(struct dentry *, int) */
	ncp_hash_dentry,	/* d_hash */
	ncp_compare_dentry,    	/* d_compare */
	ncp_delete_dentry	/* d_delete(struct dentry *) */
};


/*
 * XXX: It would be better to use the tolower from linux/ctype.h,
 * but _ctype is needed and it is not exported.
 */
#define tolower(c) (((c) >= 'A' && (c) <= 'Z') ? (c)-('A'-'a') : (c))

/*
 * Note: leave the hash unchanged if the directory
 * is case-sensitive.
 */
static int 
ncp_hash_dentry(struct dentry *dentry, struct qstr *this)
{
	unsigned long hash;
	int i;

	if (!ncp_case_sensitive(dentry->d_inode)) {
		hash = init_name_hash();
		for (i=0; i<this->len ; i++)
			hash = partial_name_hash(tolower(this->name[i]),hash);
		this->hash = end_name_hash(hash);
	}
	return 0;
}

static int
ncp_compare_dentry(struct dentry *dentry, struct qstr *a, struct qstr *b)
{
	int i;

	if (a->len != b->len) return 1;

	if (ncp_case_sensitive(dentry->d_inode))
	    return strncmp(a->name, b->name, a->len);

	for (i=0; i<a->len; i++)
	  if (tolower(a->name[i]) != tolower(b->name[i]))
	    return 1;

	return 0;
}

/*
 * This is the callback from dput() when d_count is going to 0.
 * We use this to unhash dentries with bad inodes and close files.
 */
static void
ncp_delete_dentry(struct dentry * dentry)
{
	struct inode *inode = dentry->d_inode;

	if (inode)
	{
		if (is_bad_inode(inode))
		{
			d_drop(dentry);
		}
		/*
		 * Lock the superblock, then recheck the dentry count.
		 * (Somebody might have used it again ...)
		 */
		if (dentry->d_count == 1 && NCP_FINFO(inode)->opened) {
			PPRINTK("ncp_delete_dentry: closing file %s/%s\n",
			dentry->d_parent->d_name.name, dentry->d_name.name);
			ncp_make_closed(inode);
		}
	} else
	{
	/* N.B. Unhash negative dentries? */
	}
}

static inline int
ncp_single_volume(struct ncp_server *server)
{
	return (server->m.mounted_vol[0] != '\0');
}

static inline int ncp_is_server_root(struct inode *inode)
{
	return (!ncp_single_volume(NCP_SERVER(inode)) &&
		inode == inode->i_sb->s_root->d_inode);
}


/*
 * This is the callback when the dcache has a lookup hit.
 */


#ifdef CONFIG_NCPFS_STRONG
/* try to delete a readonly file (NW R bit set) */

static int
ncp_force_unlink(struct inode *dir, struct dentry* dentry)
{
        int res=0x9c,res2;
	struct nw_modify_dos_info info;
	__u32 old_nwattr;
	struct inode *inode;

	memset(&info, 0, sizeof(info));
	
        /* remove the Read-Only flag on the NW server */
	inode = dentry->d_inode;

	old_nwattr = NCP_FINFO(inode)->nwattr;
	info.attributes = old_nwattr & ~(aRONLY|aDELETEINHIBIT|aRENAMEINHIBIT);
	res2 = ncp_modify_file_or_subdir_dos_info_path(NCP_SERVER(inode), inode, NULL, DM_ATTRIBUTES, &info);
	if (res2)
		goto leave_me;

        /* now try again the delete operation */
        res = ncp_del_file_or_subdir2(NCP_SERVER(dir), dentry);

        if (res)  /* delete failed, set R bit again */
        {
		info.attributes = old_nwattr;
		res2 = ncp_modify_file_or_subdir_dos_info_path(NCP_SERVER(inode), inode, NULL, DM_ATTRIBUTES, &info);
		if (res2)
                        goto leave_me;
        }
leave_me:
        return(res);
}
#endif	/* CONFIG_NCPFS_STRONG */

#ifdef CONFIG_NCPFS_STRONG
static int
ncp_force_rename(struct inode *old_dir, struct dentry* old_dentry, char *_old_name,
                 struct inode *new_dir, struct dentry* new_dentry, char *_new_name)
{
	struct nw_modify_dos_info info;
        int res=0x90,res2;
	struct inode *old_inode = old_dentry->d_inode;
	__u32 old_nwattr = NCP_FINFO(old_inode)->nwattr;
	__u32 new_nwattr = 0; /* shut compiler warning */
	int old_nwattr_changed = 0;
	int new_nwattr_changed = 0;

	memset(&info, 0, sizeof(info));
	
        /* remove the Read-Only flag on the NW server */

	info.attributes = old_nwattr & ~(aRONLY|aRENAMEINHIBIT|aDELETEINHIBIT);
	res2 = ncp_modify_file_or_subdir_dos_info_path(NCP_SERVER(old_inode), old_inode, NULL, DM_ATTRIBUTES, &info);
	if (!res2)
		old_nwattr_changed = 1;
	if (new_dentry && new_dentry->d_inode) {
		new_nwattr = NCP_FINFO(new_dentry->d_inode)->nwattr;
		info.attributes = new_nwattr & ~(aRONLY|aRENAMEINHIBIT|aDELETEINHIBIT);
		res2 = ncp_modify_file_or_subdir_dos_info_path(NCP_SERVER(new_dir), new_dir, _new_name, DM_ATTRIBUTES, &info);
		if (!res2)
			new_nwattr_changed = 1;
	}
        /* now try again the rename operation */
	/* but only if something really happened */
	if (new_nwattr_changed || old_nwattr_changed) {
	        res = ncp_ren_or_mov_file_or_subdir(NCP_SERVER(old_dir),
        	                                    old_dir, _old_name,
                	                            new_dir, _new_name);
	} 
	if (res)
		goto leave_me;
	/* file was successfully renamed, so:
	   do not set attributes on old file - it no longer exists
	   copy attributes from old file to new */
	new_nwattr_changed = old_nwattr_changed;
	new_nwattr = old_nwattr;
	old_nwattr_changed = 0;
	
leave_me:;
	if (old_nwattr_changed) {
		info.attributes = old_nwattr;
		res2 = ncp_modify_file_or_subdir_dos_info_path(NCP_SERVER(old_inode), old_inode, NULL, DM_ATTRIBUTES, &info);
		/* ignore errors */
	}
	if (new_nwattr_changed)	{
		info.attributes = new_nwattr;
		res2 = ncp_modify_file_or_subdir_dos_info_path(NCP_SERVER(new_dir), new_dir, _new_name, DM_ATTRIBUTES, &info);
		/* ignore errors */
	}
        return(res);
}
#endif	/* CONFIG_NCPFS_STRONG */


static int
ncp_lookup_validate(struct dentry * dentry, int flags)
{
	struct ncp_server *server;
	struct inode *dir = dentry->d_parent->d_inode;
	int res, val = 0;
	int len = dentry->d_name.len;      
	struct ncp_entry_info finfo;
	__u8 __name[dentry->d_name.len + 1];

	if (!dentry->d_inode || !dir)
		goto finished;

	server = NCP_SERVER(dir);

	if (!ncp_conn_valid(server))
		goto finished;

	/*
	 * Inspired by smbfs:
	 * The default validation is based on dentry age:
	 * We set the max age at mount time.  (But each
	 * successful server lookup renews the timestamp.)
	 */
	val = NCP_TEST_AGE(server, dentry);
	if (val)
		goto finished;

	PPRINTK("ncp_lookup_validate: %s/%s not valid, age=%ld\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		NCP_GET_AGE(dentry));

	memcpy(__name, dentry->d_name.name, len);
	__name[len] = '\0';

	PPRINTK("ncp_lookup_validate: %s, len %d\n", __name, len);
	PPRINTK("ncp_lookup_validate: server lookup for %s/%s\n",
			dentry->d_parent->d_name.name, __name);

	if (ncp_is_server_root(dir)) {
		io2vol(server, __name, 1);
		res = ncp_lookup_volume(server, __name, &(finfo.i));
	} else {
		io2vol(server, __name, !ncp_preserve_case(dir));
		res = ncp_obtain_info(server, dir, __name, &(finfo.i));
	}
	PPRINTK("ncp_lookup_validate: looked for %s/%s, res=%d\n",
		dentry->d_parent->d_name.name, __name, res);
	/*
	 * If we didn't find it, or if it has a different dirEntNum to
	 * what we remember, it's not valid any more.
	 */
	if (!res) {
		if (finfo.i.dirEntNum == NCP_FINFO(dentry->d_inode)->dirEntNum)
			val=1;
		else
			PPRINTK("ncp_lookup_validate: found, but dirEntNum changed\n");

		vol2io(server, finfo.i.entryName,
			!ncp_preserve_entry_case(dir, finfo.i.NSCreator));
		ncp_update_inode2(dentry->d_inode, &finfo);
		if (val)
			ncp_new_dentry(dentry);
	}

finished:
	PPRINTK("ncp_lookup_validate: result=%d\n", val);
	return val;
}

static struct page *
ncp_get_cache_page(struct inode *inode, unsigned long offset, int used)
{
	struct address_space *i_data = &inode->i_data;
	struct page *new_page, *page, **hash;

	hash = page_hash(i_data, offset);

	page = __find_lock_page(i_data, offset, hash);
	if (used || page)
		return page;

	new_page = page_cache_alloc();
	if (!new_page)
		return NULL;

	for (;;) {
		page = new_page;
		if (!add_to_page_cache_unique(page, i_data, offset, hash))
			break;
		page_cache_release(page);
		page = __find_lock_page(i_data, offset, hash);
		if (page) {
			page_cache_free(new_page);
			break;
		}
	}

	return page;
}

/* most parts from nfsd_d_validate() */
static int
ncp_d_validate(struct dentry *dentry)
{
	unsigned long dent_addr = (unsigned long) dentry;
	unsigned long min_addr = PAGE_OFFSET;
	unsigned long align_mask = 0x0F;
	unsigned int len;
	int valid = 0;

	if (dent_addr < min_addr)
		goto bad_addr;
	if (dent_addr > (unsigned long)high_memory - sizeof(struct dentry))
		goto bad_addr;
	if ((dent_addr & ~align_mask) != dent_addr)
		goto bad_align;
	if (!kern_addr_valid(dent_addr))
		goto bad_addr;
	/*
	 * Looks safe enough to dereference ...
	 */
	len = dentry->d_name.len;
	if (len > NCP_MAXPATHLEN)
		goto out;
	/*
	 * Note: d_validate doesn't dereference the parent pointer ...
	 * just combines it with the name hash to find the hash chain.
	 */
	valid = d_validate(dentry, dentry->d_parent, dentry->d_name.hash, len);
out:
	return valid;

bad_addr:
	PRINTK("ncp_d_validate: invalid address %lx\n", dent_addr);
	goto out;
bad_align:
	PRINTK("ncp_d_validate: unaligned address %lx\n", dent_addr);
	goto out;
}

static struct dentry *
ncp_dget_fpos(struct dentry *dentry, struct dentry *parent, unsigned long fpos)
{
	struct dentry *dent = dentry;
	struct list_head *next;

	if (ncp_d_validate(dent))
		if ((dent->d_parent == parent) &&
		    ((unsigned long)dent->d_fsdata == fpos))
			goto out;

	/* If a pointer is invalid, we search the dentry. */
	next = parent->d_subdirs.next;
	while (next != &parent->d_subdirs) {
		dent = list_entry(next, struct dentry, d_child);
		if ((unsigned long)dent->d_fsdata == fpos)
			goto out;
		next = next->next;
	}
	dent = NULL;
out:
	if (dent)
		if (dent->d_inode)
			return dget(dent);
	return NULL;
}

static time_t ncp_obtain_mtime(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct ncp_server *server = NCP_SERVER(inode);
	struct nw_info_struct i;

	if (!ncp_conn_valid(server) ||
	    ncp_is_server_root(inode))
		return 0;

	if (ncp_obtain_info(server, inode, NULL, &i))
		return 0;

	return ncp_date_dos2unix(le16_to_cpu(i.modifyTime),
						le16_to_cpu(i.modifyDate));
}

static int ncp_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct dentry *dentry = filp->f_dentry;
	struct inode *inode = dentry->d_inode;
	struct page *page = NULL;
	struct ncp_server *server = NCP_SERVER(inode);
	union  ncp_dir_cache *cache = NULL;
	struct ncp_cache_control ctl;
	int result, mtime_valid = 0;
	time_t mtime = 0;

	ctl.page  = NULL;
	ctl.cache = NULL;

	DDPRINTK("ncp_readdir: reading %s/%s, pos=%d\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		(int) filp->f_pos);

	result = -EIO;
	if (!ncp_conn_valid(server))
		goto out;

	result = 0;
	if (filp->f_pos == 0) {
		if (filldir(dirent, ".", 1, 0, inode->i_ino))
			goto out;
		filp->f_pos = 1;
	}
	if (filp->f_pos == 1) {
		if (filldir(dirent, "..", 2, 1,
				dentry->d_parent->d_inode->i_ino))
			goto out;
		filp->f_pos = 2;
	}

	page = ncp_get_cache_page(inode, 0, 0);
	if (!page)
		goto read_really;

	ctl.cache = cache = (union ncp_dir_cache *) page_address(page);
	ctl.head  = cache->head;

	if (!Page_Uptodate(page) || !ctl.head.eof)
		goto init_cache;

	if (filp->f_pos == 2) {
		if (jiffies - ctl.head.time >= NCP_MAX_AGE(server))
			goto init_cache;

		mtime = ncp_obtain_mtime(dentry);
		mtime_valid = 1;
		if ((!mtime) || (mtime != ctl.head.mtime))
			goto init_cache;
	}

	if (filp->f_pos > ctl.head.end)
		goto finished;

	ctl.fpos = filp->f_pos + (NCP_DIRCACHE_START - 2);
	ctl.ofs  = ctl.fpos / NCP_DIRCACHE_SIZE;
	ctl.idx  = ctl.fpos % NCP_DIRCACHE_SIZE;

	for (;;) {
		if (ctl.ofs != 0) {
			ctl.page = ncp_get_cache_page(inode, ctl.ofs, 1);
			if (!ctl.page)
				goto invalid_cache;
			if (!Page_Uptodate(ctl.page))
				goto invalid_cache;
			ctl.cache = (union ncp_dir_cache *)
					page_address(ctl.page);
		}
		while (ctl.idx < NCP_DIRCACHE_SIZE) {
			struct dentry *dent;
			int res;

			dent = ncp_dget_fpos(ctl.cache->dentry[ctl.idx],
						dentry, filp->f_pos);
			if (!dent)
				goto invalid_cache;
			res = filldir(dirent, dent->d_name.name,
					dent->d_name.len, filp->f_pos,
					dent->d_inode->i_ino);
			dput(dent);
			if (res)
				goto finished;
			filp->f_pos += 1;
			ctl.idx += 1;
			if (filp->f_pos > ctl.head.end)
				goto finished;
		}
		if (ctl.page) {
			SetPageUptodate(ctl.page);
			UnlockPage(ctl.page);
			page_cache_release(ctl.page);
			ctl.page = NULL;
		}
		ctl.idx  = 0;
		ctl.ofs += 1;
	}
invalid_cache:
	if (ctl.page) {
		UnlockPage(ctl.page);
		page_cache_release(ctl.page);
		ctl.page = NULL;
	}
	ctl.cache = cache;
init_cache:
	ncp_invalidate_dircache_entries(dentry);
	if (!mtime_valid) {
		mtime = ncp_obtain_mtime(dentry);
		mtime_valid = 1;
	}
	ctl.head.mtime = mtime;
	ctl.head.time = jiffies;
	ctl.head.eof = 0;
	ctl.fpos = 2;
	ctl.ofs = 0;
	ctl.idx = NCP_DIRCACHE_START;
	ctl.filled = 0;
	ctl.valid  = 1;
read_really:
	if (ncp_is_server_root(inode)) {
		ncp_read_volume_list(filp, dirent, filldir, &ctl);
	} else {
		ncp_do_readdir(filp, dirent, filldir, &ctl);
	}
	ctl.head.end = ctl.fpos - 1;
	ctl.head.eof = ctl.valid;
finished:
	if (page) {
		cache->head = ctl.head;
		SetPageUptodate(page);
		UnlockPage(page);
		page_cache_release(page);
	}
	if (ctl.page) {
		SetPageUptodate(ctl.page);
		UnlockPage(ctl.page);
		page_cache_release(ctl.page);
	}
out:
	return result;
}

static int
ncp_fill_cache(struct file *filp, void *dirent, filldir_t filldir,
		struct ncp_cache_control *ctrl, struct ncp_entry_info *entry)
{
	struct dentry *newdent, *dentry = filp->f_dentry;
	struct inode *newino, *inode = dentry->d_inode;
	struct ncp_server *server = NCP_SERVER(inode);
	struct ncp_cache_control ctl = *ctrl;
	struct qstr qname;
	ino_t ino = 0;
	int valid = 0;

	vol2io(server, entry->i.entryName,
		!ncp_preserve_entry_case(inode, entry->i.NSCreator));

	qname.name = entry->i.entryName;
	qname.len  = entry->i.nameLen;
	qname.hash = full_name_hash(qname.name, qname.len);

	if (dentry->d_op && dentry->d_op->d_hash)
		if (dentry->d_op->d_hash(dentry, &qname) != 0)
			goto end_advance;

	newdent = d_lookup(dentry, &qname);

	if (!newdent) {
		newdent = d_alloc(dentry, &qname);
		if (!newdent)
			goto end_advance;
	} else
		memcpy((char *) newdent->d_name.name, qname.name,
							newdent->d_name.len);

	if (!newdent->d_inode) {
		entry->opened = 0;
		entry->ino = iunique(inode->i_sb, 2);
		newino = ncp_iget(inode->i_sb, entry);
		if (newino) {
			newdent->d_op = &ncp_dentry_operations;
			d_add(newdent, newino);
		}
	} else
		ncp_update_inode2(newdent->d_inode, entry);

	if (newdent->d_inode) {
		ino = newdent->d_inode->i_ino;
		newdent->d_fsdata = (void *) ctl.fpos;
		ncp_new_dentry(newdent);
	}

	if (ctl.idx >= NCP_DIRCACHE_SIZE) {
		if (ctl.page) {
			SetPageUptodate(ctl.page);
			UnlockPage(ctl.page);
			page_cache_release(ctl.page);
		}
		ctl.cache = NULL;
		ctl.idx  -= NCP_DIRCACHE_SIZE;
		ctl.ofs  += 1;
		ctl.page  = ncp_get_cache_page(inode, ctl.ofs, 0);
		if (ctl.page)
			ctl.cache = (union ncp_dir_cache *)
					page_address(ctl.page);
	}
	if (ctl.cache) {
		ctl.cache->dentry[ctl.idx] = newdent;
		valid = 1;
	}
	dput(newdent);
end_advance:
	if (!valid)
		ctl.valid = 0;
	if (!ctl.filled && (ctl.fpos == filp->f_pos)) {
		if (!ino)
			ino = find_inode_number(dentry, &qname);
		if (!ino)
			ino = iunique(inode->i_sb, 2);
		ctl.filled = filldir(dirent, entry->i.entryName,
					entry->i.nameLen, filp->f_pos, ino);
		if (!ctl.filled)
			filp->f_pos += 1;
	}
	ctl.fpos += 1;
	ctl.idx  += 1;
	*ctrl = ctl;
	return (ctl.valid || !ctl.filled);
}

static void
ncp_read_volume_list(struct file *filp, void *dirent, filldir_t filldir,
			struct ncp_cache_control *ctl)
{
	struct dentry *dentry = filp->f_dentry;
	struct inode *inode = dentry->d_inode;
	struct ncp_server *server = NCP_SERVER(inode);
	struct ncp_volume_info info;
	struct ncp_entry_info entry;
	int i;

	DPRINTK("ncp_read_volume_list: pos=%ld\n",
			(unsigned long) filp->f_pos);

	for (i = 0; i < NCP_NUMBER_OF_VOLUMES; i++) {

		if (ncp_get_volume_info_with_number(server, i, &info) != 0)
			return;
		if (!strlen(info.volume_name))
			continue;

		DPRINTK("ncp_read_volume_list: found vol: %s\n",
			info.volume_name);

		if (ncp_lookup_volume(server, info.volume_name,
					&entry.i)) {
			DPRINTK("ncpfs: could not lookup vol %s\n",
				info.volume_name);
			continue;
		}
		if (!ncp_fill_cache(filp, dirent, filldir, ctl, &entry))
			return;
	}
}

static void ncp_do_readdir(struct file *filp, void *dirent, filldir_t filldir,
				struct ncp_cache_control *ctl)
{
	struct dentry *dentry = filp->f_dentry;
	struct inode *dir = dentry->d_inode;
	struct ncp_server *server = NCP_SERVER(dir);
	struct nw_search_sequence seq;
	struct ncp_entry_info entry;
	int err;

	DPRINTK("ncp_do_readdir: %s/%s, fpos=%ld\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		(unsigned long) filp->f_pos);
	PPRINTK("ncp_do_readdir: init %s, volnum=%d, dirent=%u\n",
		dentry->d_name.name, NCP_FINFO(dir)->volNumber,
		NCP_FINFO(dir)->dirEntNum);

	err = ncp_initialize_search(server, dir, &seq);
	if (err) {
		DPRINTK("ncp_do_readdir: init failed, err=%d\n", err);
		return;
	}
	for (;;) {
		err = ncp_search_for_file_or_subdir(server, &seq, &entry.i);
		if (err) {
			DPRINTK("ncp_do_readdir: search failed, err=%d\n", err);
			return;
		}
		if (!ncp_fill_cache(filp, dirent, filldir, ctl, &entry))
			return;
	}
}

int ncp_conn_logged_in(struct super_block *sb)
{
	struct ncp_server* server = NCP_SBP(sb);
	struct nw_info_struct i;
	int result;

	if (ncp_single_volume(server)) {
		struct dentry* dent;

		result = -ENOENT;
		io2vol(server, server->m.mounted_vol, 1);
		if (ncp_lookup_volume(server, server->m.mounted_vol, &i)) {
			PPRINTK("ncp_conn_logged_in: %s not found\n",
				server->m.mounted_vol);
			goto out;
		}
		vol2io(server, i.entryName, 1);
		dent = sb->s_root;
		if (dent) {
			struct inode* ino = dent->d_inode;
			if (ino) {
				NCP_FINFO(ino)->volNumber = i.volNumber;
				NCP_FINFO(ino)->dirEntNum = i.dirEntNum;
				NCP_FINFO(ino)->DosDirNum = i.DosDirNum;
			} else {
				DPRINTK("ncpfs: sb->s_root->d_inode == NULL!\n");
			}
		} else {
			DPRINTK("ncpfs: sb->s_root == NULL!\n");
		}
	}
	result = 0;

out:
	return result;
}

static struct dentry *ncp_lookup(struct inode *dir, struct dentry *dentry)
{
	struct ncp_server *server;
	struct inode *inode = NULL;
	int error, res, len = dentry->d_name.len;
	struct ncp_entry_info finfo;
	__u8 __name[dentry->d_name.len + 1];

	server = NCP_SERVER(dir);

	error = -EIO;
	if (!ncp_conn_valid(server))
		goto finished;

	memcpy(__name, dentry->d_name.name, len);
	__name[len] = '\0';
	PPRINTK("ncp_lookup: %s, len %d\n", __name, len);
	PPRINTK("ncp_lookup: server lookup for %s/%s\n",
		dentry->d_parent->d_name.name, __name);
	if (ncp_is_server_root(dir)) {
		io2vol(server, __name, 1);
		res = ncp_lookup_volume(server, __name, &(finfo.i));
	} else {
		io2vol(server, __name, !ncp_preserve_case(dir));
		res = ncp_obtain_info(server, dir, __name, &(finfo.i));
	}
	PPRINTK("ncp_lookup: looked for %s/%s, res=%d\n",
		dentry->d_parent->d_name.name, __name, res);
	/*
	 * If we didn't find an entry, make a negative dentry.
	 */
	if (res)
		goto add_entry;

	vol2io(server, finfo.i.entryName,
		!ncp_preserve_entry_case(dir, finfo.i.NSCreator));

	/*
	 * Create an inode for the entry.
	 */
	finfo.opened = 0;
	finfo.ino = iunique(dir->i_sb, 2);
	error = -EACCES;
	inode = ncp_iget(dir->i_sb, &finfo);

	if (inode) {
add_entry:
		dentry->d_op = &ncp_dentry_operations;
		d_add(dentry, inode);
		ncp_new_dentry(dentry);
		error = 0;
	}

finished:
	PPRINTK("ncp_lookup: result=%d\n", error);
	return ERR_PTR(error);
}

/*
 * This code is common to create, mkdir, and mknod.
 */
static int ncp_instantiate(struct inode *dir, struct dentry *dentry,
			struct ncp_entry_info *finfo)
{
	struct inode *inode;
	int error = -EINVAL;

	finfo->ino = iunique(dir->i_sb, 2);
	inode = ncp_iget(dir->i_sb, finfo);
	if (!inode)
		goto out_close;
	d_instantiate(dentry,inode);
	error = 0;
out:
	return error;

out_close:
	PPRINTK("ncp_instantiate: %s/%s failed, closing file\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);
	ncp_close_file(NCP_SERVER(dir), finfo->file_handle);
	goto out;
}

int ncp_create_new(struct inode *dir, struct dentry *dentry, int mode,
		int attributes)
{
	int error, result;
	struct ncp_entry_info finfo;
	struct ncp_server *server = NCP_SERVER(dir);
	__u8 _name[dentry->d_name.len + 1];
	
	PPRINTK("ncp_create_new: creating %s/%s, mode=%x\n",
		dentry->d_parent->d_name.name, dentry->d_name.name, mode);
	error = -EIO;
	if (!ncp_conn_valid(server))
		goto out;

	ncp_age_dentry(server, dentry);

	memcpy(_name, dentry->d_name.name, dentry->d_name.len);
	_name[dentry->d_name.len] = '\0';

	io2vol(server, _name, !ncp_preserve_case(dir));

	error = -EACCES;
	result = ncp_open_create_file_or_subdir(server, dir, _name,
			   OC_MODE_CREATE | OC_MODE_OPEN | OC_MODE_REPLACE,
			   attributes, AR_READ | AR_WRITE, &finfo);
	if (!result) {
		finfo.access = O_RDWR;
		error = ncp_instantiate(dir, dentry, &finfo);
	} else {
		if (result == 0x87) error = -ENAMETOOLONG;
		DPRINTK("ncp_create: %s/%s failed\n",
			dentry->d_parent->d_name.name, dentry->d_name.name);
	}

out:
	return error;
}

static int ncp_create(struct inode *dir, struct dentry *dentry, int mode)
{
	return ncp_create_new(dir, dentry, mode, 0);
}

static int ncp_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int error;
	struct ncp_entry_info finfo;
	struct ncp_server *server = NCP_SERVER(dir);
	__u8 _name[dentry->d_name.len + 1];

	DPRINTK("ncp_mkdir: making %s/%s\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);
	error = -EIO;
	if (!ncp_conn_valid(server))
		goto out;

	ncp_age_dentry(server, dentry);

	memcpy(_name, dentry->d_name.name, dentry->d_name.len);
	_name[dentry->d_name.len] = '\0';
	io2vol(server, _name, !ncp_preserve_case(dir));

	error = -EACCES;
	if (ncp_open_create_file_or_subdir(server, dir, _name,
					   OC_MODE_CREATE, aDIR, 0xffff,
					   &finfo) == 0)
	{
		error = ncp_instantiate(dir, dentry, &finfo);
	}
out:
	return error;
}

static int ncp_rmdir(struct inode *dir, struct dentry *dentry)
{
	int error, result;
	struct ncp_server *server = NCP_SERVER(dir);
	__u8 _name[dentry->d_name.len + 1];

	DPRINTK("ncp_rmdir: removing %s/%s\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);

	error = -EIO;
	if (!ncp_conn_valid(server))
		goto out;

	error = -EBUSY;
	if (!list_empty(&dentry->d_hash))
		goto out;

	memcpy(_name, dentry->d_name.name, dentry->d_name.len);
	_name[dentry->d_name.len] = '\0';
	    
	io2vol(server, _name, !ncp_preserve_case(dir));
	result = ncp_del_file_or_subdir(server, dir, _name);
	switch (result) {
		case 0x00:
			error = 0;
			break;
		case 0x85:	/* unauthorized to delete file */
		case 0x8A:	/* unauthorized to delete file */
			error = -EACCES;
			break;
		case 0x8F:
		case 0x90:	/* read only */
			error = -EPERM;
			break;
		case 0x9F:	/* in use by another client */
			error = -EBUSY;
			break;
		case 0xA0:	/* directory not empty */
			error = -ENOTEMPTY;
			break;
		case 0xFF:	/* someone deleted file */
			error = -ENOENT;
			break;
		default:
			error = -EACCES;
			break;
       	}
out:
	return error;
}

static int ncp_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct ncp_server *server = NCP_SERVER(dir);
	int error;

	DPRINTK("ncp_unlink: unlinking %s/%s\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);
	
	error = -EIO;
	if (!ncp_conn_valid(server))
		goto out;

	/*
	 * Check whether to close the file ...
	 */
	if (inode && NCP_FINFO(inode)->opened) {
		PPRINTK("ncp_unlink: closing file\n");
		ncp_make_closed(inode);
	}

	error = ncp_del_file_or_subdir2(server, dentry);
#ifdef CONFIG_NCPFS_STRONG
	/* 9C is Invalid path.. It should be 8F, 90 - read only, but
	   it is not :-( */
	if (error == 0x9C && server->m.flags & NCP_MOUNT_STRONG) { /* R/O */
		error = ncp_force_unlink(dir, dentry);
	}
#endif
	switch (error) {
		case 0x00:
			DPRINTK("ncp: removed %s/%s\n",
				dentry->d_parent->d_name.name, dentry->d_name.name);
			d_delete(dentry);
			break;
		case 0x85:
		case 0x8A:
			error = -EACCES;
			break;
		case 0x8D:	/* some files in use */
		case 0x8E:	/* all files in use */
			error = -EBUSY;
			break;
		case 0x8F:	/* some read only */
		case 0x90:	/* all read only */
		case 0x9C:	/* !!! returned when in-use or read-only by NW4 */
			error = -EPERM;
			break;
		case 0xFF:
			error = -ENOENT;
			break;
		default:
			error = -EACCES;
			break;
	}
		
out:
	return error;
}

static int ncp_rename(struct inode *old_dir, struct dentry *old_dentry,
		      struct inode *new_dir, struct dentry *new_dentry)
{
	int old_len = old_dentry->d_name.len;
	int new_len = new_dentry->d_name.len;
	int error;
	char _old_name[old_dentry->d_name.len + 1];
	char _new_name[new_dentry->d_name.len + 1];

	DPRINTK("ncp_rename: %s/%s to %s/%s\n",
		old_dentry->d_parent->d_name.name, old_dentry->d_name.name,
		new_dentry->d_parent->d_name.name, new_dentry->d_name.name);

	error = -EIO;
	if (!ncp_conn_valid(NCP_SERVER(old_dir)))
		goto out;

	ncp_age_dentry(NCP_SERVER(old_dir), old_dentry);
	ncp_age_dentry(NCP_SERVER(new_dir), new_dentry);

	memcpy(_old_name, old_dentry->d_name.name, old_len);
	_old_name[old_len] = '\0';
	io2vol(NCP_SERVER(old_dir), _old_name, !ncp_preserve_case(old_dir));

	memcpy(_new_name, new_dentry->d_name.name, new_len);
	_new_name[new_len] = '\0';
	io2vol(NCP_SERVER(new_dir), _new_name, !ncp_preserve_case(new_dir));

	error = ncp_ren_or_mov_file_or_subdir(NCP_SERVER(old_dir),
					    old_dir, _old_name,
					    new_dir, _new_name);
#ifdef CONFIG_NCPFS_STRONG
	if ((error == 0x90 || error == -EACCES) && NCP_SERVER(old_dir)->m.flags & NCP_MOUNT_STRONG) {	/* RO */
		error = ncp_force_rename(old_dir, old_dentry, _old_name,
                                         new_dir, new_dentry, _new_name);
	}
#endif
	switch (error) {
		case 0x00:
               	        DPRINTK("ncp renamed %s -> %s.\n",
                                old_dentry->d_name.name,new_dentry->d_name.name);
			/* d_move(old_dentry, new_dentry); */
			break;
		case 0x9E:
			error = -ENAMETOOLONG;
			break;
		case 0xFF:
			error = -ENOENT;
			break;
		default:
			error = -EACCES;
			break;
	}
out:
	return error;
}

/* The following routines are taken directly from msdos-fs */

/* Linear day numbers of the respective 1sts in non-leap years. */

static int day_n[] =
{0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 0, 0, 0, 0};
/* Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec */


extern struct timezone sys_tz;

static int utc2local(int time)
{
	return time - sys_tz.tz_minuteswest * 60;
}

static int local2utc(int time)
{
	return time + sys_tz.tz_minuteswest * 60;
}

/* Convert a MS-DOS time/date pair to a UNIX date (seconds since 1 1 70). */
int
ncp_date_dos2unix(unsigned short time, unsigned short date)
{
	int month, year, secs;

	/* first subtract and mask after that... Otherwise, if
	   date == 0, bad things happen */
	month = ((date >> 5) - 1) & 15;
	year = date >> 9;
	secs = (time & 31) * 2 + 60 * ((time >> 5) & 63) + (time >> 11) * 3600 +
		86400 * ((date & 31) - 1 + day_n[month] + (year / 4) + 
		year * 365 - ((year & 3) == 0 && month < 2 ? 1 : 0) + 3653);
	/* days since 1.1.70 plus 80's leap day */
	return local2utc(secs);
}


/* Convert linear UNIX date to a MS-DOS time/date pair. */
void
ncp_date_unix2dos(int unix_date, unsigned short *time, unsigned short *date)
{
	int day, year, nl_day, month;

	unix_date = utc2local(unix_date);
	*time = (unix_date % 60) / 2 + (((unix_date / 60) % 60) << 5) +
	    (((unix_date / 3600) % 24) << 11);
	day = unix_date / 86400 - 3652;
	year = day / 365;
	if ((year + 3) / 4 + 365 * year > day)
		year--;
	day -= (year + 3) / 4 + 365 * year;
	if (day == 59 && !(year & 3)) {
		nl_day = day;
		month = 2;
	} else {
		nl_day = (year & 3) || day <= 59 ? day : day - 1;
		for (month = 0; month < 12; month++)
			if (day_n[month] > nl_day)
				break;
	}
	*date = nl_day - day_n[month - 1] + 1 + (month << 5) + (year << 9);
}
