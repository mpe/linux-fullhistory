/*
** z2ram - Amiga pseudo-driver to access 16bit-RAM in ZorroII space
**         as a block device, to be used as a RAM disk or swap space
** 
** Copyright (C) 1994 by Ingo Wilken (Ingo.Wilken@informatik.uni-oldenburg.de)
**
** ++Geert: support for zorro_unused_z2ram, better range checking
** ++roman: translate accesses via an array
** ++Milan: support for ChipRAM usage
** ++yambo: converted to 2.0 kernel
** ++yambo: modularized and support added for 3 minor devices including:
**          MAJOR  MINOR  DESCRIPTION
**          -----  -----  ----------------------------------------------
**          37     0       Use Zorro II and Chip ram
**          37     1       Use only Zorro II ram
**          37     2       Use only Chip ram
**          37     4-7     Use memory list entry 1-4 (first is 0)
** ++jskov: support for 1-4th memory list entry.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#define MAJOR_NR    Z2RAM_MAJOR

#include <linux/config.h>
#include <linux/major.h>
#include <linux/malloc.h>
#include <linux/blk.h>
#include <linux/init.h>

#if defined(MODULE)
#include <linux/module.h>
#endif

#include <asm/setup.h>
#include <asm/bitops.h>
#include <asm/amigahw.h>
#ifdef CONFIG_APUS
#include <asm/pgtable.h>
#include <asm/io.h>
#endif
#include <linux/zorro.h>


extern int num_memory;
extern struct mem_info memory[NUM_MEMINFO];

#define TRUE                  (1)
#define FALSE                 (0)

#define Z2MINOR_COMBINED      (0)
#define Z2MINOR_Z2ONLY        (1)
#define Z2MINOR_CHIPONLY      (2)
#define Z2MINOR_MEMLIST1      (4)
#define Z2MINOR_MEMLIST2      (5)
#define Z2MINOR_MEMLIST3      (6)
#define Z2MINOR_MEMLIST4      (7)
#define Z2MINOR_COUNT         (8) /* Move this down when adding a new minor */

#define Z2RAM_CHUNK1024       ( Z2RAM_CHUNKSIZE >> 10 )

static u_long *z2ram_map    = NULL;
static u_long z2ram_size    = 0;
static int z2_blocksizes[Z2MINOR_COUNT];
static int z2_sizes[Z2MINOR_COUNT];
static int z2_count         = 0;
static int chip_count       = 0;
static int list_count       = 0;
static int current_device   = -1;

static void
do_z2_request( void )
{
    u_long start, len, addr, size;

    while ( TRUE )
    {
	INIT_REQUEST;

	start = CURRENT->sector << 9;
	len  = CURRENT->current_nr_sectors << 9;

	if ( ( start + len ) > z2ram_size )
	{
	    printk( KERN_ERR DEVICE_NAME ": bad access: block=%ld, count=%ld\n",
		CURRENT->sector,
		CURRENT->current_nr_sectors);
	    end_request( FALSE );
	    continue;
	}

	if ( ( CURRENT->cmd != READ ) && ( CURRENT->cmd != WRITE ) )
	{
	    printk( KERN_ERR DEVICE_NAME ": bad command: %d\n", CURRENT->cmd );
	    end_request( FALSE );
	    continue;
	}

	while ( len ) 
	{
	    addr = start & Z2RAM_CHUNKMASK;
	    size = Z2RAM_CHUNKSIZE - addr;
	    if ( len < size )
		size = len;

	    addr += z2ram_map[ start >> Z2RAM_CHUNKSHIFT ];

	    if ( CURRENT->cmd == READ )
		memcpy( CURRENT->buffer, (char *)addr, size );
	    else
		memcpy( (char *)addr, CURRENT->buffer, size );

	    start += size;
	    len -= size;
	}

	end_request( TRUE );
    }
}

static void
get_z2ram( void )
{
    int i;

    for ( i = 0; i < Z2RAM_SIZE / Z2RAM_CHUNKSIZE; i++ )
    {
	if ( test_bit( i, zorro_unused_z2ram ) )
	{
	    z2_count++;
	    z2ram_map[ z2ram_size++ ] = 
		ZTWO_VADDR( Z2RAM_START ) + ( i << Z2RAM_CHUNKSHIFT );
	    clear_bit( i, zorro_unused_z2ram );
	}
    }

    return;
}

