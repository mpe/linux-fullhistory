/*
 *  linux/fs/isofs/inode.c
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 *      1992, 1993, 1994  Eric Youngdale Modified for ISO 9660 filesystem.
 *      1994  Eberhard Moenkeberg - multi session handling.
 *      1995  Mark Dobie - allow mounting of some weird VideoCDs and PhotoCDs.
 *	1997  Gordon Chaffee - Joliet CDs
 *	1998  Eric Lammerts - ISO 9660 Level 3
 */

#include <linux/config.h>
#include <linux/module.h>

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
#include <linux/init.h>
#include <linux/nls.h>
#include <linux/ctype.h>

#include <asm/system.h>
#include <asm/uaccess.h>

/*
 * We have no support for "multi volume" CDs, but more and more disks carry
 * wrong information within the volume descriptors.
 */
#define IGNORE_WRONG_MULTI_VOLUME_SPECS
#define BEQUIET

#ifdef LEAK_CHECK
static int check_malloc = 0;
static int check_bread = 0;
#endif

static int isofs_hashi(struct dentry *parent, struct qstr *qstr);
static int isofs_hash(struct dentry *parent, struct qstr *qstr);
static int isofs_cmpi(struct dentry *dentry, struct qstr *a, struct qstr *b);
static int isofs_cmp(struct dentry *dentry, struct qstr *a, struct qstr *b);

#ifdef CONFIG_JOLIET
static int isofs_hashi_ms(struct dentry *parent, struct qstr *qstr);
static int isofs_hash_ms(struct dentry *parent, struct qstr *qstr);
static int isofs_cmpi_ms(struct dentry *dentry, struct qstr *a, struct qstr *b);
static int isofs_cmp_ms(struct dentry *dentry, struct qstr *a, struct qstr *b);
#endif

void isofs_put_super(struct super_block *sb)
{
#ifdef CONFIG_JOLIET
	if (sb->u.isofs_sb.s_nls_iocharset) {
		unload_nls(sb->u.isofs_sb.s_nls_iocharset);
		sb->u.isofs_sb.s_nls_iocharset = NULL;
	}
#endif

#ifdef LEAK_CHECK
	printk("Outstanding mallocs:%d, outstanding buffers: %d\n",
	       check_malloc, check_bread);
#endif

	MOD_DEC_USE_COUNT;
	return;
}

static struct super_operations isofs_sops = {
	isofs_read_inode,
	NULL,			/* write_inode */
	NULL,			/* put_inode */
	NULL,			/* delete_inode */
	NULL,			/* notify_change */
	isofs_put_super,
	NULL,			/* write_super */
	isofs_statfs,
	NULL
};

static struct dentry_operations isofs_dentry_ops[] = {
	{
		NULL,			/* d_revalidate */
		isofs_hash,
		isofs_cmp,
		NULL			/* d_delete */
	},
	{
		NULL,			/* d_revalidate */
		isofs_hashi,
		isofs_cmpi,
		NULL			/* d_delete */
	},
#ifdef CONFIG_JOLIET
	{
		NULL,			/* d_revalidate */
		isofs_hash_ms,
		isofs_cmp_ms,
		NULL			/* d_delete */
	},
	{
		NULL,			/* d_revalidate */
		isofs_hashi_ms,
		isofs_cmpi_ms,
		NULL			/* d_delete */
	}
#endif
};

struct iso9660_options{
	char map;
	char rock;
	char joliet;
	char cruft;
	char unhide;
	unsigned char check;
	unsigned int blocksize;
	mode_t mode;
	gid_t gid;
	uid_t uid;
	char *iocharset;
	unsigned char utf8;
};

/*
 * Compute the hash for the isofs name corresponding to the dentry.
 */
static int
isofs_hash_common(struct dentry *dentry, struct qstr *qstr, int ms)
{
	const char *name;
	int len;

	len = qstr->len;
	name = qstr->name;
	if (ms) {
		while (len && name[len-1] == '.')
			len--;
	}

	qstr->hash = full_name_hash(name, len);

	return 0;
}

/*
 * Compute the hash for the isofs name corresponding to the dentry.
 */
static int
isofs_hashi_common(struct dentry *dentry, struct qstr *qstr, int ms)
{
	const char *name;
	int len;
	char c;
	unsigned long hash;

	len = qstr->len;
	name = qstr->name;
	if (ms) {
		while (len && name[len-1] == '.')
			len--;
	}

	hash = init_name_hash();
	while (len--) {
		c = tolower(*name++);
		hash = partial_name_hash(tolower(c), hash);
	}
	qstr->hash = end_name_hash(hash);

	return 0;
}

/*
 * Case insensitive compare of two isofs names.
 */
static int
isofs_cmpi_common(struct dentry *dentry,struct qstr *a,struct qstr *b,int ms)
{
	int alen, blen;

	/* A filename cannot end in '.' or we treat it like it has none */
	alen = a->len;
	blen = b->len;
	if (ms) {
		while (alen && a->name[alen-1] == '.')
			alen--;
		while (blen && b->name[blen-1] == '.')
			blen--;
	}
	if (alen == blen) {
		if (strnicmp(a->name, b->name, alen) == 0)
			return 0;
	}
	return 1;
}

/*
 * Case sensitive compare of two isofs names.
 */
