/*
 *  linux/fs/msdos/inode.c
 *
 *  Written 1992 by Werner Almesberger
 */

#include <linux/msdos_fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>

#include <asm/segment.h>

void msdos_put_inode(struct inode *inode)
{
	struct inode *depend;

	inode->i_size = 0;
	msdos_truncate(inode);
	depend = (struct inode *) inode->i_data[D_DEPEND];
	memset(inode,0,sizeof(struct inode));
	if (depend) {
		if ((struct inode *) depend->i_data[D_OLD] != inode) {
			printk("Invalid link (0x%X): expected 0x%X, got "
			    "0x%X\r\n",(int) depend,(int) inode,
			    depend->i_data[D_OLD]);
			panic("That's fatal");
		}
		depend->i_data[D_OLD] = 0;
		iput(depend);
	}
}


void msdos_put_super(struct super_block *sb)
{
	cache_inval_dev(sb->s_dev);
	lock_super(sb);
	sb->s_dev = 0;
	free_super(sb);
	return;
}


static struct super_operations msdos_sops = { 
	msdos_read_inode,
	msdos_write_inode,
	msdos_put_inode,
	msdos_put_super,
	NULL, /* added in 0.96c */
	msdos_statfs
};


static int parse_options(char *options,char *check,char *conversion)
{
	char *this,*value;

	*check = 'n';
	*conversion = 'b';
	if (!options) return 1;
	for (this = strtok(options,","); this; this = strtok(NULL,",")) {
		if (value = strchr(this,'=')) *value++ = 0;
		if (!strcmp(this,"check") && value) {
			if (value[0] && !value[1] && strchr("rns",*value))
				*check = *value;
			else if (!strcmp(value,"relaxed")) *check = 'r';
			else if (!strcmp(value,"normal")) *check = 'n';
			else if (!strcmp(value,"strict")) *check = 's';
			else return 0;
		}
		else if (!strcmp(this,"conv") && value) {
			if (value[0] && !value[1] && strchr("bta",*value))
				*conversion = *value;
			else if (!strcmp(value,"binary")) *conversion = 'b';
			else if (!strcmp(value,"text")) *conversion = 't';
			else if (!strcmp(value,"auto")) *conversion = 'a';
			else return 0;
		}
		else return 0;
	}
	return 1;
}


/* Read the super block of an MS-DOS FS. */

struct super_block *msdos_read_super(struct super_block *s,void *data)
{
	struct buffer_head *bh;
	struct msdos_boot_sector *b;
	int data_sectors;
	char check,conversion;

