
/*
   md.c : Multiple Devices driver for Linux
          Copyright (C) 1994-96 Marc ZYNGIER
	  <zyngier@ufr-info-p7.ibp.fr> or
	  <maz@gloups.fdn.fr>

   A lot of inspiration came from hd.c ...

   kerneld support by Boris Tobotras <boris@xtalk.msk.su>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
   
   You should have received a copy of the GNU General Public License
   (for example /usr/src/linux/COPYING); if not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  
*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/md.h>
#include <linux/hdreg.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#ifdef CONFIG_KERNELD
#include <linux/kerneld.h>
#endif
#include <linux/errno.h>

#define MAJOR_NR MD_MAJOR
#define MD_DRIVER

#include <linux/blk.h>

#ifdef CONFIG_MD_SUPPORT_RAID5
int support_for_raid5; /* So raid-5 module won't be inserted if support
			  was not set in the kernel */
#endif

#ifdef CONFIG_MD_SUPPORT_RAID1
int support_for_raid1; /* So raid-1 module won't be inserted if support
			  was not set in the kernel */
#endif

static struct hd_struct md_hd_struct[MAX_MD_DEV];
static int md_blocksizes[MAX_MD_DEV];

int md_size[MAX_MD_DEV]={0, };

static void md_geninit (struct gendisk *);

static struct gendisk md_gendisk=
{
  MD_MAJOR,
  "md",
  0,
  1,
  MAX_MD_DEV,
  md_geninit,
  md_hd_struct,
  md_size,
  MAX_MD_DEV,
  NULL,
  NULL
};

static struct md_personality *pers[MAX_PERSONALITY]={NULL, };

struct real_dev devices[MAX_MD_DEV][MAX_REAL];
struct md_dev md_dev[MAX_MD_DEV];

static struct gendisk *find_gendisk (kdev_t dev)
{
  struct gendisk *tmp=gendisk_head;

  while (tmp != NULL)
  {
    if (tmp->major==MAJOR(dev))
      return (tmp);
    
    tmp=tmp->next;
  }

  return (NULL);
}


/* Picked up from genhd.c */
char *partition_name (kdev_t dev)
{
  static char name[40];		/* This should be long
				   enough for a device name ! */
  struct gendisk *hd = find_gendisk (dev);

  if (!hd)
  {
    printk ("No gendisk entry for dev %s\n", kdevname(dev));
    sprintf (name, "dev %s", kdevname(dev));
    return (name);
  }

  return disk_name (hd, MINOR(dev), name);  /* routine in genhd.c */
}


static void set_ra (void)
{
  int i, j, minra=INT_MAX;

  for (i=0; i<MAX_MD_DEV; i++)
  {
    if (!md_dev[i].pers)
      continue;
    
    for (j=0; j<md_dev[i].nb_dev; j++)
      if (read_ahead[MAJOR(devices[i][j].dev)]<minra)
	minra=read_ahead[MAJOR(devices[i][j].dev)];
  }
  
  read_ahead[MD_MAJOR]=minra;
}