static int
isofs_cmp_common(struct dentry *dentry,struct qstr *a,struct qstr *b,int ms)
{
	int alen, blen;

	/* A filename cannot end in '.' or we treat it like it has none */
	alen = a->len;
	blen = b->len;
	if (ms) {
		while (alen && a->name[alen-1] == '.')
			alen--;
		while (blen && b->name[blen-1] == '.')
			blen--;
	}
	if (alen == blen) {
		if (strncmp(a->name, b->name, alen) == 0)
			return 0;
	}
	return 1;
}

static int
isofs_hash(struct dentry *dentry, struct qstr *qstr)
{
	return isofs_hash_common(dentry, qstr, 0);
}

static int
isofs_hashi(struct dentry *dentry, struct qstr *qstr)
{
	return isofs_hashi_common(dentry, qstr, 0);
}

static int
isofs_cmp(struct dentry *dentry,struct qstr *a,struct qstr *b)
{
	return isofs_cmp_common(dentry, a, b, 0);
}

static int
isofs_cmpi(struct dentry *dentry,struct qstr *a,struct qstr *b)
{
	return isofs_cmpi_common(dentry, a, b, 0);
}

#ifdef CONFIG_JOLIET
static int
isofs_hash_ms(struct dentry *dentry, struct qstr *qstr)
{
	return isofs_hash_common(dentry, qstr, 1);
}

static int
isofs_hashi_ms(struct dentry *dentry, struct qstr *qstr)
{
	return isofs_hashi_common(dentry, qstr, 1);
}

static int
isofs_cmp_ms(struct dentry *dentry,struct qstr *a,struct qstr *b)
{
	return isofs_cmp_common(dentry, a, b, 1);
}

static int
isofs_cmpi_ms(struct dentry *dentry,struct qstr *a,struct qstr *b)
{
	return isofs_cmpi_common(dentry, a, b, 1);
}
#endif