static void
get_chipram( void )
{

    while ( amiga_chip_avail() > ( Z2RAM_CHUNKSIZE * 4 ) )
    {
	chip_count++;
	z2ram_map[ z2ram_size ] =
	    (u_long)amiga_chip_alloc( Z2RAM_CHUNKSIZE );

	if ( z2ram_map[ z2ram_size ] == 0 )
	{
	    break;
	}

	z2ram_size++;
    }
	
    return;
}

static int
z2_open( struct inode *inode, struct file *filp )
{
    int device;
    int max_z2_map = ( Z2RAM_SIZE / Z2RAM_CHUNKSIZE ) *
	sizeof( z2ram_map[0] );
    int max_chip_map = ( amiga_chip_size / Z2RAM_CHUNKSIZE ) *
	sizeof( z2ram_map[0] );

    device = DEVICE_NR( inode->i_rdev );

    if ( current_device != -1 && current_device != device )
    {
	return -EBUSY;
    }

    if ( current_device == -1 )
    {
	z2_count   = 0;
	chip_count = 0;
	list_count = 0;
	z2ram_size = 0;

	/* Use a specific list entry. */
	if (device >= Z2MINOR_MEMLIST1 && device <= Z2MINOR_MEMLIST4) {
		int index = device - Z2MINOR_MEMLIST1 + 1;
		unsigned long size, paddr, vaddr;

		if (index >= num_memory) {
			printk( KERN_ERR DEVICE_NAME
				": no such entry in z2ram_map\n" );
			return -ENOMEM;
		}

		paddr = memory[index].addr;
		size = memory[index].size & ~(Z2RAM_CHUNKSIZE-1);

#ifdef __powerpc__
		/* FIXME: ioremap doesn't build correct memory tables. */
		{
			extern void* vmalloc (unsigned long);
			extern void vfree (void*);
			vfree(vmalloc (size));
		}

		vaddr = (unsigned long) __ioremap (paddr, size, 
						   _PAGE_WRITETHRU);

#else
		vaddr = kernel_map (paddr, size, KERNELMAP_FULL_CACHING,
				    NULL);
#endif
		z2ram_map = 
			kmalloc((size/Z2RAM_CHUNKSIZE)*sizeof(z2ram_map[0]),
				GFP_KERNEL);
		if ( z2ram_map == NULL )
		{
		    printk( KERN_ERR DEVICE_NAME
			": cannot get mem for z2ram_map\n" );
		    return -ENOMEM;
		}

		while (size) {
			z2ram_map[ z2ram_size++ ] = vaddr;
			size -= Z2RAM_CHUNKSIZE;
			vaddr += Z2RAM_CHUNKSIZE;
			list_count++;
		}

		if ( z2ram_size != 0 )
		    printk( KERN_INFO DEVICE_NAME
			": using %iK List Entry %d Memory\n",
			list_count * Z2RAM_CHUNK1024, index );
	} else

	switch ( device )
	{
	    case Z2MINOR_COMBINED:

		z2ram_map = kmalloc( max_z2_map + max_chip_map, GFP_KERNEL );
		if ( z2ram_map == NULL )
		{
		    printk( KERN_ERR DEVICE_NAME
			": cannot get mem for z2ram_map\n" );
		    return -ENOMEM;
		}

		get_z2ram();
		get_chipram();

		if ( z2ram_size != 0 )
		    printk( KERN_INFO DEVICE_NAME 
			": using %iK Zorro II RAM and %iK Chip RAM (Total %dK)\n",
			z2_count * Z2RAM_CHUNK1024,
			chip_count * Z2RAM_CHUNK1024,
			( z2_count + chip_count ) * Z2RAM_CHUNK1024 );

	    break;

    	    case Z2MINOR_Z2ONLY:
		z2ram_map = kmalloc( max_z2_map, GFP_KERNEL );
		if ( z2ram_map == NULL )
		{
		    printk( KERN_ERR DEVICE_NAME
			": cannot get mem for z2ram_map\n" );
		    return -ENOMEM;
		}

		get_z2ram();

		if ( z2ram_size != 0 )
		    printk( KERN_INFO DEVICE_NAME 
			": using %iK of Zorro II RAM\n",
			z2_count * Z2RAM_CHUNK1024 );

	    break;

	    case Z2MINOR_CHIPONLY:
		z2ram_map = kmalloc( max_chip_map, GFP_KERNEL );
		if ( z2ram_map == NULL )
		{
		    printk( KERN_ERR DEVICE_NAME
			": cannot get mem for z2ram_map\n" );
		    return -ENOMEM;
		}

		get_chipram();

		if ( z2ram_size != 0 )
		    printk( KERN_INFO DEVICE_NAME 
			": using %iK Chip RAM\n",
			chip_count * Z2RAM_CHUNK1024 );
		    
	    break;

	    default:
		return -ENODEV;
	}

	if ( z2ram_size == 0 )
	{
	    kfree( z2ram_map );
	    printk( KERN_NOTICE DEVICE_NAME
		": no unused ZII/Chip RAM found\n" );
	    return -ENOMEM;
	}

	current_device = device;
	z2ram_size <<= Z2RAM_CHUNKSHIFT;
	z2_sizes[ device ] = z2ram_size >> 10;
	blk_size[ MAJOR_NR ] = z2_sizes;
    }

#if defined(MODULE)
    MOD_INC_USE_COUNT;
#endif

    return 0;
}