static int md_ioctl (struct inode *inode, struct file *file,
                     unsigned int cmd, unsigned long arg)
{
  int minor, index, err, current_ra;
  struct gendisk *gen_real;
  struct hd_geometry *loc = (struct hd_geometry *) arg;
  kdev_t dev;

  if (!suser())
    return -EACCES;

  if (((minor=MINOR(inode->i_rdev)) & 0x80) &&
      (minor & 0x7f) < MAX_PERSONALITY &&
      pers[minor & 0x7f] &&
      pers[minor & 0x7f]->ioctl)
    return (pers[minor & 0x7f]->ioctl (inode, file, cmd, arg));
  
  if (minor >= MAX_MD_DEV)
    return -EINVAL;

  switch (cmd)
  {
    case REGISTER_DEV:
    dev=to_kdev_t ((dev_t) arg);
    if (MAJOR(dev)==MD_MAJOR || md_dev[minor].nb_dev==MAX_REAL)
      return -EINVAL;

    if (!fs_may_mount (dev) || md_dev[minor].pers)
      return -EBUSY;

    if (!(gen_real=find_gendisk (dev)))
      return -ENOENT;

    index=md_dev[minor].nb_dev++;
    devices[minor][index].dev=dev;

    /* Lock the device by inserting a dummy inode. This doesn't
       smell very good, but I need to be consistent with the
       mount stuff, specially with fs_may_mount. If someone have
       a better idea, please help ! */
    
    devices[minor][index].inode=get_empty_inode ();
    devices[minor][index].inode->i_dev=dev; /* don't care about
					       other fields */
    insert_inode_hash (devices[minor][index].inode);
    
    /* Devices sizes are rounded to a multiple of page (needed for
       paging). This is NOT done by fdisk when partitioning,
       but that's a DOS thing anyway... */
    
    devices[minor][index].size=gen_real->sizes[MINOR(dev)] & ~((PAGE_SIZE >> 10)-1);
    devices[minor][index].offset=index ?
      (devices[minor][index-1].offset + devices[minor][index-1].size) : 0;

    if (!index)
      md_size[minor]=devices[minor][index].size;
    else
      md_size[minor]+=devices[minor][index].size;

    printk("REGISTER_DEV %s to md%x done\n", partition_name(dev), minor);
    break;

    case START_MD:
    if (!md_dev[minor].nb_dev)
      return -EINVAL;

    if (md_dev[minor].pers)
      return -EBUSY;

    md_dev[minor].repartition=(int) arg;
    
    if ((index=PERSONALITY(md_dev+minor) >> (PERSONALITY_SHIFT))
	>= MAX_PERSONALITY)
      return -EINVAL;

    if (!pers[index])
    {
#ifdef CONFIG_KERNELD
      char module_name[80];
      sprintf (module_name, "md-personality-%d", index);
      request_module (module_name);
      if (!pers[index])
#endif
	return -EINVAL;
    }

    md_dev[minor].pers=pers[index];

    if ((err=md_dev[minor].pers->run (minor, md_dev+minor)))
    {
      md_dev[minor].pers=NULL;
      return (err);
    }

    /* FIXME : We assume here we have blocks
       that are twice as large as sectors.
       THIS MAY NOT BE TRUE !!! */
    md_hd_struct[minor].start_sect=0;
    md_hd_struct[minor].nr_sects=md_size[minor]<<1;

    /* It would be better to have a per-md-dev read_ahead. Currently,
       we only use the smallest read_ahead among md-attached devices */

    current_ra=read_ahead[MD_MAJOR];
    
    for (index=0; index<md_dev[minor].nb_dev; index++)
    {
      if (current_ra>read_ahead[MAJOR(devices[minor][index].dev)])
	current_ra=read_ahead[MAJOR(devices[minor][index].dev)];

      devices[minor][index].fault_count=0;
      devices[minor][index].invalid=VALID;
    }

    read_ahead[MD_MAJOR]=current_ra;

    printk ("START_DEV md%x %s\n", minor, md_dev[minor].pers->name);
    break;

    case STOP_MD:
    if (inode->i_count>1 || md_dev[minor].busy>1) /* ioctl : one open channel */
    {
      printk ("STOP_MD md%x failed : i_count=%d, busy=%d\n", minor, inode->i_count, md_dev[minor].busy);
      return -EBUSY;
    }

    if (md_dev[minor].pers)
    {
      /*  The device won't exist anymore -> flush it now */
      fsync_dev (inode->i_rdev);
      invalidate_buffers (inode->i_rdev);
      md_dev[minor].pers->stop (minor, md_dev+minor);
    }

    /* Remove locks. */
    for (index=0; index<md_dev[minor].nb_dev; index++)
      clear_inode (devices[minor][index].inode);

    md_dev[minor].nb_dev=md_size[minor]=0;
    md_dev[minor].pers=NULL;

    set_ra ();			/* calculate new read_ahead */
    
    printk ("STOP_DEV md%x\n", minor);
    break;

#if defined(CONFIG_MD_SUPPORT_RAID1) || defined(CONFIG_MD_SUPPORT_RAID5)
    case MD_INVALID:
    dev=to_kdev_t ((dev_t) arg);
    if (!(err=md_valid_device (minor, dev, INVALID_ALWAYS)))
      printk ("md%d : %s disabled\n", minor, partition_name (dev));

    return (err);

    case MD_VALID:
    dev=to_kdev_t ((dev_t) arg);
    if (!(err=md_valid_device (minor, dev, VALID)))
      printk ("md%d : %s enabled\n", minor, partition_name (dev));

    return (err);
#endif
    
    case BLKGETSIZE:   /* Return device size */
    if  (!arg)  return -EINVAL;
    err=verify_area (VERIFY_WRITE, (long *) arg, sizeof(long));
    if (err)
      return err;
    put_user (md_hd_struct[MINOR(inode->i_rdev)].nr_sects, (long *) arg);
    break;

    case BLKFLSBUF:
    fsync_dev (inode->i_rdev);
    invalidate_buffers (inode->i_rdev);
    break;

    case BLKRASET:
    if (arg > 0xff)
      return -EINVAL;
    read_ahead[MAJOR(inode->i_rdev)] = arg;
    return 0;
    
    case BLKRAGET:
    if  (!arg)  return -EINVAL;
    err=verify_area (VERIFY_WRITE, (long *) arg, sizeof(long));
    if (err)
      return err;
    put_user (read_ahead[MAJOR(inode->i_rdev)], (long *) arg);
    break;

    case HDIO_GETGEO:
    if (!loc)  return -EINVAL;
    err = verify_area(VERIFY_WRITE, loc, sizeof(*loc));
    if (err)
      return err;
    put_user (2, (char *) &loc->heads);
    put_user (4, (char *) &loc->sectors);
    put_user (md_hd_struct[minor].nr_sects/8, (short *) &loc->cylinders);
    put_user (md_hd_struct[MINOR(inode->i_rdev)].start_sect,
		(long *) &loc->start);
    break;
    
    RO_IOCTLS(inode->i_rdev,arg);
    
    default:
    printk ("Unknown md_ioctl %d\n", cmd);
    return -EINVAL;
  }

  return (0);
}


