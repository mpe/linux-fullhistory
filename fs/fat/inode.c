/*
 *  linux/fs/fat/inode.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *  VFAT extensions by Gordon Chaffee, merged with msdos fs by Henrik Storner
 */

#define __NO_VERSION__
#include <linux/module.h>

#include <linux/msdos_fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/locks.h>

#include "msbuffer.h"
#include "tables.h"

#include <asm/segment.h>



void fat_put_inode(struct inode *inode)
{
	struct inode *depend, *linked;
	struct super_block *sb;

	depend = MSDOS_I(inode)->i_depend;
	linked = MSDOS_I(inode)->i_linked;
	sb = inode->i_sb;
	if (inode->i_nlink) {
		if (depend) {
			iput(depend);
		}
		if (linked) {
			iput(linked);
			MSDOS_I(inode)->i_linked = NULL;
		}
		if (MSDOS_I(inode)->i_busy) fat_cache_inval_inode(inode);
		return;
	}
	inode->i_size = 0;
	fat_truncate(inode);
	clear_inode(inode);
	if (depend) {
		if (MSDOS_I(depend)->i_old != inode) {
			printk("Invalid link (0x%p): expected 0x%p, got 0x%p\n",
			    depend, inode, MSDOS_I(depend)->i_old);
			fat_fs_panic(sb,"...");
			return;
		}
		MSDOS_I(depend)->i_old = NULL;
		iput(depend);
	}
	if (linked) {
		if (MSDOS_I(linked)->i_oldlink != inode) {
			printk("Invalid link (0x%p): expected 0x%p, got 0x%p\n",
			    linked, inode, MSDOS_I(linked)->i_oldlink);
			fat_fs_panic(sb,"...");
			return;
		}
		MSDOS_I(linked)->i_oldlink = NULL;
		iput(linked);
	}
}


void fat_put_super(struct super_block *sb)
{
	fat_cache_inval_dev(sb->s_dev);
	set_blocksize (sb->s_dev,BLOCK_SIZE);
	lock_super(sb);
	sb->s_dev = 0;
	unlock_super(sb);
	MOD_DEC_USE_COUNT;
	return;
}


static int parse_options(char *options,int *fat, int *blksize, int *debug,
			 struct fat_mount_options *opts)
{
	char *this_char,*value;

	opts->name_check = 'n';
	opts->conversion = 'b';
	opts->fs_uid = current->uid;
	opts->fs_gid = current->gid;
	opts->fs_umask = current->fs->umask;
	opts->quiet = opts->sys_immutable = opts->dotsOK = opts->showexec = opts->isvfat = 0;
	*debug = *fat = 0;

