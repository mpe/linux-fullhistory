/*
 * linux/atari/atapart.c
 *
 * Atari partition checking driver for 680x0 Linux
 * Written by Andreas Schwab (schwab@ls5.informatik.uni-dortmund.de)
 *
 * 5/3/94 Roman Hodek:
 *   Added some sanity checks
 *   Linux device names start from 1 not 0, so I changed the initial value
 *   for i to 1.
 *
 * 10/09/94 Guenther Kelleter:
 *   Added support for ICD/Supra partition info.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 */

#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>

#include <asm/atari_rootsec.h>

/* ++guenther: this should be settable by the user ("make config")?.
 */
#define ICD_PARTS

extern int current_minor;

void
atari_check_partition (struct gendisk *hd, unsigned int dev)
{
  int i, minor = current_minor, m_lim = current_minor + hd->max_p;
  struct buffer_head *bh;
  struct rootsector *rs;
  struct partition_info *pi;
  ulong extensect;
#ifdef ICD_PARTS
  int part_fmt = 0; /* 0:unknown, 1:AHDI, 2:ICD/Supra */
#endif

  bh = bread (dev, 0, 1024);
  if (!bh)
    {
      printk (" unable to read block 0\n");
      return;
    }

  rs = (struct rootsector *) bh->b_data;
  printk ("  %s%c:", hd->major_name, 'a' + (minor >> hd->minor_shift));

  pi = &rs->part[0];
  for (i = 1; pi < &rs->part[4] && minor < m_lim; i++, minor++, pi++)
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
        partsect = extensect = pi->st;
        while (1)
      {
        xbh = bread (dev, partsect / 2, 1024);
        if (!xbh)
          {
            printk (" block %ld read failed\n", partsect);
            return;
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
		
        hd->part[minor].start_sect = partsect + xrs->part[0].st;
        hd->part[minor].nr_sects = xrs->part[0].siz;
        printk (" %s%c%d", hd->major_name,
            'a' + (minor >> hd->minor_shift), i);

		if (!(xrs->part[1].flg & 1)) {
			/* end of linked partition list */
			brelse( xbh );
			break;
		}
		if (memcmp( xrs->part[1].id, "XGM", 3 ) != 0) {
			printk( "\nID of extended partion is not XGM!\n" );
			brelse( xbh );
			break;
		}
		
		partsect = xrs->part[1].st + extensect;
        brelse (xbh);
        i++;
        minor++;
        if (minor >= m_lim) {
			printk( "\nMaximum number of partitions reached!\n" );
			break;
		}
      }
      }
    else
      {
        /* we don't care about other id's */
        hd->part[minor].start_sect = pi->st;
        hd->part[minor].nr_sects = pi->siz;
        printk (" %s%c%d", hd->major_name,
            'a' + (minor >> hd->minor_shift), i);

      }
  }
    }
#ifdef ICD_PARTS
  if ( part_fmt!=1 ) /* no extended partitions -> test ICD-format */
  {
    pi = &rs->icdpart[0];
    /* sanity check: no ICD format if first partion invalid */
    if (memcmp (pi->id, "GEM", 3) == 0 ||
        memcmp (pi->id, "BGM", 3) == 0 ||
        memcmp (pi->id, "RAW", 3) == 0 )
    {
      for (i = 1; pi < &rs->icdpart[8] && minor < m_lim; i++, minor++, pi++)
      {
        /* accept only GEM,BGM,RAW partitions */
        if (pi->flg & 1 && 
            (memcmp (pi->id, "GEM", 3) == 0 ||
             memcmp (pi->id, "BGM", 3) == 0 ||
             memcmp (pi->id, "RAW", 3) == 0) )
        {
          part_fmt = 2;
          hd->part[minor].start_sect = pi->st;
          hd->part[minor].nr_sects = pi->siz;
          printk (" %s%c%d", hd->major_name,
              'a' + (minor >> hd->minor_shift), i);
        }
      }
    }
  }
#endif
  brelse (bh);

  printk ("\n");
}