	if (!parse_options((char *) data,&check,&conversion)) {
		s->s_dev = 0;
		return NULL;
	}
	cache_init();
	lock_super(s);
	bh = bread(s->s_dev, 0, BLOCK_SIZE);
	free_super(s);
	if (bh == NULL) {
		s->s_dev = 0;
		printk("MSDOS bread failed\r\n");
		return NULL;
	}
	b = (struct msdos_boot_sector *) bh->b_data;
	s->s_blocksize = 1024;	/* we cannot handle anything else yet */
	MSDOS_SB(s)->cluster_size = b->cluster_size;
	MSDOS_SB(s)->fats = b->fats;
	MSDOS_SB(s)->fat_start = b->reserved;
	MSDOS_SB(s)->fat_length = b->fat_length;
	MSDOS_SB(s)->dir_start = b->reserved+b->fats*b->fat_length;
	MSDOS_SB(s)->dir_entries = *((unsigned short *) &b->dir_entries);
	MSDOS_SB(s)->data_start = MSDOS_SB(s)->dir_start+((MSDOS_SB(s)->
	    dir_entries << 5) >> 9);
	data_sectors = (*((unsigned short *) &b->sectors) ? *((unsigned short *)
	    &b->sectors) : b->total_sect)-MSDOS_SB(s)->data_start;
	MSDOS_SB(s)->clusters = b->cluster_size ? data_sectors/b->cluster_size :
	    0;
	MSDOS_SB(s)->fat_bits = MSDOS_SB(s)->clusters > MSDOS_FAT12 ? 16 : 12;
	brelse(bh);
printk("[MS-DOS FS Rel. alpha.6, FAT %d, check=%c, conv=%c]\r\n",
  MSDOS_SB(s)->fat_bits,check,conversion);
printk("[me=0x%x,cs=%d,#f=%d,fs=%d,fl=%d,ds=%d,de=%d,data=%d,se=%d,ts=%d]\r\n",
  b->media,MSDOS_SB(s)->cluster_size,MSDOS_SB(s)->fats,MSDOS_SB(s)->fat_start,
  MSDOS_SB(s)->fat_length,MSDOS_SB(s)->dir_start,MSDOS_SB(s)->dir_entries,
  MSDOS_SB(s)->data_start,*(unsigned short *) &b->sectors,b->total_sect);
	if (!MSDOS_SB(s)->fats || (MSDOS_SB(s)->dir_entries & (MSDOS_DPS-1))
	    || !b->cluster_size || MSDOS_SB(s)->clusters+2 > MSDOS_SB(s)->
		fat_length*SECTOR_SIZE*8/MSDOS_SB(s)->fat_bits) {
		s->s_dev = 0;
		printk("Unsupported FS parameters\r\n");
		return NULL;
	}
	if (!MSDOS_CAN_BMAP(MSDOS_SB(s))) printk("No bmap support\r\n");
	s->s_magic = MSDOS_SUPER_MAGIC;
	MSDOS_SB(s)->name_check = check;
	MSDOS_SB(s)->conversion = conversion;
	/* set up enough so that it can read an inode */
	s->s_op = &msdos_sops;
	MSDOS_SB(s)->fs_uid = current->uid;
	MSDOS_SB(s)->fs_gid = current->gid;
	MSDOS_SB(s)->fs_umask = current->umask;
	if (!(s->s_mounted = iget(s->s_dev,MSDOS_ROOT_INO))) {
		s->s_dev = 0;
		printk("get root inode failed\n");
		return NULL;
	}
	return s;
}


void msdos_statfs(struct super_block *sb,struct statfs *buf)
{
	int cluster_size,free,this;

	cluster_size = MSDOS_SB(sb)->cluster_size;
	put_fs_long(sb->s_magic,&buf->f_type);
	put_fs_long(SECTOR_SIZE,&buf->f_bsize);
	put_fs_long(MSDOS_SB(sb)->clusters*cluster_size,&buf->f_blocks);
	free = 0;
	for (this = 2; this < MSDOS_SB(sb)->clusters+2; this++)
		if (!fat_access(sb,this,-1)) free++;
	free *= cluster_size;
	put_fs_long(free,&buf->f_bfree);
	put_fs_long(free,&buf->f_bavail);
	put_fs_long(0,&buf->f_files);
	put_fs_long(0,&buf->f_ffree);
}


int msdos_bmap(struct inode *inode,int block)
{
	struct msdos_sb_info *sb;
	int cluster,offset;

	sb = MSDOS_SB(inode->i_sb);
	if ((sb->cluster_size & 1) || (sb->data_start & 1)) return 0;
	if (inode->i_ino == MSDOS_ROOT_INO) {
		if (sb->dir_start & 1) return 0;
		return (sb->dir_start >> 1)+block;
	}
	cluster = (block*2)/sb->cluster_size;
	offset = (block*2) % sb->cluster_size;
	if (!(cluster = get_cluster(inode,cluster))) return 0;
	return ((cluster-2)*sb->cluster_size+sb->data_start+offset) >> 1;
}


