/*
 *  linux/fs/affs/amigaffs.c
 *
 *  (c) 1996  Hans-Joachim Widmaier - Rewritten
 *
 *  (C) 1993  Ray Burr - Amiga FFS filesystem.
 *
 */

#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/affs_fs.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/mm.h>
#include <linux/amigaffs.h>

extern struct timezone sys_tz;

/*
 * Functions for accessing Amiga-FFS structures.
 *
 */

/* Find the next used hash entry at or after *HASH_POS in a directory's hash
   table.  *HASH_POS is assigned that entry's number.  DIR_DATA points to
   the directory header block in memory.  If there are no more entries,
   0 is returned.  Otherwise, the key number in the next used hash slot
   is returned. */

int
affs_find_next_hash_entry(int hsize, void *dir_data, int *hash_pos)
{
	struct dir_front *dir_front = dir_data;
	int i;

	for (i = *hash_pos; i < hsize; i++)
		if (dir_front->hashtable[i] != 0)
			break;
	if (i >= hsize)
		return 0;
	*hash_pos = i;
	return htonl(dir_front->hashtable[i]);
}

/* Set *NAME to point to the file name in a file header block in memory
   pointed to by FH_DATA.  The length of the name is returned. */

int
affs_get_file_name(int bsize, void *fh_data, char **name)
{
	struct file_end *file_end;

	file_end = GET_END_PTR(struct file_end, fh_data, bsize);
	if (file_end->file_name[0] == 0
	    || file_end->file_name[0] > 30) {
		printk ("affs_get_file_name: OOPS! bad filename\n");
		printk ("  file_end->file_name[0] = %d\n",
			file_end->file_name[0]);
		*name = "***BAD_FILE***";
		return 14;
        }
	*name = (char *) &file_end->file_name[1];
        return file_end->file_name[0];
}

/* Find the predecessor in the hash chain */

int
affs_fix_hash_pred(struct inode *startino, int startoffset, int key, int newkey)
{
	struct buffer_head	*bh = NULL;
	int			 nextkey;
	int			 ptype, stype;
	int			 retval;

	nextkey = startino->i_ino;
	retval  = -ENOENT;
	lock_super(startino->i_sb);
	while (1) {
		pr_debug("AFFS: fix_hash_pred(): next key=%d, offset=%d\n",nextkey,startoffset);
		if (nextkey == 0)
			break;
		if (!(bh = affs_bread(startino->i_dev,nextkey,AFFS_I2BSIZE(startino))))
			break;
		if (affs_checksum_block(AFFS_I2BSIZE(startino),bh->b_data,&ptype,&stype)
		    || ptype != T_SHORT || (stype != ST_FILE && stype != ST_USERDIR &&
					    stype != ST_LINKFILE && stype != ST_LINKDIR &&
					    stype != ST_ROOT && stype != ST_SOFTLINK)) {
			printk("AFFS: bad block found in link chain (ptype=%d, stype=%d)\n",
			       ptype,stype);
			affs_brelse(bh);
			break;
		}
		nextkey = htonl(((__u32 *)bh->b_data)[startoffset]);
		if (nextkey == key) {
			((__u32 *)bh->b_data)[startoffset] = newkey;
			affs_fix_checksum(AFFS_I2BSIZE(startino),bh->b_data,5);
			mark_buffer_dirty(bh,1);
			affs_brelse(bh);
			retval = 0;
			break;
		}
		affs_brelse(bh);
		startoffset = AFFS_I2BSIZE(startino) / 4 - 4;
	}
	unlock_super(startino->i_sb);

	return retval;
}

/* Remove inode from link chain */

