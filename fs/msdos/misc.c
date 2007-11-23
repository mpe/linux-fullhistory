/*
 *  linux/fs/msdos/misc.c
 *
 *  Written 1992 by Werner Almesberger
 */

#include <linux/msdos_fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>

static char bin_extensions[] =
  "EXECOMAPPSYSOVLOBJLIB"		/* program code */
  "ARCZIPLHALZHZOOTARZ  ARJTZ "		/* common archivers */
  "GIFBMPTIFGL JPGPCX"			/* graphics */
  "TFMVF GF PK PXLDVI";			/* TeX */


/* Select binary/text conversion */

int is_binary(char conversion,char *extension)
{
	char *walk;

	switch (conversion) {
		case 'b':
			return 1;
		case 't':
			return 0;
		case 'a':
			for (walk = bin_extensions; *walk; walk += 3)
				if (!strncmp(extension,walk,3)) return 1;
			return 0;
		default:
			panic("Invalid conversion mode");
	}
}


static struct wait_queue *creation_wait = NULL;
static creation_lock = 0;


void lock_creation(void)
{
	while (creation_lock) sleep_on(&creation_wait);
	creation_lock = 1;
}


void unlock_creation(void)
{
	creation_lock = 0;
	wake_up(&creation_wait);
}


int msdos_add_cluster(struct inode *inode)
{
	static struct wait_queue *wait = NULL;
	static int lock = 0;
	static int previous = 0; /* works best if one FS is being used */
	int count,this,limit,last,current,sector;
	void *data;
	struct buffer_head *bh;

	if (inode->i_ino == MSDOS_ROOT_INO) return -ENOSPC;
	while (lock) sleep_on(&wait);
	lock = 1;
	limit = MSDOS_SB(inode->i_sb)->clusters;
	this = limit; /* to keep GCC happy */
	for (count = 0; count < limit; count++) {
		this = ((count+previous) % limit)+2;
		if (fat_access(inode->i_sb,this,-1) == 0) break;
	}
#ifdef DEBUG
printk("free cluster: %d\r\n",this);
#endif
	previous = (count+previous+1) % limit;
	if (count >= limit) {
		lock = 0;
		wake_up(&wait);
		return -ENOSPC;
	}
	fat_access(inode->i_sb,this,MSDOS_SB(inode->i_sb)->fat_bits == 12 ?
	    0xff8 : 0xfff8);
	lock = 0;
	wake_up(&wait);
#ifdef DEBUG
printk("set to %x\r\n",fat_access(inode->i_sb,this,-1));
#endif
	if (!S_ISDIR(inode->i_mode)) {
		last = inode->i_size ? get_cluster(inode,(inode->i_size-1)/
		    SECTOR_SIZE/MSDOS_SB(inode->i_sb)->cluster_size) : 0;
	}
	else {
		last = 0;
		if (current = inode->i_data[D_START]) {
			cache_lookup(inode,0x7fffffff,&last,&current);
			while (current && current != -1)
				if (!(current = fat_access(inode->i_sb,
				    last = current,-1)))
					panic("File without EOF");
			}
	}
#ifdef DEBUG
printk("last = %d\r\n",last);
#endif
	if (last) fat_access(inode->i_sb,last,this);
	else {
		inode->i_data[D_START] = this;
		inode->i_dirt = 1;
	}
#ifdef DEBUG
if (last) printk("next set to %d\r\n",fat_access(inode->i_sb,last,-1));
#endif
	for (current = 0; current < MSDOS_SB(inode->i_sb)->cluster_size;
	    current++) {
		sector = MSDOS_SB(inode->i_sb)->data_start+(this-2)*
		    MSDOS_SB(inode->i_sb)->cluster_size+current;
#ifdef DEBUG
printk("zeroing sector %d\r\n",sector);
#endif
		if (current < MSDOS_SB(inode->i_sb)->cluster_size-1 &&
		    !(sector & 1)) {
			if (!(bh = getblk(inode->i_dev,sector >> 1, BLOCK_SIZE)))
				printk("getblk failed\r\n");
			else {
				memset(bh->b_data,0,BLOCK_SIZE);
				bh->b_uptodate = 1;
			}
			current++;
		}
		else {
			if (!(bh = msdos_sread(inode->i_dev,sector,&data)))
				printk("msdos_sread failed\r\n");
			else memset(data,0,SECTOR_SIZE);
		}
		if (bh) {
			bh->b_dirt = 1;
			brelse(bh);
		}
	}
	if (S_ISDIR(inode->i_mode)) {
		if (inode->i_size & (SECTOR_SIZE-1))
			panic("Odd directory size");
		inode->i_size += SECTOR_SIZE*MSDOS_SB(inode->i_sb)->
		    cluster_size;
#ifdef DEBUG
printk("size is %d now (%x)\r\n",inode->i_size,inode);
#endif
		inode->i_dirt = 1;
	}
	return 0;
}