	if (!options) return 1;
	for (this_char = strtok(options,","); this_char; this_char = strtok(NULL,",")) {
		if ((value = strchr(this_char,'=')) != NULL)
			*value++ = 0;
		if (!strcmp(this_char,"check") && value) {
			if (value[0] && !value[1] && strchr("rns",*value))
				opts->name_check = *value;
			else if (!strcmp(value,"relaxed")) opts->name_check = 'r';
			else if (!strcmp(value,"normal")) opts->name_check = 'n';
			else if (!strcmp(value,"strict")) opts->name_check = 's';
			else return 0;
		}
		else if (!strcmp(this_char,"conv") && value) {
			if (value[0] && !value[1] && strchr("bta",*value))
				opts->conversion = *value;
			else if (!strcmp(value,"binary")) opts->conversion = 'b';
			else if (!strcmp(value,"text")) opts->conversion = 't';
			else if (!strcmp(value,"auto")) opts->conversion = 'a';
			else return 0;
		}
		else if (!strcmp(this_char,"dots")) {
			opts->dotsOK = 1;
		}
		else if (!strcmp(this_char,"nodots")) {
			opts->dotsOK = 0;
		}
		else if (!strcmp(this_char,"showexec")) {
			opts->showexec = 1;
		}
		else if (!strcmp(this_char,"dotsOK") && value) {
			if (!strcmp(value,"yes")) opts->dotsOK = 1;
			else if (!strcmp(value,"no")) opts->dotsOK = 0;
			else return 0;
		}
		else if (!strcmp(this_char,"uid")) {
			if (!value || !*value)
				return 0;
			opts->fs_uid = simple_strtoul(value,&value,0);
			if (*value)
				return 0;
		}
		else if (!strcmp(this_char,"gid")) {
			if (!value || !*value)
				return 0;
			opts->fs_gid = simple_strtoul(value,&value,0);
			if (*value)
				return 0;
		}
		else if (!strcmp(this_char,"umask")) {
			if (!value || !*value)
				return 0;
			opts->fs_umask = simple_strtoul(value,&value,8);
			if (*value)
				return 0;
		}
		else if (!strcmp(this_char,"debug")) {
			if (value) return 0;
			*debug = 1;
		}
		else if (!strcmp(this_char,"fat")) {
			if (!value || !*value)
				return 0;
			*fat = simple_strtoul(value,&value,0);
			if (*value || (*fat != 12 && *fat != 16))
				return 0;
		}
		else if (!strcmp(this_char,"quiet")) {
			if (value) return 0;
			opts->quiet = 1;
		}
		else if (!strcmp(this_char,"blocksize")) {
			*blksize = simple_strtoul(value,&value,0);
			if (*value)
				return 0;
			if (*blksize != 512 && *blksize != 1024){
				printk ("MSDOS FS: Invalid blocksize (512 or 1024)\n");
			}
		}
		else if (!strcmp(this_char,"sys_immutable")) {
			if (value)
				return 0;
			opts->sys_immutable = 1;
		}
	}
	return 1;
}


/* Read the super block of an MS-DOS FS. */

struct super_block *fat_read_super(struct super_block *sb,void *data, int silent)
{
	struct buffer_head *bh;
	struct msdos_boot_sector *b;
	int data_sectors,logical_sector_size,sector_mult,fat_clusters=0;
	int debug,error,fat;
	int blksize = 512;
	struct fat_mount_options opts;

	MOD_INC_USE_COUNT;
	if (hardsect_size[MAJOR(sb->s_dev)] != NULL){
		blksize = hardsect_size[MAJOR(sb->s_dev)][MINOR(sb->s_dev)];
		if (blksize != 512){
			printk ("MSDOS: Hardware sector size is %d\n",blksize);
		}
	}
	if (!parse_options((char *) data, &fat, &blksize, &debug, &opts)
		|| (blksize != 512 && blksize != 1024)) {
		sb->s_dev = 0;
		MOD_DEC_USE_COUNT;
		return NULL;
	}
	cache_init();
	lock_super(sb);
	/* The first read is always 1024 bytes */
	sb->s_blocksize = 1024;
	set_blocksize(sb->s_dev, 1024);
	bh = fat_bread(sb, 0);
	unlock_super(sb);
	if (bh == NULL || !fat_is_uptodate(sb,bh)) {
		fat_brelse (sb, bh);
		sb->s_dev = 0;
		printk("FAT bread failed\n");
		MOD_DEC_USE_COUNT;
		return NULL;
	}
	b = (struct msdos_boot_sector *) bh->b_data;
	set_blocksize(sb->s_dev, blksize);
/*
 * The DOS3 partition size limit is *not* 32M as many people think.  
 * Instead, it is 64K sectors (with the usual sector size being
 * 512 bytes, leading to a 32M limit).
 * 
 * DOS 3 partition managers got around this problem by faking a 
 * larger sector size, ie treating multiple physical sectors as 
 * a single logical sector.
 * 
 * We can accommodate this scheme by adjusting our cluster size,
 * fat_start, and data_start by an appropriate value.
 *
 * (by Drew Eckhardt)
 */

#define ROUND_TO_MULTIPLE(n,m) ((n) && (m) ? (n)+(m)-1-((n)-1)%(m) : 0)
    /* don't divide by zero */

