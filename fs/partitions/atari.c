/*
 *  fs/partitions/atari.c
 *
 *  Code extracted from drivers/block/genhd.c
 *
 *  Copyright (C) 1991-1998  Linus Torvalds
 *  Re-organised Feb 1998 Russell King
 */

#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/blk.h>

#include <asm/system.h>

#include "check.h"
#include "atari.h"

/* ++guenther: this should be settable by the user ("make config")?.
 */
#define ICD_PARTS

int atari_partition (struct gendisk *hd, kdev_t dev,
		     unsigned long first_sector, int first_part_minor)
{
  int minor = first_part_minor, m_lim = first_part_minor + hd->max_p;
  struct buffer_head *bh;
  struct rootsector *rs;
  struct partition_info *pi;
  ulong extensect;
  unsigned int psum;
  int i;
#ifdef ICD_PARTS
  int part_fmt = 0; /* 0:unknown, 1:AHDI, 2:ICD/Supra */
#endif

  bh = bread (dev, 0, get_ptable_blocksize(dev));
  if (!bh) {
      printk (" unable to read block 0 (partition table)\n");
      return -1;
  }

  /* Verify this is an Atari rootsector: */
  psum = 0;
  for (i=0;i<256;i++) {
    psum+=ntohs(((__u16 *) (bh->b_data))[i]);
  }
  if ((psum & 0xFFFF) != 0x1234) {
    brelse(bh);
    return 0;
  }

  rs = (struct rootsector *) bh->b_data;
  pi = &rs->part[0];
  printk (" AHDI");
  for (; pi < &rs->part[4] && minor < m_lim; minor++, pi++)
    {
      if (pi->flg & 1)
	/* active partition */
	{
	  if (memcmp (pi->id, "XGM", 3) == 0)
	    /* extension partition */
	    {
	      struct rootsector *xrs;
	      struct buffer_head *xbh;
	      ulong partsect;

#ifdef ICD_PARTS
	      part_fmt = 1;
#endif
	      printk(" XGM<");
	      partsect = extensect = ntohl(pi->st);
	      while (1)
		{
		  xbh = bread (dev, partsect / 2, get_ptable_blocksize(dev));
		  if (!xbh)
		    {
		      printk (" block %ld read failed\n", partsect);
		      brelse(bh);
		      return 0;
		    }
		  if (partsect & 1)
		    xrs = (struct rootsector *) &xbh->b_data[512];
		  else
		    xrs = (struct rootsector *) &xbh->b_data[0];

		  /* ++roman: sanity check: bit 0 of flg field must be set */
		  if (!(xrs->part[0].flg & 1)) {
		    printk( "\nFirst sub-partition in extended partition is not valid!\n" );
		    break;
		  }

		  add_gd_partition(hd, minor, partsect + ntohl(xrs->part[0].st),
				ntohl(xrs->part[0].siz));

		  if (!(xrs->part[1].flg & 1)) {
		    /* end of linked partition list */
		    brelse( xbh );
		    break;
		  }
		  if (memcmp( xrs->part[1].id, "XGM", 3 ) != 0) {
		    printk( "\nID of extended partition is not XGM!\n" );
		    brelse( xbh );
		    break;
		  }

		  partsect = ntohl(xrs->part[1].st) + extensect;
		  brelse (xbh);
		  minor++;
		  if (minor >= m_lim) {
		    printk( "\nMaximum number of partitions reached!\n" );
		    break;
		  }
		}
	      printk(" >");
	    }
	  else
	    {
	      /* we don't care about other id's */
	      add_gd_partition (hd, minor, ntohl(pi->st), ntohl(pi->siz));
	    }
	}
    }
#ifdef ICD_PARTS
  if ( part_fmt!=1 ) /* no extended partitions -> test ICD-format */
  {
    pi = &rs->icdpart[0];
    /* sanity check: no ICD format if first partition invalid */
    if (memcmp (pi->id, "GEM", 3) == 0 ||
        memcmp (pi->id, "BGM", 3) == 0 ||
        memcmp (pi->id, "LNX", 3) == 0 ||
        memcmp (pi->id, "SWP", 3) == 0 ||
        memcmp (pi->id, "RAW", 3) == 0 )
    {
      printk(" ICD<");
      for (; pi < &rs->icdpart[8] && minor < m_lim; minor++, pi++)
      {
        /* accept only GEM,BGM,RAW,LNX,SWP partitions */
        if (pi->flg & 1 && 
            (memcmp (pi->id, "GEM", 3) == 0 ||
             memcmp (pi->id, "BGM", 3) == 0 ||
             memcmp (pi->id, "LNX", 3) == 0 ||
             memcmp (pi->id, "SWP", 3) == 0 ||
             memcmp (pi->id, "RAW", 3) == 0) )
        {
          part_fmt = 2;
	  add_gd_partition (hd, minor, ntohl(pi->st), ntohl(pi->siz));
        }
      }
      printk(" >");
    }
  }
#endif
  brelse (bh);

  printk ("\n");

  return 1;
}

