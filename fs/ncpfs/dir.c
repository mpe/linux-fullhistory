/*
 *  dir.c
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *  Modified for big endian by J.F. Chadima and David S. Miller
 *  Modified 1997 Peter Waltenberg, Bill Hawes, David Woodhouse for 2.1 dcache
 *  Modified 1998 Wolfram Pienkoss for NLS
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

struct ncp_dirent {
	struct nw_info_struct i;
	struct nw_search_sequence s;	/* given back for i */
	unsigned long f_pos;
};

static kdev_t c_dev = 0;
static unsigned long c_ino = 0;
static int c_size;
static int c_seen_eof;
static int c_last_returned_index;
static struct ncp_dirent *c_entry = NULL;
static DECLARE_MUTEX(c_sem);

static int ncp_read_volume_list(struct ncp_server *, int, int,
					struct ncp_dirent *);
static int ncp_do_readdir(struct ncp_server *, struct dentry *, int, int,
					struct ncp_dirent *);

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
#ifdef NCPFS_PARANOIA
printk(KERN_DEBUG "ncp_delete_dentry: closing file %s/%s\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
#endif
			ncp_make_closed(inode);
		}
	} else
	{
	/* N.B. Unhash negative dentries? */
	}
}

/*
 * Generate a unique inode number.
 */