	logical_sector_size = CF_LE_W(*(unsigned short *) &b->sector_size);
	sector_mult = logical_sector_size >> SECTOR_BITS;
	MSDOS_SB(sb)->cluster_size = b->cluster_size*sector_mult;
	MSDOS_SB(sb)->fats = b->fats;
	MSDOS_SB(sb)->fat_start = CF_LE_W(b->reserved)*sector_mult;
	MSDOS_SB(sb)->fat_length = CF_LE_W(b->fat_length)*sector_mult;
	MSDOS_SB(sb)->dir_start = (CF_LE_W(b->reserved)+b->fats*CF_LE_W(
	    b->fat_length))*sector_mult;
	MSDOS_SB(sb)->dir_entries = CF_LE_W(*((unsigned short *) &b->dir_entries
	    ));
	MSDOS_SB(sb)->data_start = MSDOS_SB(sb)->dir_start+ROUND_TO_MULTIPLE((
	    MSDOS_SB(sb)->dir_entries << MSDOS_DIR_BITS) >> SECTOR_BITS,
	    sector_mult);
	data_sectors = (CF_LE_W(*((unsigned short *) &b->sectors)) ?
	    CF_LE_W(*((unsigned short *) &b->sectors)) :
	    CF_LE_L(b->total_sect))*sector_mult-MSDOS_SB(sb)->data_start;
	error = !b->cluster_size || !sector_mult;
	if (!error) {
		MSDOS_SB(sb)->clusters = b->cluster_size ? data_sectors/
		    b->cluster_size/sector_mult : 0;
		MSDOS_SB(sb)->fat_bits = fat ? fat : MSDOS_SB(sb)->clusters >
		    MSDOS_FAT12 ? 16 : 12;
		fat_clusters = MSDOS_SB(sb)->fat_length*SECTOR_SIZE*8/
		    MSDOS_SB(sb)->fat_bits;
		error = !MSDOS_SB(sb)->fats || (MSDOS_SB(sb)->dir_entries &
		    (MSDOS_DPS-1)) || MSDOS_SB(sb)->clusters+2 > fat_clusters+
		    MSDOS_MAX_EXTRA || (logical_sector_size & (SECTOR_SIZE-1))
		    || !b->secs_track || !b->heads;
	}
	fat_brelse(sb, bh);
	/*
		This must be done after the brelse because the bh is a dummy
		allocated by fat_bread (see buffer.c)
	*/
	sb->s_blocksize = blksize;    /* Using this small block size solves */
				/* the misfit with buffer cache and cluster */
				/* because clusters (DOS) are often aligned */
				/* on odd sectors. */
	sb->s_blocksize_bits = blksize == 512 ? 9 : 10;
	if (error || debug) {
		/* The MSDOS_CAN_BMAP is obsolete, but left just to remember */
		printk("[MS-DOS FS Rel. 12,FAT %d,check=%c,conv=%c,"
		       "uid=%d,gid=%d,umask=%03o%s]\n",
		       MSDOS_SB(sb)->fat_bits,opts.name_check,
		       opts.conversion,opts.fs_uid,opts.fs_gid,opts.fs_umask,
		       MSDOS_CAN_BMAP(MSDOS_SB(sb)) ? ",bmap" : "");
		printk("[me=0x%x,cs=%d,#f=%d,fs=%d,fl=%d,ds=%d,de=%d,data=%d,"
		       "se=%d,ts=%ld,ls=%d]\n",b->media,MSDOS_SB(sb)->cluster_size,
		       MSDOS_SB(sb)->fats,MSDOS_SB(sb)->fat_start,MSDOS_SB(sb)->fat_length,
		       MSDOS_SB(sb)->dir_start,MSDOS_SB(sb)->dir_entries,
		       MSDOS_SB(sb)->data_start,
		       CF_LE_W(*(unsigned short *) &b->sectors),
		       (unsigned long)b->total_sect,logical_sector_size);
		printk ("Transaction block size = %d\n",blksize);
	}
	if (MSDOS_SB(sb)->clusters+2 > fat_clusters)
		MSDOS_SB(sb)->clusters = fat_clusters-2;
	if (error) {
		if (!silent)
			printk("VFS: Can't find a valid MSDOS filesystem on dev "
			       "%s.\n", kdevname(sb->s_dev));
		sb->s_dev = 0;
		MOD_DEC_USE_COUNT;
		return NULL;
	}
	sb->s_magic = MSDOS_SUPER_MAGIC;
	/* set up enough so that it can read an inode */
	MSDOS_SB(sb)->free_clusters = -1; /* don't know yet */
	MSDOS_SB(sb)->fat_wait = NULL;
	MSDOS_SB(sb)->fat_lock = 0;
	MSDOS_SB(sb)->prev_free = 0;
	memcpy(&(MSDOS_SB(sb)->options), &opts, sizeof(struct fat_mount_options));
	if (!(sb->s_mounted = iget(sb,MSDOS_ROOT_INO))) {
		sb->s_dev = 0;
		printk("get root inode failed\n");
		MOD_DEC_USE_COUNT;
		return NULL;
	}
	return sb;
}