static int md_open (struct inode *inode, struct file *file)
{
  int minor=MINOR(inode->i_rdev);

  md_dev[minor].busy++;
  return (0);			/* Always succeed */
}


static void md_release (struct inode *inode, struct file *file)
{
  int minor=MINOR(inode->i_rdev);

  sync_dev (inode->i_rdev);
  md_dev[minor].busy--;
}


static struct file_operations md_fops=
{
  NULL,
  block_read,
  block_write,
  NULL,
  NULL,
  md_ioctl,
  NULL,
  md_open,
  md_release,
  block_fsync
};


static inline int remap_request (int minor, struct request *req)
{
  if (!md_dev[minor].pers)
  {
    printk ("Oops ! md%d not running, giving up !\n", minor);
    return -1;
  }

  return (md_dev[minor].pers->map(minor, md_dev+minor, req));
}

static void do_md_request (void)
{
  int minor;
  long flags;
  struct request *req;
    
  while (1)
  {
#ifdef MD_COUNT_SIZE
    int reqsize, chunksize;
#endif

    save_flags (flags);
    cli ();
    req = blk_dev[MD_MAJOR].current_request;
    if (!req || (req->rq_status == RQ_INACTIVE))
    {
      restore_flags (flags);
      return;
    }
    
#ifdef MD_COUNT_SIZE
    reqsize=req->nr_sectors>>1;
    chunksize=1 << FACTOR_SHIFT(FACTOR(md_dev+MINOR(req->rq_dev)));
    if (reqsize==chunksize) (md_dev+MINOR(req->rq_dev))->equal_count++;
    if (reqsize<chunksize) (md_dev+MINOR(req->rq_dev))->smallest_count++;
    if (reqsize>chunksize) (md_dev+MINOR(req->rq_dev))->biggest_count++;
#endif
    
    blk_dev[MD_MAJOR].current_request = req->next;
    restore_flags (flags);

    minor = MINOR(req->rq_dev);
    if ((MAJOR(req->rq_dev) != MD_MAJOR) || (minor >= MAX_REAL))
    {
      printk("md: bad device: %s\n", kdevname(req->rq_dev));
      end_request(0, req);
      continue;
    }

    switch (remap_request (minor, req))
    {
      case REDIRECTED_BHREQ:	/* All right, redirection was successful */
      req->rq_status=RQ_INACTIVE;
      wake_up (&wait_for_request);
      break;

      case REDIRECTED_REQ:
      break;			/* Redirected whole request (for swapping) */
      
      case REDIRECT_FAILED:	/* Swap redirection failed in RAID-[15] */
      end_request (0, req);
      break;
      
      default:
      printk ("remap_request returned strange value !\n");
    }
  }
}