ino_t ncp_invent_inos(unsigned long n)
{
	static ino_t ino = 2;

	if (ino + 2*n < ino)
	{
		/* wrap around */
		ino = 2;
	}
	ino += n;
	return ino;
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

static inline void ncp_lock_dircache(void)
{
	down(&c_sem);
}

static inline void ncp_unlock_dircache(void)
{
	up(&c_sem);
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
	int down_case = 0;
	int val = 0,res;
	int len = dentry->d_name.len;      
	struct ncpfs_inode_info finfo;
	__u8 __name[dentry->d_name.len + 1];
        
	server = NCP_SERVER(dir);

	if (!ncp_conn_valid(server))
		goto finished;

	strncpy(__name, dentry->d_name.name, len);
	__name[len] = '\0';
#ifdef NCPFS_PARANOIA
printk(KERN_DEBUG "ncp_lookup_validate: %s, len %d\n", __name, len);
#endif

	/* If the file is in the dir cache, we do not have to ask the
	   server. */

#ifdef NCPFS_PARANOIA
printk(KERN_DEBUG "ncp_lookup_validate: server lookup for %s/%s\n",
dentry->d_parent->d_name.name, __name);
#endif
		if (ncp_is_server_root(dir))
		{
			io2vol(server, __name, 1);
			down_case = 1;
			res = ncp_lookup_volume(server, __name,
						&(finfo.nw_info.i));
		} else
	    	{
			down_case = !ncp_preserve_case(dir);
			io2vol(server, __name, down_case);
			res = ncp_obtain_info(server, dir, __name,
						&(finfo.nw_info.i));
		}
#ifdef NCPFS_PARANOIA
printk(KERN_DEBUG "ncp_lookup_validate: looked for %s/%s, res=%d\n",
dentry->d_parent->d_name.name, __name, res);
#endif
		/*
		 * If we didn't find it, or if it has a different dirEntNum to
		 * what we remember, it's not valid any more.
		 */
	   	if (!res) {
		  if (finfo.nw_info.i.dirEntNum == NCP_FINFO(dentry->d_inode)->dirEntNum)
		    val=1;
#ifdef NCPFS_PARANOIA
		  else
		    printk(KERN_DEBUG "ncp_lookup_validate: found, but dirEntNum changed\n");
#endif
		  vol2io(server, finfo.nw_info.i.entryName,
			 !ncp_preserve_entry_case(dir,
			 finfo.nw_info.i.NSCreator));
 		  ncp_update_inode2(dentry->d_inode, &finfo.nw_info);
		}
		if (!val) ncp_invalid_dir_cache(dir);

finished:
#ifdef NCPFS_PARANOIA
printk(KERN_DEBUG "ncp_lookup_validate: result=%d\n", val);
#endif

	return val;
}


static int ncp_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct dentry *dentry = filp->f_dentry;
	struct inode *inode = dentry->d_inode;
	struct ncp_server *server = NCP_SERVER(inode);
	struct ncp_dirent *entry = NULL;
	int result, i, index = 0;

	DDPRINTK(KERN_DEBUG "ncp_readdir: reading %s/%s, pos=%d\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		(int) filp->f_pos);
	DDPRINTK(KERN_DEBUG "ncp_readdir: inode->i_ino = %ld, c_ino = %ld\n",
		 inode->i_ino, c_ino);

	result = -EIO;
	if (!ncp_conn_valid(server))
		goto out;

	ncp_lock_dircache();
	result = -ENOMEM;
	if (c_entry == NULL) {
		i = sizeof(struct ncp_dirent) * NCP_READDIR_CACHE_SIZE;
		c_entry = (struct ncp_dirent *) vmalloc(i);
		if (c_entry == NULL) {
			printk(KERN_WARNING "ncp_readdir: no MEMORY for cache\n");
			goto finished;
		}
	}

	result = 0;
	if (filp->f_pos == 0) {
		ncp_invalid_dir_cache(inode);
		if (filldir(dirent, ".", 1, 0, inode->i_ino) < 0) {
			goto finished;
		}
		filp->f_pos = 1;
	}
	if (filp->f_pos == 1) {
		if (filldir(dirent, "..", 2, 1,
				dentry->d_parent->d_inode->i_ino) < 0) {
			goto finished;
		}
		filp->f_pos = 2;
	}

	if ((inode->i_dev == c_dev) && (inode->i_ino == c_ino)) {
		for (i = 0; i < c_size; i++) {
			if (filp->f_pos == c_entry[i].f_pos) {
				entry = &c_entry[i];
				c_last_returned_index = i;
				index = i;
				break;
			}
		}
		if ((entry == NULL) && c_seen_eof) {
			goto finished;
		}
	}
	if (entry == NULL) {
		int entries;
		DDPRINTK(KERN_DEBUG "ncp_readdir: Not found in cache.\n");

		if (ncp_is_server_root(inode)) {
			entries = ncp_read_volume_list(server, filp->f_pos,
					 NCP_READDIR_CACHE_SIZE, c_entry);
			DPRINTK(KERN_DEBUG "ncp_read_volume_list returned %d\n", entries);

		} else {
			entries = ncp_do_readdir(server, dentry, filp->f_pos,
					 NCP_READDIR_CACHE_SIZE, c_entry);
			DPRINTK(KERN_DEBUG "ncp_readdir: returned %d\n", entries);
		}

		if (entries < 0) {
			c_dev = 0;
			c_ino = 0;
			result = entries;
			goto finished;
		}
		if (entries > 0) {
			c_seen_eof = (entries < NCP_READDIR_CACHE_SIZE);
			c_dev = inode->i_dev;
			c_ino = inode->i_ino;
			c_size = entries;
			entry = c_entry;
			c_last_returned_index = 0;
			index = 0;

			for (i = 0; i < c_size; i++)
			{
				vol2io(server, c_entry[i].i.entryName,
					!ncp_preserve_entry_case(inode,
					c_entry[i].i.NSCreator));
			}
		}
	}
	if (entry == NULL) {
		/* Nothing found, even from a ncp call */
		goto finished;
	}

	while (index < c_size) {
		ino_t ino;
		struct qstr qname;

		DDPRINTK(KERN_DEBUG "ncp_readdir: entry->path= %s\n", entry->i.entryName);
		DDPRINTK(KERN_DEBUG "ncp_readdir: entry->f_pos = %ld\n", entry->f_pos);

		/* For getwd() we have to return the correct
		 * inode in d_ino if the inode is currently in
		 * use. Otherwise the inode number does not
		 * matter. (You can argue a lot about this..)
		 */
		qname.name = entry->i.entryName;
		qname.len  = entry->i.nameLen;
		ino = find_inode_number(dentry, &qname);
		if (!ino)
			ino = ncp_invent_inos(1);

		if (filldir(dirent, entry->i.entryName, entry->i.nameLen,
			    entry->f_pos, ino) < 0) {
			break;
		}
		if ((inode->i_dev != c_dev)
		    || (inode->i_ino != c_ino)
		    || (entry->f_pos != filp->f_pos)) {
			/* Someone has destroyed the cache while we slept
			   in filldir */
			break;
		}
		filp->f_pos += 1;
		index += 1;
		entry += 1;
	}
      finished:
	ncp_unlock_dircache();
out:
	return result;
}

static int 
ncp_read_volume_list(struct ncp_server *server, int fpos,
			 int cache_size, struct ncp_dirent *entry)
{
	int i, total_count = 2;
	struct ncp_volume_info info;

	DPRINTK(KERN_DEBUG "ncp_read_volume_list: pos=%d\n", fpos);
#if 1
	if (fpos < 2) {
		printk(KERN_ERR "OOPS, we expect fpos >= 2");
		fpos = 2;
	}
#endif

	for (i = 0; i < NCP_NUMBER_OF_VOLUMES; i++) {

		if (ncp_get_volume_info_with_number(server, i, &info) != 0)
			goto out;
		if (!strlen(info.volume_name))
			continue;

		if (total_count < fpos) {
			DPRINTK(KERN_DEBUG "ncp_read_volume_list: skipped vol: %s\n",
				info.volume_name);
		} else if (total_count >= fpos + cache_size) {
			goto out;
		} else {
			DPRINTK(KERN_DEBUG "ncp_read_volume_list: found vol: %s\n",
				info.volume_name);

			if (ncp_lookup_volume(server, info.volume_name,
					      &(entry->i)) != 0) {
				DPRINTK(KERN_DEBUG "ncpfs: could not lookup vol %s\n",
					info.volume_name);
				continue;
			}
			entry->f_pos = total_count;
			entry += 1;
		}
		total_count += 1;
	}
out:
	return (total_count - fpos);
}

static int ncp_do_readdir(struct ncp_server *server, struct dentry *dentry,
			int fpos, int cache_size, struct ncp_dirent *entry)
{
	struct inode *dir = dentry->d_inode;
	static struct inode *last_dir;
	static int total_count;
	static struct nw_search_sequence seq;
	int err;

#if 1
	if (fpos < 2) {
		printk(KERN_ERR "OOPS, we expect fpos >= 2");
		fpos = 2;
	}
#endif
	DPRINTK(KERN_DEBUG "ncp_do_readdir: %s/%s, fpos=%d\n",
		dentry->d_parent->d_name.name, dentry->d_name.name, fpos);

	if (fpos == 2) {
		last_dir = NULL;
		total_count = 2;
	}
	if ((fpos != total_count) || (dir != last_dir))
	{
		total_count = 2;
		last_dir = dir;
		
#ifdef NCPFS_PARANOIA
printk(KERN_DEBUG "ncp_do_readdir: init %s, volnum=%d, dirent=%u\n",
dentry->d_name.name, NCP_FINFO(dir)->volNumber, NCP_FINFO(dir)->dirEntNum);
#endif
		err = ncp_initialize_search(server, dir, &seq); 
		if (err)
		{
			DPRINTK(KERN_DEBUG "ncp_do_readdir: init failed, err=%d\n", err);
			goto out;
		}
	}

	while (total_count < fpos + cache_size) {
		err = ncp_search_for_file_or_subdir(server, &seq, &(entry->i));
		if (err) {
			DPRINTK(KERN_DEBUG "ncp_do_readdir: search failed, err=%d\n", err);
			goto out;
		}
		if (total_count < fpos) {
			DPRINTK(KERN_DEBUG "ncp_do_readdir: skipped file: %s/%s\n",
				dentry->d_name.name, entry->i.entryName);
		} else {
			DDPRINTK(KERN_DEBUG "ncp_do_r: file: %s, f_pos=%d,total_count=%d",
				entry->i.entryName, fpos, total_count);
			entry->s = seq;
			entry->f_pos = total_count;
			entry += 1;
		}
		total_count += 1;
	}
out:
	return (total_count - fpos);
}

void ncp_init_dir_cache(void)
{
	c_dev = 0;
	c_ino = 0;
	c_entry = NULL;
}

void ncp_invalid_dir_cache(struct inode *ino)
{
	if ((ino->i_dev == c_dev) && (ino->i_ino == c_ino)) {
		c_dev = 0;
		c_ino = 0;
		c_seen_eof = 0;
	}
}

void ncp_free_dir_cache(void)
{
	DPRINTK(KERN_DEBUG "ncp_free_dir_cache: enter\n");

	if (c_entry == NULL) {
		return;
	}
	vfree(c_entry);
	c_entry = NULL;

	DPRINTK(KERN_DEBUG "ncp_free_dir_cache: exit\n");
}

int ncp_conn_logged_in(struct ncp_server *server)
{
	int result;

	if (ncp_single_volume(server)) {
		struct dentry* dent;

		result = -ENOENT;
		io2vol(server, server->m.mounted_vol, 1);
		if (ncp_lookup_volume(server, server->m.mounted_vol,
				      &(server->root.finfo.i)) != 0) {
#ifdef NCPFS_PARANOIA
printk(KERN_DEBUG "ncp_conn_logged_in: %s not found\n", server->m.mounted_vol);
#endif
			goto out;
		}
		vol2io(server, server->root.finfo.i.entryName, 1);
		dent = server->root_dentry;
		if (dent) {
			struct inode* ino = dent->d_inode;
			if (ino) {
				NCP_FINFO(ino)->volNumber = server->root.finfo.i.volNumber;
				NCP_FINFO(ino)->dirEntNum = server->root.finfo.i.dirEntNum;
				NCP_FINFO(ino)->DosDirNum = server->root.finfo.i.DosDirNum;
			} else {
				DPRINTK(KERN_DEBUG "ncpfs: sb->root_dentry->d_inode == NULL!\n");
			}
		} else {
			DPRINTK(KERN_DEBUG "ncpfs: sb->root_dentry == NULL!\n");
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
	int found_in_cache, down_case = 0;
	int error;
	int len = dentry->d_name.len;      
	struct ncpfs_inode_info finfo;
	__u8 __name[dentry->d_name.len + 1];

	server = NCP_SERVER(dir);

	error = -EIO;
	if (!ncp_conn_valid(server))
		goto finished;

	strncpy(__name, dentry->d_name.name, len);
	__name[len] = '\0';
#ifdef NCPFS_PARANOIA
printk(KERN_DEBUG "ncp_lookup: %s, len %d\n", __name, len);
#endif

	/* If the file is in the dir cache, we do not have to ask the
	   server. */

	found_in_cache = 0;
	ncp_lock_dircache();

	if ((dir->i_dev == c_dev) && (dir->i_ino == c_ino))
	{
		int first = c_last_returned_index;
		int i;
	    
		i = first;
		do {
#ifdef NCPFS_PARANOIA
printk(KERN_DEBUG "ncp_lookup: trying index: %d, name: %s\n", i, c_entry[i].i.entryName);
#endif
			if (strcmp(c_entry[i].i.entryName, __name) == 0) {
#ifdef NCPFS_PARANOIA
printk(KERN_DEBUG "ncp_lookup: found in cache!\n");
#endif
				finfo.nw_info.i = c_entry[i].i;
				found_in_cache = 1;
				break;
			}
			i = (i + 1) % c_size;
		} while (i != first);
	}
	ncp_unlock_dircache();

	if (found_in_cache == 0)
	{
		int res;
	    
#ifdef NCPFS_PARANOIA
printk(KERN_DEBUG "ncp_lookup: server lookup for %s/%s\n",
dentry->d_parent->d_name.name, __name);
#endif
		if (ncp_is_server_root(dir))
		{
			io2vol(server, __name, 1);
			down_case = 1;
			res = ncp_lookup_volume(server, __name,
						&(finfo.nw_info.i));
		} else
	    	{
			down_case = !ncp_preserve_case(dir);
			io2vol(server, __name, down_case);
			res = ncp_obtain_info(server, dir, __name,
						&(finfo.nw_info.i));
		}
#ifdef NCPFS_PARANOIA
printk(KERN_DEBUG "ncp_lookup: looked for %s/%s, res=%d\n",
dentry->d_parent->d_name.name, __name, res);
#endif
		/*
		 * If we didn't find an entry, make a negative dentry.
		 */
	   	if (res != 0) {
			goto add_entry;
		} else vol2io(server, finfo.nw_info.i.entryName,
			      !ncp_preserve_entry_case(dir,
			      finfo.nw_info.i.NSCreator));
	}

	/*
	 * Create an inode for the entry.
	 */
	finfo.nw_info.opened = 0;
	finfo.ino = ncp_invent_inos(1);
	error = -EACCES;
	inode = ncp_iget(dir->i_sb, &finfo);
	if (inode)
	{
	add_entry:
		dentry->d_op = &ncp_dentry_operations;
		d_add(dentry, inode);
		error = 0;
	}

finished:
#ifdef NCPFS_PARANOIA
printk(KERN_DEBUG "ncp_lookup: result=%d\n", error);
#endif
	return ERR_PTR(error);
}

/*
 * This code is common to create, mkdir, and mknod.
 */
static int ncp_instantiate(struct inode *dir, struct dentry *dentry,
			struct ncpfs_inode_info *finfo)
{
	struct inode *inode;
	int error = -EINVAL;

	ncp_invalid_dir_cache(dir);

	finfo->ino = ncp_invent_inos(1);
	inode = ncp_iget(dir->i_sb, finfo);
	if (!inode)
		goto out_close;
	d_instantiate(dentry,inode);
	error = 0;
out:
	return error;

out_close:
#ifdef NCPFS_PARANOIA
printk(KERN_DEBUG "ncp_instantiate: %s/%s failed, closing file\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
#endif
	ncp_close_file(NCP_SERVER(dir), finfo->nw_info.file_handle);
	goto out;
}

int ncp_create_new(struct inode *dir, struct dentry *dentry, int mode,
		int attributes)
{
	int error, result;
	struct ncpfs_inode_info finfo;
	__u8 _name[dentry->d_name.len + 1];
	
#ifdef NCPFS_PARANOIA
printk(KERN_DEBUG "ncp_create_new: creating %s/%s, mode=%x\n",
dentry->d_parent->d_name.name, dentry->d_name.name, mode);
#endif
	error = -EIO;
	if (!ncp_conn_valid(NCP_SERVER(dir)))
		goto out;

	strncpy(_name, dentry->d_name.name, dentry->d_name.len);
	_name[dentry->d_name.len] = '\0';

	io2vol(NCP_SERVER(dir), _name, !ncp_preserve_case(dir));

	error = -EACCES;
	result = ncp_open_create_file_or_subdir(NCP_SERVER(dir), dir, _name,
			   OC_MODE_CREATE | OC_MODE_OPEN | OC_MODE_REPLACE,
			   attributes, AR_READ | AR_WRITE, &finfo.nw_info);
	if (!result) {
		finfo.nw_info.access = O_RDWR;
		error = ncp_instantiate(dir, dentry, &finfo);
	} else {
		if (result == 0x87) error = -ENAMETOOLONG;
		DPRINTK(KERN_DEBUG "ncp_create: %s/%s failed\n",
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
	struct ncpfs_inode_info finfo;
	__u8 _name[dentry->d_name.len + 1];

	DPRINTK(KERN_DEBUG "ncp_mkdir: making %s/%s\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);
	error = -EIO;
	if (!ncp_conn_valid(NCP_SERVER(dir)))
		goto out;

	strncpy(_name, dentry->d_name.name, dentry->d_name.len);
	_name[dentry->d_name.len] = '\0';
	io2vol(NCP_SERVER(dir), _name, !ncp_preserve_case(dir));

	error = -EACCES;
	if (ncp_open_create_file_or_subdir(NCP_SERVER(dir), dir, _name,
					   OC_MODE_CREATE, aDIR, 0xffff,
					   &finfo.nw_info) == 0)
	{
		error = ncp_instantiate(dir, dentry, &finfo);
	}
out:
	return error;
}

static int ncp_rmdir(struct inode *dir, struct dentry *dentry)
{
	int error, result;
	__u8 _name[dentry->d_name.len + 1];

	DPRINTK(KERN_DEBUG "ncp_rmdir: removing %s/%s\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);

	error = -EIO;
	if (!ncp_conn_valid(NCP_SERVER(dir)))
		goto out;

	error = -EBUSY;
	if (!list_empty(&dentry->d_hash))
		goto out;

	strncpy(_name, dentry->d_name.name, dentry->d_name.len);
	_name[dentry->d_name.len] = '\0';
	    
	io2vol(NCP_SERVER(dir), _name, !ncp_preserve_case(dir));
	result = ncp_del_file_or_subdir(NCP_SERVER(dir), dir, _name);
	switch (result) {
		case 0x00:
   			ncp_invalid_dir_cache(dir);
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
	int error;

	DPRINTK(KERN_DEBUG "ncp_unlink: unlinking %s/%s\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);
	
	error = -EIO;
	if (!ncp_conn_valid(NCP_SERVER(dir)))
		goto out;

	/*
	 * Check whether to close the file ...
	 */
	if (inode && NCP_FINFO(inode)->opened) {
#ifdef NCPFS_PARANOIA
printk(KERN_DEBUG "ncp_unlink: closing file\n");
#endif
		ncp_make_closed(inode);
	}

	error = ncp_del_file_or_subdir2(NCP_SERVER(dir), dentry);
#ifdef CONFIG_NCPFS_STRONG
	/* 9C is Invalid path.. It should be 8F, 90 - read only, but
	   it is not :-( */
	if (error == 0x9C && NCP_SERVER(dir)->m.flags & NCP_MOUNT_STRONG) { /* R/O */
		error = ncp_force_unlink(dir, dentry);
	}
#endif
	switch (error) {
		case 0x00:
			DPRINTK(KERN_DEBUG "ncp: removed %s/%s\n",
				dentry->d_parent->d_name.name, dentry->d_name.name);
			ncp_invalid_dir_cache(dir);
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

	DPRINTK(KERN_DEBUG "ncp_rename: %s/%s to %s/%s\n",
		old_dentry->d_parent->d_name.name, old_dentry->d_name.name,
		new_dentry->d_parent->d_name.name, new_dentry->d_name.name);

	error = -EIO;
	if (!ncp_conn_valid(NCP_SERVER(old_dir)))
		goto out;

	strncpy(_old_name, old_dentry->d_name.name, old_len);
	_old_name[old_len] = '\0';
	io2vol(NCP_SERVER(old_dir), _old_name, !ncp_preserve_case(old_dir));

	strncpy(_new_name, new_dentry->d_name.name, new_len);
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
               	        DPRINTK(KERN_DEBUG "ncp renamed %s -> %s.\n",
                                old_dentry->d_name.name,new_dentry->d_name.name);
       	                ncp_invalid_dir_cache(old_dir);
               	        ncp_invalid_dir_cache(new_dir);
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