/* Linear day numbers of the respective 1sts in non-leap years. */

static int day_n[] = { 0,31,59,90,120,151,181,212,243,273,304,334,0,0,0,0 };
		  /* JanFebMarApr May Jun Jul Aug Sep Oct Nov Dec */


/* Convert a MS-DOS time/date pair to a UNIX date (seconds since 1 1 70). */

int date_dos2unix(unsigned short time,unsigned short date)
{
	int month,year;

	month = ((date >> 5) & 15)-1;
	year = date >> 9;
	return (time & 31)*2+60*((time >> 5) & 63)+(time >> 11)*3600+86400*
	    ((date & 31)-1+day_n[month]+(year/4)+year*365-((year & 3) == 0 &&
	    month < 2 ? 1 : 0)+3653);
			/* days since 1.1.70 plus 80's leap day */
}


/* Convert linear UNIX date to a MS-DOS time/date pair. */

void date_unix2dos(int unix_date,unsigned short *time,
    unsigned short *date)
{
	int day,year,nl_day,month;

	*time = (unix_date % 60)/2+(((unix_date/60) % 60) << 5)+
	    (((unix_date/3600) % 24) << 11);
	day = unix_date/86400-3652;
	year = day/365;
	if ((year+3)/4+365*year > day) year--;
	day -= (year+3)/4+365*year;
	if (day == 59 && !(year & 3)) {
		nl_day = day;
		month = 2;
	}
	else {
		nl_day = (year & 3) || day <= 59 ? day : day-1;
		for (month = 0; month < 12; month++)
			if (day_n[month] > nl_day) break;
	}
	*date = nl_day-day_n[month-1]+1+(month << 5)+(year << 9);
}


/* Returns the inode number of the directory entry at offset pos. If bh is
   non-NULL, it is brelse'd before. Pos is incremented. The buffer header is
   returned in bh. */

int msdos_get_entry(struct inode *dir,int *pos,struct buffer_head **bh,
    struct msdos_dir_entry **de)
{
	int sector,offset;
	void *data;

	while (1) {
		offset = *pos;
		if ((sector = msdos_smap(dir,*pos >> SECTOR_BITS)) == -1)
			return -1;
		if (!sector)
			return -1; /* FAT error ... */
		*pos += sizeof(struct msdos_dir_entry);
		if (*bh)
			brelse(*bh);
		if (!(*bh = msdos_sread(dir->i_dev,sector,&data)))
			continue;
		*de = (struct msdos_dir_entry *) (data+(offset &
		    (SECTOR_SIZE-1)));
		return (sector << MSDOS_DPS_BITS)+((offset & (SECTOR_SIZE-1)) >>
		    MSDOS_DIR_BITS);
	}
}


/* Scans a directory for a given file (name points to its formatted name) or
   for an empty directory slot (name is NULL). Returns the inode number. */

int msdos_scan(struct inode *dir,char *name,struct buffer_head **res_bh,
    struct msdos_dir_entry **res_de,int *ino)
{
	int pos;
	struct msdos_dir_entry *de;
	struct inode *inode;