extern struct semaphore request_lock;

void make_md_request (struct request *pending, int n)
{
  int i, j, max_req, major=0, rw, found;
  kdev_t dev;
  struct buffer_head *bh;
  struct request *req;
  long flags;
  
  down (&request_lock);
  save_flags (flags);
  cli();

  for (i=0; i<n; i++)
  {
    if (!pending[i].bh)
      continue;

    found=0;
    rw=pending[i].cmd;
    bh=pending[i].bh;
    major=MAJOR(dev=pending[i].rq_dev);
    
    max_req = (rw == READ) ? NR_REQUEST : ((NR_REQUEST*2)/3);
 
    if ((   major == IDE0_MAJOR	/* same as HD_MAJOR */
	 || major == IDE1_MAJOR
	 || major == SCSI_DISK_MAJOR
	 || major == IDE2_MAJOR
	 || major == IDE3_MAJOR)
	&& (req = blk_dev[major].current_request))
    {
      /*
       * Thanx to Gadi Oxman <gadio@netvision.net.il>
       * (He reads my own code better than I do... ;-)
       */
      if (major != SCSI_DISK_MAJOR)
	req = req->next;

      while (req && !found)
      {
	if (req->rq_status!=RQ_ACTIVE && &blk_dev[major].plug!=req)
	  printk ("Saw bad status request !\n");

	if (req->rq_dev == dev &&
	    !req->sem &&
	    req->cmd == rw &&
	    req->sector + req->nr_sectors == pending[i].sector &&
	    (req->nr_sectors + pending[i].nr_sectors) < 245)
	{
	  req->bhtail->b_reqnext = bh;
	  req->bhtail = pending[i].bhtail;
	  req->nr_sectors += pending[i].nr_sectors;
	  found=1;
	  continue;
	}
	
	if (!found &&
	    req->rq_dev == dev &&
	    !req->sem &&
	    req->cmd == rw &&
	    req->sector - pending[i].nr_sectors == pending[i].sector &&
	    (req->nr_sectors + pending[i].nr_sectors) < 245)
	{
	  req->nr_sectors += pending[i].nr_sectors;
	  pending[i].bhtail->b_reqnext = req->bh;
	  req->buffer = bh->b_data;
	  req->current_nr_sectors = bh->b_size >> 9;
	  req->sector = pending[i].sector;
	  req->bh = bh;
	  found=1;
	  continue;
	}    

	req = req->next;
      }
    }

    if (found)
      continue;

    up (&request_lock);
    sti ();
    req=get_md_request (max_req, dev);
    
    /* Build it up... */
    req->cmd = rw;
    req->errors = 0;
#if defined (CONFIG_MD_SUPPORT_RAID1)
    req->shared_count = 0;
#endif
    req->sector = pending[i].sector;
    req->nr_sectors = pending[i].nr_sectors;
    req->current_nr_sectors = bh->b_size >> 9;
    req->buffer = bh->b_data;
    req->sem = NULL;
    req->bh = bh;
    req->bhtail = pending[i].bhtail;
    req->next = NULL;

    add_request (blk_dev + MAJOR(dev), req);
    down (&request_lock);
    cli ();
  }

  up (&request_lock);
  restore_flags (flags);
  for (j=0; j<n; j++)
  {
    if (!pending[j].bh)
      continue;
    
    pending[j].bh=NULL;
  }
}