void fat_statfs(struct super_block *sb,struct statfs *buf, int bufsiz)
{
	int free,nr;
	struct statfs tmp;

	lock_fat(sb);
	if (MSDOS_SB(sb)->free_clusters != -1)
		free = MSDOS_SB(sb)->free_clusters;
	else {
		free = 0;
		for (nr = 2; nr < MSDOS_SB(sb)->clusters+2; nr++)
			if (!fat_access(sb,nr,-1)) free++;
		MSDOS_SB(sb)->free_clusters = free;
	}
	unlock_fat(sb);
	tmp.f_type = sb->s_magic;
	tmp.f_bsize = MSDOS_SB(sb)->cluster_size*SECTOR_SIZE;
	tmp.f_blocks = MSDOS_SB(sb)->clusters;
	tmp.f_bfree = free;
	tmp.f_bavail = free;
	tmp.f_files = 0;
	tmp.f_ffree = 0;
	tmp.f_namelen = 12;
	memcpy_tofs(buf, &tmp, bufsiz);
}


int fat_bmap(struct inode *inode,int block)
{
	struct msdos_sb_info *sb;
	int cluster,offset;

	sb = MSDOS_SB(inode->i_sb);
	if (inode->i_ino == MSDOS_ROOT_INO) {
		return sb->dir_start + block;
	}
	cluster = block/sb->cluster_size;
	offset = block % sb->cluster_size;
	if (!(cluster = get_cluster(inode,cluster))) return 0;
	return (cluster-2)*sb->cluster_size+sb->data_start+offset;
}

static int is_exec(char *extension)
{
	char *exe_extensions = "EXECOMBAT", *walk;

	for (walk = exe_extensions; *walk; walk += 3)
		if (!strncmp(extension, walk, 3))
			return 1;
	return 0;
}