	pos = 0;
	*res_bh = NULL;
	while ((*ino = msdos_get_entry(dir,&pos,res_bh,&de)) > -1) {
		if (name) {
			if (de->name[0] && ((unsigned char *) (de->name))[0]
			    != DELETED_FLAG && !(de->attr & ATTR_VOLUME) &&
			    !strncmp(de->name,name,MSDOS_NAME)) break;
		}
		else if (!de->name[0] || ((unsigned char *) (de->name))[0] ==
			    DELETED_FLAG) {
				if (!(inode = iget(dir->i_dev,*ino))) break;
				if (!inode->i_data[D_BUSY]) {
					iput(inode);
					break;
				}
	/* skip deleted files that haven't been closed yet */
				iput(inode);
			}
	}
	if (*ino == -1) {
		if (*res_bh) brelse(*res_bh);
		*res_bh = NULL;
		return name ? -ENOENT : -ENOSPC;
	}
	*res_de = de;
	return 0;
}


/* Now an ugly part: this set of directory scan routines works on clusters
   rather than on inodes and sectors. They are necessary to locate the '..'
   directory "inode". */


static int raw_found(struct super_block *sb,int sector,char *name,int number,
    int *ino)
{
	struct buffer_head *bh;
	struct msdos_dir_entry *data;
	int entry,start;

	if (!(bh = msdos_sread(sb->s_dev,sector,(void **) &data))) return -EIO;
	for (entry = 0; entry < MSDOS_DPS; entry++)
		if (name ? !strncmp(data[entry].name,name,MSDOS_NAME) :
		    *(unsigned char *) data[entry].name != DELETED_FLAG &&
		    data[entry].start == number) {
			if (ino) *ino = sector*MSDOS_DPS+entry;
			start = data[entry].start;
			brelse(bh);
			return start;
		}
	brelse(bh);
	return -1;
}


static int raw_scan_root(struct super_block *sb,char *name,int number,int *ino)
{
	int count,cluster;

	for (count = 0; count < MSDOS_SB(sb)->dir_entries/MSDOS_DPS; count++) {
		if ((cluster = raw_found(sb,MSDOS_SB(sb)->dir_start+count,name,
		    number,ino)) >= 0) return cluster;
	}
	return -ENOENT;
}


static int raw_scan_nonroot(struct super_block *sb,int start,char *name,
    int number,int *ino)
{
	int count,cluster;

	do {
		for (count = 0; count < MSDOS_SB(sb)->cluster_size; count++) {
			if ((cluster = raw_found(sb,(start-2)*MSDOS_SB(sb)->
			    cluster_size+MSDOS_SB(sb)->data_start+count,name,
			    number,ino)) >= 0) return cluster;
		}
		if (!(start = fat_access(sb,start,-1))) panic("FAT error");
	}
	while (start != -1);
	return -ENOENT;
}


static int raw_scan(struct super_block *sb,int start,char *name,int number,
    int *ino)
{
    if (start) return raw_scan_nonroot(sb,start,name,number,ino);
    else return raw_scan_root(sb,name,number,ino);
}


int msdos_parent_ino(struct inode *dir,int locked)
{
	int error,current,prev,this;

	if (!S_ISDIR(dir->i_mode)) panic("Non-directory fed to m_p_i");
	if (dir->i_ino == MSDOS_ROOT_INO) return dir->i_ino;
	if (!locked) lock_creation(); /* prevent renames */
	if ((current = raw_scan(dir->i_sb,dir->i_data[D_START],MSDOS_DOTDOT,0,
	    NULL)) < 0) {
		if (!locked) unlock_creation();
		return current;
	}
	if (!current) this = MSDOS_ROOT_INO;
	else {
		if ((prev = raw_scan(dir->i_sb,current,MSDOS_DOTDOT,0,NULL)) <
		    0) {
			if (!locked) unlock_creation();
			return prev;
		}
		if ((error = raw_scan(dir->i_sb,prev,NULL,current,&this)) < 0) {
			if (!locked) unlock_creation();
			return error;
		}
	}
	if (!locked) unlock_creation();
	return this;
}