static int parse_options(char *options, struct iso9660_options * popt)
{
	char *this_char,*value;

	popt->map = 'n';
	popt->rock = 'y';
	popt->joliet = 'y';
	popt->cruft = 'n';
	popt->unhide = 'n';
	popt->check = 'u';		/* unset */
	popt->blocksize = 1024;
	popt->mode = S_IRUGO | S_IXUGO; /* r-x for all.  The disc could
					   be shared with DOS machines so
					   virtually anything could be
					   a valid executable. */
	popt->gid = 0;
	popt->uid = 0;
	popt->iocharset = NULL;
	popt->utf8 = 0;
	if (!options) return 1;
	for (this_char = strtok(options,","); this_char; this_char = strtok(NULL,",")) {
	        if (strncmp(this_char,"norock",6) == 0) {
		  popt->rock = 'n';
		  continue;
		}
	        if (strncmp(this_char,"nojoliet",8) == 0) {
		  popt->joliet = 'n';
		  continue;
		}
	        if (strncmp(this_char,"unhide",6) == 0) {
		  popt->unhide = 'y';
		  continue;
		}
	        if (strncmp(this_char,"cruft",5) == 0) {
		  popt->cruft = 'y';
		  continue;
		}
	        if (strncmp(this_char,"utf8",4) == 0) {
		  popt->utf8 = 1;
		  continue;
		}
		if ((value = strchr(this_char,'=')) != NULL)
			*value++ = 0;

#ifdef CONFIG_JOLIET
		if (!strcmp(this_char,"iocharset") && value) {
			popt->iocharset = value;
			while (*value && *value != ',')
				value++;
			if (value == popt->iocharset)
				return 0;
			*value = 0;
		} else
#endif
		if (!strcmp(this_char,"map") && value) {
			if (value[0] && !value[1] && strchr("ano",*value))
				popt->map = *value;
			else if (!strcmp(value,"off")) popt->map = 'o';
			else if (!strcmp(value,"normal")) popt->map = 'n';
			else if (!strcmp(value,"acorn")) popt->map = 'a';
			else return 0;
		}
		else if (!strcmp(this_char,"check") && value) {
			if (value[0] && !value[1] && strchr("rs",*value))
				popt->check = *value;
			else if (!strcmp(value,"relaxed")) popt->check = 'r';
			else if (!strcmp(value,"strict")) popt->check = 's';
			else return 0;
		}
		else if (!strcmp(this_char,"conv") && value) {
			/* no conversion is done anymore;
			   we still accept the same mount options,
			   but ignore them */
			if (value[0] && !value[1] && strchr("btma",*value)) ;
			else if (!strcmp(value,"binary")) ;
			else if (!strcmp(value,"text")) ;
			else if (!strcmp(value,"mtext")) ;
			else if (!strcmp(value,"auto")) ;
			else return 0;
		}
		else if (value &&
			 (!strcmp(this_char,"block") ||
			  !strcmp(this_char,"mode") ||
			  !strcmp(this_char,"uid") ||
			  !strcmp(this_char,"gid"))) {
		  char * vpnt = value;
		  unsigned int ivalue = simple_strtoul(vpnt, &vpnt, 0);
		  if (*vpnt) return 0;
		  switch(*this_char) {
		  case 'b':
		    if (   ivalue != 512
			&& ivalue != 1024
			&& ivalue != 2048) return 0;
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
		else return 1;
	}
	return 1;
}

/*
 * look if the driver can tell the multi session redirection value
 *
 * don't change this if you don't know what you do, please!
 * Multisession is legal only with XA disks.
 * A non-XA disk with more than one volume descriptor may do it right, but
 * usually is written in a nowhere standardized "multi-partition" manner.
 * Multisession uses absolute addressing (solely the first frame of the whole
 * track is #0), multi-partition uses relative addressing (each first frame of
 * each track is #0), and a track is not a session.
 *
 * A broken CDwriter software or drive firmware does not set new standards,
 * at least not if conflicting with the existing ones.
 *
 * emoenke@gwdg.de
 */
#define WE_OBEY_THE_WRITTEN_STANDARDS 1

static unsigned int isofs_get_last_session(kdev_t dev)
{
  struct cdrom_multisession ms_info;
  unsigned int vol_desc_start;
  struct inode inode_fake;
  struct file_operations *fops;
  extern struct file_operations * get_blkfops(unsigned int);
  int i;

  vol_desc_start=0;
  fops = get_blkfops(MAJOR(dev));
  if (fops && fops->ioctl)
    {
      /* Whoops.  We must save the old FS, since otherwise
       * we would destroy the kernels idea about FS on root
       * mount in read_super... [chexum]
       */
      mm_segment_t old_fs=get_fs();
      inode_fake.i_rdev=dev;
      ms_info.addr_format=CDROM_LBA;
      set_fs(KERNEL_DS);
      i=get_blkfops(MAJOR(dev))->ioctl(&inode_fake,
				       NULL,
				       CDROMMULTISESSION,
				       (unsigned long) &ms_info);
      set_fs(old_fs);
#if 0
      printk("isofs.inode: CDROMMULTISESSION: rc=%d\n",i);
      if (i==0)
	{
	  printk("isofs.inode: XA disk: %s\n", ms_info.xa_flag ? "yes":"no");
	  printk("isofs.inode: vol_desc_start = %d\n", ms_info.addr.lba);
	}
#endif
      if (i==0)
#if WE_OBEY_THE_WRITTEN_STANDARDS
        if (ms_info.xa_flag) /* necessary for a valid ms_info.addr */
#endif
          vol_desc_start=ms_info.addr.lba;
    }
  return vol_desc_start;
}

/*
 * Initialize the superblock and read the root inode.
 *
 * Note: a check_disk_change() has been done immediately prior
 * to this call, so we don't need to check again.
 */
struct super_block *isofs_read_super(struct super_block *s, void *data,
				     int silent)
{
	kdev_t				dev = s->s_dev;
	struct buffer_head	      * bh = NULL, *pri_bh = NULL;
	struct hs_primary_descriptor  * h_pri = NULL;
	struct iso_primary_descriptor * pri = NULL;
	struct iso_supplementary_descriptor *sec = NULL;
	struct iso_directory_record   * rootp;
	int				joliet_level = 0;
	int				high_sierra;
	int				iso_blknum, block;
	int				orig_zonesize;
	int				table;
	unsigned int			blocksize, blocksize_bits;
	unsigned int			vol_desc_start;
	unsigned long			first_data_zone;
	struct inode		      * inode;
	struct iso9660_options		opt;

	MOD_INC_USE_COUNT;
	/* lock before any blocking operations */
	lock_super(s);

	if (!parse_options((char *) data, &opt))
		goto out_unlock;

#if 0
	printk("map = %c\n", opt.map);
	printk("rock = %c\n", opt.rock);
	printk("joliet = %c\n", opt.joliet);
	printk("check = %c\n", opt.check);
	printk("cruft = %c\n", opt.cruft);
	printk("unhide = %c\n", opt.unhide);
	printk("blocksize = %d\n", opt.blocksize);
	printk("gid = %d\n", opt.gid);
	printk("uid = %d\n", opt.uid);
	printk("iocharset = %s\n", opt.iocharset);
#endif

 	/*
 	 * First of all, get the hardware blocksize for this device.
 	 * If we don't know what it is, or the hardware blocksize is
 	 * larger than the blocksize the user specified, then use
 	 * that value.
 	 */
 	blocksize = get_hardblocksize(dev);
 	if(    (blocksize != 0)
 	    && (blocksize > opt.blocksize) )
 	  {
 	    /*
 	     * Force the blocksize we are going to use to be the
 	     * hardware blocksize.
 	     */
 	    opt.blocksize = blocksize;
 	  }
 
	blocksize_bits = 0;
	{
	  int i = opt.blocksize;
	  while (i != 1){
	    blocksize_bits++;
	    i >>=1;
	  }
	}

	set_blocksize(dev, opt.blocksize);

	s->u.isofs_sb.s_high_sierra = high_sierra = 0; /* default is iso9660 */

	vol_desc_start = isofs_get_last_session(dev);

  	for (iso_blknum = vol_desc_start+16;
             iso_blknum < vol_desc_start+100; iso_blknum++)
	{
	    struct hs_volume_descriptor   * hdp;
	    struct iso_volume_descriptor  * vdp;

	    block = iso_blknum << (ISOFS_BLOCK_BITS-blocksize_bits);
	    if (!(bh = bread(dev, block, opt.blocksize)))
		goto out_no_read;		

	    vdp = (struct iso_volume_descriptor *)bh->b_data;
	    hdp = (struct hs_volume_descriptor *)bh->b_data;
	    
	    /* Due to the overlapping physical location of the descriptors, 
	     * ISO CDs can match hdp->id==HS_STANDARD_ID as well. To ensure 
	     * proper identification in this case, we first check for ISO.
	     */
	    if (strncmp (vdp->id, ISO_STANDARD_ID, sizeof vdp->id) == 0) {
		if (isonum_711 (vdp->type) == ISO_VD_END)
		    break;
		if (isonum_711 (vdp->type) == ISO_VD_PRIMARY) {
		    if (pri == NULL) {
			pri = (struct iso_primary_descriptor *)vdp;
			/* Save the buffer in case we need it ... */
			pri_bh = bh;
			bh = NULL;
		    }
		}
#ifdef CONFIG_JOLIET
		else if (isonum_711 (vdp->type) == ISO_VD_SUPPLEMENTARY) {
		    sec = (struct iso_supplementary_descriptor *)vdp;
		    if (sec->escape[0] == 0x25 && sec->escape[1] == 0x2f) {
			if (opt.joliet == 'y') {
			    if (sec->escape[2] == 0x40) {
				joliet_level = 1;
			    } else if (sec->escape[2] == 0x43) {
				joliet_level = 2;
			    } else if (sec->escape[2] == 0x45) {
				joliet_level = 3;
			    }
			    printk(KERN_DEBUG"ISO 9660 Extensions: Microsoft Joliet Level %d\n",
				   joliet_level);
			}
			goto root_found;
		    } else {
			/* Unknown supplementary volume descriptor */
			sec = NULL;
		    }
		}
#endif
	    } else {
	        if (strncmp (hdp->id, HS_STANDARD_ID, sizeof hdp->id) == 0) {
		    if (isonum_711 (hdp->type) != ISO_VD_PRIMARY)
		        goto out_freebh;
		
		    s->u.isofs_sb.s_high_sierra = 1;
		    high_sierra = 1;
		    opt.rock = 'n';
		    h_pri = (struct hs_primary_descriptor *)vdp;
		    goto root_found;
		}
	    }

            /* Just skip any volume descriptors we don't recognize */

	    brelse(bh);
	    bh = NULL;
	}
	/*
	 * If we fall through, either no volume descriptor was found,
	 * or else we passed a primary descriptor looking for others.
	 */
	if (!pri)
		goto out_unknown_format;
	brelse(bh);
	bh = pri_bh;
	pri_bh = NULL;

root_found:
	brelse(pri_bh);

	if (joliet_level && opt.rock == 'n') {
	    /* This is the case of Joliet with the norock mount flag.
	     * A disc with both Joliet and Rock Ridge is handled later
	     */
	    pri = (struct iso_primary_descriptor *) sec;
	}

	if(high_sierra){
	  rootp = (struct iso_directory_record *) h_pri->root_directory_record;
#ifndef IGNORE_WRONG_MULTI_VOLUME_SPECS
	  if (isonum_723 (h_pri->volume_set_size) != 1)
		goto out_no_support;
#endif IGNORE_WRONG_MULTI_VOLUME_SPECS
	  s->u.isofs_sb.s_nzones = isonum_733 (h_pri->volume_space_size);
	  s->u.isofs_sb.s_log_zone_size = isonum_723 (h_pri->logical_block_size);
	  s->u.isofs_sb.s_max_size = isonum_733(h_pri->volume_space_size);
	} else {
	  rootp = (struct iso_directory_record *) pri->root_directory_record;
#ifndef IGNORE_WRONG_MULTI_VOLUME_SPECS
	  if (isonum_723 (pri->volume_set_size) != 1)
		goto out_no_support;
#endif IGNORE_WRONG_MULTI_VOLUME_SPECS
	  s->u.isofs_sb.s_nzones = isonum_733 (pri->volume_space_size);
	  s->u.isofs_sb.s_log_zone_size = isonum_723 (pri->logical_block_size);
	  s->u.isofs_sb.s_max_size = isonum_733(pri->volume_space_size);
	}

	s->u.isofs_sb.s_ninodes = 0; /* No way to figure this out easily */

	orig_zonesize = s -> u.isofs_sb.s_log_zone_size;
	/*
	 * If the zone size is smaller than the hardware sector size,
	 * this is a fatal error.  This would occur if the disc drive
	 * had sectors that were 2048 bytes, but the filesystem had
	 * blocks that were 512 bytes (which should only very rarely
	 * happen.)
	 */
	if(blocksize != 0 && orig_zonesize < blocksize)
		goto out_bad_size;

	/* RDE: convert log zone size to bit shift */
	switch (s -> u.isofs_sb.s_log_zone_size)
	  { case  512: s -> u.isofs_sb.s_log_zone_size =  9; break;
	    case 1024: s -> u.isofs_sb.s_log_zone_size = 10; break;
	    case 2048: s -> u.isofs_sb.s_log_zone_size = 11; break;

	    default:
		goto out_bad_zone_size;
	  }

	s->s_magic = ISOFS_SUPER_MAGIC;

	/* The CDROM is read-only, has no nodes (devices) on it, and since
	   all of the files appear to be owned by root, we really do not want
	   to allow suid.  (suid or devices will not show up unless we have
	   Rock Ridge extensions) */

	s->s_flags |= MS_RDONLY /* | MS_NODEV | MS_NOSUID */;

	/* RDE: data zone now byte offset! */

	first_data_zone = ((isonum_733 (rootp->extent) +
			  isonum_711 (rootp->ext_attr_length))
			 << s -> u.isofs_sb.s_log_zone_size);
	s->u.isofs_sb.s_firstdatazone = first_data_zone;
#ifndef BEQUIET
	printk(KERN_DEBUG "Max size:%ld   Log zone size:%ld\n",
	       s->u.isofs_sb.s_max_size,
	       1UL << s->u.isofs_sb.s_log_zone_size);
	printk(KERN_DEBUG "First datazone:%ld   Root inode number:%ld\n",
	       s->u.isofs_sb.s_firstdatazone >> s -> u.isofs_sb.s_log_zone_size,
	       s->u.isofs_sb.s_firstdatazone);
	if(high_sierra)
		printk(KERN_DEBUG "Disc in High Sierra format.\n");
#endif

	/*
	 * If the Joliet level is set, we _may_ decide to use the
	 * secondary descriptor, but can't be sure until after we
	 * read the root inode. But before reading the root inode
	 * we may need to change the device blocksize, and would
	 * rather release the old buffer first. So, we cache the
	 * first_data_zone value from the secondary descriptor.
	 */
	if (joliet_level) {
		pri = (struct iso_primary_descriptor *) sec;
		rootp = (struct iso_directory_record *)
			pri->root_directory_record;
		first_data_zone = ((isonum_733 (rootp->extent) +
			  	isonum_711 (rootp->ext_attr_length))
				 << s -> u.isofs_sb.s_log_zone_size);
	}

	/*
	 * We're all done using the volume descriptor, and may need
	 * to change the device blocksize, so release the buffer now.
	 */
	brelse(bh);

	/*
	 * Force the blocksize to 512 for 512 byte sectors.  The file
	 * read primitives really get it wrong in a bad way if we don't
	 * do this.
	 *
	 * Note - we should never be setting the blocksize to something
	 * less than the hardware sector size for the device.  If we
	 * do, we would end up having to read larger buffers and split
	 * out portions to satisfy requests.
	 *
	 * Note2- the idea here is that we want to deal with the optimal
	 * zonesize in the filesystem.  If we have it set to something less,
	 * then we have horrible problems with trying to piece together
	 * bits of adjacent blocks in order to properly read directory
	 * entries.  By forcing the blocksize in this way, we ensure
	 * that we will never be required to do this.
	 */
	if ( orig_zonesize != opt.blocksize ) {
		set_blocksize(dev, orig_zonesize);
#ifndef BEQUIET
		printk(KERN_DEBUG 
			"ISOFS: Forcing new log zone size:%d\n", orig_zonesize);
#endif
	}
	s->s_blocksize = orig_zonesize;
	s->s_blocksize_bits = s -> u.isofs_sb.s_log_zone_size;

	s->u.isofs_sb.s_nls_iocharset = NULL;

#ifdef CONFIG_JOLIET
	if (joliet_level && opt.utf8 == 0) {
		char * p = opt.iocharset ? opt.iocharset : "iso8859-1";
		s->u.isofs_sb.s_nls_iocharset = load_nls(p);
		if (! s->u.isofs_sb.s_nls_iocharset) {
			/* Fail only if explicit charset specified */
			if (opt.iocharset)
				goto out_freebh;
			s->u.isofs_sb.s_nls_iocharset = load_nls_default();
		}
	}
#endif
	s->s_op = &isofs_sops;
	s->u.isofs_sb.s_mapping = opt.map;
	s->u.isofs_sb.s_rock = (opt.rock == 'y' ? 2 : 0);
	s->u.isofs_sb.s_cruft = opt.cruft;
	s->u.isofs_sb.s_unhide = opt.unhide;
	s->u.isofs_sb.s_uid = opt.uid;
	s->u.isofs_sb.s_gid = opt.gid;
	s->u.isofs_sb.s_utf8 = opt.utf8;
	/*
	 * It would be incredibly stupid to allow people to mark every file on the disk
	 * as suid, so we merely allow them to set the default permissions.
	 */
	s->u.isofs_sb.s_mode = opt.mode & 0777;

	/*
	 * Read the root inode, which _may_ result in changing
	 * the s_rock flag. Once we have the final s_rock value,
	 * we then decide whether to use the Joliet descriptor.
	 */
	inode = iget(s, s->u.isofs_sb.s_firstdatazone);

	/*
	 * If this disk has both Rock Ridge and Joliet on it, then we
	 * want to use Rock Ridge by default.  This can be overridden
	 * by using the norock mount option.  There is still one other
	 * possibility that is not taken into account: a Rock Ridge
	 * CD with Unicode names.  Until someone sees such a beast, it
	 * will not be supported.
	 */
	if (s->u.isofs_sb.s_rock == 1) {
		joliet_level = 0;
	} else if (joliet_level) {
		s->u.isofs_sb.s_rock = 0;
		if (s->u.isofs_sb.s_firstdatazone != first_data_zone) {
			s->u.isofs_sb.s_firstdatazone = first_data_zone;
			printk(KERN_DEBUG 
				"ISOFS: changing to secondary root\n");
			iput(inode);
			inode = iget(s, s->u.isofs_sb.s_firstdatazone);
		}
	}

	if (opt.check == 'u') {
		/* Only Joliet is case insensitive by default */
		if (joliet_level) opt.check = 'r';
		else opt.check = 's';
	}
	s->u.isofs_sb.s_joliet_level = joliet_level;

	/* check the root inode */
	if (!inode)
		goto out_no_root;
	if (!inode->i_op)
		goto out_bad_root;
	/* get the root dentry */
	s->s_root = d_alloc_root(inode, NULL);
	if (!(s->s_root))
		goto out_no_root;

	table = 0;
	if (joliet_level) table += 2;
	if (opt.check == 'r') table++;
	s->s_root->d_op = &isofs_dentry_ops[table];

	unlock_super(s);
	return s;

	/*
	 * Display error messages and free resources.
	 */
out_bad_root:
	printk(KERN_WARNING "isofs_read_super: root inode not initialized\n");
	goto out_iput;
out_no_root:
	printk(KERN_WARNING "isofs_read_super: get root inode failed\n");
out_iput:
	iput(inode);
#ifdef CONFIG_JOLIET
	if (s->u.isofs_sb.s_nls_iocharset)
		unload_nls(s->u.isofs_sb.s_nls_iocharset);
#endif
	goto out_unlock;
out_no_read:
	printk(KERN_WARNING "isofs_read_super: "
		"bread failed, dev=%s, iso_blknum=%d, block=%d\n",
		kdevname(dev), iso_blknum, block);
	goto out_unlock;
out_bad_zone_size:
	printk(KERN_WARNING "Bad logical zone size %ld\n",
		s->u.isofs_sb.s_log_zone_size);
	goto out_freebh;
out_bad_size:
	printk(KERN_WARNING "Logical zone size(%d) < hardware blocksize(%u)\n",
		orig_zonesize, blocksize);
	goto out_freebh;
#ifndef IGNORE_WRONG_MULTI_VOLUME_SPECS
out_no_support:
	printk(KERN_WARNING "Multi-volume disks not supported.\n");
	goto out_freebh;
#endif
out_unknown_format:
	if (!silent)
		printk(KERN_WARNING "Unable to identify CD-ROM format.\n");

out_freebh:
	brelse(bh);
out_unlock:
	s->s_dev = 0;
	unlock_super(s);
	MOD_DEC_USE_COUNT;
	return NULL;
}

int isofs_statfs (struct super_block *sb, struct statfs *buf, int bufsiz)
{
	struct statfs tmp;

	tmp.f_type = ISOFS_SUPER_MAGIC;
	tmp.f_bsize = sb->s_blocksize;
	tmp.f_blocks = (sb->u.isofs_sb.s_nzones
                  << (sb->u.isofs_sb.s_log_zone_size - sb->s_blocksize_bits));
	tmp.f_bfree = 0;
	tmp.f_bavail = 0;
	tmp.f_files = sb->u.isofs_sb.s_ninodes;
	tmp.f_ffree = 0;
	tmp.f_namelen = NAME_MAX;
	return copy_to_user(buf, &tmp, bufsiz) ? -EFAULT : 0;
}

int isofs_bmap(struct inode * inode,int block)
{
	off_t b_off, offset, size;
	struct inode *ino;
	unsigned int firstext;
	unsigned long nextino;
	int i;

	if (block<0) {
		printk("_isofs_bmap: block<0");
		return 0;
	}

	b_off = block << ISOFS_BUFFER_BITS(inode);

	/*
	 * If we are beyond the end of this file, don't give out any
	 * blocks.
	 */
	if( b_off > inode->i_size )
	  {
	    off_t	max_legal_read_offset;

	    /*
	     * If we are *way* beyond the end of the file, print a message.
	     * Access beyond the end of the file up to the next page boundary
	     * is normal, however because of the way the page cache works.
	     * In this case, we just return 0 so that we can properly fill
	     * the page with useless information without generating any
	     * I/O errors.
	     */
	    max_legal_read_offset = (inode->i_size + PAGE_SIZE - 1)
	      & ~(PAGE_SIZE - 1);
	    if( b_off >= max_legal_read_offset )
	      {

		printk("_isofs_bmap: block>= EOF(%d, %ld)\n", block,
		       inode->i_size);
	      }
	    return 0;
	  }

	offset = 0;
	firstext = inode->u.isofs_i.i_first_extent;
	size = inode->u.isofs_i.i_section_size;
	nextino = inode->u.isofs_i.i_next_section_ino;
#ifdef DEBUG
	printk("first inode: inode=%x nextino=%x firstext=%u size=%lu\n",
		inode->i_ino, nextino, firstext, size);
#endif
	i = 0;
	if (nextino) {
		while(b_off >= offset + size) {
			offset += size;

			if(nextino == 0) return 0;
			ino = iget(inode->i_sb, nextino);
			if(!ino) return 0;
			firstext = ino->u.isofs_i.i_first_extent;
			size = ino->u.isofs_i.i_section_size;
#ifdef DEBUG
			printk("read inode: inode=%lu ino=%lu nextino=%lu firstext=%u size=%lu\n",
			       inode->i_ino, nextino, ino->u.isofs_i.i_next_section_ino, firstext, size);
#endif
			nextino = ino->u.isofs_i.i_next_section_ino;
			iput(ino);
		
			if(++i > 100) {
				printk("isofs_bmap: More than 100 file sections ?!?, aborting...\n");
				printk("isofs_bmap: ino=%lu block=%d firstext=%u size=%u nextino=%lu\n",
				       inode->i_ino, block, firstext, (unsigned)size, nextino);
				return 0;
			}
		}
	}
#ifdef DEBUG
	printk("isofs_bmap: mapped inode:block %x:%d to block %lu\n",
		inode->i_ino, block, (b_off - offset + firstext) >> ISOFS_BUFFER_BITS(inode));
#endif
	return (b_off - offset + firstext) >> ISOFS_BUFFER_BITS(inode);
}


static void test_and_set_uid(uid_t *p, uid_t value)
{
	if(value) {
		*p = value;
#if 0
		printk("Resetting to %d\n", value);
#endif
	}
}

static int isofs_read_level3_size(struct inode * inode)
{
	unsigned long ino = inode->i_ino;
	unsigned long bufsize = ISOFS_BUFFER_SIZE(inode);
	int high_sierra = inode->i_sb->u.isofs_sb.s_high_sierra;
	struct buffer_head * bh = NULL;
	int block = 0;
	int i = 0;
	void *cpnt;
	struct iso_directory_record * raw_inode;

	inode->i_size = 0;
	inode->u.isofs_i.i_next_section_ino = 0;
	do {
		unsigned char *pnt;
		unsigned int reclen;
		int offset = (ino & (bufsize - 1));

		cpnt = NULL;
		/* Check whether to update our buffer */
		if (block != ino >> ISOFS_BUFFER_BITS(inode)) {
			block = ino >> ISOFS_BUFFER_BITS(inode);
			brelse(bh);
			bh = bread(inode->i_dev, block, bufsize);
			if (!bh)
				goto out_noread;
		}
		pnt = ((unsigned char *) bh->b_data + offset);
		raw_inode = ((struct iso_directory_record *) pnt);
		/*
		 * Note: this is invariant even if the record
		 * spans buffers and must be copied ...
		 */
		reclen = *pnt;

		/* N.B. this test doesn't trigger the i++ code ... */	
		if(reclen == 0) {
			ino = (ino & ~(ISOFS_BLOCK_SIZE - 1)) + ISOFS_BLOCK_SIZE;
			continue;
		}

		/* Check whether the raw inode spans the buffer ... */	
		if (offset + reclen > bufsize){
		        int frag1 = bufsize - offset;
	
		        cpnt = kmalloc(reclen, GFP_KERNEL);
			if (cpnt == NULL)
				goto out_nomem;
			memcpy(cpnt, pnt, frag1);
			brelse(bh);
			bh = bread(inode->i_dev, ++block, bufsize);
			if (!bh)
				goto out_noread;
			offset += reclen - bufsize;
			memcpy((char *)cpnt+frag1, bh->b_data, offset);
			raw_inode = ((struct iso_directory_record *) cpnt);
		}

		inode->i_size += isonum_733 (raw_inode->size);
		if(i == 1) inode->u.isofs_i.i_next_section_ino = ino;

		ino += reclen;
		if (cpnt)
			kfree (cpnt);
		i++;
		if(i > 100)
			goto out_toomany;
	} while(raw_inode->flags[-high_sierra] & 0x80);
out:
	brelse(bh);
	return 0;

out_nomem:
	printk(KERN_INFO "ISOFS: NoMem ISO inode %lu\n", inode->i_ino);
	brelse(bh);
	return 1;
out_noread:
	printk(KERN_INFO "ISOFS: unable to read i-node block %d\n", block);
	if (cpnt)
		kfree(cpnt);
	return 1;
out_toomany:
	printk(KERN_INFO "isofs_read_level3_size: "
		"More than 100 file sections ?!?, aborting...\n"
	  	"isofs_read_level3_size: inode=%lu ino=%lu\n",
		inode->i_ino, ino);
	goto out;
}

void isofs_read_inode(struct inode * inode)
{
	struct super_block *sb = inode->i_sb;
	unsigned long bufsize = ISOFS_BUFFER_SIZE(inode);
	int block = inode->i_ino >> ISOFS_BUFFER_BITS(inode);
	int high_sierra = sb->u.isofs_sb.s_high_sierra;
	struct buffer_head * bh;
	struct iso_directory_record * raw_inode;
	unsigned char *pnt;
	int volume_seq_no, i;

	bh = bread(inode->i_dev, block, bufsize);
	if (!bh) {
		printk(KERN_WARNING "ISOFS: unable to read i-node block\n");
		goto fail;
	}

	pnt = ((unsigned char *) bh->b_data
	       + (inode->i_ino & (bufsize - 1)));
	raw_inode = ((struct iso_directory_record *) pnt);

	if (raw_inode->flags[-high_sierra] & 2) {
		inode->i_mode = S_IRUGO | S_IXUGO | S_IFDIR;
		inode->i_nlink = 1; /* Set to 1.  We know there are 2, but
				       the find utility tries to optimize
				       if it is 2, and it screws up.  It is
				       easier to give 1 which tells find to
				       do it the hard way. */
	} else {
 		/* Everybody gets to read the file. */
		inode->i_mode = inode->i_sb->u.isofs_sb.s_mode;
		inode->i_nlink = 1;
	        inode->i_mode |= S_IFREG;
		/* If there are no periods in the name,
		 * then set the execute permission bit
		 */
		for(i=0; i< raw_inode->name_len[0]; i++)
			if(raw_inode->name[i]=='.' || raw_inode->name[i]==';')
				break;
		if(i == raw_inode->name_len[0] || raw_inode->name[i] == ';')
			inode->i_mode |= S_IXUGO; /* execute permission */
	}
	inode->i_uid = inode->i_sb->u.isofs_sb.s_uid;
	inode->i_gid = inode->i_sb->u.isofs_sb.s_gid;
	inode->i_blocks = inode->i_blksize = 0;


	inode->u.isofs_i.i_section_size = isonum_733 (raw_inode->size);
	if(raw_inode->flags[-high_sierra] & 0x80) {
		if(isofs_read_level3_size(inode)) goto fail;
	} else {
		inode->i_size = isonum_733 (raw_inode->size);
	}

	/* There are defective discs out there - we do this to protect
	   ourselves.  A cdrom will never contain more than 800Mb 
	   .. but a DVD may be up to 1Gig (Ulrich Habel) */
	if((inode->i_size < 0 || inode->i_size > 1073741824) &&
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
	printk("Get inode %x: %d %d: %d\n",inode->i_ino, block,
	       ((int)pnt) & 0x3ff, inode->i_size);
#endif

	inode->i_mtime = inode->i_atime = inode->i_ctime =
	  iso_date(raw_inode->date, high_sierra);

	inode->u.isofs_i.i_first_extent = (isonum_733 (raw_inode->extent) +
					   isonum_711 (raw_inode->ext_attr_length))
	  << inode -> i_sb -> u.isofs_sb.s_log_zone_size;

/* Now test for possible Rock Ridge extensions which will override some of
   these numbers in the inode structure. */

	if (!high_sierra) {
	  parse_rock_ridge_inode(raw_inode, inode);
	  /* hmm..if we want uid or gid set, override the rock ridge setting */
	 test_and_set_uid(&inode->i_uid, inode->i_sb->u.isofs_sb.s_uid);
	}

#ifdef DEBUG
	printk("Inode: %x extent: %x\n",inode->i_ino, inode->u.isofs_i.i_first_extent);
#endif

	/* get the volume sequence number */
	volume_seq_no = isonum_723 (raw_inode->volume_sequence_number) ;

	/*
	 * All done with buffer ... no more references to buffer memory!
	 */
	brelse(bh);

	/*
	 * Disable checking if we see any volume number other than 0 or 1.
	 * We could use the cruft option, but that has multiple purposes, one
	 * of which is limiting the file size to 16Mb.  Thus we silently allow
	 * volume numbers of 0 to go through without complaining.
	 */
	if (inode->i_sb->u.isofs_sb.s_cruft == 'n' &&
	    (volume_seq_no != 0) && (volume_seq_no != 1)) {
	  printk("Warning: defective cdrom (volume sequence number). Enabling \"cruft\" mount option.\n");
	  inode->i_sb->u.isofs_sb.s_cruft = 'y';
	}

	/* Install the inode operations vector */
	inode->i_op = NULL;
#ifndef IGNORE_WRONG_MULTI_VOLUME_SPECS
	if (inode->i_sb->u.isofs_sb.s_cruft != 'y' &&
	    (volume_seq_no != 0) && (volume_seq_no != 1)) {
		printk("Multi volume CD somehow got mounted.\n");
	} else
#endif IGNORE_WRONG_MULTI_VOLUME_SPECS
	{
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
	return;

      fail:
	/* With a data error we return this information */
	inode->i_mtime = inode->i_atime = inode->i_ctime = 0;
	inode->u.isofs_i.i_first_extent = 0;
	inode->i_size = 0;
	inode->i_blocks = inode->i_blksize = 0;
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

   The BSD iso filesystem uses the extent number for an inode, and this
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
	int result;
	int directory_size;
	struct buffer_head * bh;
	struct iso_directory_record * de;

	offset = 0;
	block = extent << (ISOFS_ZONE_BITS(parent) - bufbits);
	if (!(bh = bread(parent->i_dev, block, bufsize)))  return -1;

	while (1 == 1) {
		de = (struct iso_directory_record *) (bh->b_data + offset);
		if (*((unsigned char *) de) == 0)
		{
			brelse(bh);
			printk("Directory .. not found\n");
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
	block = parent_dir << (ISOFS_ZONE_BITS(parent) - bufbits);
	if (!block || !(bh = bread(parent->i_dev,block, bufsize)))
	{
		return -1;
	}

	for(;;)
	{
		de = (struct iso_directory_record *) (bh->b_data + offset);
		inode_number = (block << bufbits)+(offset & (bufsize - 1));

		/* If the length byte is zero, we should move on to the next
		   CDROM sector.  If we are at the end of the directory, we
		   kick out of the while loop. */

 		if ((*((unsigned char *) de) == 0) || (offset == bufsize) )
		{
			brelse(bh);
			offset = 0;
			block++;
			directory_size -= bufsize;
			if(directory_size < 0) return -1;
			if((block & 1) && (ISOFS_ZONE_BITS(parent) - bufbits) == 1)
			{
				return -1;
			}
			if((block & 3) && (ISOFS_ZONE_BITS(parent) - bufbits) == 2)
			{
				return -1;
			}
			if (!block
			    || !(bh = bread(parent->i_dev,block, bufsize)))
			{
				return -1;
			}
			continue;
		}

		/* Make sure that the entire directory record is in the current
		   bh block.  If not, we malloc a buffer, and put the two
		   halves together, so that we can cleanly read the block.  */

		offset += *((unsigned char *) de);

		if (offset > bufsize)
		{
 			printk("Directory overrun\n");
 			goto out;
		}

		if (find_rock_ridge_relocation(de, parent) == extent){
			result = inode_number;
			goto out;
		}

	}

	/* We go here for any condition we cannot handle.
	   We also drop through to here at the end of the directory. */

 out:
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

static struct file_system_type iso9660_fs_type = {
	"iso9660",
	FS_REQUIRES_DEV,
	isofs_read_super, 
	NULL
};

__initfunc(int init_iso9660_fs(void))
{
        return register_filesystem(&iso9660_fs_type);
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

int init_module(void)
{
	return init_iso9660_fs();
}

void cleanup_module(void)
{
	unregister_filesystem(&iso9660_fs_type);
}

#endif
