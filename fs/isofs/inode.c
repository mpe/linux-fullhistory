/*
 *  linux/fs/isofs/inode.c
 * 
 *  (C) 1992, 1993, 1994  Eric Youngdale Modified for ISO9660 filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 */

#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#else
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#endif

#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/iso_fs.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/malloc.h>
#include <linux/errno.h>
#include <linux/cdrom.h>

#include <asm/system.h>
#include <asm/segment.h>

#ifdef LEAK_CHECK
static int check_malloc = 0;
static int check_bread = 0;
#endif

void isofs_put_super(struct super_block *sb)
{
	lock_super(sb);

#ifdef LEAK_CHECK
	printk("Outstanding mallocs:%d, outstanding buffers: %d\n", 
	       check_malloc, check_bread);
#endif
	sb->s_dev = 0;
	unlock_super(sb);
	MOD_DEC_USE_COUNT;
	return;
}

static struct super_operations isofs_sops = { 
	isofs_read_inode,
	NULL,			/* notify_change */
	NULL,			/* write_inode */
	NULL,			/* put_inode */
	isofs_put_super,
	NULL,			/* write_super */
	isofs_statfs,
	NULL
};

struct iso9660_options{
  char map;
  char rock;
  char cruft;
  char unhide;
  unsigned char conversion;
  unsigned int blocksize;
  mode_t mode;
  gid_t gid;
  uid_t uid;
};

static int parse_options(char *options, struct iso9660_options * popt)
{
	char *this_char,*value;

	popt->map = 'n';
	popt->rock = 'y';
	popt->cruft = 'n';
	popt->unhide = 'n';
	popt->conversion = 'b';		/* default: no conversion */
	popt->blocksize = 1024;
	popt->mode = S_IRUGO;
	popt->gid = 0;
	popt->uid = 0;
	if (!options) return 1;
	for (this_char = strtok(options,","); this_char; this_char = strtok(NULL,",")) {
	        if (strncmp(this_char,"norock",6) == 0) {
		  popt->rock = 'n';
		  continue;
		};
	        if (strncmp(this_char,"unhide",6) == 0) {
		  popt->unhide = 'y';
		  continue;
		};
	        if (strncmp(this_char,"cruft",5) == 0) {
		  popt->cruft = 'y';
		  continue;
		};
		if ((value = strchr(this_char,'=')) != NULL)
			*value++ = 0;
		if (!strcmp(this_char,"map") && value) {
			if (value[0] && !value[1] && strchr("on",*value))
				popt->map = *value;
			else if (!strcmp(value,"off")) popt->map = 'o';
			else if (!strcmp(value,"normal")) popt->map = 'n';
			else return 0;
		}
		else if (!strcmp(this_char,"conv") && value) {
			if (value[0] && !value[1] && strchr("btma",*value))
				popt->conversion = *value;
			else if (!strcmp(value,"binary")) popt->conversion = 'b';
			else if (!strcmp(value,"text")) popt->conversion = 't';
			else if (!strcmp(value,"mtext")) popt->conversion = 'm';
			else if (!strcmp(value,"auto")) popt->conversion = 'a';
			else return 0;
		}
		else if (value && 
			 (!strcmp(this_char,"block") ||
			  !strcmp(this_char,"mode") ||
			  !strcmp(this_char,"uid") ||
			  !strcmp(this_char,"gid"))) {
		  char * vpnt = value;
		  unsigned int ivalue;
		  ivalue = 0;
		  while(*vpnt){
		    if(*vpnt <  '0' || *vpnt > '9') break;
		    ivalue = ivalue * 10 + (*vpnt - '0');
		    vpnt++;
		  };
		  if (*vpnt) return 0;
		  switch(*this_char) {
		  case 'b':
		    if (ivalue != 1024 && ivalue != 2048) return 0;
		    popt->blocksize = ivalue;
		    break;
		  case 'u':
		    popt->uid = ivalue;
		    break;
		  case 'g':
		    popt->gid = ivalue;
		    break;
		  case 'm':
		    popt->mode = ivalue;
		    break;
		  }
		}
		else return 0;
	}
	return 1;
}