void fat_read_inode(struct inode *inode, struct inode_operations *fs_dir_inode_ops)
{
	struct super_block *sb = inode->i_sb;
	struct buffer_head *bh;
	struct msdos_dir_entry *raw_entry;
	int nr;

	MSDOS_I(inode)->i_busy = 0;
	MSDOS_I(inode)->i_depend = MSDOS_I(inode)->i_old = NULL;
	MSDOS_I(inode)->i_linked = MSDOS_I(inode)->i_oldlink = NULL;
	MSDOS_I(inode)->i_binary = 1;
	inode->i_uid = MSDOS_SB(sb)->options.fs_uid;
	inode->i_gid = MSDOS_SB(sb)->options.fs_gid;
	inode->i_version = ++event;
	if (inode->i_ino == MSDOS_ROOT_INO) {
		inode->i_mode = (S_IRWXUGO & ~MSDOS_SB(sb)->options.fs_umask) |
		    S_IFDIR;
		inode->i_op = fs_dir_inode_ops;
		inode->i_nlink = fat_subdirs(inode)+2;
		    /* subdirs (neither . nor ..) plus . and "self" */
		inode->i_size = MSDOS_SB(sb)->dir_entries*
		    sizeof(struct msdos_dir_entry);
		inode->i_blksize = MSDOS_SB(sb)->cluster_size*
		    SECTOR_SIZE;
		inode->i_blocks = (inode->i_size+inode->i_blksize-1)/
		    inode->i_blksize*MSDOS_SB(sb)->cluster_size;
		MSDOS_I(inode)->i_start = 0;
		MSDOS_I(inode)->i_attrs = 0;
		inode->i_mtime = inode->i_atime = inode->i_ctime = 0;
		return;
	}
	if (!(bh = fat_bread(sb, inode->i_ino >> MSDOS_DPB_BITS))) {
		printk("dev = %s, ino = %ld\n",
		       kdevname(inode->i_dev), inode->i_ino);
		panic("fat_read_inode: unable to read i-node block");
	}
	raw_entry = &((struct msdos_dir_entry *) (bh->b_data))
	    [inode->i_ino & (MSDOS_DPB-1)];
	if ((raw_entry->attr & ATTR_DIR) && !IS_FREE(raw_entry->name)) {
		inode->i_mode = MSDOS_MKMODE(raw_entry->attr,S_IRWXUGO &
		    ~MSDOS_SB(sb)->options.fs_umask) | S_IFDIR;
		inode->i_op = fs_dir_inode_ops;

		MSDOS_I(inode)->i_start = CF_LE_W(raw_entry->start);
		inode->i_nlink = fat_subdirs(inode);
		    /* includes .., compensating for "self" */
#ifdef DEBUG
		if (!inode->i_nlink) {
			printk("directory %d: i_nlink == 0\n",inode->i_ino);
			inode->i_nlink = 1;
		}
#endif
		inode->i_size = 0;
		if ((nr = CF_LE_W(raw_entry->start)) != 0)
			while (nr != -1) {
				inode->i_size += SECTOR_SIZE*MSDOS_SB(inode->
				    i_sb)->cluster_size;
				if (!(nr = fat_access(sb,nr,-1))) {
					printk("Directory %ld: bad FAT\n",
					    inode->i_ino);
					break;
				}
			}
	} else { /* not a directory */
		inode->i_mode = MSDOS_MKMODE(raw_entry->attr,
		    ((IS_NOEXEC(inode) || 
		      (MSDOS_SB(sb)->options.showexec &&
		       !is_exec(raw_entry->ext)))
		    	? S_IRUGO|S_IWUGO : S_IRWXUGO)
		    & ~MSDOS_SB(sb)->options.fs_umask) | S_IFREG;
		inode->i_op = (sb->s_blocksize == 1024)
			? &fat_file_inode_operations_1024
			: &fat_file_inode_operations;
		MSDOS_I(inode)->i_start = CF_LE_W(raw_entry->start);
		inode->i_nlink = 1;
		inode->i_size = CF_LE_L(raw_entry->size);
	}
	if(raw_entry->attr & ATTR_SYS)
		if (MSDOS_SB(sb)->options.sys_immutable)
			inode->i_flags |= S_IMMUTABLE;
	MSDOS_I(inode)->i_binary = is_binary(MSDOS_SB(sb)->options.conversion,
	    raw_entry->ext);
	MSDOS_I(inode)->i_attrs = raw_entry->attr & ATTR_UNUSED;
	/* this is as close to the truth as we can get ... */
	inode->i_blksize = MSDOS_SB(sb)->cluster_size*SECTOR_SIZE;
	inode->i_blocks = (inode->i_size+inode->i_blksize-1)/
	    inode->i_blksize*MSDOS_SB(sb)->cluster_size;
	inode->i_mtime = inode->i_atime =
	    date_dos2unix(CF_LE_W(raw_entry->time),CF_LE_W(raw_entry->date));
	inode->i_ctime =
		MSDOS_SB(sb)->options.isvfat
		? date_dos2unix(CF_LE_W(raw_entry->ctime),CF_LE_W(raw_entry->cdate))
		: inode->i_mtime;
	fat_brelse(sb, bh);
}


