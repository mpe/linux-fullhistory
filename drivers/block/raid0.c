
/*
   raid0.c : Multiple Devices driver for Linux
             Copyright (C) 1994-96 Marc ZYNGIER
	     <zyngier@ufr-info-p7.ibp.fr> or
	     <maz@gloups.fdn.fr>

   RAID-0 management functions.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
   
   You should have received a copy of the GNU General Public License
   (for example /usr/src/linux/COPYING); if not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  
*/

#include <linux/module.h>
#include <linux/md.h>
#include <linux/raid0.h>
#include <linux/malloc.h>

#define MAJOR_NR MD_MAJOR
#define MD_DRIVER
#define MD_PERSONALITY

static void create_strip_zones (int minor, struct md_dev *mddev)
{
  int i, j, c=0;
  int current_offset=0;
  struct real_dev *smallest_by_zone;
  struct raid0_data *data=(struct raid0_data *) mddev->private;
  
  data->nr_strip_zones=1;
  
  for (i=1; i<mddev->nb_dev; i++)
  {
    for (j=0; j<i; j++)
      if (mddev->devices[i].size==mddev->devices[j].size)
      {
	c=1;
	break;
      }

    if (!c)
      data->nr_strip_zones++;

    c=0;
  }

  data->strip_zone=kmalloc (sizeof(struct strip_zone)*data->nr_strip_zones,
			      GFP_KERNEL);

  data->smallest=NULL;
  
  for (i=0; i<data->nr_strip_zones; i++)
  {
    data->strip_zone[i].dev_offset=current_offset;
    smallest_by_zone=NULL;
    c=0;

    for (j=0; j<mddev->nb_dev; j++)
      if (mddev->devices[j].size>current_offset)
      {
	data->strip_zone[i].dev[c++]=mddev->devices+j;
	if (!smallest_by_zone ||
	    smallest_by_zone->size > mddev->devices[j].size)
	  smallest_by_zone=mddev->devices+j;
      }

    data->strip_zone[i].nb_dev=c;
    data->strip_zone[i].size=(smallest_by_zone->size-current_offset)*c;

    if (!data->smallest ||
	data->smallest->size > data->strip_zone[i].size)
      data->smallest=data->strip_zone+i;

    data->strip_zone[i].zone_offset=i ? (data->strip_zone[i-1].zone_offset+
					   data->strip_zone[i-1].size) : 0;
    current_offset=smallest_by_zone->size;
  }
}

static int raid0_run (int minor, struct md_dev *mddev)
{
  int cur=0, i=0, size, zone0_size, nb_zone;
  struct raid0_data *data;

  MOD_INC_USE_COUNT;

  mddev->private=kmalloc (sizeof (struct raid0_data), GFP_KERNEL);
  data=(struct raid0_data *) mddev->private;
  
  create_strip_zones (minor, mddev);

  nb_zone=data->nr_zones=
    md_size[minor]/data->smallest->size +
    (md_size[minor]%data->smallest->size ? 1 : 0);
  
  data->hash_table=kmalloc (sizeof (struct raid0_hash)*nb_zone, GFP_KERNEL);

  size=data->strip_zone[cur].size;

  i=0;
  while (cur<data->nr_strip_zones)
  {
    data->hash_table[i].zone0=data->strip_zone+cur;

    if (size>=data->smallest->size)/* If we completely fill the slot */
    {
      data->hash_table[i++].zone1=NULL;
      size-=data->smallest->size;

      if (!size)
      {
	if (++cur==data->nr_strip_zones) continue;
	size=data->strip_zone[cur].size;
      }

      continue;
    }

    if (++cur==data->nr_strip_zones) /* Last dev, set unit1 as NULL */
    {
      data->hash_table[i].zone1=NULL;
      continue;
    }

    zone0_size=size;		/* Here, we use a 2nd dev to fill the slot */
    size=data->strip_zone[cur].size;
    data->hash_table[i++].zone1=data->strip_zone+cur;
    size-=(data->smallest->size - zone0_size);
  }

  return (0);
}


