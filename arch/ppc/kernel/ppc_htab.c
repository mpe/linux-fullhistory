/*
 * $Id: ppc_htab.c,v 1.4 1997/08/12 04:24:54 cort Exp $
 * PowerPC hash table management proc entry.  Will show information
 * about the current hash table and will allow changes to it.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>

#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/residual.h>
#include <asm/io.h>
#include <asm/pgtable.h>

static long ppc_htab_read(struct inode * inode, struct file * file,
			   char * buf, unsigned long nbytes);
static long ppc_htab_write(struct inode * inode, struct file * file,
			    const char * buffer, unsigned long count);
static long long ppc_htab_lseek(struct inode * inode, struct file * file, 
				 long long offset, int orig);

extern PTE *Hash, *Hash_end;
extern unsigned long Hash_size, Hash_mask;

static struct file_operations ppc_htab_operations = {
    ppc_htab_lseek,	/* lseek   */
    ppc_htab_read,	/* read	   */
    ppc_htab_write,	/* write   */
    NULL,		/* readdir */
    NULL,		/* poll    */
    NULL,		/* ioctl   */
    NULL,		/* mmap	   */
    NULL,		/* no special open code	   */
    NULL,		/* no special release code */
    NULL		/* can't fsync */
};

/*
 * proc files can do almost nothing..
 */
struct inode_operations proc_ppc_htab_inode_operations = {
    &ppc_htab_operations,  /* default proc file-ops */
    NULL,	    /* create	   */
    NULL,	    /* lookup	   */
    NULL,	    /* link	   */
    NULL,	    /* unlink	   */
    NULL,	    /* symlink	   */
    NULL,	    /* mkdir	   */
    NULL,	    /* rmdir	   */
    NULL,	    /* mknod	   */
    NULL,	    /* rename	   */
    NULL,	    /* readlink	   */
    NULL,	    /* follow_link */
    NULL,	    /* readpage	   */
    NULL,	    /* writepage   */
    NULL,	    /* bmap	   */
    NULL,	    /* truncate	   */
    NULL	    /* permission  */
};


/*
 * print some useful info about the hash table.  This function
 * is _REALLY_ slow (see the nested for loops below) but nothing
 * in here should be really timing critical. -- Cort
 */
static long ppc_htab_read(struct inode * inode, struct file * file,
			   char * buf, unsigned long nbytes)
{
	int n = 0, valid;
	unsigned int kptes = 0, overflow = 0, uptes = 0;
	PTE *ptr;
	struct task_struct *p;
	
	if (nbytes < 0)
		return -EINVAL;

	/*
	 * compute user/kernel pte's table this info can be
	 * misleading since there can be valid (v bit set) entries
	 * in the table but their vsid is used by no process (mm->context)
	 * due to the way tlb invalidation is handled on the ppc
	 * -- Cort
	 */
	for ( ptr = Hash ; ptr < (PTE *)(Hash+Hash_size) ; ptr+=sizeof(PTE))
	{
		if (ptr->v)
		{
			/* make sure someone is using this context/vsid */
			for_each_task(p)
			{
				if ( (ptr->vsid >> 4) == p->mm->context )
				{
					valid = 1;
					break;
				}
			}
			if ( !valid )
				continue;
			/* user not allowed read or write */
			if (ptr->pp == PP_RWXX)
				kptes++;
			else
				uptes++;
			if (ptr->h == 1)
				overflow++;
		}
	}
	n += sprintf( buf,
		      "Size\t\t: %luKb\n"
		      "Buckets\t\t: %lu\n"
 		      "Address\t\t: %08lx\n"
		      "Entries\t\t: %lu\n"
		      "User ptes\t: %u\n"
		      "Kernel ptes\t: %u\n"
		      "Overflows\t: %u\n"
		      "Percent full\t: %%%lu\n",
                      (unsigned long)(Hash_size>>10),
		      (Hash_size/(sizeof(PTE)*8)),
		      (unsigned long)Hash,
		      Hash_size/sizeof(PTE),
                      uptes,
		      kptes,
		      overflow,
		      ((kptes+uptes)*100) / (Hash_size/sizeof(PTE))
		);
	/* if we're trying to read part of the file that isn't there */
	if ( file->f_pos > n )
		return -ENOMEM;
	file->f_pos += n;
	return n;
}

/*
 * Can't _yet_ adjust the hash table size while running. -- Cort
 */
static long
ppc_htab_write(struct inode * inode, struct file * file,
		const char * buffer, unsigned long count)
{
	if ( current->uid != 0 )
		return -EACCES;
	else
		return -ENOSYS;
	return 0;
}


static long long
ppc_htab_lseek(struct inode * inode, struct file * file, 
				 long long offset, int orig)
{
    switch (orig) {
    case 0:
	file->f_pos = offset;
	return(file->f_pos);
    case 1:
	file->f_pos += offset;
	return(file->f_pos);
    case 2:
	return(-EINVAL);
    default:
	return(-EINVAL);
    }
}


#if 0
/*
  for root.c
  */
static struct proc_dir_entry proc_root_ppc_htab = {
	PROC_PPC_HTAB, 8, "ppc_htab",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_ppc_htab_inode_operations
};
#ifdef __powerpc__
	proc_register(&proc_root, &proc_root_ppc_htab);
#endif


/* add to proc_fs.h
   	PROC_PPC_HTAB,*/
#endif