static int
z2_release( struct inode *inode, struct file *filp )
{
    if ( current_device == -1 )
	return 0;     

    sync_dev( inode->i_rdev );

#if defined(MODULE)
    MOD_DEC_USE_COUNT;
#endif

    return 0;
}

static struct file_operations z2_fops =
{
	NULL,                   /* lseek - default */
	block_read,             /* read - general block-dev read */
	block_write,            /* write - general block-dev write */
	NULL,                   /* readdir - bad */
	NULL,                   /* poll */
	NULL,                   /* ioctl */
	NULL,                   /* mmap */
	z2_open,                /* open */
	NULL,			/* flush */
	z2_release,             /* release */
	block_fsync,            /* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL,			/* revalidate */
};

__initfunc(int
z2_init( void ))
{

    if ( !MACH_IS_AMIGA )
	return -ENXIO;

    if ( register_blkdev( MAJOR_NR, DEVICE_NAME, &z2_fops ) )
    {
	printk( KERN_ERR DEVICE_NAME ": Unable to get major %d\n",
	    MAJOR_NR );
	return -EBUSY;
    }

    {
	    /* Initialize size arrays. */
	    int i;

	    for (i = 0; i < Z2MINOR_COUNT; i++) {
		    z2_blocksizes[ i ] = 1024;
		    z2_sizes[ i ] = 0;
	    }
    }    
   
    blk_dev[ MAJOR_NR ].request_fn = DEVICE_REQUEST;
    blksize_size[ MAJOR_NR ] = z2_blocksizes;
    blk_size[ MAJOR_NR ] = z2_sizes;

    return 0;
}

#if defined(MODULE)
int
init_module( void )
{
    int error;
    
    error = z2_init();
    if ( error == 0 )
    {
	printk( KERN_INFO DEVICE_NAME ": loaded as module\n" );
    }
    
    return error;
}

void
cleanup_module( void )
{
    int i, j;

    if ( unregister_blkdev( MAJOR_NR, DEVICE_NAME ) != 0 )
	printk( KERN_ERR DEVICE_NAME ": unregister of device failed\n");

    if ( current_device != -1 )
    {
	i = 0;

	for ( j = 0 ; j < z2_count; j++ )
	{
	    set_bit( i++, zorro_unused_z2ram ); 
	}

	for ( j = 0 ; j < chip_count; j++ )
	{
	    if ( z2ram_map[ i ] )
	    {
		amiga_chip_free( (void *) z2ram_map[ i++ ] );
	    }
	}

	if ( z2ram_map != NULL )
	{
	    kfree( z2ram_map );
	}
    }

    return;
} 
#endif