static struct symbol_table md_symbol_table=
{
#include <linux/symtab_begin.h>

  X(devices),
  X(md_size),
  X(add_request),
  X(make_md_request),

#ifdef CONFIG_MD_SUPPORT_RAID1
  X(support_for_raid1),
#endif

#ifdef CONFIG_MD_SUPPORT_RAID5
  X(support_for_raid5),
#endif

  X(register_md_personality),
  X(unregister_md_personality),
  X(partition_name),

#if defined(CONFIG_MD_SUPPORT_RAID1) || defined(CONFIG_MD_SUPPORT_RAID5)
  X(md_valid_device),
  X(md_can_reemit),
#endif

#include <linux/symtab_end.h>
};


static void md_geninit (struct gendisk *gdisk)
{
  int i;
  
  for(i=0;i<MAX_MD_DEV;i++)
  {
    md_blocksizes[i] = 1024;
    md_gendisk.part[i].start_sect=-1;
    md_dev[i].pers=NULL;
#ifdef MD_COUNT_SIZES
    md_dev[i].smallest_count=md_dev[i].biggest_count=md_dev[i].equal_count=0;
#endif
  }

  blksize_size[MAJOR_NR] = md_blocksizes;
  register_symtab (&md_symbol_table);

  proc_register(&proc_root,
		&(struct proc_dir_entry)
	      {
		PROC_MD, 6, "mdstat",
		S_IFREG | S_IRUGO, 1, 0, 0,
	      });
}


int get_md_status (char *page)
{
  int sz=0, i, j;

  sz+=sprintf( page+sz, "Personalities : ");
  for (i=0; i<MAX_PERSONALITY; i++)
    if (pers[i])
      sz+=sprintf (page+sz, "[%d %s] ", i, pers[i]->name);

  page[sz-1]='\n';

  sz+=sprintf (page+sz, "read_ahead ");
  if (read_ahead[MD_MAJOR]==INT_MAX)
    sz+=sprintf (page+sz, "not set\n");
  else
    sz+=sprintf (page+sz, "%d sectors\n", read_ahead[MD_MAJOR]);
  
  for (i=0; i<MAX_MD_DEV; i++)
  {
    sz+=sprintf (page+sz, "md%d : %sactive", i, md_dev[i].pers ? "" : "in");

    if (md_dev[i].pers)
      sz+=sprintf (page+sz, " %s", md_dev[i].pers->name);

    for (j=0; j<md_dev[i].nb_dev; j++)
      sz+=sprintf (page+sz, " %s%s%s",
		   (devices[i][j].invalid==VALID) ? "" : "(",
		   partition_name(devices[i][j].dev),
		   (devices[i][j].invalid==VALID) ? "" : ")");
    
    if (md_dev[i].nb_dev)
      sz+=sprintf (page+sz, " %d blocks", md_size[i]);

    if (!md_dev[i].pers)
    {
      sz+=sprintf (page+sz, "\n");
      continue;
    }

    if (md_dev[i].pers->max_invalid_dev)
      sz+=sprintf (page+sz, " maxfault=%ld", MAX_FAULT(md_dev+i));

    if (md_dev[i].pers != pers[(LINEAR>>PERSONALITY_SHIFT)])
    {
      sz+=sprintf (page+sz, " %dk chunks", 1<<FACTOR_SHIFT(FACTOR(md_dev+i)));
#ifdef MD_COUNT_SIZES
      sz+=sprintf (page+sz, " (%d/%d/%d)",
		   md_dev[i].smallest_count,
		   md_dev[i].equal_count,
		   md_dev[i].biggest_count);
#endif
    }
    sz+=sprintf (page+sz, "\n");
    sz+=md_dev[i].pers->status (page+sz, i, md_dev+i);
  }

  return (sz);
}

#if defined(CONFIG_MD_SUPPORT_RAID1) || defined(CONFIG_MD_SUPPORT_RAID5)