static int raid0_stop (int minor, struct md_dev *mddev)
{
  struct raid0_data *data=(struct raid0_data *) mddev->private;

  kfree (data->hash_table);
  kfree (data->strip_zone);
  kfree (data);

  MOD_DEC_USE_COUNT;
  return 0;
}

/*
 * FIXME - We assume some things here :
 * - requested buffers NEVER bigger than chunk size,
 * - requested buffers NEVER cross stripes limits.
 * Of course, those facts may not be valid anymore (and surely won't...)
 * Hey guys, there's some work out there ;-)
 */
static int raid0_map (struct md_dev *mddev, kdev_t *rdev,
		      unsigned long *rsector, unsigned long size)
{
  struct raid0_data *data=(struct raid0_data *) mddev->private;
  static struct raid0_hash *hash;
  struct strip_zone *zone;
  struct real_dev *tmp_dev;
  int blk_in_chunk, factor, chunk, chunk_size;
  long block, rblock;

  factor=FACTOR(mddev);
  chunk_size=(1UL << FACTOR_SHIFT(factor));
  block=*rsector >> 1;
  hash=data->hash_table+(block/data->smallest->size);

  /* Sanity check */
  if ((chunk_size*2)<(*rsector % (chunk_size*2))+size)
  {
    printk ("raid0_convert : can't convert block across chunks or bigger than %dk %ld %ld\n", chunk_size, *rsector, size);
    return (-1);
  }
  
  if (block >= (hash->zone0->size +
		hash->zone0->zone_offset))
  {
    if (!hash->zone1)
    {
      printk ("raid0_convert : hash->zone1==NULL for block %ld\n", block);
      return (-1);
    }
    
    zone=hash->zone1;
  }
  else
    zone=hash->zone0;
    
  blk_in_chunk=block & (chunk_size -1);
  chunk=(block - zone->zone_offset) / (zone->nb_dev<<FACTOR_SHIFT(factor));
  tmp_dev=zone->dev[(block >> FACTOR_SHIFT(factor)) % zone->nb_dev];
  rblock=(chunk << FACTOR_SHIFT(factor)) + blk_in_chunk + zone->dev_offset;
  
  *rdev=tmp_dev->dev;
  *rsector=rblock<<1;

  return (0);
}

			   
static int raid0_status (char *page, int minor, struct md_dev *mddev)
{
  int sz=0;
#undef MD_DEBUG
#ifdef MD_DEBUG
  int j, k;
  struct raid0_data *data=(struct raid0_data *) mddev->private;
  
  sz+=sprintf (page+sz, "      ");
  for (j=0; j<data->nr_zones; j++)
  {
    sz+=sprintf (page+sz, "[z%d",
		 data->hash_table[j].zone0-data->strip_zone);
    if (data->hash_table[j].zone1)
      sz+=sprintf (page+sz, "/z%d] ",
		   data->hash_table[j].zone1-data->strip_zone);
    else
      sz+=sprintf (page+sz, "] ");
  }
  
  sz+=sprintf (page+sz, "\n");
  
  for (j=0; j<data->nr_strip_zones; j++)
  {
    sz+=sprintf (page+sz, "      z%d=[", j);
    for (k=0; k<data->strip_zone[j].nb_dev; k++)
      sz+=sprintf (page+sz, "%s/",
		   partition_name(data->strip_zone[j].dev[k]->dev));
    sz--;
    sz+=sprintf (page+sz, "] zo=%d do=%d s=%d\n",
		 data->strip_zone[j].zone_offset,
		 data->strip_zone[j].dev_offset,
		 data->strip_zone[j].size);
  }
#endif
  return sz;
}


static struct md_personality raid0_personality=
{
  "raid0",
  raid0_map,
  raid0_run,
  raid0_stop,
  raid0_status,
  NULL,				/* no ioctls */
  0
};


#ifndef MODULE

void raid0_init (void)
{
  register_md_personality (RAID0, &raid0_personality);
}

#else

int init_module (void)
{
  return (register_md_personality (RAID0, &raid0_personality));
}

void cleanup_module (void)
{
  unregister_md_personality (RAID0);
}

#endif