int
affs_fix_link_pred(struct inode *startino, int key, int newkey)
{
	struct buffer_head	*bh = NULL;
	int			 nextkey;
	int			 offset;
	int			 etype = 0;
	int			 ptype, stype;
	int			 retval;

	offset  = AFFS_I2BSIZE(startino) / 4 - 10;
	nextkey = startino->i_ino;
	retval  = -ENOENT;
	lock_super(startino->i_sb);
	while (1) {
		if (nextkey == 0)
			break;
		pr_debug("AFFS: find_link_pred(): next key=%d\n",nextkey);
		if (!(bh = affs_bread(startino->i_dev,nextkey,AFFS_I2BSIZE(startino))))
			break;
		if (affs_checksum_block(AFFS_I2BSIZE(startino),bh->b_data,&ptype,&stype)
		    || ptype != T_SHORT) {
			affs_brelse(bh);
			break;
		}
		if (!etype) {
			if (stype != ST_FILE && stype != ST_USERDIR) {
				affs_brelse(bh);
				break;
			}
			if (stype == ST_FILE)
				etype = ST_LINKFILE;
			else
				etype = ST_LINKDIR;
		} else if (stype != etype) {
			affs_brelse(bh);
			retval = -EPERM;
			break;
		}
		nextkey = htonl(((__u32 *)bh->b_data)[offset]);
		if (nextkey == key) {
			FILE_END(bh->b_data,startino)->link_chain = newkey;
			affs_fix_checksum(AFFS_I2BSIZE(startino),bh->b_data,5);
			mark_buffer_dirty(bh,1);
			affs_brelse(bh);
			retval = 0;
			break;
		}
		affs_brelse(bh);
	}
	unlock_super(startino->i_sb);
	return retval;
}

/* Checksum a block, do various consistency checks and optionally return
   the blocks type number.  DATA points to the block.  If their pointers
   are non-null, *PTYPE and *STYPE are set to the primary and secondary
   block types respectively, *HASHSIZE is set to the size of the hashtable
   (which lets us calculate the block size).
   Returns non-zero if the block is not consistent. */

__u32
affs_checksum_block(int bsize, void *data, int *ptype, int *stype)
{
	__u32	 sum;
	__u32	*p;

	bsize /= 4;
	if (ptype)
		*ptype = htonl(((__s32 *)data)[0]);
	if (stype)
		*stype = htonl(((__s32 *)data)[bsize - 1]);

	sum    = 0;
	p      = data;
	while (bsize--)
		sum += htonl(*p++);
	return sum;
}

void
affs_fix_checksum(int bsize, void *data, int cspos)
{
	__u32	 ocs;
	__u32	 cs;

	cs   = affs_checksum_block(bsize,data,NULL,NULL);
	ocs  = htonl (((__u32 *)data)[cspos]);
	ocs -= cs;
	((__u32 *)data)[cspos] = htonl(ocs);
}

void
secs_to_datestamp(int secs, struct DateStamp *ds)
{
	__u32	 days;
	__u32	 minute;

	secs -= sys_tz.tz_minuteswest * 60 +((8 * 365 + 2) * 24 * 60 * 60);
	if (secs < 0)
		secs = 0;
	days    = secs / 86400;
	secs   -= days * 86400;
	minute  = secs / 60;
	secs   -= minute * 60;

	ds->ds_Days   = htonl(days);
	ds->ds_Minute = htonl(minute);
	ds->ds_Tick   = htonl(secs * 50);
}

int
prot_to_mode(__u32 prot)
{
	int	 mode = 0;

	if (AFFS_UMAYWRITE(prot))
		mode |= S_IWUSR;
	if (AFFS_UMAYREAD(prot))
		mode |= S_IRUSR;
	if (AFFS_UMAYEXECUTE(prot))
		mode |= S_IXUSR;
	if (AFFS_GMAYWRITE(prot))
		mode |= S_IWGRP;
	if (AFFS_GMAYREAD(prot))
		mode |= S_IRGRP;
	if (AFFS_GMAYEXECUTE(prot))
		mode |= S_IXGRP;
	if (AFFS_OMAYWRITE(prot))
		mode |= S_IWOTH;
	if (AFFS_OMAYREAD(prot))
		mode |= S_IROTH;
	if (AFFS_OMAYEXECUTE(prot))
		mode |= S_IXOTH;
	
	return mode;
}

unsigned int
mode_to_prot(int mode)
{
	unsigned int	 prot = 0;

	if (mode & S_IXUSR)
		prot |= FIBF_SCRIPT;
	if (mode & S_IRUSR)
		prot |= FIBF_READ;
	if (mode & S_IWUSR)
		prot |= FIBF_WRITE | FIBF_DELETE;
	if (mode & S_IRGRP)
		prot |= FIBF_GRP_READ;
	if (mode & S_IWGRP)
		prot |= FIBF_GRP_WRITE;
	if (mode & S_IROTH)
		prot |= FIBF_OTR_READ;
	if (mode & S_IWOTH)
		prot |= FIBF_OTR_WRITE;
	
	return prot;
}