void fat_write_inode(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct buffer_head *bh;
	struct msdos_dir_entry *raw_entry;
	struct inode *linked;

	linked = MSDOS_I(inode)->i_linked;
	if (linked) {
		if (MSDOS_I(linked)->i_oldlink != inode) {
			printk("Invalid link (0x%p): expected 0x%p, got 0x%p\n",
			       linked, inode, MSDOS_I(linked)->i_oldlink);
			fat_fs_panic(sb,"...");
			return;
		}
		linked->i_version = ++event;
		linked->i_mode = inode->i_mode;
		linked->i_uid = inode->i_uid;
		linked->i_gid = inode->i_gid;
		linked->i_size = inode->i_size;
		linked->i_atime = inode->i_atime;
		linked->i_mtime = inode->i_mtime;
		linked->i_ctime = inode->i_ctime;
		linked->i_blocks = inode->i_blocks;
		linked->i_atime = inode->i_atime;
		MSDOS_I(linked)->i_attrs = MSDOS_I(inode)->i_attrs;
		linked->i_dirt = 1;
	}

	inode->i_dirt = 0;
	if (inode->i_ino == MSDOS_ROOT_INO || !inode->i_nlink) return;
	if (!(bh = fat_bread(sb, inode->i_ino >> MSDOS_DPB_BITS))) {
		printk("dev = %s, ino = %ld\n",
		       kdevname(inode->i_dev), inode->i_ino);
		panic("msdos_write_inode: unable to read i-node block");
	}
	raw_entry = &((struct msdos_dir_entry *) (bh->b_data))
	    [inode->i_ino & (MSDOS_DPB-1)];
	if (S_ISDIR(inode->i_mode)) {
		raw_entry->attr = ATTR_DIR;
		raw_entry->size = 0;
	}
	else {
		raw_entry->attr = ATTR_NONE;
		raw_entry->size = CT_LE_L(inode->i_size);
	}
	raw_entry->attr |= MSDOS_MKATTR(inode->i_mode) |
	    MSDOS_I(inode)->i_attrs;
	raw_entry->start = CT_LE_W(MSDOS_I(inode)->i_start);
	fat_date_unix2dos(inode->i_mtime,&raw_entry->time,&raw_entry->date);
	raw_entry->time = CT_LE_W(raw_entry->time);
	raw_entry->date = CT_LE_W(raw_entry->date);
	if (MSDOS_SB(sb)->options.isvfat) {
		fat_date_unix2dos(inode->i_ctime,&raw_entry->ctime,&raw_entry->cdate);
		raw_entry->ctime = CT_LE_W(raw_entry->ctime);
		raw_entry->cdate = CT_LE_W(raw_entry->cdate);
	}
	fat_mark_buffer_dirty(sb, bh, 1);
	fat_brelse(sb, bh);
}


int fat_notify_change(struct inode * inode,struct iattr * attr)
{
	struct super_block *sb = inode->i_sb;
	int error;

	error = inode_change_ok(inode, attr);
	if (error)
		return MSDOS_SB(sb)->options.quiet ? 0 : error;

	if (((attr->ia_valid & ATTR_UID) && 
	     (attr->ia_uid != MSDOS_SB(sb)->options.fs_uid)) ||
	    ((attr->ia_valid & ATTR_GID) && 
	     (attr->ia_gid != MSDOS_SB(sb)->options.fs_gid)) ||
	    ((attr->ia_valid & ATTR_MODE) &&
	     (attr->ia_mode & ~MSDOS_VALID_MODE)))
		error = -EPERM;

	if (error)
		return MSDOS_SB(sb)->options.quiet ? 0 : error;

	inode_setattr(inode, attr);

	if (IS_NOEXEC(inode) && !S_ISDIR(inode->i_mode))
		inode->i_mode &= S_IFMT | S_IRUGO | S_IWUGO;
	else
		inode->i_mode |= S_IXUGO;

	inode->i_mode = ((inode->i_mode & S_IFMT) | ((((inode->i_mode & S_IRWXU
	    & ~MSDOS_SB(sb)->options.fs_umask) | S_IRUSR) >> 6)*S_IXUGO)) &
	    ~MSDOS_SB(sb)->options.fs_umask;
	return 0;
}


#ifdef MODULE
int init_module(void)
{
	return init_fat_fs();
}


void cleanup_module(void)
{
	/* Nothing to be done, really! */
	return;
}
#endif