int md_valid_device (int minor, kdev_t dev, int mode)
{
  int i;

  for (i=0; i<md_dev[minor].nb_dev; i++)
    if (devices[minor][i].dev==dev)
      break;

  if (i>md_dev[minor].nb_dev)
  {
    printk ("Oops, dev %04x not found in md_valid_device\n", dev);
    return -EINVAL;
  }

  switch (mode)
  {
    case VALID:
    /* Don't consider INVALID_NEXT as a real invalidation.
       Maybe that's not the good way to treat such a thing,
       we'll see. */
    if (devices[minor][i].invalid==INVALID_ALWAYS)
    {
      devices[minor][i].fault_count=0; /* reset fault count */
      if (md_dev[minor].invalid_dev_count)
	md_dev[minor].invalid_dev_count--;
    }
    break;

    case INVALID:
    if (devices[minor][i].invalid != VALID )
      return 0;			/* Don't invalidate twice */
    
    if (++devices[minor][i].fault_count > MAX_FAULT(md_dev+minor) &&
	MAX_FAULT(md_dev+minor)!=0xFF)
    {
      /* We cannot tolerate this fault.
	 So sing a song, and say GoodBye to this device... */
      
      mode=INVALID_ALWAYS;
      md_dev[minor].invalid_dev_count++;
    }
    else
      /* FIXME :
	 If we reached the max_invalid_dev count, doing one
	 more invalidation will kill the md_dev. So we choose
	 not to invalid the physical dev in such a case. But
	 next access will probably fail... */
      if (md_dev[minor].invalid_dev_count<=md_dev[minor].pers->max_invalid_dev)
	mode=INVALID_NEXT;
      else
	mode=VALID;
    break;

    case INVALID_ALWAYS:	/* Only used via MD_INVALID ioctl */
    md_dev[minor].invalid_dev_count++;
  }
  
  devices[minor][i].invalid=mode;
  return 0;
}


int md_can_reemit (int minor)
{
  /* FIXME :
     If the device is raid-1 (md_dev[minor].pers->max_invalid_dev=-1),
     always pretend that we can reemit the request.
     Problem : if the 2 devices in the pair are dead, will loop
     forever. Maybe having a per-personality can_reemit function would
     help. */

  if (!md_dev[minor].pers)
    return (0);
  
  return(md_dev[minor].pers->max_invalid_dev &&
	 ((md_dev[minor].pers->max_invalid_dev==-1) ?
	 1 :
	 md_dev[minor].invalid_dev_count<=md_dev[minor].pers->max_invalid_dev));
}

#endif

int register_md_personality (int p_num, struct md_personality *p)
{
  int i=(p_num >> PERSONALITY_SHIFT);

  if (i >= MAX_PERSONALITY)
    return -EINVAL;

  if (pers[i])
    return -EBUSY;
  
  pers[i]=p;
  printk ("%s personality registered\n", p->name);
  return 0;
}

int unregister_md_personality (int p_num)
{
  int i=(p_num >> PERSONALITY_SHIFT);

  if (i >= MAX_PERSONALITY)
    return -EINVAL;

  printk ("%s personality unregistered\n", pers[i]->name);
  pers[i]=NULL;
  return 0;
} 

void linear_init (void);
void raid0_init (void);
void raid1_init (void);
void raid5_init (void);

int md_init (void)
{
  printk ("md driver %s MAX_MD_DEV=%d, MAX_REAL=%d\n", MD_VERSION, MAX_MD_DEV, MAX_REAL);

  if (register_blkdev (MD_MAJOR, "md", &md_fops))
  {
    printk ("Unable to get major %d for md\n", MD_MAJOR);
    return (-1);
  }

  blk_dev[MD_MAJOR].request_fn=DEVICE_REQUEST;
  blk_dev[MD_MAJOR].current_request=NULL;
  read_ahead[MD_MAJOR]=INT_MAX;
  md_gendisk.next=gendisk_head;

  gendisk_head=&md_gendisk;

#ifdef CONFIG_MD_LINEAR
  linear_init ();
#endif
#ifdef CONFIG_MD_STRIPED
  raid0_init ();
#endif
#ifdef CONFIG_MD_RAID1
  raid1_init ();
#endif
#ifdef CONFIG_MD_RAID5
  raid5_init ();
#endif
  
  return (0);
}
