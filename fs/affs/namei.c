/*
 *  linux/fs/affs/namei.c
 *
 *  (C) 1993  Ray Burr - Modified for Amiga FFS filesystem.
 *
 *  (C) 1992  Eric Youngdale Modified for ISO9660 filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 */

#include <linux/sched.h>
#include <linux/affs_fs.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <asm/segment.h>

#include <linux/errno.h>


static inline int namecompare(int len, int maxlen,
	const char * name, const char * buffer)
{
	if (len >= maxlen || !buffer[len]) {
		return strncmp (name, buffer, len) == 0;
	}
	return 0;
}

/*
 * ok, we cannot use strncmp, as the name is not in our data space.
 * Thus we'll have to use affs_match. No big problem. Match also makes
 * some sanity tests.
 *
 * NOTE! unlike strncmp, affs_match returns 1 for success, 0 for failure.
 */
static int affs_match(int len,const char * name, char * compare, int dlen)
{
	if (!compare) return 0;

	/* "" means "." ---> so paths like "/usr/lib//libc.a" work */
	if (!len && dlen == 1 && compare[0] == '.')
		return 1;
	
#if 0
	if (len <= 2) printk("Match: %d %d %s %d %d \n",len,dlen,compare,de->name[0], dlen);
#endif
	
	return namecompare(len,dlen,name,compare);
}

/* Avoid pulling in ctype stuff. */

static int affs_toupper (int ch)
{
	if (ch >= 'a' && ch <= 'z')
		ch -= ('a' - 'A');
	return ch;
}

static int affs_hash_name (const char *name, int len)
{
	int i, x;

	x = len;
	for (i = 0; i < len; i++)
		x = (x * 13 + affs_toupper (name[i])) & 0x7ff;
	return x % 72;  /* FIXME: Assumes 512 byte blocks. */
}

static struct buffer_head *affs_find_entry(struct inode *dir,
	const char *name, int namelen, int *ino)
{
	struct buffer_head *bh;
	void *dir_data;
	int key;

	*ino = 0;

	bh = affs_pread (dir, dir->i_ino, &dir_data);
	if (!bh)
		return NULL;

	if (affs_match (namelen, name, ".", 1)) {
		*ino = dir->i_ino;
		return bh;
	}
	if (affs_match (namelen, name, "..", 2)) {
		*ino = affs_parent_ino (dir);
		return bh;
	}
	key = affs_get_key_entry (AFFS_I2BSIZE (dir), dir_data,
				  affs_hash_name (name, namelen));

	for (;;) {
		char *cname;
		int cnamelen;

		brelse (bh);
		if (key <= 0)
			return NULL;
		bh = affs_pread (dir, key, &dir_data);
		if (!bh)
			return NULL;
		cnamelen = affs_get_file_name (AFFS_I2BSIZE (dir),
					       dir_data, &cname);
		if (affs_match (namelen, name, cname, cnamelen))
			break;
		key = affs_get_fh_hash_link (AFFS_I2BSIZE (dir), dir_data);
	}
		
	*ino = key;

	return bh;
}

int affs_lookup(struct inode * dir,const char * name, int len,
	struct inode ** result)
{
	int ino;
	struct buffer_head *bh;

	*result = NULL;
	if (!dir)
		return -ENOENT;

#ifdef DEBUG
	printk ("lookup: %d %d\n", dir->i_ino, len);
#endif

	if (!S_ISDIR(dir->i_mode)) {
		iput(dir);
		return -ENOENT;
	}
	if (!(bh = affs_find_entry(dir, name, len, &ino))) {
	  iput(dir);
	  return -ENOENT;
	}
	brelse(bh);
	if (!(*result = iget(dir->i_sb, ino))) {
		iput(dir);
		return -EACCES;
	}
	iput (dir);
	return 0;

#if 0
	ino = 0;
	while(cache.lock);
	cache.lock = 1;
	if (dir->i_dev == cache.dev && 
	    dir->i_ino == cache.dir &&
	    len == cache.dlen && 
	    affs_match(len, name, cache.filename, cache.dlen))
	  {
	    ino = cache.ino;
	    ino_back = dir->i_ino;
	    /* These two cases are special, but since they are at the start
	       of the directory, we can just as easily search there */
	    if (cache.dlen == 1 && cache.filename[0] == '.') ino = 0;
	    if (cache.dlen == 2 && cache.filename[0] == '.' && 
		cache.filename[1] == '.') ino = 0;
	  };
	cache.lock = 0;

	if (!ino) {
	  if (!(bh = affs_find_entry(dir,name,len, &ino, &ino_back))) {
	    iput(dir);
	    return -ENOENT;
	  }
	  brelse(bh);
	};

	if (!(*result = iget(dir->i_sb,ino))) {
		iput(dir);
		return -EACCES;
	}

	/* We need this backlink for the .. entry */
	
	if (ino_back) (*result)->u.affs_i.i_backlink = ino_back; 
	
	iput(dir);
	return 0;
#endif
}