void msdos_read_inode(struct inode *inode)
{
	struct buffer_head *bh;
	struct msdos_dir_entry *raw_entry;
	int this;

/* printk("read inode %d\r\n",inode->i_ino); */
	inode->i_data[D_BUSY] = inode->i_data[D_DEPEND] =
	    inode->i_data[D_OLD] = 0;
	inode->i_data[D_BINARY] = 1;
	inode->i_uid = MSDOS_SB(inode->i_sb)->fs_uid;
	inode->i_gid = MSDOS_SB(inode->i_sb)->fs_gid;
	if (inode->i_ino == MSDOS_ROOT_INO) {
		inode->i_mode = (0777 & ~MSDOS_SB(inode->i_sb)->fs_umask) |
		    S_IFDIR;
		inode->i_op = &msdos_dir_inode_operations;
		inode->i_nlink = 1;
		inode->i_size = MSDOS_SB(inode->i_sb)->dir_entries*
		    sizeof(struct msdos_dir_entry);
		inode->i_data[D_START] = 0;
		inode->i_data[D_ATTRS] = 0;
		inode->i_mtime = inode->i_atime = inode->i_ctime = 0;
		return;
	}
	if (!(bh = bread(inode->i_dev,inode->i_ino >> MSDOS_DPB_BITS, BLOCK_SIZE)))
	    panic("unable to read i-node block");
	raw_entry = &((struct msdos_dir_entry *) (bh->b_data))
	    [inode->i_ino & (MSDOS_DPB-1)];
	if (raw_entry->attr & ATTR_DIR) {
		inode->i_mode = MSDOS_MKMODE(raw_entry->attr,0777 &
		    ~MSDOS_SB(inode->i_sb)->fs_umask) | S_IFDIR;
		inode->i_op = &msdos_dir_inode_operations;
		inode->i_nlink = 3;
		inode->i_size = 0;
		for (this = raw_entry->start; this && this != -1; this =
		    fat_access(inode->i_sb,this,-1))
			inode->i_size += SECTOR_SIZE*MSDOS_SB(inode->i_sb)->
			    cluster_size;
	}
	else {
		inode->i_mode = MSDOS_MKMODE(raw_entry->attr,0666 &
		    ~MSDOS_SB(inode->i_sb)->fs_umask) | S_IFREG;
		inode->i_op = MSDOS_CAN_BMAP(MSDOS_SB(inode->i_sb)) ? 
		    &msdos_file_inode_operations :
		    &msdos_file_inode_operations_no_bmap;
		inode->i_nlink = 1;
		inode->i_size = raw_entry->size;
	}
	inode->i_data[D_BINARY] = is_binary(MSDOS_SB(inode->i_sb)->conversion,
	    raw_entry->ext);
	inode->i_data[D_START] = raw_entry->start;
	inode->i_data[D_ATTRS] = raw_entry->attr & ATTR_UNUSED;
	inode->i_mtime = inode->i_atime = inode->i_ctime =
	    date_dos2unix(raw_entry->time,raw_entry->date);
	brelse(bh);
}


void msdos_write_inode(struct inode *inode)
{
	struct buffer_head *bh;
	struct msdos_dir_entry *raw_entry;

	inode->i_dirt = 0;
	if (inode->i_ino == MSDOS_ROOT_INO || !inode->i_nlink) return;
	if (!(bh = bread(inode->i_dev,inode->i_ino >> MSDOS_DPB_BITS, BLOCK_SIZE)))
	    panic("unable to read i-node block");
	raw_entry = &((struct msdos_dir_entry *) (bh->b_data))
	    [inode->i_ino & (MSDOS_DPB-1)];
	if (S_ISDIR(inode->i_mode)) {
		raw_entry->attr = ATTR_DIR;
		raw_entry->size = 0;
	}
	else {
		raw_entry->attr = ATTR_NONE;
		raw_entry->size = inode->i_size;
	}
	raw_entry->attr |= MSDOS_MKATTR(inode->i_mode) | inode->i_data[D_ATTRS];
	raw_entry->start = inode->i_data[D_START];
	date_unix2dos(inode->i_mtime,&raw_entry->time,&raw_entry->date);
	bh->b_dirt = 1;
	brelse(bh);
}
