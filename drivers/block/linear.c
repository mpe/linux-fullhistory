
/*
   linear.c : Multiple Devices driver for Linux
              Copyright (C) 1994-96 Marc ZYNGIER
	      <zyngier@ufr-info-p7.ibp.fr> or
	      <maz@gloups.fdn.fr>

   Linear mode management functions.

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
#include <linux/malloc.h>
#include <linux/init.h>

#include "linear.h"

#define MAJOR_NR MD_MAJOR
#define MD_DRIVER
#define MD_PERSONALITY

static int linear_run (int minor, struct md_dev *mddev)
{
  int cur=0, i, size, dev0_size, nb_zone;
  struct linear_data *data;

  MOD_INC_USE_COUNT;

  mddev->private=kmalloc (sizeof (struct linear_data), GFP_KERNEL);
  data=(struct linear_data *) mddev->private;

  /*
     Find out the smallest device. This was previously done
     at registry time, but since it violates modularity,
     I moved it here... Any comment ? ;-)
   */

  data->smallest=mddev->devices;
  for (i=1; i<mddev->nb_dev; i++)
    if (data->smallest->size > mddev->devices[i].size)
      data->smallest=mddev->devices+i;
  
  nb_zone=data->nr_zones=
    md_size[minor]/data->smallest->size +
    (md_size[minor]%data->smallest->size ? 1 : 0);
  
  data->hash_table=kmalloc (sizeof (struct linear_hash)*nb_zone, GFP_KERNEL);

  size=mddev->devices[cur].size;

  i=0;
  while (cur<mddev->nb_dev)
  {
    data->hash_table[i].dev0=mddev->devices+cur;

    if (size>=data->smallest->size) /* If we completely fill the slot */
    {
      data->hash_table[i++].dev1=NULL;
      size-=data->smallest->size;

      if (!size)
      {
	if (++cur==mddev->nb_dev) continue;
	size=mddev->devices[cur].size;
      }

      continue;
    }

    if (++cur==mddev->nb_dev) /* Last dev, set dev1 as NULL */
    {
      data->hash_table[i].dev1=NULL;
      continue;
    }

    dev0_size=size;		/* Here, we use a 2nd dev to fill the slot */
    size=mddev->devices[cur].size;
    data->hash_table[i++].dev1=mddev->devices+cur;
    size-=(data->smallest->size - dev0_size);
  }

  return 0;
}

static int linear_stop (int minor, struct md_dev *mddev)
{
  struct linear_data *data=(struct linear_data *) mddev->private;
  
  kfree (data->hash_table);
  kfree (data);

  MOD_DEC_USE_COUNT;

  return 0;
}


static int linear_map (struct md_dev *mddev, kdev_t *rdev,
		       unsigned long *rsector, unsigned long size)
{
  struct linear_data *data=(struct linear_data *) mddev->private;
  struct linear_hash *hash;
  struct real_dev *tmp_dev;
  long block;

  block=*rsector >> 1;
  hash=data->hash_table+(block/data->smallest->size);
  
  if (block >= (hash->dev0->size + hash->dev0->offset))
  {
    if (!hash->dev1)
    {
      printk ("linear_map : hash->dev1==NULL for block %ld\n", block);
      return (-1);
    }
    
    tmp_dev=hash->dev1;
  }
  else
    tmp_dev=hash->dev0;
    
  if (block >= (tmp_dev->size + tmp_dev->offset) || block < tmp_dev->offset)
    printk ("Block %ld out of bounds on dev %s size %d offset %d\n",
	    block, kdevname(tmp_dev->dev), tmp_dev->size, tmp_dev->offset);
  
  *rdev=tmp_dev->dev;
  *rsector=(block-(tmp_dev->offset)) << 1;

  return (0);
}

static int linear_status (char *page, int minor, struct md_dev *mddev)
{
  int sz=0;

#undef MD_DEBUG
#ifdef MD_DEBUG
  int j;
  struct linear_data *data=(struct linear_data *) mddev->private;
  
  sz+=sprintf (page+sz, "      ");
  for (j=0; j<data->nr_zones; j++)
  {
    sz+=sprintf (page+sz, "[%s",
		 partition_name (data->hash_table[j].dev0->dev));

    if (data->hash_table[j].dev1)
      sz+=sprintf (page+sz, "/%s] ",
		   partition_name(data->hash_table[j].dev1->dev));
    else
      sz+=sprintf (page+sz, "] ");
  }

  sz+=sprintf (page+sz, "\n");
#endif
  sz+=sprintf (page+sz, " %dk rounding", 1<<FACTOR_SHIFT(FACTOR(mddev)));
  return sz;
}


static struct md_personality linear_personality=
{
  "linear",
  linear_map,
  NULL,
  NULL,
  linear_run,
  linear_stop,
  linear_status,
  NULL,				/* no ioctls */
  0
};


#ifndef MODULE

__initfunc(void linear_init (void))
{
  register_md_personality (LINEAR, &linear_personality);
}

#else

int init_module (void)
{
  return (register_md_personality (LINEAR, &linear_personality));
}

void cleanup_module (void)
{
  unregister_md_personality (LINEAR);
}

#endif