static unsigned int isofs_get_last_session(int dev)
{
  struct cdrom_multisession ms_info;
  unsigned int vol_desc_start;
  struct inode inode_fake;
  extern struct file_operations * get_blkfops(unsigned int);
  int i;

  /*
   * look if the driver can tell the multi session redirection value
   * <emoenke@gwdg.de>
   */
  vol_desc_start=0;
  if (get_blkfops(MAJOR(dev))->ioctl!=NULL)
    {
      inode_fake.i_rdev=dev;
      ms_info.addr_format=CDROM_LBA;
      set_fs(KERNEL_DS);
      i=get_blkfops(MAJOR(dev))->ioctl(&inode_fake,
				       NULL,
				       CDROMMULTISESSION,
				       (unsigned long) &ms_info);
      set_fs(USER_DS);
#if 0 
      printk("isofs.inode: CDROMMULTISESSION: rc=%d\n",i);
      if (i==0)
	{
	  printk("isofs.inode: XA disk: %s\n", ms_info.xa_flag ? "yes":"no");
	  printk("isofs.inode: vol_desc_start = %d\n", ms_info.addr.lba);
	}
#endif 0
      if ((i==0)&&(ms_info.xa_flag)) vol_desc_start=ms_info.addr.lba;
    }
  return vol_desc_start;
}

struct super_block *isofs_read_super(struct super_block *s,void *data,
				     int silent)
{
	struct buffer_head *bh=NULL;
	int iso_blknum;
	unsigned int blocksize_bits;
	int high_sierra;
	int dev=s->s_dev;
	unsigned int vol_desc_start;

	struct iso_volume_descriptor *vdp;
	struct hs_volume_descriptor *hdp;

	struct iso_primary_descriptor *pri = NULL;
	struct hs_primary_descriptor *h_pri = NULL;

	struct iso_directory_record *rootp;

	struct iso9660_options opt;

	MOD_INC_USE_COUNT;

	if (!parse_options((char *) data,&opt)) {
		s->s_dev = 0;
		MOD_DEC_USE_COUNT;
		return NULL;
	}

#if 0
	printk("map = %c\n", opt.map);
	printk("rock = %c\n", opt.rock);
	printk("cruft = %c\n", opt.cruft);
	printk("unhide = %c\n", opt.unhide);
	printk("conversion = %c\n", opt.conversion);
	printk("blocksize = %d\n", opt.blocksize);
	printk("gid = %d\n", opt.gid);
	printk("uid = %d\n", opt.uid);
#endif
	
	blocksize_bits = 0;
	{
	  int i = opt.blocksize;
	  while (i != 1){
	    blocksize_bits++;
	    i >>=1;
	  };
	};
	set_blocksize(dev, opt.blocksize);

	lock_super(s);

	s->u.isofs_sb.s_high_sierra = high_sierra = 0; /* default is iso9660 */

	vol_desc_start = isofs_get_last_session(dev);
	
	for (iso_blknum = vol_desc_start+16; iso_blknum < vol_desc_start+100; iso_blknum++) {
#if 0
	        printk("isofs.inode: iso_blknum=%d\n", iso_blknum);
#endif 0
		if (!(bh = bread(dev, iso_blknum << (ISOFS_BLOCK_BITS-blocksize_bits), opt.blocksize))) {
			s->s_dev=0;
			printk("isofs_read_super: bread failed, dev 0x%x iso_blknum %d\n",
			       dev, iso_blknum);
			unlock_super(s);
			MOD_DEC_USE_COUNT;
			return NULL;
		}

		vdp = (struct iso_volume_descriptor *)bh->b_data;
		hdp = (struct hs_volume_descriptor *)bh->b_data;

		
		if (strncmp (hdp->id, HS_STANDARD_ID, sizeof hdp->id) == 0) {
		  if (isonum_711 (hdp->type) != ISO_VD_PRIMARY)
			goto out;
		  if (isonum_711 (hdp->type) == ISO_VD_END)
		        goto out;
		
		        s->u.isofs_sb.s_high_sierra = 1;
			high_sierra = 1;
		        opt.rock = 'n';
		        h_pri = (struct hs_primary_descriptor *)vdp;
			break;
		};
		
		if (strncmp (vdp->id, ISO_STANDARD_ID, sizeof vdp->id) == 0) {
		  if (isonum_711 (vdp->type) != ISO_VD_PRIMARY)
			goto out;
		  if (isonum_711 (vdp->type) == ISO_VD_END)
			goto out;
		
		        pri = (struct iso_primary_descriptor *)vdp;
			break;
	        };

		brelse(bh);
	      }
	if(iso_blknum == vol_desc_start + 100) {
		if (!silent)
			printk("Unable to identify CD-ROM format.\n");
		s->s_dev = 0;
		unlock_super(s);
		MOD_DEC_USE_COUNT;
		return NULL;
	};
	
	
	if(high_sierra){
	  rootp = (struct iso_directory_record *) h_pri->root_directory_record;
	  if (isonum_723 (h_pri->volume_set_size) != 1) {
	    printk("Multi-volume disks not (yet) supported.\n");
	    goto out;
	  };
	  s->u.isofs_sb.s_nzones = isonum_733 (h_pri->volume_space_size);
	  s->u.isofs_sb.s_log_zone_size = isonum_723 (h_pri->logical_block_size);
	  s->u.isofs_sb.s_max_size = isonum_733(h_pri->volume_space_size);
	} else {
	  rootp = (struct iso_directory_record *) pri->root_directory_record;
	  if (isonum_723 (pri->volume_set_size) != 1) {
	    printk("Multi-volume disks not (yet) supported.\n");
	    goto out;
	  };
	  s->u.isofs_sb.s_nzones = isonum_733 (pri->volume_space_size);
	  s->u.isofs_sb.s_log_zone_size = isonum_723 (pri->logical_block_size);
	  s->u.isofs_sb.s_max_size = isonum_733(pri->volume_space_size);
	}
	
	s->u.isofs_sb.s_ninodes = 0; /* No way to figure this out easily */
	
	/* RDE: convert log zone size to bit shift */

	switch (s -> u.isofs_sb.s_log_zone_size)
	  { case  512: s -> u.isofs_sb.s_log_zone_size =  9; break;
	    case 1024: s -> u.isofs_sb.s_log_zone_size = 10; break;
	    case 2048: s -> u.isofs_sb.s_log_zone_size = 11; break;

	    default:
	      printk("Bad logical zone size %ld\n", s -> u.isofs_sb.s_log_zone_size);
	      goto out;
	  }

	/* RDE: data zone now byte offset! */

	s->u.isofs_sb.s_firstdatazone = (isonum_733( rootp->extent) 
					 << s -> u.isofs_sb.s_log_zone_size);
	s->s_magic = ISOFS_SUPER_MAGIC;
	
	/* The CDROM is read-only, has no nodes (devices) on it, and since
	   all of the files appear to be owned by root, we really do not want
	   to allow suid.  (suid or devices will not show up unless we have
	   Rock Ridge extensions) */
	
	s->s_flags |= MS_RDONLY /* | MS_NODEV | MS_NOSUID */;
	
	brelse(bh);
	
	printk("Max size:%ld   Log zone size:%ld\n",
	       s->u.isofs_sb.s_max_size, 
	       1UL << s->u.isofs_sb.s_log_zone_size);
	printk("First datazone:%ld   Root inode number %d\n",
	       s->u.isofs_sb.s_firstdatazone >> s -> u.isofs_sb.s_log_zone_size,
	       isonum_733 (rootp->extent) << s -> u.isofs_sb.s_log_zone_size);
	if(high_sierra) printk("Disc in High Sierra format.\n");
	unlock_super(s);
	/* set up enough so that it can read an inode */
	
	s->s_dev = dev;
	s->s_op = &isofs_sops;
	s->u.isofs_sb.s_mapping = opt.map;
	s->u.isofs_sb.s_rock = (opt.rock == 'y' ? 1 : 0);
	s->u.isofs_sb.s_conversion = opt.conversion;
	s->u.isofs_sb.s_cruft = opt.cruft;
	s->u.isofs_sb.s_unhide = opt.unhide;
	s->u.isofs_sb.s_uid = opt.uid;
	s->u.isofs_sb.s_gid = opt.gid;
	/*
	 * It would be incredibly stupid to allow people to mark every file on the disk
	 * as suid, so we merely allow them to set the default permissions.
	 */
	s->u.isofs_sb.s_mode = opt.mode & 0777;
	s->s_blocksize = opt.blocksize;
	s->s_blocksize_bits = blocksize_bits;
	s->s_mounted = iget(s, isonum_733 (rootp->extent) << s -> u.isofs_sb.s_log_zone_size);
	unlock_super(s);

	if (!(s->s_mounted)) {
		s->s_dev=0;
		printk("get root inode failed\n");
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	if(!check_disk_change(s->s_dev)) {
	  return s;
	}
 out: /* Kick out for various error conditions */
	brelse(bh);
	s->s_dev = 0;
	unlock_super(s);
	MOD_DEC_USE_COUNT;
	return NULL;
}

void isofs_statfs (struct super_block *sb, struct statfs *buf)
{
	put_fs_long(ISOFS_SUPER_MAGIC, &buf->f_type);
	put_fs_long(1 << ISOFS_BLOCK_BITS, &buf->f_bsize);
	put_fs_long(sb->u.isofs_sb.s_nzones, &buf->f_blocks);
	put_fs_long(0, &buf->f_bfree);
	put_fs_long(0, &buf->f_bavail);
	put_fs_long(sb->u.isofs_sb.s_ninodes, &buf->f_files);
	put_fs_long(0, &buf->f_ffree);
	put_fs_long(NAME_MAX, &buf->f_namelen);
	/* Don't know what value to put in buf->f_fsid */
}

int isofs_bmap(struct inode * inode,int block)
{

	if (block<0) {
		printk("_isofs_bmap: block<0");
		return 0;
	}
	return (inode->u.isofs_i.i_first_extent >> ISOFS_BUFFER_BITS(inode)) + block;
}

void isofs_read_inode(struct inode * inode)
{
	unsigned long bufsize = ISOFS_BUFFER_SIZE(inode);
	struct buffer_head * bh;
	struct iso_directory_record * raw_inode;
	unsigned char *pnt = NULL;
	void *cpnt = NULL;
	int high_sierra;
	int block;
	int i;

	block = inode->i_ino >> ISOFS_BUFFER_BITS(inode);
	if (!(bh=bread(inode->i_dev,block, bufsize))) {
	  printk("unable to read i-node block");
	  goto fail;
	}
	
	pnt = ((unsigned char *) bh->b_data
	       + (inode->i_ino & (bufsize - 1)));
	raw_inode = ((struct iso_directory_record *) pnt);
	high_sierra = inode->i_sb->u.isofs_sb.s_high_sierra;

	if ((inode->i_ino & (bufsize - 1)) + *pnt > bufsize){
	        int frag1, offset;

		offset = (inode->i_ino & (bufsize - 1));
		frag1 = bufsize - offset;
	        cpnt = kmalloc(*pnt,GFP_KERNEL);
		if (cpnt == NULL) {
			printk(KERN_INFO "NoMem ISO inode %lu\n",inode->i_ino);
			brelse(bh);
			goto fail;
		}
		memcpy(cpnt, bh->b_data + offset, frag1);
		brelse(bh);
		if (!(bh = bread(inode->i_dev,++block, bufsize))) {
			kfree(cpnt);
			printk("unable to read i-node block");
			goto fail;
		}
		offset += *pnt - bufsize;
		memcpy((char *)cpnt+frag1, bh->b_data, offset);
		pnt = ((unsigned char *) cpnt);
		raw_inode = ((struct iso_directory_record *) pnt);
	}

	if (raw_inode->flags[-high_sierra] & 2) {
		inode->i_mode = S_IRUGO | S_IXUGO | S_IFDIR;
		inode->i_nlink = 1; /* Set to 1.  We know there are 2, but
				       the find utility tries to optimize
				       if it is 2, and it screws up.  It is
				       easier to give 1 which tells find to
				       do it the hard way. */
	} else {
		inode->i_mode = inode->i_sb->u.isofs_sb.s_mode; /* Everybody gets to read the file. */
		inode->i_nlink = 1;
	        inode->i_mode |= S_IFREG;
/* If there are no periods in the name, then set the execute permission bit */
		for(i=0; i< raw_inode->name_len[0]; i++)
			if(raw_inode->name[i]=='.' || raw_inode->name[i]==';')
				break;
		if(i == raw_inode->name_len[0] || raw_inode->name[i] == ';') 
			inode->i_mode |= S_IXUGO; /* execute permission */
	}
	inode->i_uid = inode->i_sb->u.isofs_sb.s_uid;
	inode->i_gid = inode->i_sb->u.isofs_sb.s_gid;
	inode->i_size = isonum_733 (raw_inode->size);

	/* There are defective discs out there - we do this to protect
	   ourselves.  A cdrom will never contain more than 700Mb */
	if((inode->i_size < 0 || inode->i_size > 700000000) &&
	    inode->i_sb->u.isofs_sb.s_cruft == 'n') {
	  printk("Warning: defective cdrom.  Enabling \"cruft\" mount option.\n");
	  inode->i_sb->u.isofs_sb.s_cruft = 'y';
	}

/* Some dipshit decided to store some other bit of information in the high
   byte of the file length.  Catch this and holler.  WARNING: this will make
   it impossible for a file to be > 16Mb on the CDROM!!!*/

	if(inode->i_sb->u.isofs_sb.s_cruft == 'y' && 
	   inode->i_size & 0xff000000){
/*	  printk("Illegal format on cdrom.  Pester manufacturer.\n"); */
	  inode->i_size &= 0x00ffffff;
	}
	
	if (raw_inode->interleave[0]) {
		printk("Interleaved files not (yet) supported.\n");
		inode->i_size = 0;
	}

	/* I have no idea what file_unit_size is used for, so
	   we will flag it for now */
	if(raw_inode->file_unit_size[0] != 0){
		printk("File unit size != 0 for ISO file (%ld).\n",inode->i_ino);
	}

	/* I have no idea what other flag bits are used for, so
	   we will flag it for now */
#ifdef DEBUG
	if((raw_inode->flags[-high_sierra] & ~2)!= 0){
		printk("Unusual flag settings for ISO file (%ld %x).\n",
		       inode->i_ino, raw_inode->flags[-high_sierra]);
	}
#endif

#ifdef DEBUG
	printk("Get inode %d: %d %d: %d\n",inode->i_ino, block, 
	       ((int)pnt) & 0x3ff, inode->i_size);
#endif
	
	inode->i_mtime = inode->i_atime = inode->i_ctime = 
	  iso_date(raw_inode->date, high_sierra);

	inode->u.isofs_i.i_first_extent = (isonum_733 (raw_inode->extent) + 
					   isonum_711 (raw_inode->ext_attr_length))
	  << inode -> i_sb -> u.isofs_sb.s_log_zone_size;
	
	inode->u.isofs_i.i_backlink = 0xffffffff; /* Will be used for previous directory */
	switch (inode->i_sb->u.isofs_sb.s_conversion){
	case 'a':
	  inode->u.isofs_i.i_file_format = ISOFS_FILE_UNKNOWN; /* File type */
	  break;
	case 'b':
	  inode->u.isofs_i.i_file_format = ISOFS_FILE_BINARY; /* File type */
	  break;
	case 't':
	  inode->u.isofs_i.i_file_format = ISOFS_FILE_TEXT; /* File type */
	  break;
	case 'm':
	  inode->u.isofs_i.i_file_format = ISOFS_FILE_TEXT_M; /* File type */
	  break;
	}

/* Now test for possible Rock Ridge extensions which will override some of
   these numbers in the inode structure. */

	if (!high_sierra)
	  parse_rock_ridge_inode(raw_inode, inode);
	
#ifdef DEBUG
	printk("Inode: %x extent: %x\n",inode->i_ino, inode->u.isofs_i.i_first_extent);
#endif
	brelse(bh);
	
	inode->i_op = NULL;

	/* A volume number of 0 is nonsense.  Disable checking if we see
	   this */
	if (inode->i_sb->u.isofs_sb.s_cruft == 'n' && 
	    isonum_723 (raw_inode->volume_sequence_number) == 0) {
	  printk("Warning: defective cdrom.  Enabling \"cruft\" mount option.\n");
	  inode->i_sb->u.isofs_sb.s_cruft = 'y';
	}

	if (inode->i_sb->u.isofs_sb.s_cruft != 'y' && 
	    isonum_723 (raw_inode->volume_sequence_number) != 1) {
		printk("Multi volume CD somehow got mounted.\n");
	} else {
	  if (S_ISREG(inode->i_mode))
	    inode->i_op = &isofs_file_inode_operations;
	  else if (S_ISDIR(inode->i_mode))
	    inode->i_op = &isofs_dir_inode_operations;
	  else if (S_ISLNK(inode->i_mode))
	    inode->i_op = &isofs_symlink_inode_operations;
	  else if (S_ISCHR(inode->i_mode))
	    inode->i_op = &chrdev_inode_operations;
	  else if (S_ISBLK(inode->i_mode))
	    inode->i_op = &blkdev_inode_operations;
	  else if (S_ISFIFO(inode->i_mode))
	    init_fifo(inode);
	}
	if (cpnt) {
		kfree (cpnt);
		cpnt = NULL;
	}
	return;
      fail:
	/* With a data error we return this information */
	inode->i_mtime = inode->i_atime = inode->i_ctime = 0;
	inode->u.isofs_i.i_first_extent = 0;
	inode->u.isofs_i.i_backlink = 0xffffffff;
	inode->i_size = 0;
	inode->i_nlink = 1;
	inode->i_uid = inode->i_gid = 0;
	inode->i_mode = S_IFREG;  /*Regular file, no one gets to read*/
	inode->i_op = NULL;
	return;
}

/* There are times when we need to know the inode number of a parent of
   a particular directory.  When control passes through a routine that
   has access to the parent information, it fills it into the inode structure,
   but sometimes the inode gets flushed out of the queue, and someone
   remembers the number.  When they try to open up again, we have lost
   the information.  The '..' entry on the disc points to the data area
   for a particular inode, so we can follow these links back up, but since
   we do not know the inode number, we do not actually know how large the
   directory is.  The disc is almost always correct, and there is
   enough error checking on the drive itself, but an open ended search
   makes me a little nervous.

   The bsd iso filesystem uses the extent number for an inode, and this
   would work really nicely for us except that the read_inode function
   would not have any clean way of finding the actual directory record
   that goes with the file.  If we had such info, then it would pay
   to change the inode numbers and eliminate this function.
*/

int isofs_lookup_grandparent(struct inode * parent, int extent)
{
	unsigned long bufsize = ISOFS_BUFFER_SIZE(parent);
	unsigned char bufbits = ISOFS_BUFFER_BITS(parent);
	unsigned int block,offset;
	int parent_dir, inode_number;
	int old_offset;
	void * cpnt = NULL;
	int result;
	int directory_size;
	struct buffer_head * bh;
	struct iso_directory_record * de;
	
	offset = 0;
	block = extent << (ISOFS_BLOCK_BITS - bufbits);
	if (!(bh = bread(parent->i_dev, block, bufsize)))  return -1;
	
	while (1 == 1) {
		de = (struct iso_directory_record *) (bh->b_data + offset);
		if (*((unsigned char *) de) == 0) 
		{
			brelse(bh);
			return -1;
		}
		
		offset += *((unsigned char *) de);

		if (offset >= bufsize) 
		{
			printk(".. Directory not in first block"
			       " of directory.\n");
			brelse(bh);
			return -1;
		}
		
		if (de->name_len[0] == 1 && de->name[0] == 1) 
		{
			parent_dir = find_rock_ridge_relocation(de, parent);
			directory_size = isonum_733 (de->size);
			brelse(bh);
			break;
		}
	}
#ifdef DEBUG
	printk("Parent dir:%x\n",parent_dir);
#endif
	/* Now we know the extent where the parent dir starts on. */
	
	result = -1;

	offset = 0;
	block = parent_dir << (ISOFS_BLOCK_BITS - bufbits);
	if (!block || !(bh = bread(parent->i_dev,block, bufsize)))
		return -1;
	
	for(;;)
	{
		de = (struct iso_directory_record *) (bh->b_data + offset);
		inode_number = (block << bufbits)+(offset & (bufsize - 1));
		
		/* If the length byte is zero, we should move on to the next
		   CDROM sector.  If we are at the end of the directory, we
		   kick out of the while loop. */
		
		if (*((unsigned char *) de) == 0) 
		{
			brelse(bh);
			offset = 0;
			block++;
			directory_size -= bufsize;
			if(directory_size < 0) return -1;
			if((block & 1) && (ISOFS_BLOCK_BITS - bufbits))
			  return -1;
			if (!block
			    || !(bh = bread(parent->i_dev,block, bufsize)))
				return -1;
			continue;
		}
		
		/* Make sure that the entire directory record is in the current
		   bh block.  If not, we malloc a buffer, and put the two
		   halves together, so that we can cleanly read the block.  */

		old_offset = offset;
		offset += *((unsigned char *) de);

		if (offset >= bufsize)
		{
 		        unsigned int frag1;
 			frag1 = bufsize - old_offset;
 			cpnt = kmalloc(*((unsigned char *) de),GFP_KERNEL);
			if (!cpnt) return -1;
 			memcpy(cpnt, bh->b_data + old_offset, frag1);
 			de = (struct iso_directory_record *) ((char *)cpnt);
			brelse(bh);
			offset -= bufsize;
 			directory_size -= bufsize;
			if(directory_size < 0) return -1;
			block++;
			if(!(bh = bread(parent->i_dev,block,bufsize))) {
 			        kfree(cpnt);
				return -1;
			};
 			memcpy((char *)cpnt+frag1, bh->b_data, offset);
		}
		
		if (find_rock_ridge_relocation(de, parent) == extent){
			result = inode_number;
			goto out;
		}
		
		if (cpnt) {
			kfree(cpnt);
			cpnt = NULL;
		}
	}

	/* We go here for any condition we cannot handle.
	   We also drop through to here at the end of the directory. */

 out:
	if (cpnt) {
	        kfree(cpnt);
		cpnt = NULL;
	}
	brelse(bh);
#ifdef DEBUG
	printk("Resultant Inode %d\n",result);
#endif
	return result;
}
    
#ifdef LEAK_CHECK
#undef malloc
#undef free_s
#undef bread
#undef brelse

void * leak_check_malloc(unsigned int size){
  void * tmp;
  check_malloc++;
  tmp = kmalloc(size, GFP_KERNEL);
  return tmp;
}

void leak_check_free_s(void * obj, int size){
  check_malloc--;
  return kfree_s(obj, size);
}

struct buffer_head * leak_check_bread(int dev, int block, int size){
  check_bread++;
  return bread(dev, block, size);
}

void leak_check_brelse(struct buffer_head * bh){
  check_bread--;
  return brelse(bh);
}

#endif

#ifdef MODULE

char kernel_version[] = UTS_RELEASE;

static struct file_system_type iso9660_fs_type = {
	isofs_read_super, "iso9660", 1, NULL
};

int init_module(void)
{
	register_filesystem(&iso9660_fs_type);
	return 0;
}

void cleanup_module(void)
{
	unregister_filesystem(&iso9660_fs_type);
}

#endif

